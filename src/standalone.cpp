#include "host/router.h"
#include "client/client.h"
#include <future>
using namespace std::literals;

//linux 
// cd vcproj
//CC=gcc-7 CXX=g++-7 cmake -DBUILD_STATIC=true -DWITH_PERF_TOOL=false -DZMQ_BUILD_TESTS=false ..

std::array<char, 16384> msg16k;
std::array<char, 32> msg32;

//#define TESTHOST "tcp://37.59.55.33:5555"
#define TESTHOST "tcp://127.0.0.1:5555"


int pingperfTest(uint32_t clientID = 123) {
    network::client::client session(TESTHOST, clientID, 1);
    std::cout << "pingtest\n";

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    uint32_t msgCount = 0;
    std::chrono::microseconds avgPing = 0us;
    while (true) {
        //zmsg * req = new zmsg("Hello world");
        //session.send("srv.echo", req);
        auto req = std::make_shared<zmsg>(msg16k.data());
        auto pre = std::chrono::high_resolution_clock::now();
        session.send("srv.echo", req);
        //std::shared_ptr<zmsg> request = session.recv().second;
        auto post = std::chrono::high_resolution_clock::now();

        auto ping = std::chrono::duration_cast<std::chrono::microseconds>(post - pre);

        if (avgPing == 0us) avgPing = ping;
        else avgPing = (avgPing + ping) / 2;
        msgCount++;
        //if (request == nullptr) {
        //    break;              //  Worker was interrupted
        //}
        //std::cerr << "recv\n";
        //request->dump();

        if (std::chrono::system_clock::now() > start + 1s) {
            auto part = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::system_clock::now() - start).count();
            std::cerr << std::dec << avgPing.count() << "us  " << msgCount*part << "/s " << part << "\n";
            msgCount = 0;
            avgPing = 0us;
            start = std::chrono::system_clock::now();
        }

    }
    return 0;
}

int clientListener(uint32_t clientID = 5) {
    network::client::client session(TESTHOST, clientID, 1);


    session.asynchronousRequestHandler = [](std::shared_ptr<zmsg> msg) {
        std::cout << "message\n";
    };

    session.synchronousRequestHandler = [](std::shared_ptr<zmsg> msg) {
        std::cout << "request\n";
        msg->clear();
        msg->push_front("Hello answer!"sv);
    };

    session.serviceHandlers[serviceType::pvar] = [](std::shared_ptr<zmsg> msg) {
        std::string dataStr = msg->pop_front();
        std::cout << "pvar\n" << dataStr<< "\n";
    };


    while (true)
        std::this_thread::sleep_for(500s);
    return 0;
}

int clientRequester(uint32_t clientID = 4) {
    network::client::client session(TESTHOST, clientID, 1);

    std::this_thread::sleep_for(2s);

    while (true) {
        std::this_thread::sleep_for(1s);
        std::cout << "rq\n";
        session.sendMessage(std::make_shared<zmsg>("testMessage"), 5);
    }

    return 0;
}

int clientAnswerRequester(uint32_t clientID = 4) {
    network::client::client session(TESTHOST, clientID, 1);

    std::this_thread::sleep_for(5s);

    while (true) {
        std::this_thread::sleep_for(1s);

        nlohmann::json info;
        info["name"] = "myvar";
        info["value"] = "value";
        info["cmd"] = 0;


        auto msg = std::make_shared<zmsg>();
        msg->push_front(info.dump(-1));

        session.send(serviceType::pvar, msg, false);

        session.sendRequest(std::make_shared<zmsg>("testMessage"), 5,[](std::shared_ptr<zmsg> answer) -> void
        {
            std::cout << "Answer!!! \n";
            answer->dump();
        });
    }

    return 0;
}

std::future<std::shared_ptr<zmsg>> makeRequest(network::client::client &session, std::shared_ptr<zmsg> data, uint32_t target) {
    auto p = std::make_shared<std::promise<std::shared_ptr<zmsg>>>();
    std::future<std::shared_ptr<zmsg>> f3 = p->get_future();
    session.sendRequest(data,target,[p](std::shared_ptr<zmsg> answ)
    {
        p->set_value(answ);
    });
    return f3;
}

int clientAnswerAsyncRequester(uint32_t clientID = 4) {
    network::client::client session(TESTHOST, clientID, 1);

    std::this_thread::sleep_for(5s);

    while (true) {
        std::this_thread::sleep_for(1s);
        auto fut = makeRequest(session, std::make_shared<zmsg>("testRequest"), 5);
        std::cout << "wait...\n";
        fut.wait();
        std::cout << "Answer!!! \n";
        fut.get()->dump();
    }

    return 0;
}

int main(int argc, char *argv[]) {
    std::generate(msg16k.begin(), msg16k.end(),[]() {
        static char id = 'a'-1;
        if (id == 'z'+1) id = 'a'-1;
        id++;
        return id;
    });
    std::generate(msg32.begin(), msg32.end(), []() {
        static char id = 'a' - 1;
        if (id == 'z' + 1) id = 'a' - 1;
        id++;
        return id;
    });

    int verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);

    //std::thread([]() {pingperfTest("15553"); }).detach();
    //std::thread([]() {pingperfTest("15753"); }).detach();
    //std::thread([]() {pingperfTest("15543"); }).detach();
    //if (argc == 1)
    if (argc > 1 && std::string(argv[1]) == "ping"sv)
        pingperfTest(123);
    if (argc > 1 && std::string(argv[1]) == "ping2"sv) {
        std::thread t1([]() { pingperfTest(15553); });
        std::thread t2([]() { pingperfTest(15753); });
        std::thread t3([]() { pingperfTest(15543); });
        pingperfTest(123);
    }
    if (argc > 1 && std::string(argv[1]) == "li"sv)
        clientListener();
    if (argc > 1 && std::string(argv[1]) == "req"sv)
        clientAnswerRequester();
    //clientAnswerRequester();
    //clientAnswerAsyncRequester();

    s_version_assert(4, 0);
    s_catch_signals();
    network::server::router rt;
    rt.bind("tcp://*:5555");
    //std::thread ([&rt](){rt.route();}).detach();
    rt.route();
    printf("exiting\n");
    //broker brk(1);
    //brk.bind("tcp://127.0.0.1:5555");
    //
    //brk.start_brokering();

    if (s_interrupted)
        printf("W: interrupt received, shutting down...\n");

    return 0;
}


