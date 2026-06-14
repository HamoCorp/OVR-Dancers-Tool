#pragma once
#include <openvr_driver.h>
#include <cstdint>

class VirtualController;

// Patches the IVRDriverInput vtable to intercept Create*/Update* calls.
// When a physical controller driver (Meta Link, lighthouse) calls
// UpdateBooleanComponent / UpdateScalarComponent, we mirror the value to the
// corresponding virtual controller's handle.  This works because all drivers
// share the same IVRDriverInput singleton inside vrserver.exe.
class InputHook {
public:
    // Patch the vtable.  Call once from DeviceProvider::Init().
    static void Install(vr::IVRDriverInput* input);

    // Clear forwarding tables (called from Cleanup).
    static void Uninstall();

    // Register physDevIdx → virt after a mapping is established.
    // Builds the handle-to-handle forward table from already-seen CreateXxx calls.
    static void AddMapping(uint32_t physDevIdx, VirtualController* virt);

    // Remove all forwarding entries for physDevIdx.
    static void RemoveMapping(uint32_t physDevIdx);
};
