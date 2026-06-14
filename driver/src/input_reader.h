#pragma once
#include <cstdint>

// Raw controller state — mirrors VRControllerState_t fields but without openvr.h dependency.
// Allows device_provider (openvr_driver.h world) to share state with input_reader
// (openvr.h world) without pulling both VR headers into the same translation unit.
struct ControllerStateRaw {
    uint32_t packetNum      = 0;
    uint64_t buttonsPressed = 0;
    uint64_t buttonsTouched = 0;
    float    axisX[5]       = {};
    float    axisY[5]       = {};
};

// Wraps vr::IVRSystem::GetControllerState() with no openvr_driver.h dependency.
// Call Init() once at driver Init() time, then GetState() each RunFrame().
class ControllerStateReader {
public:
    // Pass the IVRDriverContext* (from driver Init) as void* to avoid header conflicts.
    // Returns true if IVRSystem was acquired from the driver context.
    bool Init(void* driverContextPtr);

    // Returns true + fills 'out' if the device exists and state changed from last call.
    bool GetState(uint32_t deviceIndex, ControllerStateRaw& out);

private:
    void* m_vrSystem = nullptr; // vr::IVRSystem*, stored as void* to keep headers separate
};
