#pragma once
#include <string>
#include <set>
#include <memory>
#include <deque>
#include "zmsg.hpp"
#include <map>

namespace intercept::network::server {
    class client;
    class service {
    public:
        service(std::string name) : m_name(name) {}

        virtual ~service() {
        }

        std::string m_name;             //  Service name
        //std::deque<std::shared_ptr<zmsg>> m_requests;   //  List of client requests
        std::set<std::shared_ptr<client>> m_clients;  //  List of clients that have this service

        virtual void dispatch(std::shared_ptr<zmsg> msg);
    };

    class rpc_broker : public service {
    public:
        rpc_broker(std::string name) : service(name) {}
        static bool match_target(const std::shared_ptr<client>& cli, int32_t cs);
        void dispatch(std::shared_ptr<zmsg> msg) override;
    };

    class publicVariableService : public service {
    public:
        publicVariableService() : service("publicVariableService") {}
        void dispatch(std::shared_ptr<zmsg> msg) override;

        std::map<std::string, std::string> JIPQueue;
    };


}
