#include "pose_hook.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cmath>

extern void DLog(const char* fmt, ...);

namespace {

using namespace vr;

typedef void (*FnPoseUpdated)(IVRServerDriverHost*, uint32_t, const DriverPose_t&, uint32_t);
static FnPoseUpdated g_realPoseUpdated = nullptr;

struct MappingEntry {
    uint32_t   trackerIdx;
    Offset6DOF offset;
    bool       enabled;
    bool       redirectMode;
};

static std::unordered_map<uint32_t, MappingEntry> g_mappings;
static std::unordered_set<uint32_t>               g_hiddenTrackers;
static IVRServerDriverHost* g_host = nullptr;

// Per-device: which IVRServerDriverHost 'self' pointer the device last used.
// Allows RunFrameKeepAlive to call g_realPoseUpdated with the correct host so
// SteamVR honours the update (each driver has its own host instance).
static IVRServerDriverHost* g_deviceOwner[k_unMaxTrackedDeviceCount] = {};

static HmdMatrix34_t g_worldPoses[k_unMaxTrackedDeviceCount];
static bool          g_worldPoseValid[k_unMaxTrackedDeviceCount];

static std::mutex g_mutex;

// ── Math helpers ──────────────────────────────────────────────────────────────

static HmdQuaternion_t MatrixToQuat(const HmdMatrix34_t& m) {
    HmdQuaternion_t q{1,0,0,0};
    double tr = m.m[0][0] + m.m[1][1] + m.m[2][2];
    if (tr > 0.0) {
        double s = 0.5 / sqrt(tr + 1.0);
        q.w = 0.25/s; q.x=(m.m[2][1]-m.m[1][2])*s; q.y=(m.m[0][2]-m.m[2][0])*s; q.z=(m.m[1][0]-m.m[0][1])*s;
    } else if (m.m[0][0]>m.m[1][1] && m.m[0][0]>m.m[2][2]) {
        double s=2.0*sqrt(1.0+m.m[0][0]-m.m[1][1]-m.m[2][2]);
        q.w=(m.m[2][1]-m.m[1][2])/s; q.x=0.25*s; q.y=(m.m[0][1]+m.m[1][0])/s; q.z=(m.m[0][2]+m.m[2][0])/s;
    } else if (m.m[1][1]>m.m[2][2]) {
        double s=2.0*sqrt(1.0+m.m[1][1]-m.m[0][0]-m.m[2][2]);
        q.w=(m.m[0][2]-m.m[2][0])/s; q.x=(m.m[0][1]+m.m[1][0])/s; q.y=0.25*s; q.z=(m.m[1][2]+m.m[2][1])/s;
    } else {
        double s=2.0*sqrt(1.0+m.m[2][2]-m.m[0][0]-m.m[1][1]);
        q.w=(m.m[1][0]-m.m[0][1])/s; q.x=(m.m[0][2]+m.m[2][0])/s; q.y=(m.m[1][2]+m.m[2][1])/s; q.z=0.25*s;
    }
    return q;
}

static HmdQuaternion_t QuatMul(const HmdQuaternion_t& a, const HmdQuaternion_t& b) {
    return { a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
             a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
             a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
             a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w };
}

static void QuatRotateVec(const HmdQuaternion_t& q, double& vx, double& vy, double& vz) {
    double t = 2*(q.y*vz - q.z*vy);
    double u = 2*(q.z*vx - q.x*vz);
    double w = 2*(q.x*vy - q.y*vx);
    vx = vx + q.w*t + (q.y*w - q.z*u);
    vy = vy + q.w*u + (q.z*t - q.x*w);
    vz = vz + q.w*w + (q.x*u - q.y*t);
}

// ── Vtable patch ──────────────────────────────────────────────────────────────

static void PatchEntry(void** vtable, int idx, void* fn) {
#ifdef _WIN32
    DWORD old;
    VirtualProtect(&vtable[idx], sizeof(void*), PAGE_READWRITE, &old);
    vtable[idx] = fn;
    VirtualProtect(&vtable[idx], sizeof(void*), old, &old);
#else
    void** entry = &vtable[idx];
    size_t page = (size_t)getpagesize();
    void* page_start = (void*)((uintptr_t)entry & ~(page - 1));
    mprotect(page_start, page, PROT_READ | PROT_WRITE);
    *entry = fn;
    mprotect(page_start, page, PROT_READ);
#endif
}

// ── Hook ──────────────────────────────────────────────────────────────────────

static void HookPoseUpdated(
    IVRServerDriverHost* self,
    uint32_t             devIdx,
    const DriverPose_t&  pose,
    uint32_t             size)
{
    // Track which host instance owns each device for use in RunFrameKeepAlive.
    // Pointer assignment is naturally atomic on all supported platforms.
    if (devIdx < k_unMaxTrackedDeviceCount)
        g_deviceOwner[devIdx] = self;

    MappingEntry selfMapping{};
    bool hasSelf = false;

    struct RedirectFwd { uint32_t ctrlIdx; MappingEntry m; };
    RedirectFwd fwds[MAX_DEVICES];
    int fwdCount = 0;

    // World-matrix controllers that use devIdx as their tracker source.
    // Used for tracker-triggered keep-alive so controllers stay alive when Quest sleeps.
    struct WMSource { uint32_t ctrlIdx; MappingEntry m; };
    WMSource wmSources[MAX_DEVICES];
    int wmCount = 0;

    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_mappings.find(devIdx);
        if (it != g_mappings.end() && it->second.enabled) {
            selfMapping = it->second;
            hasSelf     = true;
        }
        for (auto& [ci, m] : g_mappings) {
            if (m.enabled && m.trackerIdx == devIdx) {
                if (m.redirectMode && fwdCount < MAX_DEVICES)
                    fwds[fwdCount++] = {ci, m};
                else if (!m.redirectMode && wmCount < MAX_DEVICES)
                    wmSources[wmCount++] = {ci, m};
            }
        }
    }

    // ── WORLD-MATRIX MODE (default) ───────────────────────────────────────────
    // Intercept the controller's own pose and replace it with the tracker's
    // world-space position plus calibrated offset.
    // We NEVER pass the controller's original pose through — even if it sends
    // deviceIsConnected=false or stops tracking. This prevents SteamVR from
    // disconnecting the controller when the Quest controller goes to sleep/menu.
    if (hasSelf && !selfMapping.redirectMode) {
        HmdMatrix34_t tw{};
        bool twValid = false;
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            twValid = g_worldPoseValid[selfMapping.trackerIdx];
            if (twValid) tw = g_worldPoses[selfMapping.trackerIdx];
        }
        if (!twValid) {
            // Tracker not yet seen — suppress the controller's own disconnected pose
            // by sending a zero-position but connected pose so SteamVR doesn't drop it.
            DriverPose_t mp = pose;
            mp.poseIsValid       = false; // no valid position, but keep device alive
            mp.result            = TrackingResult_Running_OutOfRange;
            mp.deviceIsConnected = true;
            g_realPoseUpdated(self, devIdx, mp, size);
            return;
        }

        double lox=selfMapping.offset.pos.x, loy=selfMapping.offset.pos.y, loz=selfMapping.offset.pos.z;
        double wx = tw.m[0][3] + tw.m[0][0]*lox + tw.m[0][1]*loy + tw.m[0][2]*loz;
        double wy = tw.m[1][3] + tw.m[1][0]*lox + tw.m[1][1]*loy + tw.m[1][2]*loz;
        double wz = tw.m[2][3] + tw.m[2][0]*lox + tw.m[2][1]*loy + tw.m[2][2]*loz;

        // World-space origin offset — additive position correction
        wx += selfMapping.offset.originOffset.x;
        wy += selfMapping.offset.originOffset.y;
        wz += selfMapping.offset.originOffset.z;

        HmdQuaternion_t trackerRot = MatrixToQuat(tw);
        HmdQuaternion_t desiredRot = QuatMul(trackerRot,
            {selfMapping.offset.rot.w, selfMapping.offset.rot.x,
             selfMapping.offset.rot.y, selfMapping.offset.rot.z});

        // World-space origin rotation applied on top
        HmdQuaternion_t oRot = {selfMapping.offset.originRot.w, selfMapping.offset.originRot.x,
                                 selfMapping.offset.originRot.y, selfMapping.offset.originRot.z};
        desiredRot = QuatMul(oRot, desiredRot);

        DriverPose_t mp = pose;
        mp.poseIsValid       = true;
        mp.result            = TrackingResult_Running_OK;
        mp.deviceIsConnected = true;
        mp.qWorldFromDriverRotation         = {1,0,0,0};
        mp.vecWorldFromDriverTranslation[0] = 0;
        mp.vecWorldFromDriverTranslation[1] = 0;
        mp.vecWorldFromDriverTranslation[2] = 0;
        mp.vecDriverFromHeadTranslation[0]  = 0;
        mp.vecDriverFromHeadTranslation[1]  = 0;
        mp.vecDriverFromHeadTranslation[2]  = 0;
        mp.qDriverFromHeadRotation          = {1,0,0,0};
        mp.vecPosition[0] = (float)wx;
        mp.vecPosition[1] = (float)wy;
        mp.vecPosition[2] = (float)wz;
        mp.qRotation      = desiredRot;
        mp.vecVelocity[0] = mp.vecVelocity[1] = mp.vecVelocity[2] = 0;
        mp.vecAngularVelocity[0] = mp.vecAngularVelocity[1] = mp.vecAngularVelocity[2] = 0;
        g_realPoseUpdated(self, devIdx, mp, size);
        return;
    }

    // ── REDIRECT MODE ─────────────────────────────────────────────────────────
    // When redirect mode is on for a controller, suppress its own pose updates
    // and instead drive it from the tracker's pose callbacks (OVRIE approach).
    if (hasSelf && selfMapping.redirectMode)
        return; // suppress controller's own pose

    // Forward tracker pose to any redirect-mode controllers mapped to this device.
    bool isSource = (fwdCount > 0);
    for (int i = 0; i < fwdCount; i++) {
        const auto& f = fwds[i];
        DriverPose_t rp = pose;

        // Tracker-local calibrated offset (in driver/tracker space)
        double lox = f.m.offset.pos.x, loy = f.m.offset.pos.y, loz = f.m.offset.pos.z;
        QuatRotateVec(rp.qRotation, lox, loy, loz);
        rp.vecPosition[0] += (float)lox;
        rp.vecPosition[1] += (float)loy;
        rp.vecPosition[2] += (float)loz;
        rp.qRotation = QuatMul(rp.qRotation,
            {f.m.offset.rot.w, f.m.offset.rot.x, f.m.offset.rot.y, f.m.offset.rot.z});

        // Origin offset: translate in world space by back-transforming through qWfD
        double ox = f.m.offset.originOffset.x, oy = f.m.offset.originOffset.y, oz = f.m.offset.originOffset.z;
        if (ox != 0.0 || oy != 0.0 || oz != 0.0) {
            HmdQuaternion_t qi = {rp.qWorldFromDriverRotation.w, -rp.qWorldFromDriverRotation.x,
                                  -rp.qWorldFromDriverRotation.y, -rp.qWorldFromDriverRotation.z};
            QuatRotateVec(qi, ox, oy, oz);
            rp.vecPosition[0] += (float)ox;
            rp.vecPosition[1] += (float)oy;
            rp.vecPosition[2] += (float)oz;
        }

        // Origin rotation: premultiply into qWfD so world rotation = originRot * trackerWorldRot * calibRot
        HmdQuaternion_t oRot = {f.m.offset.originRot.w, f.m.offset.originRot.x,
                                 f.m.offset.originRot.y, f.m.offset.originRot.z};
        rp.qWorldFromDriverRotation = QuatMul(oRot, rp.qWorldFromDriverRotation);

        rp.poseIsValid       = true;
        rp.result            = TrackingResult_Running_OK;
        rp.deviceIsConnected = true;
        g_realPoseUpdated(self, f.ctrlIdx, rp, size);
    }

    bool isTrackerHidden = g_hiddenTrackers.count(devIdx) > 0;
    if (isSource) {
        // Redirect mode: pass the real tracker pose through so the overlay can
        // read it via GetDeviceToAbsoluteTrackingPose (needed for smooth cal).
        // The user can hide the tracker visually via the Hide Trackers setting.
        g_realPoseUpdated(self, devIdx, pose, size);
    } else if (isTrackerHidden) {
        DriverPose_t disc = pose;
        disc.poseIsValid = false;
        disc.result      = TrackingResult_Running_OutOfRange;
        g_realPoseUpdated(self, devIdx, disc, size);
    } else {
        g_realPoseUpdated(self, devIdx, pose, size);
    }

    // ── WORLD-MATRIX TRACKER-TRIGGERED KEEP-ALIVE ────────────────────────────
    // When the Lighthouse tracker fires a pose, immediately push a fresh synthetic
    // pose to any world-matrix controllers mapped to it.  This keeps those controllers
    // alive in SteamVR even when the Quest controller enters sleep/standby and stops
    // sending its own TrackedDevicePoseUpdated calls.
    if (wmCount > 0) {
        HmdMatrix34_t tw{};
        bool twValid = false;
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            twValid = g_worldPoseValid[devIdx];
            if (twValid) tw = g_worldPoses[devIdx];
        }
        if (twValid) {
            for (int i = 0; i < wmCount; i++) {
                const auto& wm = wmSources[i];
                double lox = wm.m.offset.pos.x, loy = wm.m.offset.pos.y, loz = wm.m.offset.pos.z;
                double wx = tw.m[0][3] + tw.m[0][0]*lox + tw.m[0][1]*loy + tw.m[0][2]*loz;
                double wy = tw.m[1][3] + tw.m[1][0]*lox + tw.m[1][1]*loy + tw.m[1][2]*loz;
                double wz = tw.m[2][3] + tw.m[2][0]*lox + tw.m[2][1]*loy + tw.m[2][2]*loz;
                wx += wm.m.offset.originOffset.x;
                wy += wm.m.offset.originOffset.y;
                wz += wm.m.offset.originOffset.z;
                HmdQuaternion_t trackerRot = MatrixToQuat(tw);
                HmdQuaternion_t desiredRot = QuatMul(trackerRot,
                    {wm.m.offset.rot.w, wm.m.offset.rot.x, wm.m.offset.rot.y, wm.m.offset.rot.z});
                HmdQuaternion_t oRot = {wm.m.offset.originRot.w, wm.m.offset.originRot.x,
                                         wm.m.offset.originRot.y, wm.m.offset.originRot.z};
                desiredRot = QuatMul(oRot, desiredRot);

                DriverPose_t mp = pose;
                mp.poseIsValid       = true;
                mp.result            = TrackingResult_Running_OK;
                mp.deviceIsConnected = true;
                mp.qWorldFromDriverRotation         = {1, 0, 0, 0};
                mp.vecWorldFromDriverTranslation[0] = 0;
                mp.vecWorldFromDriverTranslation[1] = 0;
                mp.vecWorldFromDriverTranslation[2] = 0;
                mp.vecDriverFromHeadTranslation[0]  = 0;
                mp.vecDriverFromHeadTranslation[1]  = 0;
                mp.vecDriverFromHeadTranslation[2]  = 0;
                mp.qDriverFromHeadRotation          = {1, 0, 0, 0};
                mp.vecPosition[0] = (float)wx;
                mp.vecPosition[1] = (float)wy;
                mp.vecPosition[2] = (float)wz;
                mp.qRotation      = desiredRot;
                mp.vecVelocity[0] = mp.vecVelocity[1] = mp.vecVelocity[2] = 0;
                mp.vecAngularVelocity[0] = mp.vecAngularVelocity[1] = mp.vecAngularVelocity[2] = 0;
                IVRServerDriverHost* ctrlHost =
                    (wm.ctrlIdx < k_unMaxTrackedDeviceCount && g_deviceOwner[wm.ctrlIdx])
                    ? g_deviceOwner[wm.ctrlIdx] : self;
                g_realPoseUpdated(ctrlHost, wm.ctrlIdx, mp, size);
            }
        }
    }
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void PoseHook::Install(vr::IVRServerDriverHost* host) {
    if (!host) return;
    g_host = host;
    void** vtable = *reinterpret_cast<void***>(host);
    g_realPoseUpdated = reinterpret_cast<FnPoseUpdated>(vtable[1]);
    PatchEntry(vtable, 1, reinterpret_cast<void*>(HookPoseUpdated));
    DLog("OVRDancers: PoseHook installed\n");
}

void PoseHook::Uninstall() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_mappings.clear();
    memset(g_worldPoseValid, 0, sizeof(g_worldPoseValid));
}

void PoseHook::UpdatePoseCache(const vr::TrackedDevicePose_t* poses, uint32_t count) {
    std::lock_guard<std::mutex> lk(g_mutex);
    for (uint32_t i = 0; i < count && i < k_unMaxTrackedDeviceCount; i++) {
        // Only update when valid — keep last known pose when tracker is briefly occluded.
        // This prevents the controller from jumping to the Quest's raw pose on tracker glitches.
        if (poses[i].bPoseIsValid) {
            g_worldPoseValid[i] = true;
            g_worldPoses[i] = poses[i].mDeviceToAbsoluteTracking;
        }
    }
}

bool PoseHook::GetWorldMatrix(uint32_t devIdx, vr::HmdMatrix34_t& out) {
    if (devIdx >= vr::k_unMaxTrackedDeviceCount) return false;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_worldPoseValid[devIdx]) return false;
    out = g_worldPoses[devIdx];
    return true;
}

void PoseHook::AddMapping(uint32_t ctrlIdx, uint32_t trackerIdx,
                          const Offset6DOF& offset, bool enabled, bool redirectMode) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_mappings[ctrlIdx] = {trackerIdx, offset, enabled, redirectMode};
    DLog("OVRDancers: PoseHook::AddMapping ctrl=%u tracker=%u enabled=%d redirect=%d\n",
         ctrlIdx, trackerIdx, (int)enabled, (int)redirectMode);
}

void PoseHook::UpdateOffset(uint32_t ctrlIdx, const Offset6DOF& offset) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_mappings.find(ctrlIdx);
    if (it != g_mappings.end()) it->second.offset = offset;
}

void PoseHook::SetEnabled(uint32_t ctrlIdx, bool enabled) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_mappings.find(ctrlIdx);
    if (it != g_mappings.end()) it->second.enabled = enabled;
    DLog("OVRDancers: PoseHook::SetEnabled ctrl=%u enabled=%d\n", ctrlIdx, (int)enabled);
}

void PoseHook::RemoveMapping(uint32_t ctrlIdx) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_mappings.erase(ctrlIdx);
    DLog("OVRDancers: PoseHook::RemoveMapping ctrl=%u\n", ctrlIdx);
}

void PoseHook::SetHiddenTracker(uint32_t trackerIdx, bool hidden) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (hidden) g_hiddenTrackers.insert(trackerIdx);
    else        g_hiddenTrackers.erase(trackerIdx);
}

void PoseHook::RunFrameKeepAlive() {
    // For world-matrix-mode controllers: send a synthetic pose each RunFrame so
    // SteamVR never marks the controller as disconnected when it enters sleep/standby.
    if (!g_realPoseUpdated || !g_host) return;

    struct PendingPose { uint32_t ctrlIdx; DriverPose_t pose; };
    PendingPose pending[MAX_DEVICES];
    int count = 0;

    {
        std::lock_guard<std::mutex> lk(g_mutex);
        for (auto& [ctrlIdx, m] : g_mappings) {
            if (!m.enabled || m.redirectMode) continue; // redirect mode drives itself
            if (!g_worldPoseValid[m.trackerIdx]) continue;
            if (count >= MAX_DEVICES) break;

            const auto& tw = g_worldPoses[m.trackerIdx];
            double lox = m.offset.pos.x, loy = m.offset.pos.y, loz = m.offset.pos.z;
            double wx = tw.m[0][3] + tw.m[0][0]*lox + tw.m[0][1]*loy + tw.m[0][2]*loz;
            double wy = tw.m[1][3] + tw.m[1][0]*lox + tw.m[1][1]*loy + tw.m[1][2]*loz;
            double wz = tw.m[2][3] + tw.m[2][0]*lox + tw.m[2][1]*loy + tw.m[2][2]*loz;
            wx += m.offset.originOffset.x;
            wy += m.offset.originOffset.y;
            wz += m.offset.originOffset.z;

            HmdQuaternion_t trackerRot = MatrixToQuat(tw);
            HmdQuaternion_t desiredRot = QuatMul(trackerRot,
                {m.offset.rot.w, m.offset.rot.x, m.offset.rot.y, m.offset.rot.z});
            HmdQuaternion_t oRot = {m.offset.originRot.w, m.offset.originRot.x,
                                     m.offset.originRot.y, m.offset.originRot.z};
            desiredRot = QuatMul(oRot, desiredRot);

            DriverPose_t mp{};
            mp.poseIsValid       = true;
            mp.result            = TrackingResult_Running_OK;
            mp.deviceIsConnected = true;
            mp.qWorldFromDriverRotation         = {1, 0, 0, 0};
            mp.vecWorldFromDriverTranslation[0] = 0;
            mp.vecWorldFromDriverTranslation[1] = 0;
            mp.vecWorldFromDriverTranslation[2] = 0;
            mp.vecDriverFromHeadTranslation[0]  = 0;
            mp.vecDriverFromHeadTranslation[1]  = 0;
            mp.vecDriverFromHeadTranslation[2]  = 0;
            mp.qDriverFromHeadRotation          = {1, 0, 0, 0};
            mp.vecPosition[0] = (float)wx;
            mp.vecPosition[1] = (float)wy;
            mp.vecPosition[2] = (float)wz;
            mp.qRotation      = desiredRot;

            pending[count++] = {ctrlIdx, mp};
        }
    }

    for (int i = 0; i < count; i++) {
        // Use the host pointer from the device's last real pose update so SteamVR
        // recognises the call as coming from the correct driver (fixes freeze when
        // Quest controller goes to sleep and stops sending its own updates).
        uint32_t ci = pending[i].ctrlIdx;
        IVRServerDriverHost* host = (ci < k_unMaxTrackedDeviceCount && g_deviceOwner[ci])
                                    ? g_deviceOwner[ci] : g_host;
        g_realPoseUpdated(host, ci, pending[i].pose, sizeof(DriverPose_t));
    }
}
