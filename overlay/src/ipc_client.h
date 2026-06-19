#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
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
#ifdef _WIN32
    bool IsConnected() const { return m_pipe != INVALID_HANDLE_VALUE; }
#else
    bool IsConnected() const { return m_sockFd >= 0; }
#endif

    // Callbacks invoked on the receive thread
    std::function<void(const Msg_StateUpdate&)>      onStateUpdate;
    std::function<void(const Msg_DeviceListUpdate&)> onDeviceListUpdate;
    std::function<void()>                            onDisconnected;

    // Send helpers (thread-safe, may be called from any thread)
    void SendSetMapping(const DeviceMapping& mapping);
    void SendClearMapping(uint32_t controllerIndex);
    void SendSetOffset(uint32_t controllerIndex, const Offset6DOF& offset);
    void SendEnableMapping(uint32_t controllerIndex);
    void SendDisableMapping(uint32_t controllerIndex);
    void SendRequestState();
    void SendInputUpdate(const Msg_InputUpdate& msg);
    void SendHideSettings(bool hideControllers, bool hideTrackers);
    void SendCreateVirtualControllers();
    void SendSetVirtCtrlActive(uint8_t side, bool active);

    void StartReceiveThread();

    // Internal — used by send helper
    bool WriteAll(const void* data, size_t size);

private:
    void ReceiveLoop();
    bool ReadAll(void* dst, size_t size);

#ifdef _WIN32
    HANDLE            m_pipe = INVALID_HANDLE_VALUE;
#else
    int               m_sockFd = -1;
#endif
    std::mutex        m_sendMutex;
    std::thread       m_recvThread;
    std::atomic<bool> m_running{false};
};
