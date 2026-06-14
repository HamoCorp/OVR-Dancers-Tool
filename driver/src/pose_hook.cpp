#include "pose_hook.h"
#include <windows.h>
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <cmath>

extern void DLog(const char* fmt, ...);

namespace {

using namespace vr;

typedef void (*FnPoseUpdated)(IVRServerDriverHost*, uint32_t, const DriverPose_t&, uint32_t);
static FnPoseUpdated g_realPoseUpdated = nullptr;

struct PoseMappingEntry {
    uint32_t   trackerIdx;
    Offset6DOF offset;
    bool       enabled;
};

static std::unordered_map<uint32_t, PoseMappingEntry> g_mappings;

// physDevIdx → ghost virtual controller device index (receives the pre-hook real pose)
static std::unordered_map<uint32_t, uint32_t> g_ghostDevIdx;

// ghostDevIdx → last world-space pose forwarded from the physical controller hook.
// VirtualController::GetPose() reads this so SteamVR's poll never resets the ghost to invalid.
static std::unordered_map<uint32_t, vr::DriverPose_t> g_ghostPoseCache;

// World-space 3x4 matrices from GetRawTrackedDevicePoses, refreshed each RunFrame.
static HmdMatrix34_t g_worldPoses[k_unMaxTrackedDeviceCount];
static bool          g_worldPoseValid[k_unMaxTrackedDeviceCount];

static std::mutex g_mutex;

// ── Math helpers (same as VirtualController) ──────────────────────────────────

static HmdMatrix34_t OffsetToMatrix(const Offset6DOF& off) {
    float w = off.rot.w, x = off.rot.x, y = off.rot.y, z = off.rot.z;
    HmdMatrix34_t m;
    m.m[0][0] = 1-2*(y*y+z*z); m.m[0][1] = 2*(x*y-w*z);   m.m[0][2] = 2*(x*z+w*y);   m.m[0][3] = off.pos.x;
    m.m[1][0] = 2*(x*y+w*z);   m.m[1][1] = 1-2*(x*x+z*z); m.m[1][2] = 2*(y*z-w*x);   m.m[1][3] = off.pos.y;
    m.m[2][0] = 2*(x*z-w*y);   m.m[2][1] = 2*(y*z+w*x);   m.m[2][2] = 1-2*(x*x+y*y); m.m[2][3] = off.pos.z;
    return m;
}

static HmdMatrix34_t MulMatrix34(const HmdMatrix34_t& a, const HmdMatrix34_t& b) {
    HmdMatrix34_t r = {};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a.m[i][0]*b.m[0][j] + a.m[i][1]*b.m[1][j] + a.m[i][2]*b.m[2][j]
                      + (j == 3 ? a.m[i][3] : 0.0f);
    return r;
}

// Convert a world-space 3x4 matrix back into the controller's driver-local frame.
// ctrlPose supplies the controller's qWorldFromDriverRotation and
// vecWorldFromDriverTranslation so we can invert the world→driver transform.
// For most tracking systems both are identity/zero and the math is a no-op.
static void WorldToDriverSpace(
    const HmdMatrix34_t& worldMat,
    const DriverPose_t&  ctrlPose,
    double               outPos[3],
    HmdQuaternion_t&     outRot)
{
    // --- Position ---
    // driverPos = inverse(qWfD) * (worldPos - vecWorldFromDriverTranslation)
    double dx = worldMat.m[0][3] - ctrlPose.vecWorldFromDriverTranslation[0];
    double dy = worldMat.m[1][3] - ctrlPose.vecWorldFromDriverTranslation[1];
    double dz = worldMat.m[2][3] - ctrlPose.vecWorldFromDriverTranslation[2];

    // Conjugate of qWorldFromDriverRotation (unit quaternion inverse)
    double qw =  ctrlPose.qWorldFromDriverRotation.w;
    double qx = -ctrlPose.qWorldFromDriverRotation.x;
    double qy = -ctrlPose.qWorldFromDriverRotation.y;
    double qz = -ctrlPose.qWorldFromDriverRotation.z;
    double tx = 2.0*(qy*dz - qz*dy);
    double ty = 2.0*(qz*dx - qx*dz);
    double tz = 2.0*(qx*dy - qy*dx);
    outPos[0] = dx + qw*tx + qy*tz - qz*ty;
    outPos[1] = dy + qw*ty + qz*tx - qx*tz;
    outPos[2] = dz + qw*tz + qx*ty - qy*tx;

    // --- Rotation: extract quaternion from world matrix (Shepperd method) ---
    double wrw, wrx, wry, wrz;
    double tr = worldMat.m[0][0] + worldMat.m[1][1] + worldMat.m[2][2];
    if (tr > 0.0) {
        double s = 0.5 / sqrt(tr + 1.0);
        wrw = 0.25 / s;
        wrx = (worldMat.m[2][1] - worldMat.m[1][2]) * s;
        wry = (worldMat.m[0][2] - worldMat.m[2][0]) * s;
        wrz = (worldMat.m[1][0] - worldMat.m[0][1]) * s;
    } else if (worldMat.m[0][0] > worldMat.m[1][1] && worldMat.m[0][0] > worldMat.m[2][2]) {
        double s = 2.0 * sqrt(1.0 + worldMat.m[0][0] - worldMat.m[1][1] - worldMat.m[2][2]);
        wrw = (worldMat.m[2][1] - worldMat.m[1][2]) / s;
        wrx = 0.25 * s;
        wry = (worldMat.m[0][1] + worldMat.m[1][0]) / s;
        wrz = (worldMat.m[0][2] + worldMat.m[2][0]) / s;
    } else if (worldMat.m[1][1] > worldMat.m[2][2]) {
        double s = 2.0 * sqrt(1.0 + worldMat.m[1][1] - worldMat.m[0][0] - worldMat.m[2][2]);
        wrw = (worldMat.m[0][2] - worldMat.m[2][0]) / s;
        wrx = (worldMat.m[0][1] + worldMat.m[1][0]) / s;
        wry = 0.25 * s;
        wrz = (worldMat.m[1][2] + worldMat.m[2][1]) / s;
    } else {
        double s = 2.0 * sqrt(1.0 + worldMat.m[2][2] - worldMat.m[0][0] - worldMat.m[1][1]);
        wrw = (worldMat.m[1][0] - worldMat.m[0][1]) / s;
        wrx = (worldMat.m[0][2] + worldMat.m[2][0]) / s;
        wry = (worldMat.m[1][2] + worldMat.m[2][1]) / s;
        wrz = 0.25 * s;
    }

    // outRot = conjugate(qWfD) * worldRot  (brings worldRot into driver space)
    outRot.w = qw*wrw - qx*wrx - qy*wry - qz*wrz;
    outRot.x = qw*wrx + qx*wrw + qy*wrz - qz*wry;
    outRot.y = qw*wry - qx*wrz + qy*wrw + qz*wrx;
    outRot.z = qw*wrz + qx*wry - qy*wrx + qz*wrw;
}

// ── Vtable write helper ───────────────────────────────────────────────────────

static void PatchEntry(void** vtable, int idx, void* newFn) {
    void** entry = &vtable[idx];
    DWORD  old;
    VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old);
    *entry = newFn;
    VirtualProtect(entry, sizeof(void*), old, &old);
}

// ── Hook function ─────────────────────────────────────────────────────────────

static void HookPoseUpdated(
    IVRServerDriverHost* self,
    uint32_t             devIdx,
    const DriverPose_t&  pose,
    uint32_t             size)
{
    // Forward the real (pre-hook) pose to the ghost virtual controller so the user can
    // see where the physical controller actually is, even when the mapping is active.
    // The physical controller's DriverPose_t is in its driver's local coordinate frame.
    // To guarantee the ghost appears at the correct world position regardless of whether
    // qWorldFromDriverRotation is identity, explicitly convert to world space and express
    // the ghost pose in our driver's identity coordinate frame.
    {
        uint32_t ghostIdx = k_unTrackedDeviceIndexInvalid;
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            auto git = g_ghostDevIdx.find(devIdx);
            if (git != g_ghostDevIdx.end())
                ghostIdx = git->second;
        }
        if (ghostIdx != k_unTrackedDeviceIndexInvalid) {
            // worldPos  = qWfD * vecPosition + vecWfDT
            // worldRot  = qWfD * qRotation
            double qw = pose.qWorldFromDriverRotation.w;
            double qx = pose.qWorldFromDriverRotation.x;
            double qy = pose.qWorldFromDriverRotation.y;
            double qz = pose.qWorldFromDriverRotation.z;
            double px = pose.vecPosition[0], py = pose.vecPosition[1], pz = pose.vecPosition[2];
            double cx = 2.0*(qy*pz - qz*py);
            double cy = 2.0*(qz*px - qx*pz);
            double cz = 2.0*(qx*py - qy*px);
            double wX = px + qw*cx + qy*cz - qz*cy + pose.vecWorldFromDriverTranslation[0];
            double wY = py + qw*cy + qz*cx - qx*cz + pose.vecWorldFromDriverTranslation[1];
            double wZ = pz + qw*cz + qx*cy - qy*cx + pose.vecWorldFromDriverTranslation[2];
            double rw = pose.qRotation.w, rx = pose.qRotation.x,
                   ry = pose.qRotation.y, rz = pose.qRotation.z;
            double grw = qw*rw - qx*rx - qy*ry - qz*rz;
            double grx = qw*rx + qx*rw + qy*rz - qz*ry;
            double gry = qw*ry - qx*rz + qy*rw + qz*rx;
            double grz = qw*rz + qx*ry - qy*rx + qz*rw;

            DriverPose_t gp = pose;
            gp.vecPosition[0] = wX;
            gp.vecPosition[1] = wY;
            gp.vecPosition[2] = wZ;
            gp.qRotation = { grw, grx, gry, grz };
            // Identity world-from-driver: vecPosition IS the world position.
            gp.qWorldFromDriverRotation       = { 1, 0, 0, 0 };
            gp.vecWorldFromDriverTranslation[0] = 0;
            gp.vecWorldFromDriverTranslation[1] = 0;
            gp.vecWorldFromDriverTranslation[2] = 0;
            gp.qDriverFromHeadRotation           = { 1, 0, 0, 0 };
            gp.vecDriverFromHeadTranslation[0]  = 0;
            gp.vecDriverFromHeadTranslation[1]  = 0;
            gp.vecDriverFromHeadTranslation[2]  = 0;
            gp.poseIsValid       = true;
            gp.result            = vr::TrackingResult_Running_OK;
            gp.deviceIsConnected = true;
            gp.vecVelocity[0]        = gp.vecVelocity[1]        = gp.vecVelocity[2]        = 0;
            gp.vecAngularVelocity[0] = gp.vecAngularVelocity[1] = gp.vecAngularVelocity[2] = 0;
            {
                std::lock_guard<std::mutex> lk(g_mutex);
                g_ghostPoseCache[ghostIdx] = gp;
            }
            g_realPoseUpdated(self, ghostIdx, gp, size);
        }
    }

    // Look up whether this device has an active pose mapping.
    PoseMappingEntry mapping{};
    bool hasMapping = false;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_mappings.find(devIdx);
        if (it != g_mappings.end() && it->second.enabled) {
            mapping   = it->second;
            hasMapping = true;
        }
    }

    if (!hasMapping) {
        g_realPoseUpdated(self, devIdx, pose, size);
        return;
    }

    // Get the tracker's latest world-space matrix (refreshed from RunFrame).
    HmdMatrix34_t trackerWorld{};
    bool trackerValid = false;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (mapping.trackerIdx < k_unMaxTrackedDeviceCount &&
            g_worldPoseValid[mapping.trackerIdx])
        {
            trackerWorld = g_worldPoses[mapping.trackerIdx];
            trackerValid = true;
        }
    }

    if (!trackerValid) {
        // Tracker pose not yet available — pass controller's own pose through.
        g_realPoseUpdated(self, devIdx, pose, size);
        return;
    }

    // Compute final world position: tracker_world * offset_matrix
    HmdMatrix34_t finalWorld = MulMatrix34(trackerWorld, OffsetToMatrix(mapping.offset));

    // Build a modified DriverPose that keeps all of the controller's timing/validity
    // fields but replaces position and rotation with the tracker-derived values.
    DriverPose_t modPose = pose;
    modPose.poseIsValid       = true;
    modPose.result            = TrackingResult_Running_OK;
    modPose.deviceIsConnected = true;

    WorldToDriverSpace(finalWorld, pose, modPose.vecPosition, modPose.qRotation);

    // Forward velocity from the world matrix → driver space as well.
    // For simplicity we zero it; prediction is not critical for tracker-driven poses.
    modPose.vecVelocity[0] = modPose.vecVelocity[1] = modPose.vecVelocity[2] = 0.0;
    modPose.vecAngularVelocity[0] = modPose.vecAngularVelocity[1] = modPose.vecAngularVelocity[2] = 0.0;

    static int s_log = 0;
    if (++s_log <= 5)
        DLog("OVRDancers: PoseHook ctrl=%u → tracker=%u pos=(%.3f,%.3f,%.3f)\n",
             devIdx, mapping.trackerIdx,
             modPose.vecPosition[0], modPose.vecPosition[1], modPose.vecPosition[2]);

    g_realPoseUpdated(self, devIdx, modPose, size);
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void PoseHook::Install(vr::IVRServerDriverHost* host) {
    if (!host) return;
    void** vtable = *reinterpret_cast<void***>(host);
    g_realPoseUpdated = reinterpret_cast<FnPoseUpdated>(vtable[1]);
    PatchEntry(vtable, 1, reinterpret_cast<void*>(HookPoseUpdated));
    DLog("OVRDancers: PoseHook installed — IVRServerDriverHost::TrackedDevicePoseUpdated hooked\n");
}

void PoseHook::Uninstall() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_mappings.clear();
    memset(g_worldPoseValid, 0, sizeof(g_worldPoseValid));
    // Note: vtable is NOT restored — doing so safely requires the original vtable pointer
    // which may have been patched by other hooks. SteamVR process will exit shortly anyway.
}

void PoseHook::UpdatePoseCache(const vr::TrackedDevicePose_t* poses, uint32_t count) {
    std::lock_guard<std::mutex> lk(g_mutex);
    for (uint32_t i = 0; i < count && i < vr::k_unMaxTrackedDeviceCount; i++) {
        g_worldPoseValid[i] = poses[i].bPoseIsValid;
        if (poses[i].bPoseIsValid)
            g_worldPoses[i] = poses[i].mDeviceToAbsoluteTracking;
    }
}

void PoseHook::AddMapping(uint32_t ctrlIdx, uint32_t trackerIdx,
                          const Offset6DOF& offset, bool enabled)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_mappings[ctrlIdx] = { trackerIdx, offset, enabled };
    DLog("OVRDancers: PoseHook::AddMapping ctrl=%u tracker=%u enabled=%d\n",
         ctrlIdx, trackerIdx, (int)enabled);
}

void PoseHook::UpdateOffset(uint32_t ctrlIdx, const Offset6DOF& offset) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_mappings.find(ctrlIdx);
    if (it != g_mappings.end())
        it->second.offset = offset;
}

void PoseHook::SetEnabled(uint32_t ctrlIdx, bool enabled) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_mappings.find(ctrlIdx);
    if (it != g_mappings.end())
        it->second.enabled = enabled;
    DLog("OVRDancers: PoseHook::SetEnabled ctrl=%u enabled=%d\n", ctrlIdx, (int)enabled);
}

void PoseHook::RemoveMapping(uint32_t ctrlIdx) {
    uint32_t ghostIdx = k_unTrackedDeviceIndexInvalid;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_mappings.erase(ctrlIdx);
        auto git = g_ghostDevIdx.find(ctrlIdx);
        if (git != g_ghostDevIdx.end()) {
            ghostIdx = git->second;
            g_ghostDevIdx.erase(git);
        }
    }
    // Make the ghost disappear when the mapping is cleared
    if (ghostIdx != k_unTrackedDeviceIndexInvalid && g_realPoseUpdated) {
        DriverPose_t off{};
        off.poseIsValid       = false;
        off.result            = TrackingResult_Uninitialized;
        off.deviceIsConnected = false;
        off.qWorldFromDriverRotation = {1,0,0,0};
        off.qDriverFromHeadRotation  = {1,0,0,0};
        // self pointer not available here; g_realPoseUpdated is a plain function, not method.
        // Call via the driver host interface directly.
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(ghostIdx, off, sizeof(off));
    }
    DLog("OVRDancers: PoseHook::RemoveMapping ctrl=%u\n", ctrlIdx);
}

void PoseHook::SetGhostDevice(uint32_t ctrlIdx, uint32_t ghostDevIdx) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_ghostDevIdx[ctrlIdx] = ghostDevIdx;
    DLog("OVRDancers: PoseHook::SetGhostDevice ctrl=%u ghost=%u\n", ctrlIdx, ghostDevIdx);
}

bool PoseHook::GetGhostPose(uint32_t ghostDevIdx, vr::DriverPose_t& outPose) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_ghostPoseCache.find(ghostDevIdx);
    if (it == g_ghostPoseCache.end()) return false;
    outPose = it->second;
    return true;
}
