#pragma once
#include "zmq.hpp"
#include "zmsg.hpp"
#include "services.hpp"
#include "defines.hpp"

class Client {
public:
    clientIdentity ident;
    std::shared_ptr<Service>  m_service;      //  Owning service, if known
    std::chrono::system_clock::time_point m_expiry;         //  Expires at unless heartbeat
    bool initialized{ false };

    Client(int32_t identity, uint64_t serverIdent, std::shared_ptr<Service> service = 0) {
        ident.clientID = identity;
        ident.serverIdentity = serverIdent;
        m_service = service;
        m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY;
    }
    clientIdentity getIdentity() {
        return ident;
    }
    ~Client() {

    }
};
