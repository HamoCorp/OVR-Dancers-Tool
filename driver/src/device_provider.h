#pragma once
#include <openvr_driver.h>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include "../../shared/protocol.h"
#include "pose_hook.h"
#include "virtual_controller.h"

class IPCServer;

class DeviceProvider : public vr::IServerTrackedDeviceProvider {
public:
    DeviceProvider();
    ~DeviceProvider();

    // IServerTrackedDeviceProvider
    vr::EVRInitError Init(vr::IVRDriverContext* ctx) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override;
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {}
    void LeaveStandby() override {}

    // Called by IPC server thread on incoming messages
    void OnSetMapping(const DeviceMapping& mapping);
    void OnClearMapping(uint32_t controllerIndex);
    void OnSetOffset(uint32_t controllerIndex, const Offset6DOF& offset);
    void OnEnableMapping(uint32_t controllerIndex);
    void OnDisableMapping(uint32_t controllerIndex);
    void OnRequestState();
    void OnInputUpdate(const Msg_InputUpdate& msg);
    void OnHideSettings(bool hideControllers, bool hideTrackers);
    void OnCreateVirtualControllers();
    void OnSetVirtCtrlActive(uint8_t side, bool active);

    void SendStateToOverlay();

    std::atomic<bool> m_stateDirty{false};

private:
    void ScanDevices();
    void ScanDevices(const vr::TrackedDevicePose_t* poses);
    void BuildDeviceList(TrackedDeviceInfo* out, uint32_t& count);

    std::mutex m_mutex;
    std::unordered_map<uint32_t, DeviceMapping>  m_mappings;
    std::unordered_map<uint32_t, std::string>    m_deviceSerials;
    std::unordered_map<uint32_t, uint32_t>       m_deviceClasses;
    std::unordered_map<uint32_t, std::string>    m_deviceModels;
    std::unordered_map<uint32_t, uint32_t>       m_deviceRoles;
    std::unordered_map<uint32_t, std::string>    m_deviceControllerTypes;
    std::unordered_map<uint32_t, std::string>    m_deviceInputProfiles;
    std::unordered_map<uint32_t, std::string>    m_deviceRenderModels;

    bool                 m_hideControllers = false;
    bool                 m_hideTrackers    = false;
    uint32_t             m_lastDeviceCount = 0;
    uint32_t             m_frameCount      = 0;

    std::unique_ptr<IPCServer>       m_ipcServer;
    std::thread                      m_ipcThread;

    // Virtual controllers for "held in hand" tracker-as-controller mode.
    // Always registered and connected so they never freeze when physical hardware sleeps.
    std::unique_ptr<VirtualController> m_virtCtrlL;
    std::unique_ptr<VirtualController> m_virtCtrlR;

    vr::CVRPropertyHelpers* m_props = nullptr;
};
