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
        std::string m_identity;   //  clientID of worker
        std::shared_ptr<service>  m_service;      //  Owning service, if known
        std::chrono::system_clock::time_point m_expiry;         //  Expires at unless heartbeat
        bool initialized{ false };

        client(std::string identity, std::shared_ptr<service>  service = 0, std::chrono::system_clock::time_point expiry = {}) {
            m_identity = identity;
            m_service = service;
            m_expiry = expiry;
        }
    };

    class service {
    public:
        service(std::string name) : m_name(name) {}

        virtual ~service() {
            m_requests.clear();
        }

        std::string m_name;             //  Service name
        std::deque<std::shared_ptr<zmsg>> m_requests;   //  List of client requests
        std::set<std::shared_ptr<client>> m_clients;  //  List of clients that have this service

        virtual void dispatch(std::shared_ptr<zmsg> msg);
    };

    class rpc_broker : public service {
    public:
        rpc_broker(std::string name) : service(name) {}
        static bool match_target(const std::shared_ptr<client>& cli, const std::string& cs);
        void dispatch(std::shared_ptr<zmsg> msg) override;
    };

    class router {
    public:
        router();
        ~router();



        void bind(std::string endpoint) {
            m_endpoint = endpoint;
            m_socket->bind(m_endpoint.c_str());
            s_console("I: MDP broker/0.1.1 is active at %s", endpoint.c_str());
        }

        void check_timeouts() {
            std::vector<std::shared_ptr<client>> toPurge;
            std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
            for (auto& worker : m_clients) {
                if (worker.second->m_expiry <= now)
                    toPurge.push_back(worker.second);
            }
            for (auto& worker : toPurge) {
                if (m_verbose) {
                    s_console("I: deleting expired worker: %s",
                        worker->m_identity.c_str());
                }
                disconnect_client(worker, false);
            }
        }
        void disconnect_client(std::shared_ptr<client> &wrk, bool sendDisconnectMessage) {
            assert(wrk);
            if (sendDisconnectMessage) {
                worker_send(wrk, MDPW_DISCONNECT, "");
            }

            if (wrk->m_service) {
                wrk->m_service->m_clients.erase(wrk);
            }
            //  This implicitly calls the worker destructor
            m_clients.erase(wrk->m_identity);
        }

        void worker_send(std::shared_ptr<client> worker, char *command, std::string option = {}, std::shared_ptr<zmsg> msg = {}) const {
            msg = (msg ? std::make_shared<zmsg>(*msg) : std::make_shared<zmsg>());

            //  Stack protocol envelope to start of message
            if (option.size() > 0) {                 //  Optional frame after command
                msg->push_front(option);
            }
            msg->push_front(command);
            //  Stack routing envelope to start of message
            msg->wrap(worker->m_identity.c_str(), "");

            if (m_verbose) {
                s_console("I: sending %s to worker",
                    mdps_commands[static_cast<int>(*command)]);
                msg->dump();
            }
            /*
             * identity
             * <empty>
             * command
             * option(optional)
             */
            msg->send(*m_socket);
        }
        std::shared_ptr<service> get_service(std::string name) {
            assert(name.size() > 0);
            if (m_services.count(name)) {
                return m_services.at(name);
            } else {
                std::shared_ptr<service> srv = std::make_shared<service>(name);
                m_services.insert(std::make_pair(name, srv));
                if (m_verbose) {
                    s_console("I: received message:");
                }
                return srv;
            }
        }
        std::shared_ptr<client>  get_client(std::string identity) {
            assert(identity.length() != 0);

            //  self->workers is keyed off worker identity
            if (m_clients.count(identity)) {
                return m_clients.at(identity);
            } else {
                std::shared_ptr<client> wrk = std::make_shared<client>(identity);
                m_clients.insert(std::make_pair(identity, wrk));
                if (m_verbose) {
                    s_console("I: registering new worker: %s", identity.c_str());
                }
                return wrk;
            }
        }

        void stop() {
            shouldRun = false;
            running.lock();
            for (auto& cli : m_clients) {
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
                std::shared_ptr<service>  srv = m_services.at(msg->body());
                if (srv && srv->m_clients.empty()) {
                    msg->body_set("200");
                } else {
                    msg->body_set("404");
                }
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

        void processMessage(std::shared_ptr<client>& sender, std::shared_ptr<zmsg> msg) {
            __itt_task_begin(domain, __itt_null, __itt_null, handle_processMessage);
            assert(msg && msg->parts() >= 1);     //  At least, command
            /*
             * <command>
             * data...
             */
            std::string command = msg->pop_front();

            if (command.compare(MDPW_READY) == 0) {

                if (sender->initialized) {//Can't init twice. Protocol violation
                    __itt_task_end(domain);
                    disconnect_client(sender, true);
                    return;
                }

                //  Attach worker to service and mark as idle
                std::string service_name = msg->pop_front();
                sender->m_service = get_service(service_name);
                sender->initialized = true;
                if (sender->m_service)
                    sender->m_service->m_clients.emplace(sender);

            } else if (command.compare(MDPW_REQUEST) == 0) {
                if (!sender->initialized) {//Not initialized. Protocol violation
                    __itt_task_end(domain);
                    disconnect_client(sender, true);
                    return;
                }

                std::string service_name = msg->pop_front();
                std::shared_ptr<service> srv = get_service(service_name);
                //  Set reply return address to client sender
                msg->push_front(sender->m_identity);//This is being read inside the service to know where to reply back to
                if (service_name.length() >= 4
                    && service_name.find_first_of("srv.") == 0) {
                    service_internal(service_name, msg);
                } else {
                    if (srv)
                        srv->dispatch(msg);
                    else {
                        std::string client = msg->unwrap();
                        //These moves prevent a unnecessary copy. But also discard the values making them unusable after the move
                        //#TODO add a pushFront function that takes a vector. It premoves as many elements as it needs and then moves the data in.
                        //Instead of move elements -> put data -> move elements -> put data
                        //vector::insert can already insert a iterator range at the start. I guess it probably does exactly what I need
                        msg->wrap(MDPW_REPLY, std::move(service_name));
                        msg->wrap(std::move(client), "");
                        /*
                         *<client identifier>
                         *<service name>
                         *<MDPW_REPLY>
                         *other data...
                         */
                        msg->send(*m_socket);


                    }
                }



                //  Remove & save client return envelope and insert the
                //  protocol header and service name, then rewrap envelope.
                //std::string client = msg->unwrap();
                //msg->wrap(MDPC_CLIENT, sender->m_service->m_name.c_str());
                //msg->wrap(client.c_str(), "");
                //msg->send(*m_socket);
            } else  if (command.compare(MDPW_DISCONNECT) == 0) {
                disconnect_client(sender, 0);
            } else {
                s_console("E: invalid input message (%d)", (int) *command.c_str());
                msg->dump();
            }
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
                    __itt_task_end(domain);
                }


                //  Process next input message, if any
                if (!pollNext || items[0].revents & ZMQ_POLLIN) {
                    __itt_counter_inc(handle_recvCounter);
                    __itt_task_begin(domain, __itt_null, __itt_null, handle_routeMessage);
                    std::shared_ptr<zmsg> msg = std::make_shared<zmsg>(*m_socket);

                    if (msg->parts() == 0) {
                        pollNext = false;
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

                    auto sender = msg->pop_front();
                    auto empty = msg->pop_front(); //empty message
                    auto header = msg->pop_front();
                    std::shared_ptr<client> cli = get_client(sender);
                    if (!cli) {
                    #ifndef __GNUC__
                        __debugbreak();
                    #endif
                    }
                    //              std::cout << "sbrok, sender: "<< sender << std::endl;
                    //              std::cout << "sbrok, header: "<< header << std::endl;
                    //              std::cout << "msg size: " << msg->parts() << std::endl;
                    //              msg->dump();
                    if (header.compare(MDPW_HEARTBEAT) == 0) {//Hardbeats handled here directly for top performance
                        std::cerr << "pong\n";
                        cli->m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY;
                    } else if (header.compare(MDPW_WORKER) == 0) {
                        cli->m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY;//Receiving a message == heartbeat
                        processMessage(cli, msg);
                    } else {
                        s_console("E: invalid message:");
                        msg->dump();
                    }
                    messageCounter++;
                    __itt_task_end(domain);
                }

                __itt_task_begin(domain, __itt_null, __itt_null, handle_heartBeating);

                //  Disconnect and delete any expired workers
                //  Send heartbeats to idle workers if needed
                now = std::chrono::system_clock::now();
                if (now >= heartbeat_at) {
                    check_timeouts();
                    for (auto& worker : m_clients) {
                        std::cerr << "ping\n";
                        worker_send(worker.second, MDPW_HEARTBEAT);
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
        }



        zmq::context_t * m_context;                  //  0MQ context
        zmq::socket_t * m_socket;                    //  Socket for clients & workers
        bool m_verbose;                               //  Print activity to stdout
        std::string m_endpoint;                      //  Broker binds to this endpoint
        std::map<std::string, std::shared_ptr<service>> m_services;  //  Hash of known services
        std::map<std::string, std::shared_ptr<client>> m_clients;    //  Hash of known workers
    };

    static router* GRouter;
}