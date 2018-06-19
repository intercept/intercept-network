#pragma once
#include <chrono>
using namespace std::chrono_literals;

#define HEARTBEAT_LIVENESS  3       //  3-5 is reasonable
#define HEARTBEAT_INTERVAL  2500ms    //  msecs
#define HEARTBEAT_EXPIRY    HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS

static bool m_verbose = true;


#ifndef __GNUC__
#define DEBUG_BREAK __debugbreak()
#else
#define DEBUG_BREAK assert(false);
#endif