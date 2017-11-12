#pragma once
#include "zmsg.hpp"
#include "mdp.h"
#include <memory>
#include <thread>
#include <chrono>
#include <map>
using namespace std::chrono_literals;
#define HEARTBEAT_LIVENESS  3       //  3-5 is reasonable
namespace intercept::network::client {
    class client {
    public:

        //  ---------------------------------------------------------------------
        //  Constructor

        client(std::string broker, std::string service, int verbose = 0);

        //  ---------------------------------------------------------------------
        //  Destructor

        ~client();


        //  ---------------------------------------------------------------------
        //  Send message to broker
        //  If no _msg is provided, creates one internally
        ///Only internal use
        void send_to_broker(char *command, std::vector<std::string> options, std::shared_ptr<zmsg> _msg) {
            std::shared_ptr<zmsg> msg = _msg ? std::make_shared<zmsg>(*_msg) : std::make_shared<zmsg>();

            //  Stack protocol envelope to start of message       
            for (auto& option : options)
                if (option.length() != 0) {
                    msg->push_front(option);
                }
            msg->push_front(command);
            msg->push_front(MDPW_WORKER);
            msg->push_front("");

            /*
             * empty
             * <header>
             * command
             * options....
             */
            if (m_verbose) {
                s_console("I: sending %s to broker",
                    mdps_commands[(int) *command]);
                msg->dump();
            }
            msg->send(*m_worker);
        }

        void sendHeartbeat() {
            std::shared_ptr<zmsg> msg = std::make_shared<zmsg>();
            msg->push_front(MDPW_HEARTBEAT);
            msg->push_front("");

            /*
            * empty
            * <header> (MDPW_HEARTBEAT)
            */
            //if (m_verbose) {
            //    s_console("ping");
            //    msg->dump();
            //}
            msg->send(*m_worker);
        }

        void send(std::string service, std::shared_ptr<zmsg> request, bool copyRequest = true) {

            //  Prefix request with protocol frames
            //  Frame 1: "MDPCxy" (six bytes, MDP/Client x.y)
            //  Frame 2: Service name (printable string)
            request->push_front(service.c_str());
            request->push_front(MDPW_REQUEST);
            request->push_front(MDPW_WORKER);
            request->push_front("");
            /*
             * empty
             * <header>
             * <MDPW_REQUEST>
             * <service name>
             */
            if (m_verbose) {
                s_console("I: send request to '%s' service:", service.c_str());
                request->dump();
            }
            std::shared_ptr<zmsg> msg;
            if (copyRequest)
                msg = std::make_shared<zmsg>(*request);
            else
                msg = request;
            msg->send(*m_worker);
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
            reply
        };

        std::pair<messageType, std::shared_ptr<zmsg>> recv() {

            while (!s_interrupted && !shouldStop) {
                zmq::pollitem_t items[] = {
                    { *m_worker,  0, ZMQ_POLLIN, 0 } };
                zmq::poll(items, 1, m_heartbeat);

                if (items[0].revents & ZMQ_POLLIN) {
                    std::shared_ptr<zmsg> msg = std::make_shared<zmsg>(*m_worker);
                    if (m_verbose) {
                        //s_console ("I: received message from broker:");
                        //msg->dump ();
                    }
                    m_liveness = HEARTBEAT_LIVENESS;

                    //  Don't try to handle errors, just assert noisily
                    assert(msg->parts() >= 2);
                    auto empty = msg->pop_front();
                    assert(empty.compare("") == 0);
                    //assert (strcmp (empty, "") == 0);
                    //free (empty);

                    //auto header = msg->pop_front();
                    //assert(header.compare((unsigned char *)MDPW_WORKER) == 0);
                    //free (header);

                    std::string command = msg->pop_front();
                    if (command.compare(MDPW_REQUEST) == 0) {
                        //  We should pop and save as many addresses as there are
                        //  up to a null part, but for now, just save one...
                        m_reply_to = msg->unwrap();
                        return { messageType::request, msg };     //  We have a request to process
                    } else if (command.compare(MDPW_REPLY) == 0) {
                        return { messageType::reply, msg };
                    } else if (command.compare(MDPW_HEARTBEAT) == 0) {
                        std::cerr << "pong\n";
                        //  Do nothing for heartbeats
                    } else if (command.compare(MDPW_DISCONNECT) == 0) {
                        connect_to_broker();//I don't know why he disconnected us.. But I want to stay connected!
                    } else {
                        s_console("E: invalid input message (%d)",
                            (int) *(command.c_str()));
                        msg->dump();
                    }
                } else
                    if (--m_liveness == 0) {
                        if (m_verbose) {
                            s_console("W: disconnected from broker - retrying...");
                        }
                        std::this_thread::sleep_for(m_reconnect);
                        connect_to_broker();
                    }
                //  Send HEARTBEAT if it's time
                if (std::chrono::system_clock::now() >= m_heartbeat_at) {
                    std::cerr << "ping\n";
                    sendHeartbeat();
                    m_heartbeat_at += m_heartbeat;
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
                        if (type == "q") {



                            auto requestID = packet.second->pop_front();
                            if (requestID == "0") {
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
                                request->push_front(requestID); //Don't want answer
                                request->push_front("p"); //is reply
                                request->push_front(m_reply_to);
                                send("rpc", request);
                            }



                        } else {
                            //printf("reply\n");


                            auto requestID = packet.second->pop_front();

                            auto reqID = std::stoull(requestID);

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
                    case messageType::reply:

                        break;
                    default:;
                }


            }





        }
        //Sends a message to someone. Not expecting any answer
        //#TODO also variant that takes zmsg rvalue and consumes it
        void sendMessage(std::shared_ptr<zmsg> data, std::string target) {//#TODO unique_ptr. And take ownership
            auto request = data;
            request->push_front("0"); //Don't want answer
            request->push_front("q"); //is request
            request->push_front(target);
            send("rpc", request);
        }

        //Sends a request to someone. Expecting an answer.
        void sendRequest(std::shared_ptr<zmsg> data, std::string target, std::function<void(std::shared_ptr<zmsg> answer)> onAnswer) {
            auto request = data;
            request->push_front(std::to_string(lastRequestID)); //Want answer. And that's the ID
            waitingRequests[lastRequestID] = onAnswer;
            lastRequestID++;
            request->push_front("q"); //is request
            request->push_front(target);
            send("rpc", request);
        }

        std::function<void(std::shared_ptr<zmsg>)> synchronousRequestHandler;
        std::function<void(std::shared_ptr<zmsg>)> asynchronousRequestHandler;

    private:
        std::string m_broker;
        std::string m_clientID;
        std::shared_ptr<zmq::context_t> m_context;
        std::shared_ptr<zmq::socket_t> m_worker;     //  Socket to broker
        std::thread workThread;
        int m_verbose;                //  Print activity to stdout
        uint64_t lastRequestID = 1;

        std::map<uint64_t, std::function<void(std::shared_ptr<zmsg>)>> waitingRequests; //#TODO add timeout
                                      //  Heartbeat management
        std::chrono::system_clock::time_point m_heartbeat_at;      //  When to send HEARTBEAT
        size_t m_liveness;            //  How many attempts left
        std::chrono::milliseconds m_heartbeat;              //  Heartbeat delay, msecs
        std::chrono::milliseconds m_reconnect;              //  Reconnect delay, msecs

                                       //  Return address, if any
        std::string m_reply_to;
    };
}