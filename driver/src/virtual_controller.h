#pragma once
#include <openvr_driver.h>
#include <atomic>
#include <string>
#include <unordered_map>
#include "../../shared/protocol.h"

// Ghost indicator controller: stays registered at priority -1 so games never use it
// as a hand controller.  PoseHook forwards the physical controller's pre-hook real
// position here each frame so the user can see where their physical controller is.
// All skeleton/animation/input-forwarding code has been removed — this device only
// provides a visible render model at the real physical controller location.
class VirtualController : public vr::ITrackedDeviceServerDriver {
public:
    explicit VirtualController(bool isRight);
    ~VirtualController();

    // ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t deviceId) override;
    void Deactivate() override;
    void EnterStandby() override {}
    void* GetComponent(const char*) override { return nullptr; }
    void DebugRequest(const char*, char*, uint32_t) override {}
    vr::DriverPose_t GetPose() override;

    std::string GetSerial() const;
    uint32_t GetDeviceId() const { return m_deviceId.load(); }

    // Mirror the real controller render model so the ghost looks like the right controller.
    void ApplyRealControllerProperties(const std::string& renderModel);

    // Optional: mirror physical battery level onto the ghost for display.
    void UpdateBattery(float pct);

    // Stubs so input_hook.cpp (kept for reference) still compiles.
    vr::VRInputComponentHandle_t GetBoolHandleForPath(const std::string&) const {
        return vr::k_ulInvalidInputComponentHandle;
    }
    vr::VRInputComponentHandle_t GetScalarHandleForPath(const std::string&) const {
        return vr::k_ulInvalidInputComponentHandle;
    }

private:
    bool m_isRight;
    std::atomic<uint32_t> m_deviceId{vr::k_unTrackedDeviceIndexInvalid};
};
