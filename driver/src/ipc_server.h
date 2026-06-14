#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <mutex>
#include <atomic>
#include "../../shared/protocol.h"

class DeviceProvider;

// Named pipe server running on its own thread.
// Accepts one client at a time (the overlay app).
class IPCServer {
public:
    explicit IPCServer(DeviceProvider* provider);
    ~IPCServer();

    void Run();   // Blocking — run on dedicated thread
    void Stop();

    // Send raw bytes to the connected client (thread-safe)
    void Broadcast(const void* data, size_t size);

private:
    void HandleClient(HANDLE pipe);
    void Dispatch(const MsgHeader& hdr, const uint8_t* payload);

    DeviceProvider*  m_provider;
    std::atomic<bool> m_running{false};
    HANDLE           m_stopEvent = INVALID_HANDLE_VALUE;

    std::mutex       m_clientMutex;
    HANDLE           m_clientPipe = INVALID_HANDLE_VALUE;
};
