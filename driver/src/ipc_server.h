#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <vector>
#include <mutex>
#include <atomic>
#include "../../shared/protocol.h"

class DeviceProvider;

// IPC server running on its own thread.
// Accepts one client at a time (the overlay app).
// Transport: named pipe (Windows) / Unix domain socket (Linux).
class IPCServer {
public:
    explicit IPCServer(DeviceProvider* provider);
    ~IPCServer();

    void Run();   // Blocking — run on dedicated thread
    void Stop();

    // Send raw bytes to the connected client (thread-safe)
    void Broadcast(const void* data, size_t size);

private:
#ifdef _WIN32
    void HandleClient(HANDLE pipe);
#else
    void HandleClient(int clientFd);
#endif
    void Dispatch(const MsgHeader& hdr, const uint8_t* payload);

    DeviceProvider*    m_provider;
    std::atomic<bool>  m_running{false};

#ifdef _WIN32
    HANDLE             m_stopEvent  = INVALID_HANDLE_VALUE;
    std::mutex         m_clientMutex;
    HANDLE             m_clientPipe = INVALID_HANDLE_VALUE;
#else
    std::mutex         m_clientMutex;
    int                m_clientFd   = -1;
    int                m_stopPipe[2]= {-1,-1}; // self-pipe for stop signaling
#endif
};
