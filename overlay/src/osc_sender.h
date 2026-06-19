#pragma once
#include <cstdint>

struct OSCSettings {
    bool     enabled       = false;
    char     ip[64]        = "127.0.0.1";
    uint16_t port          = 9000;
    char     paramName[64] = "OVRDancers_Active";
};

// Init/shutdown — call once at startup and shutdown.
void OSCInit();
void OSCShutdown();

// Send an OSC int (0/1) to /avatar/parameters/<paramName>.
// Compatible with VRChat and Resonite OSC avatar parameters.
void OSCSendBool(const OSCSettings& s, const char* paramName, bool value);
