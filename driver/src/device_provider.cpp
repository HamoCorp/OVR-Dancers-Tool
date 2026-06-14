#include "device_provider.h"
#include "ipc_server.h"
#include "input_hook.h"
#include <cstring>
#include <cstdio>
#include <windows.h>

// ── Driver-side file logger ───────────────────────────────────────────────────
// File write MUST come first — if VRDriverLog() crashes the file write is the only record.
void DLog(const char* fmt, ...) {
    char buf[512];
    va_list a; va_start(a, fmt); vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);

    static HANDLE hFile = INVALID_HANDLE_VALUE;
    if (hFile == INVALID_HANDLE_VALUE)
        hFile = CreateFileA(
            "C:\\Users\\hamoc\\Desktop\\BreakersTool\\driver_debug.log",
            GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hFile, 0, nullptr, FILE_END);
        DWORD w; WriteFile(hFile, buf, (DWORD)strlen(buf), &w, nullptr);
        FlushFileBuffers(hFile); // flush so crash can't eat buffered output
    }

    // Null-safe — context may be briefly unavailable on some SteamVR versions
    auto* log = vr::VRDriverLog();
    if (log) log->Log(buf);
}

// ── DeviceProvider ────────────────────────────────────────────────────────────

DeviceProvider::DeviceProvider()  = default;
DeviceProvider::~DeviceProvider() = default;

vr::EVRInitError DeviceProvider::Init(vr::IVRDriverContext* ctx) {
    VR_INIT_SERVER_DRIVER_CONTEXT(ctx);
    m_props = vr::VRProperties();
    DLog("OVRDancers: driver DLL build " __DATE__ " " __TIME__ "\n");

    // Register two virtual controllers (left + right) as our own tracked devices.
    // Each gets an openvr device_id via Activate() and drives its own pose thread.
    m_leftCtrl  = std::make_unique<VirtualController>(false);
    m_rightCtrl = std::make_unique<VirtualController>(true);

    vr::VRServerDriverHost()->TrackedDeviceAdded(
        m_leftCtrl->GetSerial().c_str(),  vr::TrackedDeviceClass_Controller, m_leftCtrl.get());
    vr::VRServerDriverHost()->TrackedDeviceAdded(
        m_rightCtrl->GetSerial().c_str(), vr::TrackedDeviceClass_Controller, m_rightCtrl.get());

    m_ipcServer = std::make_unique<IPCServer>(this);
    m_ipcThread = std::thread([this]() { m_ipcServer->Run(); });

    // Hook IVRDriverInput vtable to intercept Create*/Update* calls from ALL drivers.
    // Must be installed after TrackedDeviceAdded (our Activate runs and claims handles)
    // but before physical controller drivers call Activate/CreateBooleanComponent.
    // In practice BreakersTool loads alphabetically before meta/lighthouse drivers.
    InputHook::Install(vr::VRDriverInput());

    // Hook IVRServerDriverHost to intercept physical controller pose updates.
    // When a controller has an active mapping, its reported pose is replaced with
    // tracker_pose * offset so the game sees it at the tracker's position.
    // This gives us working haptics and input for free (no forwarding needed).
    PoseHook::Install(vr::VRServerDriverHost());

    // Try to acquire IVRSystem directly from the driver context so RunFrame can poll
    // real controller state without going through the overlay IPC round-trip.
    if (m_stateReader.Init(ctx))
        DLog("OVRDancers: IVRSystem acquired in driver context — input polling active\n");
    else
        DLog("OVRDancers: IVRSystem unavailable in driver context — relying on overlay IPC for input\n");

    DLog("OVRDancers: driver initialized, virtual controllers registered\n");
    return vr::VRInitError_None;
}

void DeviceProvider::Cleanup() {
    InputHook::Uninstall();
    PoseHook::Uninstall();
    if (m_ipcServer) m_ipcServer->Stop();
    if (m_ipcThread.joinable()) m_ipcThread.join();
    // Virtual controllers deactivate themselves in their destructors
    m_leftCtrl.reset();
    m_rightCtrl.reset();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* DeviceProvider::GetInterfaceVersions() {
    return vr::k_InterfaceVersions;
}

void DeviceProvider::RunFrame() {
    ++m_frameCount;
    vr::TrackedDevicePose_t rawPoses[vr::k_unMaxTrackedDeviceCount];
    vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.0f, rawPoses, vr::k_unMaxTrackedDeviceCount);

    // Keep pose hook's tracker cache current so it can compute redirected positions.
    PoseHook::UpdatePoseCache(rawPoses, vr::k_unMaxTrackedDeviceCount);

    if (m_frameCount % 60 == 0) {
        ScanDevices(rawPoses);
    }

    // Copy battery level from physical controller(s) to their virtual counterparts.
    // Run at 1 Hz (every 60 RunFrame ticks) to keep battery indicator current without overhead.
    if (m_frameCount % 60 == 1) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [ctrlIdx, isRight] : m_ctrlToVirt) {
            if (!m_props) break;
            auto physCont = m_props->TrackedDeviceToPropertyContainer(ctrlIdx);
            if (physCont == vr::k_ulInvalidPropertyContainer) continue;
            vr::ETrackedPropertyError perr;
            float pct = m_props->GetFloatProperty(physCont, vr::Prop_DeviceBatteryPercentage_Float, &perr);
            if (perr != vr::TrackedProp_Success) continue;
            VirtualController* ctrl = isRight ? m_rightCtrl.get() : m_leftCtrl.get();
            if (ctrl) ctrl->UpdateBattery(pct);
        }
    }

    // Pose hook approach: physical controller retains its own input channel.
    // No input forwarding to virtual controllers is needed.
}

void DeviceProvider::PollAndForwardButtonEvents() {
    // Physical controllers retain their own input channel via the pose-hook approach.
    // No button/axis forwarding is needed — this function is now a no-op.
    vr::VREvent_t e;
    while (vr::VRServerDriverHost()->PollNextEvent(&e, sizeof(e))) {}
}

void DeviceProvider::OnHideSettings(bool hideControllers, bool hideTrackers) {
    DLog("OVRDancers: OnHideSettings controllers=%d trackers=%d\n",
         (int)hideControllers, (int)hideTrackers);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_hideControllers = hideControllers;
    m_hideTrackers    = hideTrackers;

    if (!m_props) return;

    for (auto& [ctrlIdx, mapping] : m_mappings) {
        // Physical controller
        {
            auto container = m_props->TrackedDeviceToPropertyContainer(ctrlIdx);
            if (container != vr::k_ulInvalidPropertyContainer) {
                if (hideControllers) {
                    m_props->SetStringProperty(container, vr::Prop_RenderModelName_String, "");
                } else {
                    auto it = m_deviceRenderModels.find(ctrlIdx);
                    if (it != m_deviceRenderModels.end())
                        m_props->SetStringProperty(container, vr::Prop_RenderModelName_String,
                            it->second.c_str());
                }
            }
        }
        // Tracker
        {
            uint32_t trackerIdx = mapping.trackerIndex;
            if (trackerIdx >= vr::k_unMaxTrackedDeviceCount) continue;
            auto container = m_props->TrackedDeviceToPropertyContainer(trackerIdx);
            if (container == vr::k_ulInvalidPropertyContainer) continue;

            if (hideTrackers) {
                // Cache original render model the first time we hide
                if (m_deviceRenderModels.find(trackerIdx) == m_deviceRenderModels.end()) {
                    char model[128] = {};
                    vr::ETrackedPropertyError err;
                    m_props->GetStringProperty(container, vr::Prop_RenderModelName_String,
                        model, sizeof(model), &err);
                    m_deviceRenderModels[trackerIdx] = model;
                }
                m_props->SetStringProperty(container, vr::Prop_RenderModelName_String, "");
            } else {
                auto it = m_deviceRenderModels.find(trackerIdx);
                if (it != m_deviceRenderModels.end())
                    m_props->SetStringProperty(container, vr::Prop_RenderModelName_String,
                        it->second.c_str());
            }
        }
    }
}

// ── Device scanning ───────────────────────────────────────────────────────────

void DeviceProvider::ScanDevices(const vr::TrackedDevicePose_t* poses) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++)
        if (poses[i].bDeviceIsConnected) count++;

    if (count == m_lastDeviceCount) return;
    m_lastDeviceCount = count;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deviceSerials.clear();
        m_deviceClasses.clear();
        m_deviceModels.clear();
        m_deviceRoles.clear();
        m_deviceControllerTypes.clear();
        m_deviceInputProfiles.clear();
        m_deviceRenderModels.clear();

        for (uint32_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
            if (!poses[i].bDeviceIsConnected) continue;
            if (!m_props) continue;

            vr::PropertyContainerHandle_t c = m_props->TrackedDeviceToPropertyContainer(i);
            if (c == vr::k_ulInvalidPropertyContainer) continue;

            vr::ETrackedPropertyError err;
            uint32_t cls = (uint32_t)m_props->GetInt32Property(c, vr::Prop_DeviceClass_Int32, &err);
            if (err != vr::TrackedProp_Success) continue;

            char serial[32] = {};
            m_props->GetStringProperty(c, vr::Prop_SerialNumber_String, serial, sizeof(serial), &err);

            // Skip our own virtual controllers — don't show them in the device list
            if (strncmp(serial, "OVRDancers-", 11) == 0) continue;

            char model[64] = {};
            m_props->GetStringProperty(c, vr::Prop_ModelNumber_String, model, sizeof(model), &err);

            m_deviceSerials[i] = serial;
            m_deviceClasses[i] = cls;
            m_deviceModels[i]  = model;

            // Cache controller role and appearance so virtual controller can mirror the real one
            if (cls == vr::TrackedDeviceClass_Controller) {
                int32_t role = m_props->GetInt32Property(c, vr::Prop_ControllerRoleHint_Int32, &err);
                if (err == vr::TrackedProp_Success)
                    m_deviceRoles[i] = (uint32_t)role;

                char ctrlType[64]       = {};
                char inputProfile[128]  = {};
                char renderModel[128]   = {};
                m_props->GetStringProperty(c, vr::Prop_ControllerType_String,
                    ctrlType, sizeof(ctrlType), &err);
                m_props->GetStringProperty(c, vr::Prop_InputProfilePath_String,
                    inputProfile, sizeof(inputProfile), &err);
                m_props->GetStringProperty(c, vr::Prop_RenderModelName_String,
                    renderModel, sizeof(renderModel), &err);
                m_deviceControllerTypes[i] = ctrlType;
                m_deviceInputProfiles[i]   = inputProfile;
                m_deviceRenderModels[i]    = renderModel;
            }
        }
    }

    DLog("OVRDancers: device list changed, flagging state dirty\n");
    m_stateDirty = true;
}

void DeviceProvider::ScanDevices() {
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
    vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.0f, poses, vr::k_unMaxTrackedDeviceCount);
    ScanDevices(poses);
}

void DeviceProvider::BuildDeviceList(TrackedDeviceInfo* out, uint32_t& count) {
    count = 0;
    for (auto& [idx, cls] : m_deviceClasses) {
        if (count >= (uint32_t)(MAX_DEVICES * 2)) break;
        if (cls != vr::TrackedDeviceClass_Controller &&
            cls != vr::TrackedDeviceClass_GenericTracker) continue;

        TrackedDeviceInfo& info = out[count++];
        info.index       = idx;
        info.deviceClass = cls;
        info.connected   = true;

        auto sit = m_deviceSerials.find(idx);
        if (sit != m_deviceSerials.end())
            strncpy_s(info.serial, sit->second.c_str(), sizeof(info.serial)-1);

        auto mit = m_deviceModels.find(idx);
        if (mit != m_deviceModels.end())
            strncpy_s(info.modelNumber, mit->second.c_str(), sizeof(info.modelNumber)-1);
    }
}

// ── Virtual controller routing ────────────────────────────────────────────────
// Must be called with m_mutex held.

VirtualController* DeviceProvider::GetVirtualCtrl(uint32_t controllerIndex) {
    // Already routed? Return the same side every time.
    auto it = m_ctrlToVirt.find(controllerIndex);
    if (it != m_ctrlToVirt.end())
        return it->second ? m_rightCtrl.get() : m_leftCtrl.get();

    // Try the role hint first — most reliable.
    auto roleIt = m_deviceRoles.find(controllerIndex);
    if (roleIt != m_deviceRoles.end()) {
        bool isRight = (roleIt->second == vr::TrackedControllerRole_RightHand);
        DLog("OVRDancers: ctrl=%u role=%u → %s\n", controllerIndex, roleIt->second,
             isRight ? "right" : "left");
        m_ctrlToVirt[controllerIndex] = isRight;
        return isRight ? m_rightCtrl.get() : m_leftCtrl.get();
    }

    // Role unknown — pick whichever virtual controller isn't already claimed.
    // This prevents two controllers from both defaulting to the same side.
    bool rightClaimed = false, leftClaimed = false;
    for (auto& [idx, side] : m_ctrlToVirt) {
        if (side) rightClaimed = true;
        else      leftClaimed  = true;
    }
    bool isRight = !rightClaimed; // prefer right first; fall back to left
    DLog("OVRDancers: ctrl=%u role unknown, assigning %s (rightClaimed=%d leftClaimed=%d)\n",
         controllerIndex, isRight ? "right" : "left", (int)rightClaimed, (int)leftClaimed);
    m_ctrlToVirt[controllerIndex] = isRight;
    return isRight ? m_rightCtrl.get() : m_leftCtrl.get();
}

// ── State sync ────────────────────────────────────────────────────────────────

void DeviceProvider::SendStateToOverlay() {
    if (!m_ipcServer) return;

    struct { MsgHeader hdr; Msg_StateUpdate body; } pkt;
    pkt.hdr = { MsgType::StateUpdate, sizeof(Msg_StateUpdate) };
    pkt.body = {};

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [idx, mapping] : m_mappings) {
            if (pkt.body.mappingCount < MAX_DEVICES)
                pkt.body.mappings[pkt.body.mappingCount++] = mapping;
        }
        BuildDeviceList(pkt.body.devices, pkt.body.deviceCount);
    }

    DLog("OVRDancers: SendStateToOverlay mappings=%u devices=%u\n",
         pkt.body.mappingCount, pkt.body.deviceCount);

    m_ipcServer->Broadcast(&pkt, sizeof(pkt));
}

// ── IPC callbacks ─────────────────────────────────────────────────────────────

void DeviceProvider::OnSetMapping(const DeviceMapping& mapping) {
    DLog("OVRDancers: OnSetMapping ctrl=%u tracker=%u enabled=%d\n",
         mapping.controllerIndex, mapping.trackerIndex, (int)mapping.enabled);

    DeviceMapping m = mapping;
    VirtualController* ghost = nullptr;
    std::string renderModel;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Determine side + pick matching ghost controller.
        ghost = GetVirtualCtrl(m.controllerIndex);
        if (ghost) {
            bool isRight = (ghost == m_rightCtrl.get());
            m.side       = isRight ? 2 : 1;
            m.virtDevIdx = ghost->GetDeviceId();
        }

        auto rIt = m_deviceRenderModels.find(m.controllerIndex);
        if (rIt != m_deviceRenderModels.end()) renderModel = rIt->second;

        m_mappings[m.controllerIndex] = m;
    }

    // Mirror the physical controller's render model onto the ghost so it looks right.
    if (ghost && !renderModel.empty())
        ghost->ApplyRealControllerProperties(renderModel);

    // Pose hook: redirect physical controller pose → tracker + offset.
    PoseHook::AddMapping(m.controllerIndex, m.trackerIndex, m.offset, m.enabled);
    // Tell PoseHook which ghost device to forward the real (pre-hook) pose to.
    if (m.virtDevIdx != 0xFFFFFFFF)
        PoseHook::SetGhostDevice(m.controllerIndex, m.virtDevIdx);

    m_stateDirty = true;
}

void DeviceProvider::OnClearMapping(uint32_t controllerIndex) {
    DLog("OVRDancers: OnClearMapping ctrl=%u\n", controllerIndex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mappings.erase(controllerIndex);
        m_ctrlToVirt.erase(controllerIndex);
    }
    PoseHook::RemoveMapping(controllerIndex);
    m_stateDirty = true;
}

void DeviceProvider::OnSetOffset(uint32_t controllerIndex, const Offset6DOF& offset) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mappings.find(controllerIndex);
        if (it != m_mappings.end())
            it->second.offset = offset;
    }
    PoseHook::UpdateOffset(controllerIndex, offset);
}

void DeviceProvider::OnEnableMapping(uint32_t controllerIndex) {
    DLog("OVRDancers: OnEnableMapping ctrl=%u\n", controllerIndex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mappings.find(controllerIndex);
        if (it != m_mappings.end())
            it->second.enabled = true;
    }
    PoseHook::SetEnabled(controllerIndex, true);
    m_stateDirty = true;
}

void DeviceProvider::OnDisableMapping(uint32_t controllerIndex) {
    DLog("OVRDancers: OnDisableMapping ctrl=%u\n", controllerIndex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mappings.find(controllerIndex);
        if (it != m_mappings.end())
            it->second.enabled = false;
    }
    PoseHook::SetEnabled(controllerIndex, false);
    m_stateDirty = true;
}

void DeviceProvider::OnRequestState() { SendStateToOverlay(); }

void DeviceProvider::OnInputUpdate(const Msg_InputUpdate& msg) {
    // Physical controllers keep their own input channel (pose-hook approach).
    // Overlay IPC still sends these messages; discard them here.
    (void)msg;
}
