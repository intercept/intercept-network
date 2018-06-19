#include "router.h"
#include <json.hpp>
#include <fstream>
using namespace network::server;

router* network::server::GRouter{nullptr};

trafficLogger* trafficLog{};
std::mutex trafficMutex;
uint32_t outgoingTraffic;
uint32_t incomingTraffic;




router::router() {
    GRouter = this;
    m_context = new zmq::context_t(1);
    m_context->setctxopt(ZMQ_IO_THREADS, 8);
    m_socket = std::make_shared<zmq::socket_t>(*m_context, ZMQ_ROUTER);
    int hwm = 100000;
    m_socket->setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));
    m_verbose = false;
    m_localCluster = std::make_shared<LocalCluster>();
    trafficLog = new trafficLogger();

    std::thread([]() {
        std::ofstream tlog("P:/traffic.log");
        while (true) {
            std::this_thread::sleep_for(1s);
            trafficMutex.lock();
            tlog << incomingTraffic << "B\t" << outgoingTraffic << "B\n";
            incomingTraffic = outgoingTraffic = 0;
            tlog.flush();
            trafficMutex.unlock();
        }
    }).detach();

}


router::~router() {
    m_clusters.clear();
}




void trafficLogger::out(int bytes) {
    trafficMutex.lock();
    outgoingTraffic += bytes;
    trafficMutex.unlock();
}

void trafficLogger::in(int bytes) {
    trafficMutex.lock();
    incomingTraffic += bytes;
    trafficMutex.unlock();
}