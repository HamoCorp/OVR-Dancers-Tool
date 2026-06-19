#pragma once
#include <openvr_driver.h>
#include <atomic>
#include <cstdint>

// A driver-owned controller device that never disconnects.
// Registered at startup via TrackedDeviceAdded so it always appears in SteamVR.
// Its pose is driven by a Lighthouse tracker through the normal mapping + pose hook path.
class VirtualController : public vr::ITrackedDeviceServerDriver {
public:
    VirtualController(const char* serial, vr::ETrackedControllerRole role);

    // ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void             Deactivate() override;
    void             EnterStandby() override {}
    void*            GetComponent(const char*) override { return nullptr; }
    void             DebugRequest(const char*, char*, uint32_t) override {}
    vr::DriverPose_t GetPose() override;

    uint32_t    GetDeviceIndex() const { return m_deviceIndex.load(); }
    const char* GetSerial()      const { return m_serial; }
    bool        IsConnected()    const { return m_connected.load(); }
    void        SetRole(vr::ETrackedControllerRole role);
    // Soft-disconnect: hides device from SteamVR without unregistering it.
    // Lets user re-enable it in the same session without a SteamVR restart.
    void        SetConnected(bool connected);

private:
    char                          m_serial[64];
    vr::ETrackedControllerRole    m_role;
    std::atomic<uint32_t>         m_deviceIndex{vr::k_unTrackedDeviceIndexInvalid};
    std::atomic<bool>             m_connected{false};

    // Input component handles — registered in Activate() so SteamVR recognises the profile
    vr::VRInputComponentHandle_t  m_grip         = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t  m_trigger      = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t  m_appMenu      = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t  m_system       = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t  m_padClick     = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t  m_padTouch     = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t  m_padX         = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t  m_padY         = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t  m_haptic       = vr::k_ulInvalidInputComponentHandle;
};
