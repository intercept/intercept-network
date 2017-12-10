#pragma once
#include "zmsg.hpp"
#include "mdp.h"
#include <set>                       
#include <map>
#include <list>
#include <deque>
#include <memory>
#include <chrono>
#include <string_view>
#include <mutex>
#include "services.hpp"
using namespace std::chrono_literals;

static __itt_domain* domain = __itt_domain_create("router");
static __itt_string_handle* handle_processMessage = __itt_string_handle_create("processMessage");
static __itt_string_handle* handle_pollWait = __itt_string_handle_create("pollWait");
static __itt_string_handle* handle_routeMessage = __itt_string_handle_create("routeMessage");
static __itt_string_handle* handle_heartBeating = __itt_string_handle_create("heartBeating");
static __itt_counter handle_pollCounter = __itt_counter_create("poll", "router");
static __itt_counter handle_recvCounter = __itt_counter_create("recv", "router");
/*
16.09.17
dedmen [9:37 PM]
We have large traffic on start anyway because of the JIP queue.... Hm....


[9:38]
Just thinking about how to handle publicVars...
How about a `GlobalVariable` or `NetworkedVariable` class... You give it it's name in the constructor. and it automatically registeres a handler for the pubvar event


[9:39]
and if you assign a value to it it automatically get's distributed... or.. you can set the distribution scheme in the constructor..
So you don't have to worry about calling publicVariable and stuff.
you just `var = "test"` and it propagates automatically


[9:39]
and you could also configure which clients specifically to propagate to.. But that's probably overkill

[9:44]
Btw I'm also planning a compressing serializer for game_value.
For example if you send a function Arma sends the full raw string. But we would compress it first.. using zlib for example

[9:46]
Ouh gosh....
CODE variables are compiled when you receive them.. Networking lives in a seperate thread and we will need a invoker_lock to compile them... That will be crusty


[9:49]
And when we deserialize we actually don't know what we are even deserializing... So I need a special flag to tell the receiver if the deserialization needs a invoker_lock
*/


#define HEARTBEAT_LIVENESS  3       //  3-5 is reasonable
#define HEARTBEAT_INTERVAL  2500ms    //  msecs
#define HEARTBEAT_EXPIRY    HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS
namespace intercept::network::server {
    class service;
    class client {
    public:
        clientIdentity ident;
        std::shared_ptr<service>  m_service;      //  Owning service, if known
        std::chrono::system_clock::time_point m_expiry;         //  Expires at unless heartbeat
        bool initialized{ false };

        client(int32_t identity, uint64_t serverIdent, std::shared_ptr<service> service = 0) {
            ident.clientID = identity;
            ident.serverIdentity = serverIdent;
            m_service = service;
            m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY;
        }
        clientIdentity getIdentity() {
            return ident;
        }
        ~client() {

        }
    };


    class router {
    public:
        router();
        ~router();


        class serverCli {
        public:
            serverCli() {
                m_services.insert({ "rpc",std::make_shared<rpc_broker>("rpc") });
                m_services.insert({ "publicVariableService",std::make_shared<publicVariableService>() });
            }
            std::shared_ptr<service> getService(std::string name, bool createIfNotExist = true) {
                assert(name.size() > 0);
                if (m_services.count(name)) {
                    return m_services.at(name);
                } else if (createIfNotExist) {
                    std::shared_ptr<service> srv = std::make_shared<service>(name);
                    m_services.insert(std::make_pair(name, srv));
                    //if (m_verbose) {
                    //    s_console("I: received message:");
                    //}
                    return srv;
                }
                return nullptr;
            }
            std::map<int32_t, std::shared_ptr<client>> m_clients;
            std::map<std::string, std::shared_ptr<service>> m_services;  //  Hash of known services
        };


        void bind(std::string endpoint) {
            m_endpoint = endpoint;
            m_socket->bind(m_endpoint.c_str());
            s_console("I: MDP broker/0.1.1 is active at %s", endpoint.c_str());
        }

        void check_timeouts() {
            std::vector<std::shared_ptr<client>> toPurge;
            std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
            for (auto& server : m_servers)
                for (auto& worker : server.second->m_clients) {
                    if (worker.second->m_expiry <= now)
                        toPurge.push_back(worker.second);
                }
            for (auto& worker : toPurge) {
                if (m_verbose) {
                    s_console("I: deleting expired worker: %d", worker->ident.clientID);
                }
                disconnect_client(worker, false);
            }
        }
        void disconnect_client(std::shared_ptr<client> &wrk, bool sendDisconnectMessage) {
            assert(wrk);
            if (sendDisconnectMessage) {
                zmsg discMsg;
                RF_serverMessage rf(static_cast<uint32_t>(serverMessageType::disconnect));
                rf.senderID = -1;
                //No server identification needed
                discMsg.setRoutingFrame(rf);
                discMsg.send(*m_socket, wrk->getIdentity());
            }

            if (wrk->m_service) {
                wrk->m_service->m_clients.erase(wrk);
            }

            auto serverIdent = wrk->ident.serverIdentity;
            auto server = getServer(serverIdent);
            if (server) {
                server->m_clients.erase(wrk->ident.clientID); //  This implicitly calls the worker destructor
                if (server->m_clients.empty()) {
                    m_servers.erase(serverIdent);
                    //#TODO log server leave
                }
            }
        }

        void worker_send(std::shared_ptr<client> worker, std::shared_ptr<zmsg> msg) const {
            msg = (msg ? std::make_shared<zmsg>(*msg) : std::make_shared<zmsg>());

            //  Stack protocol envelope to start of message
            //if (option.size() > 0) {                 //  Optional frame after command
            //    msg->push_front(option);
            //}

            //if (m_verbose) {
            //    s_console("I: sending %s to worker",
            //        mdps_commands[static_cast<int>(*command)]);
            //    msg->dump();
            //}
            /*
             * identity
             * <empty>
             * command
             * option(optional)
             */
            msg->send(*m_socket, worker->getIdentity());
        }

        void worker_send(std::shared_ptr<client> worker, std::shared_ptr<zmsg> msg, routingFrameVariant overrideRF, std::string option = {}) const {


            //  Stack protocol envelope to start of message
            if (option.size() > 0) {                 //  Optional frame after command
                msg = (msg ? std::make_shared<zmsg>(*msg) : std::make_shared<zmsg>());
                msg->push_front(option);
            }

            //  Stack routing envelope to start of message
            //#TODO client/serverID?

            //if (m_verbose) {
            //    s_console("I: sending %s to worker",
            //        mdps_commands[static_cast<int>(*command)]);
            //    msg->dump();
            //}
            /*
            * identity
            * <empty>
            * command
            * option(optional)
            */
            if (msg)
                msg->sendKeep(*m_socket, worker->getIdentity(), overrideRF);
        }

        void worker_send(std::shared_ptr<client> worker, zmsg& msg, routingFrameVariant overrideRF) const {
            msg.sendKeep(*m_socket, worker->getIdentity(), overrideRF);
        }

        std::shared_ptr<serverCli> getServer(uint64_t serverIdent, bool createIfNotExist = true) {
            if (m_servers.count(serverIdent)) {
                return m_servers.at(serverIdent);
            } else if (createIfNotExist) {
                std::shared_ptr<serverCli> wrk = std::make_shared<serverCli>();
                m_servers.insert(std::make_pair(serverIdent, wrk));
                if (m_verbose) {
                    s_console("I: registering new server: %lld", serverIdent);
                }
                return wrk;
            }
            return nullptr;
        }

        std::shared_ptr<client> get_client(uint64_t serverIdent, int32_t clientID) {
            auto server = getServer(serverIdent);
            //  self->workers is keyed off worker identity
            if (server->m_clients.count(clientID)) {
                return server->m_clients.at(clientID);
            } else {
                std::shared_ptr<client> wrk = std::make_shared<client>(clientID, serverIdent);
                server->m_clients.insert(std::make_pair(clientID, wrk));
                if (m_verbose) {
                    s_console("I: registering new worker: %lld %d", serverIdent, clientID);
                }
                return wrk;
            }
        }

        void stop() {
            shouldRun = false;
            running.lock();
            for (auto& srv : m_servers) {
                for (auto& cli : srv.second->m_clients)
                    disconnect_client(cli.second, true);
            }
            m_socket->close();
        }
        __itt_string_handle* handle_service_internal = __itt_string_handle_create("service_internal");
        void service_internal(const std::string& service_name, std::shared_ptr<zmsg> msg) {
            __itt_task_begin(domain, __itt_null, __itt_null, handle_service_internal);
            //std::cerr << "service\n";
            //msg->dump();
            if (service_name.compare("srv.service") == 0) {
                //std::shared_ptr<service>  srv = m_services.at(msg->body());
                //if (srv && srv->m_clients.empty()) {
                //    msg->body_set("200");
                //} else {
                //    msg->body_set("404");
                //}
            } else if (service_name.compare("srv.echo") == 0) {
            } else {
                msg->body_set("501");
            }

            //  Remove & save client return envelope and insert the
            //  protocol header and service name, then rewrap envelope.
            std::string client = msg->unwrap();
            msg->wrap(MDPW_REPLY, service_name);
            msg->wrap(client.c_str(), "");
            //std::cerr << "serviceend\n";
            //msg->dump();
            /*
             *<client identifier>
             *<service name>
             *<MDPW_REPLY>
             *other data...
             */
            msg->send(*m_socket);
            __itt_task_end(domain);
        }

        void processServiceMessage(RF_serviceMsg& rf, std::shared_ptr<client>& sender, std::shared_ptr<zmsg> msg) {
            __itt_task_begin(domain, __itt_null, __itt_null, handle_processMessage);
            assert(msg && msg->parts() >= 1);     //  At least, command
            /*
             * <command>
             * data...
             */
            std::cout << "srv\n";
            if (!sender->initialized) {//Not initialized. Protocol violation
                __itt_task_end(domain);
                disconnect_client(sender, true);
                return;
            }
            std::string serviceName;
            if (rf.messageType != serviceType::generic) {
                if (rf.messageType == serviceType::rpc)
                    serviceName = "rpc";
                if (rf.messageType == serviceType::pvar)
                    serviceName = "publicVariableService";
            } else {
                serviceName = msg->pop_front();
            }

            std::shared_ptr<service> srv = getServer(sender->ident.serverIdentity)->getService(serviceName);
            if (serviceName.length() >= 4
                && serviceName.find_first_of("srv.") == 0) {
                service_internal(serviceName, msg);
            } else {
                if (srv)
                    srv->dispatch(msg);
                else {
                    //These moves prevent a unnecessary copy. But also discard the values making them unusable after the move
                    //#TODO add a pushFront function that takes a vector. It premoves as many elements as it needs and then moves the data in.
                    //Instead of move elements -> put data -> move elements -> put data
                    //vector::insert can already insert a iterator range at the start. I guess it probably does exactly what I need

                    RF_directMessage replyRF;
                    replyRF.senderID = -1;
                    replyRF.targetID = rf.senderID;
                    replyRF.snIdent = rf.snIdent;
                    replyRF.clientFlags = (uint8_t) clientFlags::reply;

                    /*
                     *<client identifier>
                     *<service name>
                     *<MDPW_REPLY>
                     *other data...
                     */
                    msg->sendKeep(*m_socket, sender->getIdentity(), replyRF);


                }
            }



            //  Remove & save client return envelope and insert the
            //  protocol header and service name, then rewrap envelope.
            //std::string client = msg->unwrap();
            //msg->wrap(MDPC_CLIENT, sender->m_service->m_name.c_str());
            //msg->wrap(client.c_str(), "");
            //msg->send(*m_socket);

            //s_console("E: invalid input message (%d)", (int) *command.c_str());
            //msg->dump();
            __itt_task_end(domain);
        }
        bool shouldRun = true;
        std::mutex running;

        void route() {
            std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
            std::chrono::system_clock::time_point heartbeat_at = now + HEARTBEAT_INTERVAL;
            std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
            uint64_t messageCounter = 0;
            bool pollNext = true; //Poll next time instead of going straight to receiving messages
            std::unique_lock<std::mutex> runLock(running);
            while (shouldRun) {
                zmq::pollitem_t items[] = {
                    { *m_socket,  0, ZMQ_POLLIN, 0 }
                };
                std::chrono::milliseconds timeout = std::chrono::duration_cast<std::chrono::milliseconds>(heartbeat_at - now);
                if (timeout < 0ms)
                    timeout = 0ms;

                int polled = 0;
                if (pollNext) {
                    __itt_task_begin(domain, __itt_null, __itt_null, handle_pollWait);
                    execAtReturn endTask([]() {__itt_task_end(domain); });
                    /*
                    Polling is good and all. But it creates a problem. Atleast in my Tests on Windows loopback poll blocks for atleast 5ms and max `timeout`
                    Which is rather dumb.. If our recv queue has more than one element in it, instead of processing it right away we process one element and then poll again
                    with each poll taking 5ms we effectively limit ourselves to 200 messages/s, which was confirmed by benchmarks.
                    See #dev slack 22.09.2017 actually the benchmarks shows about exactly 200 messages.
                    But we need asynchronous recv() because we also need to process our heartbeats. That's why I opted for a poll/async recv mix.
                     */
                    __itt_counter_inc(handle_pollCounter);
                    polled = zmq::poll(items, 1, 1ms);
                    if (items[0].revents & ZMQ_POLLIN) pollNext = false; //We got data! Maybe there is more. So better check for more data before polling again
                }


                //  Process next input message, if any
                if (!pollNext || items[0].revents & ZMQ_POLLIN) {
                    __itt_counter_inc(handle_recvCounter);
                    __itt_task_begin(domain, __itt_null, __itt_null, handle_routeMessage);
                    execAtReturn endTask([]() {__itt_task_end(domain); });
                    std::shared_ptr<zmsg> msg = std::make_shared<zmsg>(*m_socket);

                    //If we didn't get a message. Poll again
                    if (msg->parts() == 0 && (msg->getRoutingFrame().index() == 0 || msg->getRoutingFrame().valueless_by_exception())) {
                        pollNext = true;
                        continue;
                    }

                    if (m_verbose) {
                        //s_console("I: received message:");
                        //msg->dump();
                    }



                    /*
                    * sender name (added by server upon msg recv)
                    * <empty>
                    * <header>
                    */

                    RF_base routingBase(routingFrameType::none);
                    std::visit([&routingBase](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::nullptr_t>) {
                            __debugbreak();
                            return;
                        } else
                            routingBase = arg;
                    }, msg->getRoutingFrame());

                    if (routingBase.type == routingFrameType::none) {
                        __debugbreak();
                    }

                    std::shared_ptr<client> cli = get_client(routingBase.snIdent, routingBase.senderID);
                    if (!cli) {
                    #ifndef __GNUC__
                        __debugbreak();
                    #endif
                    }

                    switch (static_cast<routingFrameType>(msg->getRoutingFrame().index())) {

                        case routingFrameType::none: break;
                        case routingFrameType::serverMessage: {

                            RF_serverMessage rf = std::get<RF_serverMessage>(msg->getRoutingFrame());


                            switch (static_cast<serverMessageType>(rf.messageType)) {

                                case serverMessageType::ready: {
                                    std::cout << "rdy\n";
                                    if (cli->initialized) {//Can't init twice. Protocol violation
                                        disconnect_client(cli, true);
                                        break;
                                    }

                                    //  Attach worker to service and mark as idle
                                    auto server = getServer(cli->ident.serverIdentity);

                                    while (msg->parts()) {
                                        std::string service_name = msg->pop_front();
                                        cli->m_service = server->getService(service_name);
                                        cli->initialized = true;
                                        if (cli->m_service)
                                            cli->m_service->m_clients.emplace(cli);
                                    }
                                }break;
                                case serverMessageType::heartbeat: {
                                    std::cerr << "pong\n";
                                    cli->m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY;
                                }break;
                                case serverMessageType::disconnect: {
                                    disconnect_client(cli, 0);
                                }break;
                                default:;
                            }
                        }break;
                        case routingFrameType::directMessage: {
                            cli->m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY;//Receiving a message == heartbeat

                            RF_directMessage rf = std::get<RF_directMessage>(msg->getRoutingFrame());

                            //Forward
                            std::shared_ptr<client> rcv = get_client(rf.snIdent, rf.targetID);
                            if (!cli) {
                                //#TODO send error?
                            }

                            worker_send(rcv, msg);//forward directly
                        }break;
                        case routingFrameType::serviceMessage: {
                            cli->m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY;//Receiving a message == heartbeat

                            RF_serviceMsg rf = std::get<RF_serviceMsg>(msg->getRoutingFrame());

                            processServiceMessage(rf, cli, msg);
                        }break;
                        default: __debugbreak();
                    }


                    //s_console("E: invalid message:");
                    //msg->dump();
                    messageCounter++;
                }

                __itt_task_begin(domain, __itt_null, __itt_null, handle_heartBeating);

                //  Disconnect and delete any expired workers
                //  Send heartbeats to idle workers if needed
                now = std::chrono::system_clock::now();
                if (now >= heartbeat_at) {
                    check_timeouts();
                    for (auto& srv : m_servers)
                        for (auto& worker : srv.second->m_clients) {
                            std::cerr << "ping\n";
                            zmsg heartbeat;
                            RF_serverMessage rf(static_cast<uint32_t>(serverMessageType::heartbeat));
                            rf.senderID = -1;
                            //No server identification needed
                            heartbeat.setRoutingFrame(rf);
                            heartbeat.send(*m_socket, worker.second->getIdentity());
                        }
                    heartbeat_at += HEARTBEAT_INTERVAL;
                    now = std::chrono::system_clock::now();
                }
                __itt_task_end(domain);
                if (std::chrono::system_clock::now() > start + 1s) {
                    auto part = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::system_clock::now() - start).count();
                    __itt_frame_end_v3(domain, NULL);
                    std::cerr << std::dec << messageCounter*part << " " << part << "\n";
                    messageCounter = 0;
                    __itt_frame_begin_v3(domain, NULL);
                    start = std::chrono::system_clock::now();
                }

            }
            __debugbreak();
        }



        zmq::context_t * m_context;                  //  0MQ context
        zmq::socket_t * m_socket;                    //  Socket for clients & workers
        bool m_verbose;                               //  Print activity to stdout
        std::string m_endpoint;                      //  Broker binds to this endpoint

        std::map<uint64_t, std::shared_ptr<serverCli>> m_servers;


    };

    extern router* GRouter;
}