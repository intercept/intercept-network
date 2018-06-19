#include "cluster.hpp"
void LocalCluster::processMessage(std::shared_ptr<zmsg> msg, const RF_base& routingBase) {
    auto found = m_servers.find(routingBase.snIdent);
    if (found == m_servers.end())
        DEBUG_BREAK;
    found->second->processMessage(msg, routingBase);
}

void RemoteCluster::processMessage(std::shared_ptr<zmsg> msg, const RF_base& routingBase) {
    msg->sendKeep(**socket);
}
