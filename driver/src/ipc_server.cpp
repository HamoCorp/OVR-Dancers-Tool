#include "ipc_server.h"
#include "device_provider.h"
#include <cstring>
#include <vector>

extern void DLog(const char* fmt, ...);

// ═══════════════════════════════════════════════════════════════════════════════
// Windows implementation (named pipe + overlapped I/O)
// ═══════════════════════════════════════════════════════════════════════════════
#ifdef _WIN32

IPCServer::IPCServer(DeviceProvider* provider) : m_provider(provider) {
    m_stopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
}

IPCServer::~IPCServer() {
    Stop();
    if (m_stopEvent != INVALID_HANDLE_VALUE) CloseHandle(m_stopEvent);
}

void IPCServer::Stop() {
    m_running = false;
    SetEvent(m_stopEvent);
}

void IPCServer::Run() {
    m_running = true;
    while (m_running) {
        HANDLE pipe = CreateNamedPipeA(
            OVRDANCERS_PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 65536, 65536, 0, nullptr);

        if (pipe == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }

        OVERLAPPED ov = {};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        ConnectNamedPipe(pipe, &ov);

        HANDLE handles[] = { ov.hEvent, m_stopEvent };
        DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        CloseHandle(ov.hEvent);

        if (!m_running || wait == WAIT_OBJECT_0 + 1) { CloseHandle(pipe); break; }

        { std::lock_guard<std::mutex> lock(m_clientMutex); m_clientPipe = pipe; }
        HandleClient(pipe);
        { std::lock_guard<std::mutex> lock(m_clientMutex); m_clientPipe = INVALID_HANDLE_VALUE; }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void IPCServer::Broadcast(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (m_clientPipe == INVALID_HANDLE_VALUE) return;
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    DWORD written = 0;
    BOOL ok = WriteFile(m_clientPipe, data, (DWORD)size, &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 200) == WAIT_TIMEOUT) CancelIo(m_clientPipe);
        GetOverlappedResult(m_clientPipe, &ov, &written, TRUE);
    }
    CloseHandle(ov.hEvent);
}

void IPCServer::HandleClient(HANDLE pipe) {
    m_provider->SendStateToOverlay();
    std::vector<uint8_t> buf;

    while (m_running) {
        if (m_provider->m_stateDirty.exchange(false)) m_provider->SendStateToOverlay();

        OVERLAPPED ov = {};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        MsgHeader hdr;
        DWORD read = 0;
        BOOL ok = ReadFile(pipe, &hdr, sizeof(hdr), &read, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, 100) == WAIT_TIMEOUT) CancelIo(pipe);
            ok = GetOverlappedResult(pipe, &ov, &read, TRUE);
        }
        CloseHandle(ov.hEvent);
        if (!ok || read != sizeof(hdr)) {
            if (!ok && GetLastError() == ERROR_OPERATION_ABORTED) continue;
            break;
        }

        buf.resize(hdr.payloadSize);
        if (hdr.payloadSize > 0) {
            DWORD got = 0;
            OVERLAPPED ov2 = {};
            ov2.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            ok = ReadFile(pipe, buf.data(), hdr.payloadSize, &got, &ov2);
            if (!ok && GetLastError() == ERROR_IO_PENDING)
                ok = GetOverlappedResult(pipe, &ov2, &got, TRUE);
            CloseHandle(ov2.hEvent);
            if (!ok || got != hdr.payloadSize) break;
        }
        Dispatch(hdr, buf.data());
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Linux implementation (Unix domain socket + poll)
// ═══════════════════════════════════════════════════════════════════════════════
#else // !_WIN32

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <cstdio>

IPCServer::IPCServer(DeviceProvider* provider) : m_provider(provider) {
    pipe2(m_stopPipe, O_NONBLOCK);  // self-pipe for stop signaling
}

IPCServer::~IPCServer() {
    Stop();
    if (m_stopPipe[0] >= 0) { close(m_stopPipe[0]); close(m_stopPipe[1]); }
}

void IPCServer::Stop() {
    m_running = false;
    if (m_stopPipe[1] >= 0) { char b = 1; write(m_stopPipe[1], &b, 1); }
}

void IPCServer::Run() {
    m_running = true;
    unlink(OVRDANCERS_PIPE_NAME);

    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) { DLog("OVRDancers: IPC socket failed\n"); return; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, OVRDANCERS_PIPE_NAME, sizeof(addr.sun_path)-1);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(serverFd, 1) < 0) {
        DLog("OVRDancers: IPC bind/listen failed\n");
        close(serverFd); return;
    }

    while (m_running) {
        struct pollfd pfd[2];
        pfd[0].fd = serverFd;      pfd[0].events = POLLIN;
        pfd[1].fd = m_stopPipe[0]; pfd[1].events = POLLIN;
        int r = poll(pfd, 2, 1000);
        if (!m_running || (r > 0 && (pfd[1].revents & POLLIN))) break;
        if (r <= 0 || !(pfd[0].revents & POLLIN)) continue;

        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) continue;

        { std::lock_guard<std::mutex> lock(m_clientMutex); m_clientFd = clientFd; }
        HandleClient(clientFd);
        { std::lock_guard<std::mutex> lock(m_clientMutex); m_clientFd = -1; }

        close(clientFd);
    }
    close(serverFd);
    unlink(OVRDANCERS_PIPE_NAME);
}

void IPCServer::Broadcast(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (m_clientFd < 0) return;
    const uint8_t* p = (const uint8_t*)data;
    size_t rem = size;
    while (rem > 0) {
        ssize_t n = write(m_clientFd, p, rem);
        if (n <= 0) break;
        p += n; rem -= (size_t)n;
    }
}

void IPCServer::HandleClient(int clientFd) {
    m_provider->SendStateToOverlay();
    std::vector<uint8_t> buf;

    while (m_running) {
        if (m_provider->m_stateDirty.exchange(false)) m_provider->SendStateToOverlay();

        struct pollfd pfd[2];
        pfd[0].fd = clientFd;      pfd[0].events = POLLIN;
        pfd[1].fd = m_stopPipe[0]; pfd[1].events = POLLIN;
        int r = poll(pfd, 2, 100);
        if (!m_running || (r > 0 && (pfd[1].revents & POLLIN))) break;
        if (r <= 0) continue;   // timeout — loop to check dirty flag
        if (!(pfd[0].revents & POLLIN)) continue;

        MsgHeader hdr;
        ssize_t got = recv(clientFd, &hdr, sizeof(hdr), MSG_WAITALL);
        if (got != (ssize_t)sizeof(hdr)) break;

        buf.resize(hdr.payloadSize);
        if (hdr.payloadSize > 0) {
            got = recv(clientFd, buf.data(), hdr.payloadSize, MSG_WAITALL);
            if (got != (ssize_t)hdr.payloadSize) break;
        }
        Dispatch(hdr, buf.data());
    }
}

#endif // _WIN32

// ── Dispatch (shared) ─────────────────────────────────────────────────────────
void IPCServer::Dispatch(const MsgHeader& hdr, const uint8_t* payload) {
    bool sendState = false;
    switch (hdr.type) {
    case MsgType::SetMapping: {
        Msg_SetMapping msg; memcpy(&msg, payload, sizeof(msg));
        m_provider->OnSetMapping(msg.mapping); sendState = true; break;
    }
    case MsgType::ClearMapping: {
        Msg_ClearMapping msg; memcpy(&msg, payload, sizeof(msg));
        m_provider->OnClearMapping(msg.controllerIndex); sendState = true; break;
    }
    case MsgType::SetOffset: {
        Msg_SetOffset msg; memcpy(&msg, payload, sizeof(msg));
        m_provider->OnSetOffset(msg.controllerIndex, msg.offset); break;
    }
    case MsgType::EnableMapping: {
        Msg_EnableDisable msg; memcpy(&msg, payload, sizeof(msg));
        m_provider->OnEnableMapping(msg.controllerIndex); sendState = true; break;
    }
    case MsgType::DisableMapping: {
        Msg_EnableDisable msg; memcpy(&msg, payload, sizeof(msg));
        m_provider->OnDisableMapping(msg.controllerIndex); sendState = true; break;
    }
    case MsgType::RequestState: sendState = true; break;
    case MsgType::InputUpdate: {
        Msg_InputUpdate msg; memcpy(&msg, payload, sizeof(msg));
        m_provider->OnInputUpdate(msg); break;
    }
    case MsgType::HideSettings: {
        Msg_HideSettings msg; memcpy(&msg, payload, sizeof(msg));
        m_provider->OnHideSettings(msg.hideControllers != 0, msg.hideTrackers != 0); break;
    }
    case MsgType::CreateVirtualControllers:
        m_provider->OnCreateVirtualControllers(); sendState = true; break;
    case MsgType::SetVirtCtrlActive: {
        Msg_SetVirtCtrlActive msg; memcpy(&msg, payload, sizeof(msg));
        m_provider->OnSetVirtCtrlActive(msg.side, msg.active != 0); sendState = true; break;
    }
    default: break;
    }
    if (sendState) {
        m_provider->m_stateDirty = false;
        m_provider->SendStateToOverlay();
    }
}
