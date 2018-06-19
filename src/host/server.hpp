#pragma once
#include "host_client.hpp"
#include <map>
#include <utility>
#include "mdp.h"

class IServer {
public:
    virtual ~IServer() = default;
    virtual void processMessage(std::shared_ptr<zmsg> msg, const RF_base& routingBase) = 0;
    virtual void shutdown() = 0;
    virtual void check_timeouts() = 0;
};

class LocalServer : public IServer {
public:
    void processMessage(std::shared_ptr<zmsg> msg, const RF_base& routingBase) override;
    void shutdown() override;


    void disconnect_client(std::shared_ptr<Client> &wrk, bool sendDisconnectMessage) {
        assert(wrk);
        if (sendDisconnectMessage) {
            zmsg discMsg;
            RF_serverMessage rf(static_cast<uint32_t>(serverMessageType::disconnect));
            rf.senderID = -1;
            //No server identification needed

            discMsg.setRoutingFrame(rf);
            discMsg.send(*socket, wrk->getIdentity());
        }

        if (wrk->m_service) {
            wrk->m_service->m_clients.erase(wrk);
        }

        auto serverIdent = wrk->ident.serverIdentity;
        m_clients.erase(wrk->ident.clientID); //  This implicitly calls the worker destructor
        if (m_clients.empty()) {
            //m_servers.erase(serverIdent);
            //#TODO log server leave
            //#TODO mark server for deletion
        }
    }



    LocalServer(std::shared_ptr<zmq::socket_t> routerSocket) : socket(std::move(routerSocket)) {
        m_services.insert({ "rpc",std::make_shared<rpc_broker>("rpc", this) });
        m_services.insert({ "publicVariableService",std::make_shared<publicVariableService>(this) });
    }

    std::shared_ptr<Service> getService(std::string name, bool createIfNotExist = true);

    std::shared_ptr<Client> getClient(int32_t clientID);

    void check_timeouts() override {
        std::vector<std::shared_ptr<Client>> toPurge;
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        for (auto& worker : m_clients) {
            if (worker.second->m_expiry <= now)
                toPurge.push_back(worker.second);
        }
        for (auto& worker : toPurge) {
            if (m_verbose) {
                s_console("I: deleting expired worker: %d", worker->ident.clientID);
            }
            disconnect_client(worker, false);
        }

        for (auto& worker : m_clients) {
            //std::cerr << "ping\n";
            zmsg heartbeat;
            RF_serverMessage rf(static_cast<uint32_t>(serverMessageType::heartbeat));
            rf.senderID = -1;
            //No server identification needed
            heartbeat.setRoutingFrame(rf);
            heartbeat.send(*socket, worker.second->getIdentity());
        }

    }

private:

    void processServiceMessage(RF_serviceMsg& rf, std::shared_ptr<Client>& sender, std::shared_ptr<zmsg> msg);
    void service_internal(const std::string& service_name, std::shared_ptr<zmsg> msg, std::shared_ptr<Client>& sender);



public:
    void worker_send(std::shared_ptr<Client> worker, std::shared_ptr<zmsg> msg) const;

    void worker_send(std::shared_ptr<Client> worker, std::shared_ptr<zmsg> msg, routingFrame overrideRF,
                     std::string option = {}) const;

    void worker_send(std::shared_ptr<Client> worker, zmsg& msg, routingFrame overrideRF) const;

private:
    std::map<int32_t, std::shared_ptr<Client>> m_clients;
    std::map<std::string, std::shared_ptr<Service>> m_services;
    std::shared_ptr<zmq::socket_t> socket;
    uint64_t serverIdent = 0x123;
};

class RemoteServer : public IServer {
public:
    void processMessage(std::shared_ptr<zmsg> msg, const RF_base& routingBase) override;

private:
    std::unique_ptr<zmq::socket_t, std::function<void(zmq::socket_t*)>> socket;
};