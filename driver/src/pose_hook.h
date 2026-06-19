#pragma once
#include <openvr_driver.h>
#include <cstdint>
#include "../../shared/protocol.h"

// Hooks IVRServerDriverHost::TrackedDevicePoseUpdated (vtable[1]).
// When a physical controller has an active enabled mapping, its reported pose is
// replaced with tracker_pose * offset so the game sees it at the tracker position.
// The physical controller retains its own device class, hand role, and input
// components — games never lose input because they're still talking to the same device.
// (This mirrors OVR Input Emulator's "redirect target" mode 3 approach.)
class PoseHook {
public:
    static void Install(vr::IVRServerDriverHost* host);
    static void Uninstall();

    // Call from RunFrame after GetRawTrackedDevicePoses to keep tracker cache fresh.
    static void UpdatePoseCache(const vr::TrackedDevicePose_t* poses, uint32_t count);
    static bool GetWorldMatrix(uint32_t devIdx, vr::HmdMatrix34_t& out);

    // Redirect ctrlIdx poses → trackerIdx + offset when enabled.
    static void AddMapping(uint32_t ctrlIdx, uint32_t trackerIdx,
                           const Offset6DOF& offset, bool enabled, bool redirectMode = false);
    static void UpdateOffset(uint32_t ctrlIdx, const Offset6DOF& offset);
    static void SetEnabled(uint32_t ctrlIdx, bool enabled);
    static void RemoveMapping(uint32_t ctrlIdx);
    // Mark a tracker as hidden from games (poseIsValid=false on outgoing pose).
    // The tracker still feeds the world-matrix cache so controller mappings keep working.
    static void SetHiddenTracker(uint32_t trackerIdx, bool hidden);
    // Send synthetic keep-alive poses for world-matrix-mode controllers so they
    // don't disconnect when the physical controller sleeps/goes into standby.
    static void RunFrameKeepAlive();
};
