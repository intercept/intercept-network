#include "services.hpp"
#include <json.hpp>
#include "router.h"
using namespace intercept::network::server;

void service::dispatch(std::shared_ptr<zmsg> msg) {
    __debugbreak();
    //if (msg) { //  Queue message if any
    //    m_requests.push_back(msg);
    //}
    //
    //GRouter->check_timeouts();
    //while (!m_clients.empty() && !m_requests.empty()) {
    //    // Choose the most recently seen idle worker; others might be about to expire
    //    std::set<std::shared_ptr<client>>::iterator wrk = m_clients.begin();
    //    std::set<std::shared_ptr<client>>::iterator next = wrk;
    //    for (++next; next != m_clients.end(); ++next) {
    //        if ((*next)->m_expiry > (*wrk)->m_expiry)
    //            wrk = next;
    //    }
    //
    //    std::shared_ptr<zmsg> msg = m_requests.front();
    //    m_requests.pop_front();
    //    //GRouter->worker_send(*wrk, (char*)MDPW_REQUEST, "", msg);
    //}
}

bool rpc_broker::match_target(const std::shared_ptr<client>& cli, int32_t cs) {
    return cli->ident.clientID == cs;// cs == cli->m_identity; //#FIXME
}

void rpc_broker::dispatch(std::shared_ptr<zmsg> msg) {
    //if (msg) { //  Queue message if any
    //    m_requests.push_back(msg);
    //}
    std::cout << "route\n";

    RF_base routingBase = msg->getRoutingBaseFrame();

    auto sender = msg->peek_front(); //Don't care about who it came from
    nlohmann::json targetInfo = nlohmann::json::parse(sender);
    int32_t target = targetInfo[0]["clientID"];

    std::cout << "rpc from " << routingBase.snIdent << "|" << routingBase.senderID << " to " << target << "\n";
    msg->push_front(sender); //need to know where it came from
    //GRouter->check_timeouts();
    for (auto& cli : m_clients) {
        if (cli->ident.serverIdentity == routingBase.snIdent && match_target(cli, target)) {
            GRouter->worker_send(cli, msg);
        }
    }
}


enum class publicVariableSrvCommand {
    setVar,
    delVar,
    cleanJIP,
    getJIPQueue
};

void publicVariableService::dispatch(std::shared_ptr<zmsg> msg) {
    
    RF_base routingBase = msg->getRoutingBaseFrame();
    
    auto message = msg->peek_front(); //Don't care about who it came from
    nlohmann::json messageJson = nlohmann::json::parse(message);

    uint32_t command = messageJson["cmd"];

    switch(static_cast<publicVariableSrvCommand>(command)) {
        case publicVariableSrvCommand::setVar: {
            std::string varName = messageJson["name"];
            //nlohmann::json varValue = messageJson["value"];

            RF_serviceMsg rf(serviceType::pvar);
            rf.senderID = routingBase.senderID;
            rf.snIdent = routingBase.snIdent;

            for (auto& cli : m_clients) {
                GRouter->worker_send(cli, msg, rf);
            }

            JIPQueue[varName] = message;
        }break;
        case publicVariableSrvCommand::delVar: {
            std::string varName = messageJson["name"];
            std::string varValue = messageJson["value"];

            RF_serviceMsg rf(serviceType::pvar);
            rf.senderID = routingBase.senderID;
            rf.snIdent = routingBase.snIdent;

            for (auto& cli : m_clients) {
                GRouter->worker_send(cli, msg, rf);
            }

            JIPQueue.erase(varName);
        }break;
        case publicVariableSrvCommand::cleanJIP: {
            if (routingBase.senderID == 0) JIPQueue.clear();
        }break;
        case publicVariableSrvCommand::getJIPQueue: {
            auto senderID = routingBase.senderID;
            auto& cli = std::find_if(m_clients.begin(), m_clients.end(),[senderID](const std::shared_ptr<client>& v){
                return v->ident.clientID == senderID;
            });

            RF_serviceMsg rf(serviceType::pvar);
            rf.senderID = 0;
            rf.snIdent = routingBase.snIdent;

            zmsg outMessage;

            for (auto& it : JIPQueue) {
                outMessage.push_front(it.second);
                GRouter->worker_send(*cli, outMessage, rf);
                outMessage.clear();
            }
        }break;
        default: ;
    }




}
