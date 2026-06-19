#pragma once
#include <cstdint>

struct WSSettings {
    bool     enabled       = false;
    uint16_t port          = 8080;
    char     paramName[64] = "OVRDancers_Active";
};

// Start/stop the WebSocket server background thread.
void WSInit(const WSSettings& s);
void WSShutdown();

// Broadcast a parameter value to all connected clients as JSON:
//   {"param":"<paramName>","value":1}
void WSBroadcast(const char* paramName, bool value);

// Restart the server with new settings (stops old, starts new if enabled).
void WSUpdateSettings(const WSSettings& s);
