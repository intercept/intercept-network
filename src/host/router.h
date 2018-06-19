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
#include "server.hpp"
#include "cluster.hpp"
using namespace std::chrono_literals;

static __itt_domain* domain = __itt_domain_create("router");
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

#include "defines.hpp"
namespace network::server {
    class service;

    class router {
    public:
        router();
        ~router();

        using serverCli = IServer;


        void bind(std::string endpoint) {
            m_endpoint = endpoint;
            m_socket->bind(m_endpoint.c_str());
            s_console("I: MDP broker/0.1.1 is active at %s", endpoint.c_str());
        }


        std::shared_ptr<ICluster> getCluster(uint64_t serverIdent, bool createIfNotExist = true) {
            if (auto found = m_localCluster->m_servers.find(serverIdent); found != m_localCluster->m_servers.end()) {
                return std::static_pointer_cast<ICluster>(m_localCluster);
            } else if (auto found = m_clusters.find(serverIdent); found != m_clusters.end()) {
                return found->second;
            } else if (createIfNotExist) {//#TODO may want to delegate to remote cluster
                std::shared_ptr<IServer> wrk = std::static_pointer_cast<IServer>(std::make_shared<LocalServer>(m_socket));
                m_localCluster->m_servers.insert(std::make_pair(serverIdent, wrk));
                if (m_verbose) {
                    s_console("I: registering new server: %lld", serverIdent);
                }
                return std::static_pointer_cast<ICluster>(m_localCluster);
            }
            return nullptr;
        }

        void stop() {
            shouldRun = false;
            running.lock();
            for (auto& srv : m_localCluster->m_servers) {
                srv.second->shutdown();
            }
            //#TODO tell other clusters that we leave
            m_socket->close();
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
                    if (msg->parts() == 0 && (msg->getRoutingFrame().index() == 0 || msg->getRoutingFrame().isInvalid())) {
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

                    auto& routingBase = msg->getRoutingBaseFrame();
                    if (routingBase.type == routingFrameType::none) {
                        __debugbreak();
                    }


                    auto cluster = getCluster(routingBase.snIdent);
                    if (!cluster) DEBUG_BREAK;

                    cluster->processMessage(msg, routingBase);

                    //s_console("E: invalid message:");
                    //msg->dump();
                    messageCounter++;
                }

                __itt_task_begin(domain, __itt_null, __itt_null, handle_heartBeating);

                //  Disconnect and delete any expired workers
                //  Send heartbeats to idle workers if needed
                now = std::chrono::system_clock::now();
                if (now >= heartbeat_at) {
                    for (auto& srv : m_localCluster->m_servers)
                        srv.second->check_timeouts(); //Disconnect timeouts and send heardbeats
                    heartbeat_at += HEARTBEAT_INTERVAL;
                    now = std::chrono::system_clock::now();
                }
                __itt_task_end(domain);
                if (std::chrono::system_clock::now() > start + 1s) {
                    auto part = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::system_clock::now() - start).count();
                    __itt_frame_end_v3(domain, NULL);
                    //std::cerr << std::dec << messageCounter*part << " " << part << "\n";
                    messageCounter = 0;
                    __itt_frame_begin_v3(domain, NULL);
                    start = std::chrono::system_clock::now();
                }

            }
            __debugbreak();
        }



        zmq::context_t * m_context;                  //  0MQ context
        std::shared_ptr<zmq::socket_t> m_socket;                    //  Socket for clients & workers
        bool m_verbose;                               //  Print activity to stdout
        std::string m_endpoint;                      //  Broker binds to this endpoint

        std::map<uint64_t, std::shared_ptr<ICluster>> m_clusters;//Map for server->cluster
        std::shared_ptr<LocalCluster> m_localCluster;


    };

    extern router* GRouter;
}