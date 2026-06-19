#include "device_provider.h"
#include "ipc_server.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <cstdlib>
#endif

// Cross-platform log helper: writes to SteamVR's driver log (vrserver.txt) and
// to a file in the user's AppData / XDG data directory for offline inspection.
void DLog(const char* fmt, ...) {
    char buf[512];
    va_list a; va_start(a, fmt); vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);

    // Always route through SteamVR's own log sink (visible in vrserver.txt)
    if (auto* log = vr::VRDriverLog()) log->Log(buf);

    // Additionally write to a file for offline debugging
    static FILE* s_file = nullptr;
    if (!s_file) {
#ifdef _WIN32
        char appdata[MAX_PATH] = {};
        GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
        std::string dir = std::string(appdata) + "\\OVRDancersTool";
        CreateDirectoryA(dir.c_str(), nullptr);
        std::string path = dir + "\\driver_debug.log";
        fopen_s(&s_file, path.c_str(), "a");
#else
        const char* xdg = getenv("XDG_DATA_HOME");
        std::string dir;
        if (xdg) { dir = std::string(xdg) + "/OVRDancersTool"; }
        else {
            const char* home = getenv("HOME");
            dir = std::string(home ? home : "/tmp") + "/.local/share/OVRDancersTool";
        }
        mkdir(dir.c_str(), 0755);
        std::string path = dir + "/driver_debug.log";
        s_file = fopen(path.c_str(), "a");
#endif
    }
    if (s_file) { fputs(buf, s_file); fflush(s_file); }
}

DeviceProvider::DeviceProvider()  = default;
DeviceProvider::~DeviceProvider() = default;

vr::EVRInitError DeviceProvider::Init(vr::IVRDriverContext* ctx) {
    VR_INIT_SERVER_DRIVER_CONTEXT(ctx);
    m_props = vr::VRProperties();
    DLog("OVRDancers: driver DLL build " __DATE__ " " __TIME__ "\n");

    m_ipcServer = std::make_unique<IPCServer>(this);
    m_ipcThread = std::thread([this]() { m_ipcServer->Run(); });

    // Hook TrackedDevicePoseUpdated so any controller with an active mapping
    // has its pose silently replaced with tracker+offset. No virtual devices needed.
    PoseHook::Install(vr::VRServerDriverHost());

    DLog("OVRDancers: driver initialized\n");
    return vr::VRInitError_None;
}

void DeviceProvider::Cleanup() {
    PoseHook::Uninstall();
    if (m_ipcServer) m_ipcServer->Stop();
    if (m_ipcThread.joinable()) m_ipcThread.join();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* DeviceProvider::GetInterfaceVersions() {
    return vr::k_InterfaceVersions;
}

void DeviceProvider::RunFrame() {
    ++m_frameCount;

    vr::TrackedDevicePose_t rawPoses[vr::k_unMaxTrackedDeviceCount];
    vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.0f, rawPoses, vr::k_unMaxTrackedDeviceCount);
    PoseHook::UpdatePoseCache(rawPoses, vr::k_unMaxTrackedDeviceCount);
    PoseHook::RunFrameKeepAlive();

    if (m_frameCount % 60 == 0)
        ScanDevices(rawPoses);

    if (m_stateDirty.exchange(false))
        SendStateToOverlay();
}

void DeviceProvider::OnHideSettings(bool hideControllers, bool hideTrackers) {
    DLog("OVRDancers: OnHideSettings controllers=%d trackers=%d\n",
         (int)hideControllers, (int)hideTrackers);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hideControllers = hideControllers;
    m_hideTrackers    = hideTrackers;

    // Update PoseHook so tracker pose validity is suppressed (hides from games, not just visually)
    for (auto& [idx, cls] : m_deviceClasses) {
        if (cls == vr::TrackedDeviceClass_GenericTracker)
            PoseHook::SetHiddenTracker(idx, hideTrackers);
    }
    if (!m_props) return;
    for (auto& [ctrlIdx, mapping] : m_mappings) {
        auto cc = m_props->TrackedDeviceToPropertyContainer(ctrlIdx);
        if (cc != vr::k_ulInvalidPropertyContainer) {
            if (hideControllers)
                m_props->SetStringProperty(cc, vr::Prop_RenderModelName_String, "");
            else {
                auto it = m_deviceRenderModels.find(ctrlIdx);
                if (it != m_deviceRenderModels.end())
                    m_props->SetStringProperty(cc, vr::Prop_RenderModelName_String, it->second.c_str());
            }
        }
        uint32_t ti = mapping.trackerIndex;
        if (ti >= vr::k_unMaxTrackedDeviceCount) continue;
        auto tc = m_props->TrackedDeviceToPropertyContainer(ti);
        if (tc == vr::k_ulInvalidPropertyContainer) continue;
        if (hideTrackers) {
            if (m_deviceRenderModels.find(ti) == m_deviceRenderModels.end()) {
                char model[128]{}; vr::ETrackedPropertyError err;
                m_props->GetStringProperty(tc, vr::Prop_RenderModelName_String, model, sizeof(model), &err);
                m_deviceRenderModels[ti] = model;
            }
            m_props->SetStringProperty(tc, vr::Prop_RenderModelName_String, "");
        } else {
            auto it = m_deviceRenderModels.find(ti);
            if (it != m_deviceRenderModels.end())
                m_props->SetStringProperty(tc, vr::Prop_RenderModelName_String, it->second.c_str());
        }
    }
}

void DeviceProvider::ScanDevices(const vr::TrackedDevicePose_t* poses) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++)
        if (poses[i].bDeviceIsConnected) count++;
    if (count == m_lastDeviceCount) return;
    m_lastDeviceCount = count;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deviceSerials.clear(); m_deviceClasses.clear(); m_deviceModels.clear();
        m_deviceRoles.clear(); m_deviceControllerTypes.clear();
        m_deviceInputProfiles.clear(); m_deviceRenderModels.clear();

        for (uint32_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
            if (!poses[i].bDeviceIsConnected || !m_props) continue;
            auto c = m_props->TrackedDeviceToPropertyContainer(i);
            if (c == vr::k_ulInvalidPropertyContainer) continue;
            vr::ETrackedPropertyError err;
            uint32_t cls = (uint32_t)m_props->GetInt32Property(c, vr::Prop_DeviceClass_Int32, &err);
            if (err != vr::TrackedProp_Success) continue;
            char serial[32]{};
            m_props->GetStringProperty(c, vr::Prop_SerialNumber_String, serial, sizeof(serial), &err);
            char model[64]{};
            m_props->GetStringProperty(c, vr::Prop_ModelNumber_String, model, sizeof(model), &err);
            m_deviceSerials[i] = serial;
            m_deviceClasses[i] = cls;
            m_deviceModels[i]  = model;
            if (cls == vr::TrackedDeviceClass_Controller ||
                cls == 5u /*TrackedDeviceClass_HandTracker — added in OpenVR 2.x*/) {
                int32_t role = m_props->GetInt32Property(c, vr::Prop_ControllerRoleHint_Int32, &err);
                if (err == vr::TrackedProp_Success) m_deviceRoles[i] = (uint32_t)role;
                char ct[64]{}, ip[128]{}, rm[128]{};
                m_props->GetStringProperty(c, vr::Prop_ControllerType_String, ct, sizeof(ct), &err);
                m_props->GetStringProperty(c, vr::Prop_InputProfilePath_String, ip, sizeof(ip), &err);
                m_props->GetStringProperty(c, vr::Prop_RenderModelName_String, rm, sizeof(rm), &err);
                m_deviceControllerTypes[i] = ct;
                m_deviceInputProfiles[i]   = ip;
                m_deviceRenderModels[i]    = rm;
            }
        }
    }
    DLog("OVRDancers: device list changed, count=%u\n", count);
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
            cls != vr::TrackedDeviceClass_GenericTracker &&
            cls != 5u /*TrackedDeviceClass_HandTracker*/) continue;
        TrackedDeviceInfo& info = out[count++];
        info.index = idx; info.deviceClass = cls; info.connected = true;
        auto sit = m_deviceSerials.find(idx);
        if (sit != m_deviceSerials.end())
            strncpy_s(info.serial, sit->second.c_str(), sizeof(info.serial)-1);
        auto mit = m_deviceModels.find(idx);
        if (mit != m_deviceModels.end())
            strncpy_s(info.modelNumber, mit->second.c_str(), sizeof(info.modelNumber)-1);
    }
}

void DeviceProvider::SendStateToOverlay() {
    if (!m_ipcServer) return;
    struct { MsgHeader hdr; Msg_StateUpdate body; } pkt;
    pkt.hdr = {MsgType::StateUpdate, sizeof(Msg_StateUpdate)};
    pkt.body = {};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [idx, m] : m_mappings)
            if (pkt.body.mappingCount < MAX_DEVICES)
                pkt.body.mappings[pkt.body.mappingCount++] = m;
        BuildDeviceList(pkt.body.devices, pkt.body.deviceCount);
    }
    DLog("OVRDancers: SendStateToOverlay mappings=%u devices=%u\n",
         pkt.body.mappingCount, pkt.body.deviceCount);
    m_ipcServer->Broadcast(&pkt, sizeof(pkt));
}

void DeviceProvider::OnSetMapping(const DeviceMapping& mapping) {
    DLog("OVRDancers: OnSetMapping ctrl=%u tracker=%u enabled=%d\n",
         mapping.controllerIndex, mapping.trackerIndex, (int)mapping.enabled);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mappings[mapping.controllerIndex] = mapping;
    }
    PoseHook::AddMapping(mapping.controllerIndex, mapping.trackerIndex,
                         mapping.offset, mapping.enabled, mapping.redirectMode);
    m_stateDirty = true;
}

void DeviceProvider::OnClearMapping(uint32_t controllerIndex) {
    DLog("OVRDancers: OnClearMapping ctrl=%u\n", controllerIndex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mappings.erase(controllerIndex);
    }
    PoseHook::RemoveMapping(controllerIndex);
    // Relinquish hand role when mapping is removed
    if (m_virtCtrlL && m_virtCtrlL->GetDeviceIndex() == controllerIndex)
        m_virtCtrlL->SetRole(vr::TrackedControllerRole_OptOut);
    else if (m_virtCtrlR && m_virtCtrlR->GetDeviceIndex() == controllerIndex)
        m_virtCtrlR->SetRole(vr::TrackedControllerRole_OptOut);
    m_stateDirty = true;
}

void DeviceProvider::OnSetOffset(uint32_t controllerIndex, const Offset6DOF& offset) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mappings.find(controllerIndex);
        if (it != m_mappings.end()) it->second.offset = offset;
    }
    PoseHook::UpdateOffset(controllerIndex, offset);
    m_stateDirty = true; // so the updated offset echoes back to the overlay UI
}

void DeviceProvider::OnEnableMapping(uint32_t controllerIndex) {
    DLog("OVRDancers: OnEnableMapping ctrl=%u\n", controllerIndex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mappings.find(controllerIndex);
        if (it != m_mappings.end()) it->second.enabled = true;
    }
    PoseHook::SetEnabled(controllerIndex, true);
    // Grant hand role to virtual controller when mapping is active
    if (m_virtCtrlL && m_virtCtrlL->GetDeviceIndex() == controllerIndex)
        m_virtCtrlL->SetRole(vr::TrackedControllerRole_LeftHand);
    else if (m_virtCtrlR && m_virtCtrlR->GetDeviceIndex() == controllerIndex)
        m_virtCtrlR->SetRole(vr::TrackedControllerRole_RightHand);
    m_stateDirty = true;
}

void DeviceProvider::OnDisableMapping(uint32_t controllerIndex) {
    DLog("OVRDancers: OnDisableMapping ctrl=%u\n", controllerIndex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mappings.find(controllerIndex);
        if (it != m_mappings.end()) it->second.enabled = false;
    }
    PoseHook::SetEnabled(controllerIndex, false);
    // Relinquish hand role so real controllers reclaim it
    if (m_virtCtrlL && m_virtCtrlL->GetDeviceIndex() == controllerIndex)
        m_virtCtrlL->SetRole(vr::TrackedControllerRole_OptOut);
    else if (m_virtCtrlR && m_virtCtrlR->GetDeviceIndex() == controllerIndex)
        m_virtCtrlR->SetRole(vr::TrackedControllerRole_OptOut);
    m_stateDirty = true;
}

void DeviceProvider::OnRequestState() { SendStateToOverlay(); }

void DeviceProvider::OnInputUpdate(const Msg_InputUpdate& msg) { (void)msg; }

void DeviceProvider::OnSetVirtCtrlActive(uint8_t side, bool active) {
    DLog("OVRDancers: OnSetVirtCtrlActive side=%u active=%d\n", (unsigned)side, (int)active);
    if (side == 1 && m_virtCtrlL) m_virtCtrlL->SetConnected(active);
    if (side == 2 && m_virtCtrlR) m_virtCtrlR->SetConnected(active);
    m_stateDirty = true;
}

void DeviceProvider::OnCreateVirtualControllers() {
    if (m_virtCtrlL || m_virtCtrlR) {
        DLog("OVRDancers: OnCreateVirtualControllers — already registered\n");
        return;
    }
    m_virtCtrlL = std::make_unique<VirtualController>("VirtCtrl_L", vr::TrackedControllerRole_LeftHand);
    m_virtCtrlR = std::make_unique<VirtualController>("VirtCtrl_R", vr::TrackedControllerRole_RightHand);
    vr::VRServerDriverHost()->TrackedDeviceAdded("VirtCtrl_L", vr::TrackedDeviceClass_Controller, m_virtCtrlL.get());
    vr::VRServerDriverHost()->TrackedDeviceAdded("VirtCtrl_R", vr::TrackedDeviceClass_Controller, m_virtCtrlR.get());
    uint32_t lIdx = m_virtCtrlL->GetDeviceIndex();
    uint32_t rIdx = m_virtCtrlR->GetDeviceIndex();
    DLog("OVRDancers: Virtual controllers registered (L=%u R=%u)\n", lIdx, rIdx);
    // If a mapping for this controller was already enabled before registration, restore role
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto itL = m_mappings.find(lIdx);
        if (itL != m_mappings.end() && itL->second.enabled)
            m_virtCtrlL->SetRole(vr::TrackedControllerRole_LeftHand);
        auto itR = m_mappings.find(rIdx);
        if (itR != m_mappings.end() && itR->second.enabled)
            m_virtCtrlR->SetRole(vr::TrackedControllerRole_RightHand);
    }
    m_stateDirty = true;
}
