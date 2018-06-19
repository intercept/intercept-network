#pragma once
#include "server.hpp"

class ICluster {
public:
    virtual ~ICluster() = default;
    virtual void processMessage(std::shared_ptr<zmsg> msg, const RF_base& routingBase) = 0;
};

class LocalCluster : public ICluster {
public:
    void processMessage(std::shared_ptr<zmsg> msg, const RF_base& routingBase) override;


    std::map<uint64_t, std::shared_ptr<IServer>> m_servers;
};

class RemoteCluster : public ICluster {
public:
    void processMessage(std::shared_ptr<zmsg> msg, const RF_base& routingBase) override;


private:
    std::unique_ptr<zmq::socket_t*, std::function<void(zmq::socket_t*)>> socket;
};