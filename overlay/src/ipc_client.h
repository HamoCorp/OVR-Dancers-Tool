#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include "../../shared/protocol.h"

class IPCClient {
public:
    IPCClient();
    ~IPCClient();

    bool Connect();
    void Disconnect();
    bool IsConnected() const { return m_pipe != INVALID_HANDLE_VALUE; }

    // Callbacks invoked on the receive thread
    std::function<void(const Msg_StateUpdate&)>      onStateUpdate;
    std::function<void(const Msg_DeviceListUpdate&)> onDeviceListUpdate;
    std::function<void()>                            onDisconnected;

    // Internal — public so the template helper in .cpp can reach it
    bool WriteWithTimeout(const void* data, DWORD size);

    // Send helpers (thread-safe)
    void SendSetMapping(const DeviceMapping& mapping);
    void SendClearMapping(uint32_t controllerIndex);
    void SendSetOffset(uint32_t controllerIndex, const Offset6DOF& offset);
    void SendEnableMapping(uint32_t controllerIndex);
    void SendDisableMapping(uint32_t controllerIndex);
    void SendRequestState();
    void SendInputUpdate(const Msg_InputUpdate& msg);
    void SendHideSettings(bool hideControllers, bool hideTrackers);

    // Start background receive loop
    void StartReceiveThread();

private:
    void ReceiveLoop();
    bool Send(const void* data, size_t size);

    HANDLE            m_pipe = INVALID_HANDLE_VALUE;
    std::mutex        m_sendMutex;
    std::thread       m_recvThread;
    std::atomic<bool> m_running{false};
};
