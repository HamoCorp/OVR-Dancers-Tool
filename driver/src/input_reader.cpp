// This file deliberately includes ONLY openvr.h (client API), NOT openvr_driver.h.
// Both headers define the same VRControllerState_t / EVRInitError types, so they
// cannot coexist in one translation unit. Keeping them in separate .cpp files avoids
// the redefinition errors while still letting the driver context acquire IVRSystem.
#include "input_reader.h"
#include <openvr.h>  // provides IVRSystem, VRControllerState_t, IVRSystem_Version

// IVRDriverContext only has two virtual methods (vtable slots 0 and 1).
// We only need slot 0 (GetGenericInterface), so a minimal shim is safe.
namespace {
struct DriverContextShim {
    virtual void* GetGenericInterface(const char* version, vr::EVRInitError* err) = 0;
    virtual uint64_t GetDriverHandle() = 0;
};
} // namespace

bool ControllerStateReader::Init(void* driverContextPtr) {
    if (!driverContextPtr) return false;
    auto* ctx = static_cast<DriverContextShim*>(driverContextPtr);
    vr::EVRInitError err = vr::VRInitError_None;
    m_vrSystem = ctx->GetGenericInterface(vr::IVRSystem_Version, &err);
    return m_vrSystem != nullptr && err == vr::VRInitError_None;
}

bool ControllerStateReader::GetState(uint32_t deviceIndex, ControllerStateRaw& out) {
    if (!m_vrSystem) return false;
    auto* sys = static_cast<vr::IVRSystem*>(m_vrSystem);
    vr::VRControllerState_t state{};
    if (!sys->GetControllerState(deviceIndex, &state, sizeof(state))) return false;
    out.packetNum       = state.unPacketNum;
    out.buttonsPressed  = state.ulButtonPressed;
    out.buttonsTouched  = state.ulButtonTouched;
    for (int i = 0; i < 5; i++) {
        out.axisX[i] = state.rAxis[i].x;
        out.axisY[i] = state.rAxis[i].y;
    }
    return true;
}
