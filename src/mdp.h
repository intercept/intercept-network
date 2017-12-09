//
//  mdp.h
//  Majordomo Protocol definitions
//
#ifndef __MDP_H_INCLUDED__
#define __MDP_H_INCLUDED__

//  This is the version of MDP/Client we implement
#define MDPC_CLIENT         "MDPC01"

//  This is the version of MDP/Worker we implement
#define MDPW_WORKER         "MDPW01"

//  MDP/Server commands, as strings
#define MDPW_READY          "\001"
#define MDPW_REQUEST        "\002"
#define MDPW_REPLY          "\003"
#define MDPW_HEARTBEAT      "\004"
#define MDPW_DISCONNECT     "\005"

enum class clientFlags {
    none,
    request,
    reply
};

enum class serverMessageType {
    ready,
    heartbeat, //Heartbeats have no server Identification
    disconnect
};


static char *mdps_commands [] = {
    NULL, (char*)"READY", (char*)"REQUEST", (char*)"REPLY", (char*)"HEARTBEAT", (char*)"DISCONNECT"
};


class execAtReturn {
public:
    explicit execAtReturn(std::function<void()> func) :function(func) {}
    ~execAtReturn() { function(); }
private:
    std::function<void()> function;
};


#endif

