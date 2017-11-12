#include <stdio.h>
#include <cstdint>
#include <atomic>


#include "router.h"
#include <intercept.hpp>
#include "client.h"


int intercept::api_version() {
    return 1;
}

void  intercept::on_frame() {}
intercept::network::client::client* pClient = nullptr;
intercept::network::server::router* pRouter = nullptr;

void intercept::pre_start() {
    if (sqf::is_server()) {  //We need to host a Server
        if (!network::server::GRouter) network::server::GRouter = new intercept::network::server::router();
        network::server::GRouter->bind("tcp://0.0.0.0:5555");  //#TODO correct port
        std::thread([]() { network::server::GRouter->route(); }).detach();
    }
}

void  intercept::pre_init() {
    if (!sqf::is_server()) {
    #ifndef __linux__
        if (pClient) __debugbreak();
    #endif
        pClient = new intercept::network::client::client("tcp://", std::to_string(sqf::client_owner()));


        pClient->asynchronousRequestHandler = [](std::shared_ptr<zmsg> msg)
        {
            std::cout << "message\n";
        };

        pClient->synchronousRequestHandler = [](std::shared_ptr<zmsg> msg) {
            std::cout << "request\n";
            msg->clear();
            msg->push_front("Hello answer!");
        };







    }
}

void intercept::post_init() {

}

void intercept::mission_stopped() {
    //Server never closes socket..
    //#TODO we might tell clients to leave though.. But they should already leave by themselves
    //GRouter.stop();
    if (pClient) delete pClient; //#TODO we probably need to disconnect first ^^
    pClient = nullptr;
}
