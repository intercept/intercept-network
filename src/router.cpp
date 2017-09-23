#include "router.h"
using namespace intercept::network::server;

void service::dispatch(std::shared_ptr<zmsg> msg) {
    if (msg) { //  Queue message if any
        m_requests.push_back(msg);
    }

    GRouter->check_timeouts();
    while (!m_clients.empty() && !m_requests.empty()) {
        // Choose the most recently seen idle worker; others might be about to expire
        std::set<std::shared_ptr<client>>::iterator wrk = m_clients.begin();
        std::set<std::shared_ptr<client>>::iterator next = wrk;
        for (++next; next != m_clients.end(); ++next) {
            if ((*next)->m_expiry > (*wrk)->m_expiry)
                wrk = next;
        }

        std::shared_ptr<zmsg> msg = m_requests.front();
        m_requests.pop_front();
        GRouter->worker_send(*wrk, (char*)MDPW_REQUEST, "", msg);
    }
}

bool rpc_broker::match_target(const std::shared_ptr<client>& cli, const std::string& cs) {
    return cs == cli->m_identity;
}

void rpc_broker::dispatch(std::shared_ptr<zmsg> msg) {
    if (msg) { //  Queue message if any
        m_requests.push_back(msg);
    }

    /*
     * <identity>
     * data from packet
     *
     *
     */

    auto sender = msg->pop_front(); //Don't care about who it came from
    std::string target = msg->pop_front();
    GRouter->check_timeouts();
    while (!m_clients.empty() && !m_requests.empty()) {
        // Choose the most recently seen idle worker; others might be about to expire
        for (auto& cli : m_clients) {
            if (match_target(cli, target)) {
                std::shared_ptr<zmsg> msg = m_requests.front();
                m_requests.pop_front();
                GRouter->worker_send(cli, MDPW_REQUEST, "", msg);
            }
        }
    }
}

router::router() {
    GRouter = this;
    m_context = new zmq::context_t(1);
    m_socket = new zmq::socket_t(*m_context, ZMQ_ROUTER);
    //int hwm = 10000;
    //m_socket->setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));
    m_verbose = false;
    m_services.insert({ "rpc",std::make_shared<rpc_broker>("rpc") });

}


router::~router() {
    m_services.clear();
    m_clients.clear();
}
