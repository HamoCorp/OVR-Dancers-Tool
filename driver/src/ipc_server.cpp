#include "ipc_server.h"
#include "device_provider.h"
#include <cstring>

extern void DLog(const char* fmt, ...);

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

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        OVERLAPPED ov = {};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        ConnectNamedPipe(pipe, &ov);

        HANDLE handles[] = { ov.hEvent, m_stopEvent };
        DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        CloseHandle(ov.hEvent);

        if (!m_running || wait == WAIT_OBJECT_0 + 1) {
            CloseHandle(pipe);
            break;
        }

        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            m_clientPipe = pipe;
        }

        HandleClient(pipe);

        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            m_clientPipe = INVALID_HANDLE_VALUE;
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

// ── Non-blocking broadcast using overlapped write with 200ms timeout ──────────
// Never blocks RunFrame — IPC server has its own thread, but we still want a
// hard ceiling so a stuck overlay can't stall driver bookkeeping.
void IPCServer::Broadcast(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (m_clientPipe == INVALID_HANDLE_VALUE) return;

    OVERLAPPED ov = {};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    DWORD written = 0;
    BOOL ok = WriteFile(m_clientPipe, data, (DWORD)size, &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        DWORD wait = WaitForSingleObject(ov.hEvent, 200);
        if (wait == WAIT_TIMEOUT) {
            CancelIo(m_clientPipe);
        }
        // Always wait for completion (TRUE) — closing the event before the
        // overlapped op finishes causes a kernel use-after-free.
        GetOverlappedResult(m_clientPipe, &ov, &written, TRUE);
    }
    CloseHandle(ov.hEvent);
}

// ── Client handler — reads incoming messages and drains the dirty-state queue ─
void IPCServer::HandleClient(HANDLE pipe) {
    // Send current state immediately on connect
    m_provider->SendStateToOverlay();

    std::vector<uint8_t> buf;

    // Use overlapped reads so we can also check m_stateDirty between messages
    while (m_running) {

        // Drain pending state update before blocking on the next read
        if (m_provider->m_stateDirty.exchange(false)) {
            m_provider->SendStateToOverlay();
        }

        OVERLAPPED ov = {};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

        MsgHeader hdr;
        DWORD read = 0;
        BOOL ok = ReadFile(pipe, &hdr, sizeof(hdr), &read, &ov);

        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, 100);
            if (wait == WAIT_TIMEOUT) {
                // Cancel the pending read so the next iteration starts a clean one.
                // Must wait (TRUE) for the cancel to land before closing the event —
                // closing it first is a kernel use-after-free.
                CancelIo(pipe);
            }
            ok = GetOverlappedResult(pipe, &ov, &read, TRUE);
        }

        CloseHandle(ov.hEvent);

        if (!ok || read != sizeof(hdr)) {
            // ERROR_OPERATION_ABORTED = clean 100ms timeout — loop to check dirty flag.
            // Any other error (broken pipe, etc.) = client gone — exit.
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

void IPCServer::Dispatch(const MsgHeader& hdr, const uint8_t* payload) {
    DLog("OVRDancers: Dispatch type=%u payloadSize=%u\n",
         (unsigned)hdr.type, hdr.payloadSize);
    bool sendState = false;

    switch (hdr.type) {
    case MsgType::SetMapping: {
        DLog("OVRDancers: Dispatch: memcpy SetMapping size=%u\n", (unsigned)sizeof(Msg_SetMapping));
        Msg_SetMapping msg;
        memcpy(&msg, payload, sizeof(msg));
        DLog("OVRDancers: Dispatch: calling OnSetMapping ctrl=%u tracker=%u\n",
             msg.mapping.controllerIndex, msg.mapping.trackerIndex);
        m_provider->OnSetMapping(msg.mapping);
        DLog("OVRDancers: Dispatch: OnSetMapping returned\n");
        sendState = true;
        break;
    }
    case MsgType::ClearMapping: {
        Msg_ClearMapping msg;
        memcpy(&msg, payload, sizeof(msg));
        m_provider->OnClearMapping(msg.controllerIndex);
        sendState = true;
        break;
    }
    case MsgType::SetOffset: {
        Msg_SetOffset msg;
        memcpy(&msg, payload, sizeof(msg));
        m_provider->OnSetOffset(msg.controllerIndex, msg.offset);
        // Don't flood state updates on every slider tick — offset is driver-only
        break;
    }
    case MsgType::EnableMapping: {
        Msg_EnableDisable msg;
        memcpy(&msg, payload, sizeof(msg));
        m_provider->OnEnableMapping(msg.controllerIndex);
        sendState = true;
        break;
    }
    case MsgType::DisableMapping: {
        Msg_EnableDisable msg;
        memcpy(&msg, payload, sizeof(msg));
        m_provider->OnDisableMapping(msg.controllerIndex);
        sendState = true;
        break;
    }
    case MsgType::RequestState:
        sendState = true;
        break;
    case MsgType::InputUpdate: {
        Msg_InputUpdate msg;
        memcpy(&msg, payload, sizeof(msg));
        m_provider->OnInputUpdate(msg);
        // Don't send a state update for every input tick — would flood the pipe
        break;
    }
    case MsgType::HideSettings: {
        Msg_HideSettings msg;
        memcpy(&msg, payload, sizeof(msg));
        m_provider->OnHideSettings(msg.hideControllers != 0, msg.hideTrackers != 0);
        break;
    }
    default:
        break;
    }

    // Send updated state immediately from this (IPC server) thread.
    // Safe because Broadcast uses overlapped I/O and we're not holding m_clientMutex here.
    DLog("OVRDancers: Dispatch done, sendState=%d\n", (int)sendState);
    if (sendState) {
        m_provider->m_stateDirty = false;
        DLog("OVRDancers: Dispatch: calling SendStateToOverlay\n");
        m_provider->SendStateToOverlay();
        DLog("OVRDancers: Dispatch: SendStateToOverlay returned\n");
    }
}
