#include "ipc_client.h"
#include "log.h"
#include <cstring>
#include <vector>

IPCClient::IPCClient() = default;
IPCClient::~IPCClient() { Disconnect(); }

bool IPCClient::Connect() {
    LOG_INFO("IPC: trying to connect to driver pipe...");
    if (!WaitNamedPipeA(OVRDANCERS_PIPE_NAME, 2000)) {
        LOG_WARN("IPC: pipe not available (driver not running?)");
        return false;
    }

    // Open with FILE_FLAG_OVERLAPPED so writes never block the render thread
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
    LOG_INFO("IPC: connected to driver");
    return true;
}

void IPCClient::Disconnect() {
    LOG_INFO("IPC: disconnecting");
    m_running = false;
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIo(m_pipe);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
    if (m_recvThread.joinable()) m_recvThread.join();
}

void IPCClient::StartReceiveThread() {
    m_running = true;
    m_recvThread = std::thread([this]() { ReceiveLoop(); });
}

// ── Receive loop (background thread) ─────────────────────────────────────────

void IPCClient::ReceiveLoop() {
    LOG_INFO("IPC recv: thread started");

    while (m_running && m_pipe != INVALID_HANDLE_VALUE) {
        MsgHeader hdr;
        DWORD read = 0;

        // Blocking read with overlapped handle works fine on receive thread
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

        BOOL ok = ReadFile(m_pipe, &hdr, sizeof(hdr), &read, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, 5000);
            if (wait == WAIT_TIMEOUT) {
                CancelIo(m_pipe); // cancel before closing event (kernel use-after-free otherwise)
            }
            ok = GetOverlappedResult(m_pipe, &ov, &read, TRUE);
        }
        CloseHandle(ov.hEvent);

        if (!ok || read != sizeof(hdr)) {
            if (!ok && GetLastError() == ERROR_OPERATION_ABORTED) continue; // clean 5s idle timeout
            LOG_WARN("IPC recv: header read failed (err=%lu, read=%lu) — disconnected", GetLastError(), read);
            break;
        }

        std::vector<uint8_t> buf(hdr.payloadSize);
        if (hdr.payloadSize > 0) {
            DWORD got = 0;
            OVERLAPPED ov2 = {};
            ov2.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            ok = ReadFile(m_pipe, buf.data(), hdr.payloadSize, &got, &ov2);
            if (!ok && GetLastError() == ERROR_IO_PENDING)
                ok = GetOverlappedResult(m_pipe, &ov2, &got, TRUE);
            CloseHandle(ov2.hEvent);
            if (!ok || got != hdr.payloadSize) {
                LOG_WARN("IPC recv: payload read failed");
                break;
            }
        }

        LOG_INFO("IPC recv: msg type=%u size=%u", (unsigned)hdr.type, hdr.payloadSize);

        switch (hdr.type) {
        case MsgType::StateUpdate:
            if (onStateUpdate) {
                Msg_StateUpdate msg;
                memcpy(&msg, buf.data(), sizeof(msg));
                onStateUpdate(msg);
            }
            break;
        case MsgType::DeviceListUpdate:
            if (onDeviceListUpdate) {
                Msg_DeviceListUpdate msg;
                memcpy(&msg, buf.data(), sizeof(msg));
                onDeviceListUpdate(msg);
            }
            break;
        default:
            LOG_WARN("IPC recv: unknown msg type %u", (unsigned)hdr.type);
            break;
        }
    }

    m_pipe = INVALID_HANDLE_VALUE;
    LOG_INFO("IPC recv: thread exiting");
    if (onDisconnected) onDisconnected();
}

// ── Write helper — non-blocking with 500ms timeout ───────────────────────────
// Never called from the receive thread; always called under m_sendMutex.

bool IPCClient::WriteWithTimeout(const void* data, DWORD size) {
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    DWORD written = 0;
    BOOL ok = WriteFile(m_pipe, data, size, &written, &ov);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, 500); // 500ms max
            if (wait == WAIT_TIMEOUT) {
                LOG_ERR("IPC send: WriteFile timed out — cancelling");
                CancelIo(m_pipe);
            }
            // Always wait for the op to settle before closing the event handle.
            ok = GetOverlappedResult(m_pipe, &ov, &written, TRUE);
        }
        if (!ok) {
            LOG_ERR("IPC send: WriteFile error %lu", GetLastError());
            CloseHandle(ov.hEvent);
            return false;
        }
    }
    CloseHandle(ov.hEvent);
    return written == size;
}

// ── Send helpers ──────────────────────────────────────────────────────────────

template<typename T>
static void SendMsg(IPCClient& c, std::mutex& mtx, HANDLE& pipe, MsgType type, const T& payload) {
    std::lock_guard<std::mutex> lock(mtx);
    if (pipe == INVALID_HANDLE_VALUE) { LOG_WARN("IPC send: not connected"); return; }
    MsgHeader hdr{ type, sizeof(T) };
    LOG_INFO("IPC send: type=%u", (unsigned)type);
    if (!c.WriteWithTimeout(&hdr, sizeof(hdr))) return;
    c.WriteWithTimeout(&payload, sizeof(payload));
}

void IPCClient::SendSetMapping(const DeviceMapping& mapping) {
    Msg_SetMapping msg{ mapping };
    SendMsg(*this, m_sendMutex, m_pipe, MsgType::SetMapping, msg);
}
void IPCClient::SendClearMapping(uint32_t controllerIndex) {
    Msg_ClearMapping msg{ controllerIndex };
    SendMsg(*this, m_sendMutex, m_pipe, MsgType::ClearMapping, msg);
}
void IPCClient::SendSetOffset(uint32_t controllerIndex, const Offset6DOF& offset) {
    Msg_SetOffset msg{ controllerIndex, offset };
    SendMsg(*this, m_sendMutex, m_pipe, MsgType::SetOffset, msg);
}
void IPCClient::SendEnableMapping(uint32_t controllerIndex) {
    Msg_EnableDisable msg{ controllerIndex };
    SendMsg(*this, m_sendMutex, m_pipe, MsgType::EnableMapping, msg);
}
void IPCClient::SendDisableMapping(uint32_t controllerIndex) {
    Msg_EnableDisable msg{ controllerIndex };
    SendMsg(*this, m_sendMutex, m_pipe, MsgType::DisableMapping, msg);
}
void IPCClient::SendRequestState() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (m_pipe == INVALID_HANDLE_VALUE) return;
    MsgHeader hdr{ MsgType::RequestState, 0 };
    WriteWithTimeout(&hdr, sizeof(hdr));
}

void IPCClient::SendHideSettings(bool hideControllers, bool hideTrackers) {
    Msg_HideSettings msg{};
    msg.hideControllers = hideControllers ? 1 : 0;
    msg.hideTrackers    = hideTrackers    ? 1 : 0;
    SendMsg(*this, m_sendMutex, m_pipe, MsgType::HideSettings, msg);
}

void IPCClient::SendInputUpdate(const Msg_InputUpdate& msg) {
    // Called every frame per mapped controller — skip the per-call log to avoid spam
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (m_pipe == INVALID_HANDLE_VALUE) return;
    MsgHeader hdr{ MsgType::InputUpdate, sizeof(Msg_InputUpdate) };
    if (!WriteWithTimeout(&hdr, sizeof(hdr))) return;
    WriteWithTimeout(&msg, sizeof(msg));
}
