#pragma once
#include <string>
#include <set>
#include <memory>
#include "zmsg.hpp"
#include <map>

class Client;
class LocalServer;
class Service {
public:
    Service(std::string name, LocalServer* par) : m_name(name), parent(par) {}

    virtual ~Service() {
    }

    std::string m_name;             //  Service name
    //std::deque<std::shared_ptr<zmsg>> m_requests;   //  List of client requests
    std::set<std::shared_ptr<Client>> m_clients;  //  List of clients that have this service
    LocalServer* parent;

    virtual void dispatch(std::shared_ptr<zmsg> msg);
};

//#TODO seperate namespace for these services
class rpc_broker : public Service {
public:
    rpc_broker(std::string name, LocalServer* par) : Service(name, par) {}
    static bool match_target(const std::shared_ptr<Client>& cli, int32_t cs);
    void dispatch(std::shared_ptr<zmsg> msg) override;
};

class publicVariableService : public Service {
public:
    publicVariableService(LocalServer* par) : Service("publicVariableService", par) {}
    void dispatch(std::shared_ptr<zmsg> msg) override;

    std::map<std::string, std::string> JIPQueue;
};