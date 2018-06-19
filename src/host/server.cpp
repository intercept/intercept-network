#include "server.hpp"
#include "router.h"

static __itt_string_handle* handle_processMessage = __itt_string_handle_create("processMessage");

void LocalServer::processMessage(std::shared_ptr<zmsg> msg, const RF_base& routingBase) {

    std::shared_ptr<Client> cli = getClient(routingBase.senderID);

    switch (msg->getRoutingFrame().type) {

        case routingFrameType::none: break;
        case routingFrameType::serverMessage: {

            auto& rf = std::get<RF_serverMessage>(msg->getRoutingFrame());

            switch (static_cast<serverMessageType>(rf.messageType)) {

                case serverMessageType::ready: {
                    std::cout << cli << "rdy\n";
                    if (cli->initialized) {
                        //Can't init twice. Protocol violation
                        std::cout << cli << "dblinit\n";
                        disconnect_client(cli, true);
                        break;
                    }

                    //  Attach worker to service and mark as idle
                    while (msg->parts()) {
                        std::string service_name = msg->pop_front();
                        cli->m_service           = getService(service_name);
                        cli->initialized         = true;
                        if (cli->m_service)
                            cli->m_service->m_clients.emplace(cli);
                    }
                }
                break;
                case serverMessageType::heartbeat: {
                    //std::cerr << "pong\n";
                    cli->m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY;
                }
                break;
                case serverMessageType::disconnect: {
                    disconnect_client(cli, 0);
                }
                break;
                default: ;
            }
        }
        break;
        case routingFrameType::directMessage: {
            cli->m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY; //Receiving a message == heartbeat

            RF_directMessage rf = std::get<RF_directMessage>(msg->getRoutingFrame());

            //Forward
            auto rcv = getClient(rf.targetID);
            if (!cli) {
                //#TODO send error?
            }

            worker_send(rcv, msg); //forward directly
        }
        break;
        case routingFrameType::serviceMessage: {
            cli->m_expiry = std::chrono::system_clock::now() + HEARTBEAT_EXPIRY; //Receiving a message == heartbeat

            RF_serviceMsg rf = std::get<RF_serviceMsg>(msg->getRoutingFrame());

            processServiceMessage(rf, cli, msg);
        }
        break;
        default: __debugbreak();
    }

}

void LocalServer::shutdown() {
    for (auto& cli : m_clients)
        disconnect_client(cli.second, true);
}

std::shared_ptr<Service> LocalServer::getService(std::string name, bool createIfNotExist) {
    assert(!name.empty());
    if (m_services.count(name)) {
        return m_services.at(name);
    } else if (createIfNotExist) {
        //#TODO makes no sense. Empty service is useless
        std::shared_ptr<Service> srv = std::make_shared<Service>(name, this);
        m_services.insert(std::make_pair(name, srv));
        //if (m_verbose) {
        //    s_console("I: received message:");
        //}
        return srv;
    }
    return nullptr;
}

std::shared_ptr<Client> LocalServer::getClient(int32_t clientID) {
    if (m_clients.count(clientID)) {
        return m_clients.at(clientID);
    } else {
        std::shared_ptr<Client> wrk = std::make_shared<Client>(clientID, serverIdent);
        m_clients.insert(std::make_pair(clientID, wrk));
        if (m_verbose) {
            s_console("I: registering new worker: %lld %d", serverIdent, clientID);
        }
        return wrk;
    }
}

void LocalServer::processServiceMessage(RF_serviceMsg& rf, std::shared_ptr<Client>& sender,
                                        std::shared_ptr<zmsg> msg) {
    __itt_task_begin(domain, __itt_null, __itt_null, handle_processMessage);
    assert(msg && msg->parts() >= 1); //  At least, command
    /*
    * <command>
    * data...
    */
    //std::cout << sender << "srv\n";
    if (!sender->initialized) {
        //Not initialized. Protocol violation
        std::cout << sender << "no init disconnect\n";
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

    std::shared_ptr<Service> srv = getService(serviceName);
    if (serviceName.length() >= 4
        && serviceName.find_first_of("srv.") == 0) {
        service_internal(serviceName, msg, sender);
    } else {
        std::cout << sender << " srv disp " << serviceName << "\n";
        if (srv)
            srv->dispatch(msg);
        else {
            //These moves prevent a unnecessary copy. But also discard the values making them unusable after the move
            //#TODO add a pushFront function that takes a vector. It premoves as many elements as it needs and then moves the data in.
            //Instead of move elements -> put data -> move elements -> put data
            //vector::insert can already insert a iterator range at the start. I guess it probably does exactly what I need

            RF_directMessage replyRF;
            replyRF.senderID    = -1;
            replyRF.targetID    = rf.senderID;
            replyRF.snIdent     = rf.snIdent;
            replyRF.clientFlags = (uint8_t)clientFlags::reply;

            /*
            *<client identifier>
            *<service name>
            *<MDPW_REPLY>
            *other data...
            */
            msg->sendKeep(*socket, sender->getIdentity(), replyRF);


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


__itt_string_handle* handle_service_internal = __itt_string_handle_create("service_internal");

void LocalServer::service_internal(const std::string& service_name, std::shared_ptr<zmsg> msg, std::shared_ptr<Client>& sender) {
    __itt_task_begin(domain, __itt_null, __itt_null, handle_service_internal);
    //std::cerr << "service\n";
    //msg->dump();
    if (service_name == "srv.service") {//#TODO string view literal
        //std::shared_ptr<service>  srv = m_services.at(msg->body());
        //if (srv && srv->m_clients.empty()) {
        //    msg->body_set("200");
        //} else {
        //    msg->body_set("404");
        //}
    } else if (service_name == "srv.echo") { } else {
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

    msg->send(*socket, sender->getIdentity());
    __itt_task_end(domain);
}

void LocalServer::worker_send(std::shared_ptr<Client> worker, std::shared_ptr<zmsg> msg) const {
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
    msg->send(*socket, worker->getIdentity());
}

void LocalServer::worker_send(std::shared_ptr<Client> worker, std::shared_ptr<zmsg> msg, routingFrame overrideRF,
                              std::string option) const {


    //  Stack protocol envelope to start of message
    if (option.size() > 0) {
        //  Optional frame after command
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
        msg->sendKeep(*socket, worker->getIdentity(), overrideRF);
}

void LocalServer::worker_send(std::shared_ptr<Client> worker, zmsg& msg, routingFrame overrideRF) const {
    msg.sendKeep(*socket, worker->getIdentity(), overrideRF);
}
