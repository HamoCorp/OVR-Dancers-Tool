#pragma once
#include <openvr_driver.h>
#include <cstdint>
#include "../../shared/protocol.h"

// Hooks IVRServerDriverHost::TrackedDevicePoseUpdated (vtable[1]).
// When a mapped physical controller reports its pose, the position is replaced
// with tracker_pose * offset so the game sees the controller at the tracker location.
//
// Advantages over virtual controller approach:
//  - Haptics work (game talks directly to the physical device)
//  - All input works (no forwarding needed, correct for any controller type)
//  - No vtable timing / load-order issues for input
//  - Future-proof: works with any controller driver

class PoseHook {
public:
    static void Install(vr::IVRServerDriverHost* host);
    static void Uninstall();

    // Call from RunFrame after GetRawTrackedDevicePoses to keep tracker cache fresh.
    static void UpdatePoseCache(const vr::TrackedDevicePose_t* poses, uint32_t count);

    // Redirect ctrlIdx poses to trackerIdx + offset when enabled.
    static void AddMapping(uint32_t ctrlIdx, uint32_t trackerIdx,
                           const Offset6DOF& offset, bool enabled);
    static void UpdateOffset(uint32_t ctrlIdx, const Offset6DOF& offset);
    static void SetEnabled(uint32_t ctrlIdx, bool enabled);
    static void RemoveMapping(uint32_t ctrlIdx);

    // Associate a ghost virtual controller device with a physical controller.
    // PoseHook will forward the physical controller's pre-hook real pose to this
    // ghost device every frame so the user can see where the physical controller is.
    static void SetGhostDevice(uint32_t ctrlIdx, uint32_t ghostDevIdx);

    // Called by VirtualController::GetPose() to retrieve the latest pose pushed by
    // the physical controller hook. Returns false if no pose has been stored yet.
    static bool GetGhostPose(uint32_t ghostDevIdx, vr::DriverPose_t& outPose);
};
