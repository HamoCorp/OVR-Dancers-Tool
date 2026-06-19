#include "ipc_client.h"
#include "log.h"
#include <cstring>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════════
// Windows implementation (named pipe + overlapped I/O)
// ═══════════════════════════════════════════════════════════════════════════════
#ifdef _WIN32

IPCClient::IPCClient() = default;
IPCClient::~IPCClient() { Disconnect(); }

bool IPCClient::Connect() {
    LOG_INFO("IPC: connecting to driver pipe...");
    if (!WaitNamedPipeA(OVRDANCERS_PIPE_NAME, 2000)) {
        LOG_WARN("IPC: pipe not available (driver not running?)");
        return false;
    }
    m_pipe = CreateFileA(
        OVRDANCERS_PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);

    if (m_pipe == INVALID_HANDLE_VALUE) {
        LOG_ERR("IPC: CreateFileA failed: %lu", GetLastError());
        return false;
    }
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(m_pipe, &mode, nullptr, nullptr);
    LOG_INFO("IPC: connected");
    return true;
}

void IPCClient::Disconnect() {
    m_running = false;
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIo(m_pipe);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
    if (m_recvThread.joinable()) m_recvThread.join();
}

bool IPCClient::WriteAll(const void* data, size_t size) {
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    DWORD written = 0;
    BOOL ok = WriteFile(m_pipe, data, (DWORD)size, &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 500) == WAIT_TIMEOUT) {
            LOG_ERR("IPC send: timeout"); CancelIo(m_pipe);
        }
        ok = GetOverlappedResult(m_pipe, &ov, &written, TRUE);
    }
    CloseHandle(ov.hEvent);
    return ok && written == (DWORD)size;
}

bool IPCClient::ReadAll(void* dst, size_t size) {
    uint8_t* p = (uint8_t*)dst;
    size_t   rem = size;
    while (rem > 0) {
        if (!m_running) return false;
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        DWORD read = 0;
        BOOL ok = ReadFile(m_pipe, p, (DWORD)rem, &read, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, 5000);
            if (wait == WAIT_TIMEOUT) CancelIo(m_pipe);
            ok = GetOverlappedResult(m_pipe, &ov, &read, TRUE);
        }
        CloseHandle(ov.hEvent);
        if (!ok) {
            // ERROR_OPERATION_ABORTED = clean 5 s idle timeout — not a real disconnect
            if (GetLastError() == ERROR_OPERATION_ABORTED) continue;
            return false; // broken pipe or intentional shutdown
        }
        p += read; rem -= (size_t)read;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Linux implementation (Unix domain socket)
// ═══════════════════════════════════════════════════════════════════════════════
#else

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

IPCClient::IPCClient() = default;
IPCClient::~IPCClient() { Disconnect(); }

bool IPCClient::Connect() {
    LOG_INFO("IPC: connecting to driver socket...");
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { LOG_ERR("IPC: socket() failed"); return false; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, OVRDANCERS_PIPE_NAME, sizeof(addr.sun_path)-1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_WARN("IPC: connect failed (driver not running?)");
        close(fd); return false;
    }
    m_sockFd = fd;
    LOG_INFO("IPC: connected");
    return true;
}

void IPCClient::Disconnect() {
    m_running = false;
    if (m_sockFd >= 0) { shutdown(m_sockFd, SHUT_RDWR); close(m_sockFd); m_sockFd = -1; }
    if (m_recvThread.joinable()) m_recvThread.join();
}

bool IPCClient::WriteAll(const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    size_t rem = size;
    while (rem > 0) {
        ssize_t n = write(m_sockFd, p, rem);
        if (n <= 0) { LOG_ERR("IPC send: write failed"); return false; }
        p += n; rem -= (size_t)n;
    }
    return true;
}

bool IPCClient::ReadAll(void* dst, size_t size) {
    uint8_t* p = (uint8_t*)dst;
    size_t rem = size;
    while (rem > 0) {
        ssize_t n = recv(m_sockFd, p, rem, 0);
        if (n <= 0) return false;
        p += n; rem -= (size_t)n;
    }
    return true;
}

#endif // _WIN32

// ── Shared receive loop ───────────────────────────────────────────────────────
void IPCClient::StartReceiveThread() {
    m_running = true;
    m_recvThread = std::thread([this]() { ReceiveLoop(); });
}

void IPCClient::ReceiveLoop() {
    LOG_INFO("IPC recv: thread started");
    while (m_running) {
        MsgHeader hdr;
        if (!ReadAll(&hdr, sizeof(hdr))) {
            LOG_WARN("IPC recv: header read failed — disconnected");
            break;
        }
        std::vector<uint8_t> buf(hdr.payloadSize);
        if (hdr.payloadSize > 0 && !ReadAll(buf.data(), hdr.payloadSize)) {
            LOG_WARN("IPC recv: payload read failed");
            break;
        }
        switch (hdr.type) {
        case MsgType::StateUpdate:
            if (onStateUpdate) {
                Msg_StateUpdate msg; memcpy(&msg, buf.data(), sizeof(msg));
                onStateUpdate(msg);
            }
            break;
        case MsgType::DeviceListUpdate:
            if (onDeviceListUpdate) {
                Msg_DeviceListUpdate msg; memcpy(&msg, buf.data(), sizeof(msg));
                onDeviceListUpdate(msg);
            }
            break;
        default:
            LOG_WARN("IPC recv: unknown msg type %u", (unsigned)hdr.type);
            break;
        }
    }
    LOG_INFO("IPC recv: thread exiting");
    if (onDisconnected && m_running) onDisconnected(); // only on unexpected disconnect, not clean Disconnect()
}

// ── Send helpers (shared, thread-safe via m_sendMutex) ───────────────────────
template<typename T>
static void SendMsg(IPCClient& c, std::mutex& mtx, bool connected,
                    MsgType type, const T& payload) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!connected) { LOG_WARN("IPC send: not connected"); return; }
    MsgHeader hdr{ type, sizeof(T) };
    if (!c.WriteAll(&hdr, sizeof(hdr))) return;
    c.WriteAll(&payload, sizeof(payload));
}

void IPCClient::SendSetMapping(const DeviceMapping& m) {
    Msg_SetMapping msg{m}; SendMsg(*this, m_sendMutex, IsConnected(), MsgType::SetMapping, msg);
}
void IPCClient::SendClearMapping(uint32_t idx) {
    Msg_ClearMapping msg{idx}; SendMsg(*this, m_sendMutex, IsConnected(), MsgType::ClearMapping, msg);
}
void IPCClient::SendSetOffset(uint32_t idx, const Offset6DOF& off) {
    Msg_SetOffset msg{idx, off}; SendMsg(*this, m_sendMutex, IsConnected(), MsgType::SetOffset, msg);
}
void IPCClient::SendEnableMapping(uint32_t idx) {
    Msg_EnableDisable msg{idx}; SendMsg(*this, m_sendMutex, IsConnected(), MsgType::EnableMapping, msg);
}
void IPCClient::SendDisableMapping(uint32_t idx) {
    Msg_EnableDisable msg{idx}; SendMsg(*this, m_sendMutex, IsConnected(), MsgType::DisableMapping, msg);
}
void IPCClient::SendRequestState() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (!IsConnected()) return;
    MsgHeader hdr{ MsgType::RequestState, 0 };
    WriteAll(&hdr, sizeof(hdr));
}
void IPCClient::SendHideSettings(bool hideCtrl, bool hideTrkr) {
    Msg_HideSettings msg{}; msg.hideControllers = hideCtrl?1:0; msg.hideTrackers = hideTrkr?1:0;
    SendMsg(*this, m_sendMutex, IsConnected(), MsgType::HideSettings, msg);
}
void IPCClient::SendCreateVirtualControllers() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (!IsConnected()) return;
    MsgHeader hdr{ MsgType::CreateVirtualControllers, 0 };
    WriteAll(&hdr, sizeof(hdr));
}
void IPCClient::SendSetVirtCtrlActive(uint8_t side, bool active) {
    Msg_SetVirtCtrlActive msg{}; msg.side = side; msg.active = active ? 1 : 0;
    SendMsg(*this, m_sendMutex, IsConnected(), MsgType::SetVirtCtrlActive, msg);
}
void IPCClient::SendInputUpdate(const Msg_InputUpdate& msg) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (!IsConnected()) return;
    MsgHeader hdr{ MsgType::InputUpdate, sizeof(Msg_InputUpdate) };
    if (!WriteAll(&hdr, sizeof(hdr))) return;
    WriteAll(&msg, sizeof(msg));
}
