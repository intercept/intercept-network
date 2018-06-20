#ifndef __ZMSG_H_INCLUDED__
#define __ZMSG_H_INCLUDED__
#include "defines.hpp"
#include <optional>

class trafficLogger {
public:
    static void out(int bytes);
    static void in(int bytes);
};



#define TRAFFICLOG_OUTGOING(x) if (trafficLog) trafficLog->out(x);
#define TRAFFICLOG_INCOMING(x) if (trafficLog) trafficLog->in(x);
extern trafficLogger* trafficLog;

#include "zhelpers.hpp"

#include <vector>
#include <string>
#include <cstdarg>
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
class routingFrame {
    public:
    routingFrameType type = routingFrameType::none;
    int index() const noexcept {
        return (int)type;
    }
    bool isInvalid() const noexcept {
        return type == routingFrameType::none;
    }
    union {
        RF_base base;
        RF_serverMessage server;
        RF_directMessage direct;
        RF_serviceMsg service;
    };
    routingFrame(RF_serverMessage msg) : type(routingFrameType::serverMessage), server(msg) {};
    routingFrame(RF_directMessage msg) : type(routingFrameType::directMessage), direct(msg) {};
    routingFrame(RF_serviceMsg msg) : type(routingFrameType::serviceMessage), service(msg) {};
    routingFrame() {};

    void rebuildInto(zmq::message_t& msg) const {
        switch (type) {
            case routingFrameType::none: DEBUG_BREAK; break;
            case routingFrameType::serverMessage: msg.rebuild(&server, sizeof(RF_serverMessage)); break;
            case routingFrameType::directMessage: msg.rebuild(&direct, sizeof(RF_directMessage)); break;
            case routingFrameType::serviceMessage: msg.rebuild(&service, sizeof(RF_serviceMsg)); break;
            default: ;
        }
    }

};



namespace std {
    template<size_t I> _NODISCARD constexpr decltype(auto)  get(const routingFrame &c) {
        if constexpr (I == (size_t)routingFrameType::none)
            return nullptr;
        if constexpr (I == (size_t)routingFrameType::serverMessage)
            return c.server;
        if constexpr (I == (size_t)routingFrameType::directMessage)
            return c.direct;
        if constexpr (I == (size_t)routingFrameType::serviceMessage)
            return c.service;
        _THROW(bad_variant_access{});
    }

    template<class T> _NODISCARD constexpr auto& get(const routingFrame &c) noexcept;
    
    template<>
        _NODISCARD inline auto& get<RF_serverMessage>(const routingFrame &c) noexcept {
        if (c.type != routingFrameType::serverMessage) DEBUG_BREAK;
        return c.server;
    }
    template<>
    _NODISCARD inline auto& get<RF_directMessage>(const routingFrame &c) noexcept {
        if (c.type != routingFrameType::directMessage) DEBUG_BREAK;
        return c.direct;
    }
    template<>
    _NODISCARD inline auto& get<RF_serviceMsg>(const routingFrame &c) noexcept {
        if (c.type != routingFrameType::serviceMessage) DEBUG_BREAK;
        return c.service;
    }

}

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
        m_part_data = msg.m_part_data;
        //m_part_data.resize(msg.m_part_data.size());
        //std::copy(msg.m_part_data.begin(), msg.m_part_data.end(), m_part_data.begin());
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

    //void set_part(size_t part_nbr, char *data) {
    //    if (part_nbr < m_part_data.size()) {
    //        m_part_data[part_nbr] = data;
    //    }
    //}

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
                TRAFFICLOG_INCOMING(identity.size());
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
            TRAFFICLOG_INCOMING(routingFrameMsg.size());
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
                TRAFFICLOG_INCOMING(message.size());
            } catch (zmq::error_t error) {
                std::cout << "E: " << error.what() << std::endl;
                return false;
            }
            //std::cerr << "recv: \"" << (unsigned char*) message.data() << "\", size " << message.size() << std::endl;

            waitingForMore = message.more();

            m_part_data.emplace_back(std::move(message));

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

                if (data.hasMessage()) {

                    try {
                        __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send_send);
                        TRAFFICLOG_OUTGOING(data.size());
                        socket.send(data.getMessage(), part_nbr < m_part_data.size() - 1 ? ZMQ_SNDMORE : 0);
                        __itt_task_end(zmsg_domain);
                    }
                    catch (zmq::error_t error) {
                        assert(error.num() != 0);
                    }
                    continue;
                }


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
                    TRAFFICLOG_OUTGOING(data.size());
                    socket.send(message, part_nbr < m_part_data.size() - 1 ? ZMQ_SNDMORE : 0);
                    __itt_task_end(zmsg_domain);
                } catch (zmq::error_t error) {
                    assert(error.num() != 0);
                }
            }
        }
        void sendPartsKeep(zmq::socket_t& socket) {
            for (size_t part_nbr = 0; part_nbr < m_part_data.size(); part_nbr++) {
                auto& data = m_part_data[part_nbr];




                //zmq::message_t message(data.size());
                //__itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send_rebuild);
                //memcpy(message.data(), data.c_str(), data.size());
                //__itt_task_end(zmsg_domain);
                bool sndMore = part_nbr < m_part_data.size() - 1;
                try {
                    __itt_task_begin(zmsg_domain, __itt_null, __itt_null, handle_send_send);
                    TRAFFICLOG_OUTGOING(data.size());
                    socket.send(data.getMessage(), sndMore ? ZMQ_SNDMORE : 0);//message
                    __itt_task_end(zmsg_domain);
                }
                catch (zmq::error_t error) {
                    assert(error.num() != 0);
                }
            }
        }

        void sendRoutingFrame(zmq::socket_t& socket, routingFrame& RF) {
            if (RF.type == routingFrameType::none) DEBUG_BREAK;

            zmq::message_t routingFrameMsg;
            RF.rebuildInto(routingFrameMsg);
            TRAFFICLOG_OUTGOING(routingFrameMsg.size());
            socket.send(routingFrameMsg, m_part_data.empty() ? 0 : ZMQ_SNDMORE);
        }
        void sendClientIdentity(zmq::socket_t& socket, clientIdentity& ident) {
            zmq::message_t identMessage;
            identMessage.rebuild(&ident, sizeof(clientIdentity));
            TRAFFICLOG_OUTGOING(identMessage.size());
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

    void sendKeep(zmq::socket_t& socket, routingFrame overrideRF) {
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
        //dump();

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

    void sendKeep(zmq::socket_t& socket, clientIdentity targetIdent, routingFrame overrideRF) {
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

    void body_set(std::string_view body) {
        if (!m_part_data.empty()) {
            m_part_data.erase(m_part_data.end() - 1);
        }
        push_back(body);
    }

    void
        body_fmt(const char *format, ...) {
        char value[255 + 1];
        va_list args;

        va_start(args, format);
        vsnprintf(value, 255, format, args);
        va_end(args);

        body_set(std::string_view(value, strlen(value)));
    }

    char * body() {
        if (m_part_data.size())
            return const_cast<char *>(m_part_data[m_part_data.size() - 1].c_str());
        else
            return 0;
    }

    // zmsg_push
    //void push_front(char *part) {
    //    m_part_data.insert(m_part_data.begin(), part);
    //}

    // zmsg_append
    void push_back(std::string_view part) {
        m_part_data.push_back(part);
    }

    // zmsg_push
    void push_front(const std::string& part) {
        m_part_data.insert(m_part_data.begin(), part);
    }

    void push_front(std::string_view part) {
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
        if (m_part_data.empty()) {
            return "";
        }
        auto part = std::move(m_part_data.front());
        m_part_data.erase(m_part_data.begin());
        return part;
    }

    std::string_view peek_front() {
        return m_part_data.front();
    }


    //void append(const char *part) {
    //    assert(part);
    //    push_back((char*) part);
    //}

    char *address() {
        if (m_part_data.size() > 0) {
            return (char*) m_part_data[0].c_str();
        } else {
            return 0;
        }
    }

    //void wrap(const char *address, const char *delim) {
    //    if (delim) {
    //        push_front((char*) delim);
    //    }
    //    push_front((char*) address);
    //}
    void wrap(std::string address, std::string delim = {}) {
        push_front(std::move(delim));
        push_front(std::move(address));
    }

    void wrap(std::string_view address, std::string&& delim) {
        push_front(std::move(delim));
        push_front(address);
    }
    void wrap(std::string&& address, std::string_view delim) {
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

    void setRoutingFrame(routingFrame rf) {
        routingFrame = rf;
    }

    const routingFrame& getRoutingFrame() const {
        return routingFrame;
    }

    const RF_base& getRoutingBaseFrame() const {
        if (routingFrame.type == routingFrameType::none) DEBUG_BREAK;
        return routingFrame.base;
    }

    class zmq_part {
    public:
        zmq_part(zmq::message_t&& m) : msg(std::move(m)) {}
        zmq_part(std::string&& m) : str(std::move(m)) {}
        zmq_part(const std::string& m) : str(m) {}
        zmq_part(std::string_view m) : str(m){}

        zmq_part(const zmq_part& ot) : str(static_cast<std::string_view>(ot)) {
            if (ot.msg)
                DEBUG_BREAK;
        }
        zmq_part(zmq_part&& ot) noexcept : msg(std::move(ot.msg)), str(std::move(ot.str)) {
            ot.msg = std::nullopt;
            ot.str = std::nullopt;
        }

        zmq_part& operator=(const zmq_part& ot) {
            str = static_cast<std::string_view>(ot);
            return *this;
        }

        zmq_part& operator=(zmq_part&& ot) noexcept {
            str = std::move(ot.str);
            msg = std::move(ot.msg);
            return *this;
        }

        const char* c_str() const noexcept{
            if (str)
                return str->c_str();
            if (msg)
                return static_cast<const char*>(msg->data());
            DEBUG_BREAK;
        }
        size_t size() const noexcept {
            if (str)
                return str->length();
            if (msg)
                return msg->size();
            DEBUG_BREAK;
        }
        char operator[](size_t offs) const noexcept {
            if (str)
                return (*str)[offs];
            if (msg)
                return static_cast<const char*>(msg->data())[offs];
            DEBUG_BREAK;
        }

        operator std::string_view() const noexcept {
            if (str)
                return (*str);
            if (msg)
                return std::string_view(static_cast<const char*>(msg->data()), msg->size());
            DEBUG_BREAK;
        }

        operator std::string() noexcept {
            if (str)
                return *str;
            if (msg) {
                str = std::string(static_cast<const char*>(msg->data()), msg->size());
                return *str;
            }
            DEBUG_BREAK;
        }

        bool hasMessage() const noexcept {
            return msg.has_value();
        }

        zmq::message_t getMessage() {
            if (msg) {
                auto msgO = std::move(*msg);
                msg = std::nullopt;
                return msgO;
            }
            zmq::message_t out(str->length());
            memcpy(out.data(), str->c_str(), str->size());
            return out;
        }


        std::optional<zmq::message_t> msg;
        std::optional<std::string> str;
    };



private:
    



    std::vector<zmq_part> m_part_data;
    routingFrame routingFrame;
};

#endif /* ZMSG_H_ */
