#include "router.h"
#include <json.hpp>
using namespace intercept::network::server;

router* intercept::network::server::GRouter{nullptr};

router::router() {
    GRouter = this;
    m_context = new zmq::context_t(1);
    m_socket = new zmq::socket_t(*m_context, ZMQ_ROUTER);
    //int hwm = 10000;
    //m_socket->setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));
    m_verbose = false;
}


router::~router() {
    m_servers.clear();
}
