#include "input_hook.h"
#include "virtual_controller.h"
#include <openvr_driver.h>
#include <windows.h>
#include <unordered_map>
#include <mutex>
#include <string>
#include <cstdint>

extern void DLog(const char* fmt, ...);

// ── path translation ──────────────────────────────────────────────────────────
// Quest/Oculus uses "joystick" for the thumbstick; normalize to "thumbstick".
// We do NOT translate "trackpad" → "thumbstick" — Index/Vive controllers have a
// real physical touchpad whose events should reach the virtual /input/trackpad/*
// components that we register separately in Activate().
static const char* TranslatePath(const char* p) {
    if (!p) return p;
    if (strcmp(p, "/input/joystick/x")      == 0) return "/input/thumbstick/x";
    if (strcmp(p, "/input/joystick/y")      == 0) return "/input/thumbstick/y";
    if (strcmp(p, "/input/joystick/click")  == 0) return "/input/thumbstick/click";
    if (strcmp(p, "/input/joystick/touch")  == 0) return "/input/thumbstick/touch";
    if (strcmp(p, "/input/pad/x")           == 0) return "/input/thumbstick/x";
    if (strcmp(p, "/input/pad/y")           == 0) return "/input/thumbstick/y";
    if (strcmp(p, "/input/pad/click")       == 0) return "/input/thumbstick/click";
    if (strcmp(p, "/input/pad/touch")       == 0) return "/input/thumbstick/touch";
    return p;
}

// ── internal state ────────────────────────────────────────────────────────────
namespace {

using namespace vr;

// Win64: all virtual calls pass 'this' in RCX; static functions with an explicit
// first parameter match this ABI perfectly — no special calling-convention needed.
typedef EVRInputError (*FnCreateBool  )(IVRDriverInput*, PropertyContainerHandle_t, const char*, VRInputComponentHandle_t*);
typedef EVRInputError (*FnUpdateBool  )(IVRDriverInput*, VRInputComponentHandle_t, bool, double);
typedef EVRInputError (*FnCreateScalar)(IVRDriverInput*, PropertyContainerHandle_t, const char*, VRInputComponentHandle_t*, EVRScalarType, EVRScalarUnits);
typedef EVRInputError (*FnUpdateScalar)(IVRDriverInput*, VRInputComponentHandle_t, float, double);

static FnCreateBool   g_realCreateBool   = nullptr;
static FnUpdateBool   g_realUpdateBool   = nullptr;
static FnCreateScalar g_realCreateScalar = nullptr;
static FnUpdateScalar g_realUpdateScalar = nullptr;

struct HandleInfo {
    PropertyContainerHandle_t container;
    std::string               path;
};

// All component handles seen so far via Create* hooks
static std::unordered_map<uint64_t, HandleInfo> g_boolHandles;
static std::unordered_map<uint64_t, HandleInfo> g_scalarHandles;

// physBoolHandle → virtBoolHandle  (rebuilt on AddMapping)
static std::unordered_map<uint64_t, uint64_t> g_boolFwd;
static std::unordered_map<uint64_t, uint64_t> g_scalarFwd;

// physContainer → VirtualController* for devices with an active mapping.
// Used by the Create* hooks to immediately wire up handles that arrive after
// AddMapping (e.g. controller reconnect after driver load).
static std::unordered_map<uint64_t, VirtualController*> g_containerToVirt;

static std::mutex g_mutex;

// ── hook functions ────────────────────────────────────────────────────────────

static EVRInputError HookCreateBool(
    IVRDriverInput*           self,
    PropertyContainerHandle_t container,
    const char*               name,
    VRInputComponentHandle_t* pHandle)
{
    EVRInputError err = g_realCreateBool(self, container, name, pHandle);
    if (err == VRInputError_None && pHandle && *pHandle != k_ulInvalidInputComponentHandle) {
        std::lock_guard<std::mutex> lk(g_mutex);
        uint64_t h = *pHandle;
        g_boolHandles[h] = { container, name };

        // If this container already has an active mapping, wire it up immediately
        auto vit = g_containerToVirt.find(container);
        if (vit != g_containerToVirt.end()) {
            const char* translated = TranslatePath(name);
            VRInputComponentHandle_t vh = vit->second->GetBoolHandleForPath(translated);
            if (vh != k_ulInvalidInputComponentHandle)
                g_boolFwd[h] = vh;
            // Log what the physical driver actually registered (first 60 per container)
            static int s_createLog = 0;
            if (++s_createLog <= 60)
                DLog("OVRDancers: HookCreateBool container=%llu path='%s' translated='%s' wired=%d\n",
                     (unsigned long long)container, name, translated,
                     (int)(vh != k_ulInvalidInputComponentHandle));
        }
    }
    return err;
}

static EVRInputError HookUpdateBool(
    IVRDriverInput*          self,
    VRInputComponentHandle_t handle,
    bool                     value,
    double                   timeOffset)
{
    EVRInputError err = g_realUpdateBool(self, handle, value, timeOffset);

    VRInputComponentHandle_t virtHandle = k_ulInvalidInputComponentHandle;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_boolFwd.find(handle);
        if (it != g_boolFwd.end())
            virtHandle = it->second;
    }
    if (virtHandle != k_ulInvalidInputComponentHandle)
        vr::VRDriverInput()->UpdateBooleanComponent(virtHandle, value, 0.0);

    return err;
}

static EVRInputError HookCreateScalar(
    IVRDriverInput*           self,
    PropertyContainerHandle_t container,
    const char*               name,
    VRInputComponentHandle_t* pHandle,
    EVRScalarType             eType,
    EVRScalarUnits            eUnits)
{
    EVRInputError err = g_realCreateScalar(self, container, name, pHandle, eType, eUnits);
    if (err == VRInputError_None && pHandle && *pHandle != k_ulInvalidInputComponentHandle) {
        std::lock_guard<std::mutex> lk(g_mutex);
        uint64_t h = *pHandle;
        g_scalarHandles[h] = { container, name };

        auto vit = g_containerToVirt.find(container);
        if (vit != g_containerToVirt.end()) {
            const char* translated = TranslatePath(name);
            VRInputComponentHandle_t vh = vit->second->GetScalarHandleForPath(translated);
            if (vh != k_ulInvalidInputComponentHandle)
                g_scalarFwd[h] = vh;
            static int s_sLog = 0;
            if (++s_sLog <= 60)
                DLog("OVRDancers: HookCreateScalar container=%llu path='%s' translated='%s' wired=%d\n",
                     (unsigned long long)container, name, translated,
                     (int)(vh != k_ulInvalidInputComponentHandle));
        }
    }
    return err;
}

static EVRInputError HookUpdateScalar(
    IVRDriverInput*          self,
    VRInputComponentHandle_t handle,
    float                    value,
    double                   timeOffset)
{
    EVRInputError err = g_realUpdateScalar(self, handle, value, timeOffset);

    VRInputComponentHandle_t virtHandle = k_ulInvalidInputComponentHandle;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_scalarFwd.find(handle);
        if (it != g_scalarFwd.end())
            virtHandle = it->second;
    }
    if (virtHandle != k_ulInvalidInputComponentHandle)
        vr::VRDriverInput()->UpdateScalarComponent(virtHandle, value, 0.0);

    return err;
}

// ── vtable write helper ───────────────────────────────────────────────────────

static void PatchEntry(void** vtable, int idx, void* newFn) {
    void** entry = &vtable[idx];
    DWORD  old;
    VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old);
    *entry = newFn;
    VirtualProtect(entry, sizeof(void*), old, &old);
}

} // namespace

// ── public API ────────────────────────────────────────────────────────────────

void InputHook::Install(vr::IVRDriverInput* input) {
    if (!input) return;
    void** vtable = *reinterpret_cast<void***>(input);

    g_realCreateBool   = reinterpret_cast<FnCreateBool  >(vtable[0]);
    g_realUpdateBool   = reinterpret_cast<FnUpdateBool  >(vtable[1]);
    g_realCreateScalar = reinterpret_cast<FnCreateScalar>(vtable[2]);
    g_realUpdateScalar = reinterpret_cast<FnUpdateScalar>(vtable[3]);

    PatchEntry(vtable, 0, reinterpret_cast<void*>(HookCreateBool));
    PatchEntry(vtable, 1, reinterpret_cast<void*>(HookUpdateBool));
    PatchEntry(vtable, 2, reinterpret_cast<void*>(HookCreateScalar));
    PatchEntry(vtable, 3, reinterpret_cast<void*>(HookUpdateScalar));

    DLog("OVRDancers: IVRDriverInput vtable hooked — all Update* calls will be intercepted\n");
}

void InputHook::Uninstall() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_boolFwd.clear();
    g_scalarFwd.clear();
    g_containerToVirt.clear();
}

void InputHook::AddMapping(uint32_t physDevIdx, VirtualController* virt) {
    if (!virt) return;

    vr::PropertyContainerHandle_t physCont =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(physDevIdx);
    if (physCont == vr::k_ulInvalidPropertyContainer) {
        DLog("OVRDancers: InputHook::AddMapping dev=%u — invalid property container\n", physDevIdx);
        return;
    }

    std::lock_guard<std::mutex> lk(g_mutex);

    g_containerToVirt[physCont] = virt;

    int bools = 0, scalars = 0;

    for (auto& [h, info] : g_boolHandles) {
        if (info.container != physCont) continue;
        const char* xlated = TranslatePath(info.path.c_str());
        vr::VRInputComponentHandle_t vh = virt->GetBoolHandleForPath(xlated);
        DLog("OVRDancers: AddMapping bool path='%s' xlated='%s' found=%d\n",
             info.path.c_str(), xlated, (int)(vh != vr::k_ulInvalidInputComponentHandle));
        if (vh != vr::k_ulInvalidInputComponentHandle) {
            g_boolFwd[h] = vh;
            ++bools;
        }
    }
    for (auto& [h, info] : g_scalarHandles) {
        if (info.container != physCont) continue;
        const char* xlated = TranslatePath(info.path.c_str());
        vr::VRInputComponentHandle_t vh = virt->GetScalarHandleForPath(xlated);
        DLog("OVRDancers: AddMapping scalar path='%s' xlated='%s' found=%d\n",
             info.path.c_str(), xlated, (int)(vh != vr::k_ulInvalidInputComponentHandle));
        if (vh != vr::k_ulInvalidInputComponentHandle) {
            g_scalarFwd[h] = vh;
            ++scalars;
        }
    }

    DLog("OVRDancers: InputHook::AddMapping dev=%u → %d bool + %d scalar handles wired\n",
         physDevIdx, bools, scalars);
    if (bools == 0 && scalars == 0)
        DLog("OVRDancers: InputHook — no handles seen yet for dev=%u; will wire on reconnect\n",
             physDevIdx);
}

void InputHook::RemoveMapping(uint32_t physDevIdx) {
    vr::PropertyContainerHandle_t physCont =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(physDevIdx);

    std::lock_guard<std::mutex> lk(g_mutex);
    g_containerToVirt.erase(physCont);

    auto eraseByContainer = [physCont](auto& fwdMap, auto& infoMap) {
        for (auto it = fwdMap.begin(); it != fwdMap.end(); ) {
            auto hi = infoMap.find(it->first);
            if (hi != infoMap.end() && hi->second.container == physCont)
                it = fwdMap.erase(it);
            else
                ++it;
        }
    };
    eraseByContainer(g_boolFwd,   g_boolHandles);
    eraseByContainer(g_scalarFwd, g_scalarHandles);

    DLog("OVRDancers: InputHook::RemoveMapping dev=%u\n", physDevIdx);
}
