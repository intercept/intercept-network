#include "client.h"
using namespace intercept::network::client;
client::client(std::string broker, std::string clientID, int verbose) {
    s_version_assert(4, 0);

    m_broker = broker;
    m_clientID = clientID;
    m_context = std::make_shared<zmq::context_t>(1);
    m_worker = nullptr;
    m_verbose = verbose;
    m_heartbeat = 2500ms; //  msecs
    m_reconnect = 2500ms; //  msecs

    s_catch_signals();
    connect_to_broker();

    m_signalStopAddr = "inproc://%lx%x" +std::to_string( (unsigned long) this) + std::to_string(rand());
    m_signalStopSock = std::make_unique<zmq::socket_t>(*m_context, ZMQ_PAIR);
    m_signalStopSock->bind(m_signalStopAddr);

    workThread = std::thread([this]() { work(); });
}

void client::connect_to_broker() {
    m_worker = std::make_shared<zmq::socket_t>(*m_context, ZMQ_DEALER);

    //No Lingering
    int linger = 0;
    m_worker->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    m_worker->setsockopt(ZMQ_IDENTITY, m_clientID.c_str(), m_clientID.length()); //#TODO clientID
    //s_set_id(*m_worker, 123);
    m_worker->connect(m_broker.c_str());
    if (m_verbose)
        s_console("I: connecting to broker at %s...", m_broker.c_str());

    //  Register rpc service

    send_to_broker((char*)MDPW_READY, {"rpc"}, NULL);

    //  If liveness hits zero, queue is considered disconnected
    m_liveness = HEARTBEAT_LIVENESS;
    m_heartbeat_at = std::chrono::system_clock::now() + m_heartbeat;
}

client::~client() {
    shouldStop = true;
    workThread.join();
}
