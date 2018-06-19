#pragma once
#include "zmsg.hpp"
#include "mdp.h"
#include <memory>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>
#include <complex>
#include <json.hpp>
#include "defines.hpp"

using namespace std::chrono_literals;
namespace network::client {
    class client {
    public:

        //  ---------------------------------------------------------------------
        //  Constructor

        client(std::string broker, uint32_t service, int verbose = 0);

        //  ---------------------------------------------------------------------
        //  Destructor

        ~client();


        //  ---------------------------------------------------------------------
        //  Send message to broker
        //  If no _msg is provided, creates one internally
        ///Only internal use
        void sendReady(std::vector<std::string> options) const {
            zmsg msg;

            RF_serverMessage rf(static_cast<uint32_t>(serverMessageType::ready));
            rf.snIdent = serverIdent;
            rf.senderID = m_clientID;
            msg.setRoutingFrame(rf);

            //  Stack protocol envelope to start of message
            for (auto& option : options)
                if (option.length() != 0) {
                    msg.push_front(option);
                }

            //if (m_verbose) {
            //    msg.dump();
            //}
            msg.send(*m_worker);
        }

        void sendHeartbeat() const {
            zmsg heartbeat;
            RF_serverMessage rf(static_cast<uint32_t>(serverMessageType::heartbeat));
            rf.senderID = m_clientID;
            rf.snIdent = serverIdent;
            heartbeat.setRoutingFrame(rf);

            heartbeat.send(*m_worker);
        }

        void send(serviceType service, std::shared_ptr<zmsg> request, bool copyRequest = true) {

            //if (m_verbose) {
            //    s_console("I: send request to '%s' service:", service.c_str());
            //    request->dump();
            //}
            std::shared_ptr<zmsg> msg;
            if (copyRequest)
                msg = std::make_shared<zmsg>(*request);
            else
                msg = request;

            RF_serviceMsg rf(service);
            rf.senderID = m_clientID;
            rf.snIdent = serverIdent;
            msg->setRoutingFrame(rf);

            sendQueMtx.lock();
            sendqueue.emplace_back(std::move(msg));
            sendQueMtx.unlock();
            sendMsgInterrupt();
        }
        void send(std::string service, std::shared_ptr<zmsg> request, bool copyRequest = true) {

            //if (m_verbose) {
            //    s_console("I: send request to '%s' service:", service.c_str());
            //    request->dump();
            //}
            std::shared_ptr<zmsg> msg;
            if (copyRequest)
                msg = std::make_shared<zmsg>(*request);
            else
                msg = request;

            RF_serviceMsg rf(serviceType::generic);
            msg->push_front(service);
            rf.senderID = m_clientID;
            rf.snIdent = serverIdent;
            msg->setRoutingFrame(rf);

            //msg->dump();

            sendQueMtx.lock();
            sendqueue.emplace_back(std::move(msg));
            sendQueMtx.unlock();

            sendMsgInterrupt();
        }


        //  ---------------------------------------------------------------------
        //  Connect or reconnect to broker

        void connect_to_broker();


        //  ---------------------------------------------------------------------
        //  Set heartbeat delay

        void set_heartbeat(std::chrono::milliseconds heartbeat) {
            m_heartbeat = heartbeat;
        }


        //  ---------------------------------------------------------------------
        //  Set reconnect delay

        void set_reconnect(std::chrono::milliseconds reconnect) {
            m_reconnect = reconnect;
        }

        //  ---------------------------------------------------------------------
        //  Send reply, if any, to broker and wait for next request.

        bool shouldStop = false;
        bool running = false;

        enum class messageType {
            none,
            request,
            reply,
            service
        };
         void sendMsgInterrupt() {
             char dummy;
             zmq::socket_t doSignal(*m_context, ZMQ_PAIR);
             doSignal.connect(m_signalStopAddr);
             doSignal.send(&dummy, sizeof(dummy), ZMQ_DONTWAIT);
         }

        std::pair<messageType, std::shared_ptr<zmsg>> recv() {

            while (!s_interrupted && !shouldStop) {
                zmq::pollitem_t items[] = {
                    { *m_worker,  0, ZMQ_POLLIN, 0 },
                    { *m_signalStopSock, 0, ZMQ_POLLIN, 0 }
                };
                zmq::poll(items, 2, m_heartbeat);

                if (items[1].revents & ZMQ_POLLIN) {
                    sendQueMtx.lock();
                    char x;
                    
                    while (m_signalStopSock->getsockopt<int>(ZMQ_EVENTS) & ZMQ_POLLIN)
                        m_signalStopSock->recv(&x, ZMQ_DONTWAIT);
                    
                    for (auto& it : sendqueue)
                        it->send(*m_worker);
                    sendqueue.clear();
                    sendQueMtx.unlock();
                }

                if (items[0].revents & ZMQ_POLLIN) {
                    std::shared_ptr<zmsg> msg = std::make_shared<zmsg>(*m_worker);
                    if (m_verbose) {
                        //s_console ("I: received message from broker:");
                        //msg->dump ();
                    }
                    m_liveness = HEARTBEAT_LIVENESS;

                    //  Don't try to handle errors, just assert noisily
                    auto rfV = msg->getRoutingFrame();

                    if (!(msg->parts() >= 1 || rfV.index() != 0)) {
                        std::cerr << "received empty packet?\n";
                        continue;
                    }

                    switch (static_cast<routingFrameType>(rfV.index())) {
                        case routingFrameType::serverMessage: {
                            auto& rf = std::get<RF_serverMessage>(rfV);
                            switch (static_cast<serverMessageType>(rf.type)) {
                                case serverMessageType::heartbeat: {
                                    std::cerr << "pong\n";
                                    //  Do nothing for heartbeats
                                }break;
                                case serverMessageType::disconnect: {
                                    std::cerr << "disconnected by router\n";
                                    connect_to_broker();//I don't know why he disconnected us.. But I want to stay connected!
                                }break;
                                default:;
                            }
                        }break;
                        case routingFrameType::directMessage: {
                            auto& rf = std::get<RF_directMessage>(rfV);
                            if (static_cast<clientFlags>(rf.clientFlags) == clientFlags::request) {
                                return { messageType::request, msg };     //  We have a request to process
                            } else if (static_cast<clientFlags>(rf.clientFlags) == clientFlags::reply) {
                                return { messageType::reply, msg };
                            }
                        }break;
                        case routingFrameType::serviceMessage: {
                            return { messageType::service, msg };     //  We have a request to process
                        }break;
                        default: __debugbreak();
                    }

                    //s_console("E: invalid input message (%d)",
                    //    (int) *(command.c_str()));
                    //msg->dump();

                }
                //  Send HEARTBEAT if it's time
                if (std::chrono::system_clock::now() >= m_heartbeat_at) {
                    std::cerr << "ping\n";
                    sendHeartbeat();
                    m_heartbeat_at += m_heartbeat;

                    //Are we timing out?
                    if (--m_liveness == 0) {
                        if (m_verbose) {
                            s_console("W: disconnected from broker - retrying...");
                        }
                        std::this_thread::sleep_for(m_reconnect);
                        connect_to_broker();
                    }

                }
            }
            if (s_interrupted || shouldStop)
                printf("W: interrupt received, killing worker...\n");
            return { messageType::none, nullptr };
        }

        void work() {
            running = true;

            while (true) {

                auto packet = recv();
                if (shouldStop) {
                    running = false;
                    return;
                }
                switch (packet.first) {

                    case messageType::none: break;
                    case messageType::request: {

                        auto type = packet.second->pop_front();

                        nlohmann::json targetInfo = nlohmann::json::parse(type);

                        if (targetInfo[2] == 1) {//is request

                            bool wantAnswer = targetInfo[1];
                            if (!wantAnswer) {//don't want answer
                                //printf("message\n");
                                //packet.second->push_front(requestID);
                                packet.second->push_front(m_reply_to);
                                //packet.second->dump();

                                asynchronousRequestHandler(packet.second);
                            } else {
                                //printf("request\n");
                                //packet.second->push_front(requestID);
                                packet.second->push_front(m_reply_to);
                                //packet.second->dump();

                                synchronousRequestHandler(packet.second);

                                auto request = packet.second;

                                nlohmann::json info;
                                info[1] = false; //Don't want answer
                                info[2] = 2; //is reply
                                nlohmann::json target;

                                RF_base routingBase = packet.second->getRoutingBaseFrame();

                                target["clientID"] = routingBase.senderID;

                                info[0] = target;
                                info[3] = targetInfo[3];
                                request->push_front(info.dump(-1));

                                send(serviceType::rpc, request);
                            }



                        } else {
                            //printf("reply\n");

                            uint64_t reqID = targetInfo[3];

                            auto found = waitingRequests.find(reqID);

                            if (found != waitingRequests.end()) {
                                //auto message = packet.second->pop_front();
                                found->second(packet.second);
                                waitingRequests.erase(found);
                                //packet.second->push_front(message);
                            }



                            //packet.second->push_front(requestID);
                            //packet.second->push_front(m_reply_to);
                            //packet.second->dump();
                        }

                    }



                                               break;
                    case messageType::service: {
                        auto& rf = std::get<RF_serviceMsg>(packet.second->getRoutingFrame());
                        if (serviceHandlers.find(rf.messageType) != serviceHandlers.end())
                            serviceHandlers[rf.messageType](packet.second);
                    }break;
                    default:;
                }


            }





        }
        //Sends a message to someone. Not expecting any answer
        //#TODO also variant that takes zmsg rvalue and consumes it
        void sendMessage(std::shared_ptr<zmsg> data, int32_t target) {//#TODO unique_ptr. And take ownership
            auto request = data;
            nlohmann::json info;
            info[1] = false; //Don't want answer
            info[2] = 1; //is request
            nlohmann::json targetInfo;
            targetInfo["clientID"] = target;
            info[0] = targetInfo;
            request->push_front(info.dump(-1));
            send(serviceType::rpc, request);
        }

        //Sends a request to someone. Expecting an answer.
        void sendRequest(std::shared_ptr<zmsg> data, int32_t target, std::function<void(std::shared_ptr<zmsg> answer)> onAnswer) {
            auto request = data;

            nlohmann::json info;
            info[1] = true; //Want answer
            info[2] = 1; //is request
            info[3] = lastRequestID;
            nlohmann::json targetInfo;
            targetInfo["clientID"] = target;
            info[0] = targetInfo;
            request->push_front(info.dump(-1));

            waitingRequests[lastRequestID] = onAnswer;
            lastRequestID++;


            send(serviceType::rpc, request);
        }

        std::function<void(std::shared_ptr<zmsg>)> synchronousRequestHandler;
        std::function<void(std::shared_ptr<zmsg>)> asynchronousRequestHandler;
        std::map<serviceType, std::function<void(std::shared_ptr<zmsg>)>> serviceHandlers;

    private:
        std::string m_broker;
        int32_t m_clientID;
        uint64_t serverIdent = 0x123;
        std::shared_ptr<zmq::context_t> m_context;
        std::shared_ptr<zmq::socket_t> m_worker;     //  Socket to broker

        std::mutex sendQueMtx;
        std::vector<std::shared_ptr<zmsg>> sendqueue;


        std::thread workThread;
        int m_verbose;                //  Print activity to stdout
        uint64_t lastRequestID = 1;

        std::map<uint64_t, std::function<void(std::shared_ptr<zmsg>)>> waitingRequests; //#TODO add timeout
                                      //  Heartbeat management
        std::chrono::system_clock::time_point m_heartbeat_at;      //  When to send HEARTBEAT
        size_t m_liveness;            //  How many attempts left
        std::chrono::milliseconds m_heartbeat;              //  Heartbeat delay, msecs
        std::chrono::milliseconds m_reconnect;              //  Reconnect delay, msecs
        std::unique_ptr<zmq::socket_t> m_signalStopSock;
        std::string m_signalStopAddr;
                                       //  Return address, if any
        std::string m_reply_to;
    };
}