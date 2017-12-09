#ifndef __ZMSG_H_INCLUDED__
#define __ZMSG_H_INCLUDED__

#include "zhelpers.hpp"

#include <vector>
#include <string>
#include <stdarg.h>
#define INTEL_NO_ITTNOTIFY_API
#include <ittnotify.h>
#include <variant>

static __itt_domain* zmsg_domain = __itt_domain_create("zmsg");
static __itt_string_handle* handle_send = __itt_string_handle_create("send");
static __itt_string_handle* handle_send_rebuild = __itt_string_handle_create("send_rebuild");
static __itt_string_handle* handle_send_send = __itt_string_handle_create("send_send");
static __itt_string_handle* handle_send_clear0copy = __itt_string_handle_create("send_clear0copy");

#pragma pack(push,1)

enum class routingFrameType : unsigned char {
    none,
    serverMessage,
    directMessage,
    serviceMessage
};

enum class serviceType : unsigned char{
    generic, //Service name is in topmost data
    rpc,
    pvar
};

struct clientIdentity {
    int32_t clientID{ 0 };
    uint64_t serverIdentity{ 0 }; //unique server identifier
};

class RF_base {
public:
    RF_base(routingFrameType type_) : type(type_) {}
    routingFrameType type;
    uint8_t routingFlags{ 0 };
    uint8_t clientFlags{ 0 };
    int32_t senderID{ 0 };
    uint64_t snIdent{ 0 }; //unique server identifier
};

class RF_serverMessage : public RF_base {
public:
    RF_serverMessage(uint32_t messageType_) : RF_base(routingFrameType::serverMessage), messageType(messageType_) {}
    uint32_t messageType{ 0 };
};

class RF_directMessage : public RF_base {
public:
    RF_directMessage() : RF_base(routingFrameType::directMessage) {}
    int32_t targetID{ 0 };
};

class RF_serviceMsg : public RF_base {
public:
    RF_serviceMsg(serviceType messageType_) : RF_base(routingFrameType::serviceMessage), messageType(messageType_) {}
    serviceType messageType;
};

#pragma pack(pop)
using routingFrameVariant = std::variant<std::nullptr_t, RF_serverMessage, RF_directMessage, RF_serviceMsg>;

class zmsg {
public:
    zmsg() {}

    //  --------------------------------------------------------------------------
    //  Constructor, sets initial body
    zmsg(char const *body) {
        body_set(body);
    }

    //  -------------------------------------------------------------------------
    //  Constructor, sets initial body and sends message to socket
    zmsg(char const *body, zmq::socket_t &socket) {
        body_set(body);
        send(socket);
    }

    //  --------------------------------------------------------------------------
    //  Constructor, calls first receive automatically
    zmsg(zmq::socket_t &socket) {
        recv(socket);
    }

    //  --------------------------------------------------------------------------
    //  Copy Constructor, equivalent to zmsg_dup
    zmsg(zmsg &msg) {
        m_part_data.resize(msg.m_part_data.size());
        std::copy(msg.m_part_data.begin(), msg.m_part_data.end(), m_part_data.begin());
        routingFrame = msg.routingFrame;
    }

    virtual ~zmsg() {
        clear();
    }

    //  --------------------------------------------------------------------------
    //  Erases all messages
    void clear() {
        m_part_data.clear();
    }

    void set_part(size_t part_nbr, char *data) {
        if (part_nbr < m_part_data.size()) {
            m_part_data[part_nbr] = data;
        }
    }

    bool recv(zmq::socket_t& socket) {
        clear();                         //#TODO get clientID?


        bool isRouter = socket.getsockopt<int>(ZMQ_TYPE) == ZMQ_ROUTER;
        clientIdentity senderID;
        if (isRouter) {
            zmq::message_t identity(0);
            try {
                if (!socket.recv(&identity, ZMQ_DONTWAIT)) {//block if we know there is data in the pipe
                    return false;
                }
            } catch (zmq::error_t error) {
                std::cout << "E: " << error.what() << std::endl;
                return false;
            }
            //#TODO check that size matches clientIdentity size
            senderID = *static_cast<clientIdentity*>(identity.data());
        }

        zmq::message_t routingFrameMsg(0);
        try {
            if (!socket.recv(&routingFrameMsg, ZMQ_DONTWAIT)) {//block if we know there is data in the pipe
                return false;
            }
        } catch (zmq::error_t error) {
            std::cout << "E: " << error.what() << std::endl;
            return false;
        }

        auto rfbase = static_cast<RF_base*>(routingFrameMsg.data());
        if (isRouter && (rfbase->senderID != senderID.clientID || rfbase->snIdent != senderID.serverIdentity)) {
            __debugbreak(); //SenderID fake
        }
        auto size = routingFrameMsg.size();
        switch (rfbase->type) {
            case routingFrameType::none: __debugbreak(); break;
            case routingFrameType::serverMessage: {
                auto rf = static_cast<RF_serverMessage*>(routingFrameMsg.data());
                setRoutingFrame(*rf);
            } break;
            case routingFrameType::directMessage: {
                auto rf = static_cast<RF_directMessage*>(routingFrameMsg.data());
                setRoutingFrame(*rf);
            } break;
            case routingFrameType::serviceMessage: {
                auto rf = static_cast<RF_serviceMsg*>(routingFrameMsg.data());
                setRoutingFrame(*rf);
            } break;
            default: __debugbreak(); break;
        }

        bool waitingForMore = false;
        while (1) {
            zmq::message_t message(0);
            try {
                if (!socket.recv(&message, waitingForMore ? 0 : ZMQ_DONTWAIT)) {//block if we know there is data in the pipe
                    return false;
                }
            } catch (zmq::error_t error) {
                std::cout << "E: " << error.what() << std::endl;
                return false;
            }
            //std::cerr << "recv: \"" << (unsigned char*) message.data() << "\", size " << message.size() << std::endl;

            m_part_data.push_back(std::string((char*) message.data(), message.size()));

            waitingForMore = message.more();
            if (!waitingForMore) {
                break;
            }
        }
        return true;
    }


    private:
        void sendParts(zmq::socket_t& socket) {
            for (size_t part_nbr = 0; part_nbr < m_part_data.size(); part_nbr++) {
                zmq::message_t message;
                auto& data = m_part_data[part_nbr];

                __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send_rebuild);
                auto datHeap = new std::string(std::move(data));
                //zero-copy move
                message.rebuild((void*) datHeap->c_str(), datHeap->size(), [](void *data, void *hint) {
                    //__itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send_clear0copy);
                    delete ((std::string*)hint);
                    //__itt_task_end(zmsg_domain);
                }, (void*) datHeap);
                __itt_task_end(zmsg_domain);
                try {
                    __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send_send);
                    socket.send(message, part_nbr < m_part_data.size() - 1 ? ZMQ_SNDMORE : 0);
                    __itt_task_end(zmsg_domain);
                } catch (zmq::error_t error) {
                    assert(error.num() != 0);
                }
            }
        }
        void sendPartsKeep(zmq::socket_t& socket) {
            for (size_t part_nbr = 0; part_nbr < m_part_data.size(); part_nbr++) {
                zmq::message_t message;
                auto& data = m_part_data[part_nbr];
                __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send_rebuild);
                memcpy(message.data(), data.c_str(), data.size());
                __itt_task_end(zmsg_domain);
                try {
                    __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send_send);
                    socket.send(message, part_nbr < m_part_data.size() - 1 ? ZMQ_SNDMORE : 0);
                    __itt_task_end(zmsg_domain);
                }
                catch (zmq::error_t error) {
                    assert(error.num() != 0);
                }
            }
        }

        void sendRoutingFrame(zmq::socket_t& socket, routingFrameVariant& RF) {
            if (RF.index() == 0) __debugbreak();

            zmq::message_t routingFrameMsg;
            std::visit([&routingFrameMsg](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                routingFrameMsg.rebuild(&arg, sizeof(T));
            }, RF);

            socket.send(routingFrameMsg, m_part_data.empty() ? 0 : ZMQ_SNDMORE);
        }
        void sendClientIdentity(zmq::socket_t& socket, clientIdentity& ident) {
            zmq::message_t identMessage;
            identMessage.rebuild(&ident, sizeof(clientIdentity));
            socket.send(identMessage, ZMQ_SNDMORE);
        }
    public:


    void send(zmq::socket_t& socket) {
        __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send);
    #ifdef _DEBUG
        if (socket.getsockopt<int>(ZMQ_TYPE) == ZMQ_ROUTER) __debugbreak(); //Have to set targetID!
    #endif
        sendRoutingFrame(socket, routingFrame);

        sendParts(socket);
        
        clear();
        __itt_task_end(zmsg_domain);
    }

    //This sends like send() does but doesn't clear our internal data. Meaning you can reuse the message and send it to others.
    void sendKeep(zmq::socket_t& socket) {
        __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send);
    #ifdef _DEBUG
        if (socket.getsockopt<int>(ZMQ_TYPE) == ZMQ_ROUTER) __debugbreak(); //Have to set targetID!
    #endif
        sendRoutingFrame(socket, routingFrame);

        sendPartsKeep(socket);

        __itt_task_end(zmsg_domain);
    }

    void sendKeep(zmq::socket_t& socket, routingFrameVariant overrideRF) {
        __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send);
    #ifdef _DEBUG
        if (socket.getsockopt<int>(ZMQ_TYPE) == ZMQ_ROUTER) __debugbreak(); //Have to set targetID!
    #endif
        sendRoutingFrame(socket, overrideRF);

        sendPartsKeep(socket);
        
        __itt_task_end(zmsg_domain);
    }

    void send(zmq::socket_t& socket, clientIdentity targetIdent) {
        __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send);
    #ifdef _DEBUG
        if (socket.getsockopt<int>(ZMQ_TYPE) != ZMQ_ROUTER) __debugbreak(); //This is for routers
    #endif

        sendClientIdentity(socket, targetIdent);

        sendRoutingFrame(socket, routingFrame);

        sendParts(socket);

        clear();
        __itt_task_end(zmsg_domain);
    }

    //This sends like send() does but doesn't clear our internal data. Meaning you can reuse the message and send it to others.
    void sendKeep(zmq::socket_t& socket, clientIdentity targetIdent) {
        __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send);
    #ifdef _DEBUG
        if (socket.getsockopt<int>(ZMQ_TYPE) != ZMQ_ROUTER) __debugbreak(); //This is for routers
    #endif

        sendClientIdentity(socket, targetIdent);

        sendRoutingFrame(socket, routingFrame);

        sendPartsKeep(socket);

        __itt_task_end(zmsg_domain);
    }

    void sendKeep(zmq::socket_t& socket, clientIdentity targetIdent, routingFrameVariant overrideRF) {
        __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send);
    #ifdef _DEBUG
        if (socket.getsockopt<int>(ZMQ_TYPE) != ZMQ_ROUTER) __debugbreak(); //This is for routers
    #endif

        sendClientIdentity(socket, targetIdent);

        sendRoutingFrame(socket, overrideRF);

        sendPartsKeep(socket);

        __itt_task_end(zmsg_domain);
    }




    size_t parts() {
        return m_part_data.size();
    }

    void body_set(const char *body) {
        if (m_part_data.size() > 0) {
            m_part_data.erase(m_part_data.end() - 1);
        }
        push_back((char*) body);
    }

    void
        body_fmt(const char *format, ...) {
        char value[255 + 1];
        va_list args;

        va_start(args, format);
        vsnprintf(value, 255, format, args);
        va_end(args);

        body_set(value);
    }

    char * body() {
        if (m_part_data.size())
            return ((char *) m_part_data[m_part_data.size() - 1].c_str());
        else
            return 0;
    }

    // zmsg_push
    void push_front(char *part) {
        m_part_data.insert(m_part_data.begin(), part);
    }

    // zmsg_append
    void push_back(char *part) {
        m_part_data.push_back(part);
    }

    // zmsg_push
    void push_front(const std::string& part) {
        m_part_data.insert(m_part_data.begin(), part);
    }

    // zmsg_append
    void push_back(const std::string& part) {
        m_part_data.push_back(part);
    }

    // zmsg_push
    void push_front(std::string&& part) {
        m_part_data.insert(m_part_data.begin(), std::move(part));
    }

    // zmsg_append
    void push_back(std::string&& part) {
        m_part_data.push_back(std::move(part));
    }

    // zmsg_pop
    std::string pop_front() {
        if (m_part_data.size() == 0) {
            return 0;
        }
        auto part = std::move(m_part_data.front());
        m_part_data.erase(m_part_data.begin());
        return part;
    }

    std::string& peek_front() {
        return m_part_data.front();
    }


    void append(const char *part) {
        assert(part);
        push_back((char*) part);
    }

    char *address() {
        if (m_part_data.size() > 0) {
            return (char*) m_part_data[0].c_str();
        } else {
            return 0;
        }
    }

    void wrap(const char *address, const char *delim) {
        if (delim) {
            push_front((char*) delim);
        }
        push_front((char*) address);
    }
    void wrap(std::string address, std::string delim = {}) {
        push_front(std::move(delim));
        push_front(std::move(address));
    }

    void wrap(const char* address, std::string&& delim) {
        push_front(std::move(delim));
        push_front(address);
    }
    void wrap(std::string&& address, const char* delim) {
        push_front(delim);
        push_front(std::move(address));
    }
    std::string unwrap() {
        if (m_part_data.size() == 0) {
            return NULL;
        }
        std::string addr = std::move(pop_front());
        if (address() && *address() == 0) {
            pop_front();
        }
        return addr;
    }

    void dump() {
        std::cerr << "--------------------------------------" << std::endl;
        for (unsigned int part_nbr = 0; part_nbr < m_part_data.size(); part_nbr++) {
            auto& data = m_part_data[part_nbr];

            // Dump the message as text or binary
            int is_text = 1;
            for (unsigned int char_nbr = 0; char_nbr < data.size(); char_nbr++)
                if (data[char_nbr] < 32 || data[char_nbr] > 127)
                    is_text = 0;

            std::cerr << "[" << std::setw(3) << std::setfill('0') << (int) data.size() << "] ";
            for (unsigned int char_nbr = 0; char_nbr < data.size(); char_nbr++) {
                if (is_text) {
                    std::cerr << (char) data[char_nbr];
                } else {
                    std::cerr << std::hex << std::setw(2) << std::setfill('0') << (short int) data[char_nbr];
                }
            }
            std::cerr << std::endl;
        }
    }

    void setRoutingFrame(routingFrameVariant rf) {
        routingFrame = rf;
    }

    routingFrameVariant getRoutingFrame() const {
        return routingFrame;
    }

    RF_base getRoutingBaseFrame() const {
        RF_base routingBase(routingFrameType::none);
        std::visit([&routingBase](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                __debugbreak();
                return;
            } else
                routingBase = arg;
        }, routingFrame);
        return routingBase;
    }

private:
    std::vector<std::string> m_part_data;
    routingFrameVariant routingFrame{ nullptr };
};

#endif /* ZMSG_H_ */
