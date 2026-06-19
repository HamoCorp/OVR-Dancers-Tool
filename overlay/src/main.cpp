#include "gl3_loader.h"   // FBO function pointers (loaded after context creation)
#include "log.h"
#include <GLFW/glfw3.h>
#include <openvr.h>
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
#include "ipc_client.h"
#include "ui/main_window.h"
#include "ui/translations.h"
#include "persistence.h"
#include "osc_sender.h"
#include "websocket_server.h"
#include <stb_image.h>

// ── Shared state (IPC thread → render thread) ─────────────────────────────────
static std::mutex        g_stateMutex;
static Msg_StateUpdate   g_pendingState  = {};
static Msg_StateUpdate   g_activeState   = {}; // current state used this frame
static std::atomic<bool> g_hasPendingState{false};
static std::atomic<bool> g_driverConnected{false};
static std::atomic<bool> g_connectingNow{false}; // guard against concurrent attempts

static IPCClient  g_ipc;
static MainWindow g_ui;

// ── Persistence ───────────────────────────────────────────────────────────────
static std::vector<SavedMapping> g_savedMappings;
static std::string               g_savePath;
// Serials of controllers we've intentionally switched away from via auto-switch.
// TryRestoreSavedMappings skips these so the old device isn't re-added after a remap.
static std::unordered_set<std::string> g_suppressedCtrlSerials;

static vr::IVRSystem* g_vrSystem = nullptr; // initialised in InitVROverlay

// ── Event-based controller state ──────────────────────────────────────────────
// We maintain per-device state updated by VREvent_ButtonPress/Unpress/Touch/Untouch.
// Events may arrive for the PHYSICAL controller index OR for the VIRTUAL controller
// index (whichever currently holds the hand role) — we track all devices.
struct EventCtrlState {
    uint64_t pressed  = 0;
    uint64_t touched  = 0;
    float    axisX[5] = {};
    float    axisY[5] = {};
    uint32_t version  = 0;
};
static EventCtrlState g_evtState[vr::k_unMaxTrackedDeviceCount] = {};
static uint32_t       g_lastSentVersion[vr::k_unMaxTrackedDeviceCount] = {};

static void ApplyButtonEvent(EventCtrlState& s, uint32_t evtType, uint32_t btn) {
    switch (evtType) {
    case vr::VREvent_ButtonPress:
        s.pressed |= (1ULL << btn);
        if (btn == vr::k_EButton_Axis1) s.axisX[1] = 1.0f;
        if (btn == vr::k_EButton_Axis2 || btn == vr::k_EButton_Grip) s.axisX[2] = 1.0f;
        ++s.version;
        break;
    case vr::VREvent_ButtonUnpress:
        s.pressed &= ~(1ULL << btn);
        if (btn == vr::k_EButton_Axis1) s.axisX[1] = 0.0f;
        if (btn == vr::k_EButton_Axis2 || btn == vr::k_EButton_Grip) s.axisX[2] = 0.0f;
        ++s.version;
        break;
    case vr::VREvent_ButtonTouch:
        s.touched |= (1ULL << btn);
        if (btn == vr::k_EButton_Axis1 && s.axisX[1] < 0.05f) s.axisX[1] = 0.05f;
        ++s.version;
        break;
    case vr::VREvent_ButtonUntouch:
        s.touched &= ~(1ULL << btn);
        if (btn == vr::k_EButton_Axis1 && s.axisX[1] < 0.06f) s.axisX[1] = 0.0f;
        ++s.version;
        break;
    }
}

static void PollControllerEvents() {
    if (!g_vrSystem) return;
    vr::VREvent_t e;
    static int s_anyEvt = 0;
    while (g_vrSystem->PollNextEvent(&e, sizeof(e))) {
        uint32_t idx = e.trackedDeviceIndex;
        // Log all events for the first 60 to see what arrives and from which device
        if (++s_anyEvt <= 60) {
            LOG_INFO("VREvent #%d: type=%u device=%u btn=%u",
                     s_anyEvt, e.eventType, idx, e.data.controller.button);
        }
        if (idx >= vr::k_unMaxTrackedDeviceCount) continue;
        uint32_t btn = e.data.controller.button;
        ApplyButtonEvent(g_evtState[idx], e.eventType, btn);
    }
}

// ── SteamVR Input action system ───────────────────────────────────────────────
static vr::VRActionSetHandle_t g_actionSet = vr::k_ulInvalidActionSetHandle;
struct ActionHandles {
    vr::VRActionHandle_t triggerL      = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t triggerR      = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t gripL         = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t gripR         = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t stickL        = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t stickR        = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t trigClickL    = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t trigClickR    = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t gripClickL    = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t gripClickR    = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t stickClickL   = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t stickClickR   = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t aClick        = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t bClick        = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t xClick        = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t yClick        = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t toggleHands   = vr::k_ulInvalidActionHandle;
} g_acts;
static bool g_actionsReady = false;

static void SetupActionManifest() {
    if (!vr::VRInput()) { LOG_INFO("IVRInput unavailable"); return; }

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
    std::string exeDir(exePath);
    auto slash = exeDir.rfind('\\');
    if (slash != std::string::npos) exeDir = exeDir.substr(0, slash);
    std::string manifest = exeDir + "\\resources\\actions.json";

    LOG_INFO("SetActionManifestPath: %s", manifest.c_str());
    vr::EVRInputError err = vr::VRInput()->SetActionManifestPath(manifest.c_str());
    if (err != vr::VRInputError_None) { LOG_INFO("SetActionManifestPath failed: %d", (int)err); return; }

    err = vr::VRInput()->GetActionSetHandle("/actions/ovrdancers", &g_actionSet);
    LOG_INFO("GetActionSetHandle: err=%d handle=%llu", (int)err, (unsigned long long)g_actionSet);

    auto getAct = [](const char* path, vr::VRActionHandle_t& h) {
        vr::EVRInputError e = vr::VRInput()->GetActionHandle(path, &h);
        if (e != vr::VRInputError_None)
            LOG_INFO("GetActionHandle(%s) err=%d", path, (int)e);
    };
    getAct("/actions/ovrdancers/in/trigger_left",       g_acts.triggerL);
    getAct("/actions/ovrdancers/in/trigger_right",      g_acts.triggerR);
    getAct("/actions/ovrdancers/in/grip_left",          g_acts.gripL);
    getAct("/actions/ovrdancers/in/grip_right",         g_acts.gripR);
    getAct("/actions/ovrdancers/in/thumbstick_left",    g_acts.stickL);
    getAct("/actions/ovrdancers/in/thumbstick_right",   g_acts.stickR);
    getAct("/actions/ovrdancers/in/trigger_click_l",    g_acts.trigClickL);
    getAct("/actions/ovrdancers/in/trigger_click_r",    g_acts.trigClickR);
    getAct("/actions/ovrdancers/in/grip_click_l",       g_acts.gripClickL);
    getAct("/actions/ovrdancers/in/grip_click_r",       g_acts.gripClickR);
    getAct("/actions/ovrdancers/in/thumbstick_click_l", g_acts.stickClickL);
    getAct("/actions/ovrdancers/in/thumbstick_click_r", g_acts.stickClickR);
    getAct("/actions/ovrdancers/in/a_click",            g_acts.aClick);
    getAct("/actions/ovrdancers/in/b_click",            g_acts.bClick);
    getAct("/actions/ovrdancers/in/x_click",            g_acts.xClick);
    getAct("/actions/ovrdancers/in/y_click",            g_acts.yClick);
    getAct("/actions/ovrdancers/in/toggle_hands",       g_acts.toggleHands);

    g_actionsReady = (g_actionSet != vr::k_ulInvalidActionSetHandle);
    LOG_INFO("Action manifest %s", g_actionsReady ? "ready" : "FAILED");
}

// ── Persistence helpers ───────────────────────────────────────────────────────
// Called after receiving state from driver — matches saved mapping serials to
// real device indices and sends SetMapping/EnableMapping for any that are ready.
static void TryRestoreSavedMappings() {
    for (const auto& pm : g_savedMappings) {
        // Skip if we intentionally switched away from this device via auto-switch
        if (g_suppressedCtrlSerials.count(pm.controllerSerial)) continue;
        // Skip if already active
        bool already = false;
        for (uint32_t mi = 0; mi < g_activeState.mappingCount; mi++) {
            const auto& am = g_activeState.mappings[mi];
            if (strcmp(am.controllerSerial, pm.controllerSerial) == 0 &&
                strcmp(am.trackerSerial,    pm.trackerSerial)    == 0)
                { already = true; break; }
        }
        if (already) continue;

        uint32_t ctrlIdx = 0xFFFFFFFF, trkrIdx = 0xFFFFFFFF;
        for (uint32_t di = 0; di < g_activeState.deviceCount; di++) {
            const auto& dev = g_activeState.devices[di];
            if (strcmp(dev.serial, pm.controllerSerial) == 0) ctrlIdx = dev.index;
            if (strcmp(dev.serial, pm.trackerSerial)    == 0) trkrIdx = dev.index;
        }
        if (ctrlIdx == 0xFFFFFFFF || trkrIdx == 0xFFFFFFFF) continue;

        DeviceMapping dm{};
        dm.controllerIndex = ctrlIdx;
        dm.trackerIndex    = trkrIdx;
        dm.enabled         = pm.enabled;
        dm.redirectMode    = pm.redirectMode;
        dm.side            = pm.side;
        dm.offset          = pm.offset;
        strncpy_s(dm.controllerSerial, sizeof(dm.controllerSerial), pm.controllerSerial, _TRUNCATE);
        strncpy_s(dm.trackerSerial,    sizeof(dm.trackerSerial),    pm.trackerSerial,    _TRUNCATE);
        g_ipc.SendSetMapping(dm);
        if (pm.enabled) g_ipc.SendEnableMapping(ctrlIdx);
        LOG_INFO("Auto-restored saved mapping: ctrl=%s trkr=%s", pm.controllerSerial, pm.trackerSerial);
    }
    g_ui.UpdateSavedMappings(g_savedMappings);
}

// ── IPC connection on background thread ───────────────────────────────────────
// WaitNamedPipeA blocks for up to 2 seconds — never call this on the render thread.
static void TryConnectAsync() {
    if (g_connectingNow.exchange(true)) return; // already in progress

    std::thread([]() {
        if (g_ipc.Connect()) {
            g_driverConnected = true;
            LOG_INFO("Driver connected — requesting state");
            g_ipc.onStateUpdate = [](const Msg_StateUpdate& s) {
                LOG_INFO("State update received: %u mappings, %u devices",
                    s.mappingCount, s.deviceCount);
                for (uint32_t i = 0; i < s.mappingCount; i++)
                    LOG_INFO("  mapping[%u]: ctrl=%u tracker=%u enabled=%d virtDevIdx=%u",
                        i, s.mappings[i].controllerIndex,
                        s.mappings[i].trackerIndex, (int)s.mappings[i].enabled,
                        s.mappings[i].virtDevIdx);
                std::lock_guard<std::mutex> lock(g_stateMutex);
                g_pendingState    = s;
                g_hasPendingState = true;
            };
            g_ipc.onDisconnected = []() {
                LOG_WARN("Driver disconnected");
                g_driverConnected = false;
            };
            g_ipc.StartReceiveThread();
            g_ipc.SendRequestState();
            // Re-apply visibility settings so driver state matches UI after reconnect
            g_ipc.SendHideSettings(g_ui.GetHideControllers(), g_ui.GetHideTrackers());
            // Re-register virtual controllers if they were enabled in a previous session
            if (g_ui.GetVirtualCtrlRegistered())
                g_ipc.SendCreateVirtualControllers();
        } else {
            LOG_INFO("Driver not reachable yet, will retry");
        }
        g_connectingNow = false;
    }).detach();
}

// ── SteamVR Dashboard Overlay ─────────────────────────────────────────────────
// Appears as a tab in the SteamVR dashboard (same as OVR Input Emulator /
// Space Calibrator). The GLFW window is hidden — it only hosts the GL context.
// ─────────────────────────────────────────────────────────────────────────────
static vr::VROverlayHandle_t g_dashHandle  = vr::k_ulOverlayHandleInvalid;
static vr::VROverlayHandle_t g_thumbHandle = vr::k_ulOverlayHandleInvalid;
static GLuint g_fboTex = 0;
static GLuint g_fbo    = 0;
static GLuint g_rbo    = 0;
static int    g_fboW   = 0;
static int    g_fboH   = 0;

static void InitVROverlay(GLFWwindow* window, int w, int h) {
    vr::EVRInitError err = vr::VRInitError_None;
    g_vrSystem = vr::VR_Init(&err, vr::VRApplication_Overlay);
    if (err != vr::VRInitError_None) {
        g_vrSystem = nullptr;
        return; // SteamVR not running — run as plain desktop window
    }

    // Dashboard overlay: two handles — main panel + thumbnail tab icon
    vr::VROverlay()->CreateDashboardOverlay(
        "ovrdancers.dashboard",     // unique key
        "OVR Dancers Tool",         // name shown in dashboard
        &g_dashHandle,
        &g_thumbHandle);

    vr::VROverlay()->SetOverlayWidthInMeters(g_dashHandle, 3.0f);
    vr::VROverlay()->SetOverlayInputMethod(g_dashHandle, vr::VROverlayInputMethod_Mouse);

    // Mouse scale uses the physical framebuffer size so VR event coordinates map 1:1
    // to the texture pixels we submit. The Y flip in ForwardVRInput uses the same height.
    vr::HmdVector2_t mouseScale{ (float)w, (float)h };
    vr::VROverlay()->SetOverlayMouseScale(g_dashHandle, &mouseScale);

    // Solid colour thumbnail (orange-red brand colour) — replace with icon PNG if desired
    uint8_t thumb[4] = { 245, 63, 39, 255 }; // #F53F27
    vr::VROverlay()->SetOverlayRaw(g_thumbHandle, thumb, 1, 1, 4);

    // Build FBO to render ImGui into, then submit as overlay texture
    g_fboW = w; g_fboH = h;

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

    glGenTextures(1, &g_fboTex);
    glBindTexture(GL_TEXTURE_2D, g_fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fboTex, 0);

    glGenRenderbuffers(1, &g_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, g_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_rbo);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Desktop window stays visible for testing alongside the dashboard overlay

    SetupActionManifest();
}

// Forward VR laser pointer events into ImGui's IO so buttons/sliders work in headset
static void ForwardVRInput(int /*renderW*/, int /*renderH*/) {
    if (g_dashHandle == vr::k_ulOverlayHandleInvalid) return;
    ImGuiIO& io = ImGui::GetIO();

    vr::VREvent_t e;
    while (vr::VROverlay()->PollNextOverlayEvent(g_dashHandle, &e, sizeof(e))) {
        switch (e.eventType) {
        case vr::VREvent_MouseMove:
            // SteamVR overlay mouse events use Y=0 at top, same as ImGui — no flip needed.
            io.AddMousePosEvent(e.data.mouse.x, e.data.mouse.y);
            break;
        case vr::VREvent_MouseButtonDown:
            if (e.data.mouse.button & vr::VRMouseButton_Left)   io.AddMouseButtonEvent(0, true);
            if (e.data.mouse.button & vr::VRMouseButton_Right)  io.AddMouseButtonEvent(1, true);
            if (e.data.mouse.button & vr::VRMouseButton_Middle) io.AddMouseButtonEvent(2, true);
            break;
        case vr::VREvent_MouseButtonUp:
            if (e.data.mouse.button & vr::VRMouseButton_Left)   io.AddMouseButtonEvent(0, false);
            if (e.data.mouse.button & vr::VRMouseButton_Right)  io.AddMouseButtonEvent(1, false);
            if (e.data.mouse.button & vr::VRMouseButton_Middle) io.AddMouseButtonEvent(2, false);
            break;
        case vr::VREvent_ScrollSmooth:
            io.AddMouseWheelEvent(e.data.scroll.xdelta, e.data.scroll.ydelta);
            break;
        default: break;
        }
    }
}

static void SubmitOverlayFrame() {
    if (g_dashHandle == vr::k_ulOverlayHandleInvalid) return;

    // Blit default framebuffer → FBO texture
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbo);
    glBlitFramebuffer(0, 0, g_fboW, g_fboH,
                      0, 0, g_fboW, g_fboH,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    vr::Texture_t vrTex;
    vrTex.handle      = (void*)(uintptr_t)g_fboTex;
    vrTex.eType       = vr::TextureType_OpenGL;
    vrTex.eColorSpace = vr::ColorSpace_Auto;
    vr::VROverlay()->SetOverlayTexture(g_dashHandle, &vrTex);
}

static void ShutdownVROverlay() {
    if (g_fbo)    { glDeleteFramebuffers(1,  &g_fbo);    g_fbo    = 0; }
    if (g_fboTex) { glDeleteTextures(1,      &g_fboTex); g_fboTex = 0; }
    if (g_rbo)    { glDeleteRenderbuffers(1, &g_rbo);    g_rbo    = 0; }
    if (g_vrSystem) { vr::VR_Shutdown(); g_vrSystem = nullptr; }
}

// Forward real controller button/axis state to the driver each frame.
//
// Three independent paths, tried in order of reliability:
//   PATH A: SteamVR Input action system (IVRInput) — overlay-safe, no focus needed.
//           Bindings route /user/hand/left|right → actions; may read the Meta runtime's
//           physical input even after the hand role is taken.
//   PATH B: Event state (PollNextEvent) for BOTH the physical controller index AND the
//           virtual controller index (which holds the hand role). Button events may
//           arrive for either device depending on the runtime.
//   PATH C: GetControllerState() as a last resort for analog axes.
//
// We always send at least once when a mapping is first enabled (initial zero state).
static void ForwardControllerInputs() {
    if (!g_vrSystem || !g_driverConnected) return;
    static int s_sendCount = 0;
    static bool s_initSentOnce = false;
    static uint32_t s_sentOnceFor[MAX_DEVICES];
    if (!s_initSentOnce) {
        s_initSentOnce = true;
        for (int k = 0; k < MAX_DEVICES; k++) s_sentOnceFor[k] = 0xFFFFFFFF;
    }

    // ── PATH A: update SteamVR Input action state ──────────────────────────
    if (g_actionsReady) {
        vr::VRActiveActionSet_t activeSet{};
        activeSet.ulActionSet = g_actionSet;
        vr::VRInput()->UpdateActionState(&activeSet, sizeof(activeSet), 1);

        // toggle_hands action — configurable via SteamVR binding UI
        if (g_acts.toggleHands != vr::k_ulInvalidActionHandle) {
            vr::InputDigitalActionData_t td{};
            if (vr::VRInput()->GetDigitalActionData(g_acts.toggleHands, &td, sizeof(td),
                    vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None
                && td.bActive && td.bChanged && td.bState) {
                bool anyEnabled = false;
                for (const auto& m : g_ui.GetMappings()) anyEnabled |= m.enabled;
                if (g_ui.callbacks.onToggleAll)
                    g_ui.callbacks.onToggleAll(!anyEnabled);
            }
        }

    }

    for (uint32_t mi = 0; mi < g_activeState.mappingCount; mi++) {
        const DeviceMapping& m = g_activeState.mappings[mi];
        if (m.controllerIndex == 0xFFFFFFFF || !m.enabled) continue;
        uint32_t physIdx = m.controllerIndex;
        if (physIdx >= vr::k_unMaxTrackedDeviceCount) continue;

        bool isLeft = (m.side == 1);   // side==1 → left, side==2 → right

        // ── Find the virtual controller that currently holds this hand role ──
        vr::ETrackedControllerRole wantRole = isLeft
            ? vr::TrackedControllerRole_LeftHand
            : vr::TrackedControllerRole_RightHand;
        uint32_t virtIdx = g_vrSystem->GetTrackedDeviceIndexForControllerRole(wantRole);
        // virtIdx is our virtual controller (device 1 or 2) once it wins the hand role

        // ── PATH A: read from action system ──────────────────────────────────
        bool pathAUsed = false;
        EventCtrlState actionState{};
        if (g_actionsReady) {
            auto readF1 = [](vr::VRActionHandle_t h, float& out) {
                if (h == vr::k_ulInvalidActionHandle) return;
                vr::InputAnalogActionData_t d{};
                if (vr::VRInput()->GetAnalogActionData(h, &d, sizeof(d),
                        vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None && d.bActive)
                    out = d.x;
            };
            auto readF2 = [](vr::VRActionHandle_t h, float& ox, float& oy) {
                if (h == vr::k_ulInvalidActionHandle) return;
                vr::InputAnalogActionData_t d{};
                if (vr::VRInput()->GetAnalogActionData(h, &d, sizeof(d),
                        vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None && d.bActive)
                    { ox = d.x; oy = d.y; }
            };
            auto readBool = [](vr::VRActionHandle_t h) -> bool {
                if (h == vr::k_ulInvalidActionHandle) return false;
                vr::InputDigitalActionData_t d{};
                return vr::VRInput()->GetDigitalActionData(h, &d, sizeof(d),
                        vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None
                    && d.bActive && d.bState;
            };

            vr::VRActionHandle_t hTrig  = isLeft ? g_acts.triggerL     : g_acts.triggerR;
            vr::VRActionHandle_t hGrip  = isLeft ? g_acts.gripL        : g_acts.gripR;
            vr::VRActionHandle_t hStick = isLeft ? g_acts.stickL       : g_acts.stickR;
            vr::VRActionHandle_t hTC    = isLeft ? g_acts.trigClickL   : g_acts.trigClickR;
            vr::VRActionHandle_t hGC    = isLeft ? g_acts.gripClickL   : g_acts.gripClickR;
            vr::VRActionHandle_t hSC    = isLeft ? g_acts.stickClickL  : g_acts.stickClickR;

            float trigVal = 0, gripVal = 0, sx = 0, sy = 0;
            readF1(hTrig,  trigVal);
            readF1(hGrip,  gripVal);
            readF2(hStick, sx, sy);

            bool trigClick  = readBool(hTC);
            bool gripClick  = readBool(hGC);
            bool stickClick = readBool(hSC);
            bool aBtn   = readBool(g_acts.aClick);
            bool bBtn   = readBool(g_acts.bClick);
            bool xBtn   = readBool(g_acts.xClick);
            bool yBtn   = readBool(g_acts.yClick);

            // Log first change in trigger to see which device provides data
            {
                static float s_lastTrigL = -1.f, s_lastTrigR = -1.f;
                float& lastTrig = isLeft ? s_lastTrigL : s_lastTrigR;
                if (trigVal != lastTrig) {
                    lastTrig = trigVal;
                    // Find origin device
                    vr::InputAnalogActionData_t d{};
                    char originDesc[64] = "unknown";
                    if (vr::VRInput()->GetAnalogActionData(hTrig, &d, sizeof(d),
                            vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None) {
                        vr::InputOriginInfo_t info{};
                        if (vr::VRInput()->GetOriginTrackedDeviceInfo(d.activeOrigin,
                                &info, sizeof(info)) == vr::VRInputError_None) {
                            snprintf(originDesc, sizeof(originDesc), "dev=%u", info.trackedDeviceIndex);
                        }
                    }
                    LOG_INFO("ActionInput %s: trig=%.3f click=%d origin=%s",
                             isLeft ? "L" : "R", trigVal, (int)trigClick, originDesc);
                }
            }

            actionState.axisX[1] = trigVal;
            actionState.axisX[2] = gripVal;
            actionState.axisX[0] = sx; actionState.axisY[0] = sy;
            if (trigClick)   actionState.pressed |= (1ULL << 33);
            if (gripClick)   actionState.pressed |= (1ULL << 2);
            if (stickClick)  actionState.pressed |= (1ULL << 32);
            if (isLeft) {
                if (xBtn) actionState.pressed |= (1ULL << 7);
                if (yBtn) actionState.pressed |= (1ULL << 1);
            } else {
                if (aBtn) actionState.pressed |= (1ULL << 7);
                if (bBtn) actionState.pressed |= (1ULL << 1);
            }
            // Consider path A "used" if trigger is non-trivially nonzero OR any button set
            pathAUsed = (trigVal > 0.01f || gripVal > 0.01f ||
                         sx != 0.f || sy != 0.f || actionState.pressed != 0);
        }

        // ── PATH B: event-based state from PollControllerEvents ──────────────
        // Check BOTH the physical index and the virtual (hand-role) index.
        EventCtrlState evtState{};
        {
            EventCtrlState& physEvt  = g_evtState[physIdx];
            bool haveVirt = (virtIdx != vr::k_unTrackedDeviceIndexInvalid &&
                             virtIdx != physIdx &&
                             virtIdx < vr::k_unMaxTrackedDeviceCount);
            EventCtrlState& virtEvt  = haveVirt ? g_evtState[virtIdx] : g_evtState[physIdx];

            // Merge: newer version wins; OR the button bits so both sources contribute
            if (!haveVirt || physEvt.version >= virtEvt.version) {
                evtState = physEvt;
                if (haveVirt) {
                    evtState.pressed |= virtEvt.pressed;
                    evtState.touched |= virtEvt.touched;
                }
            } else {
                evtState = virtEvt;
                evtState.pressed |= physEvt.pressed;
                evtState.touched |= physEvt.touched;
            }

            // Path C: GetControllerState for better analog values on either index
            for (uint32_t tidx : { physIdx, virtIdx }) {
                if (tidx == vr::k_unTrackedDeviceIndexInvalid ||
                    tidx >= vr::k_unMaxTrackedDeviceCount) continue;
                vr::VRControllerState_t cs{};
                if (g_vrSystem->GetControllerState(tidx, &cs, sizeof(cs))) {
                    evtState.axisX[0] = cs.rAxis[0].x; evtState.axisY[0] = cs.rAxis[0].y;
                    evtState.axisX[1] = cs.rAxis[1].x;
                    evtState.axisX[2] = cs.rAxis[2].x;
                    evtState.pressed |= cs.ulButtonPressed;
                    evtState.touched |= cs.ulButtonTouched;
                    break;
                }
            }
        }

        // ── Merge paths: if Path A has real data use it; otherwise use Path B ─
        EventCtrlState final{};
        if (pathAUsed) {
            final = actionState;
            // Fill thumbstick from event state if action has none
            if (final.axisX[0] == 0.f && final.axisY[0] == 0.f) {
                final.axisX[0] = evtState.axisX[0];
                final.axisY[0] = evtState.axisY[0];
            }
            final.pressed |= evtState.pressed;
            final.touched |= evtState.touched;
        } else {
            final = evtState;
        }

        // ── Compute a "state version" across all sources ──────────────────────
        // Use a simple hash of the meaningful fields to detect changes.
        uint32_t stateHash = (uint32_t)(final.pressed ^ final.touched
            ^ (uint32_t)(final.axisX[0] * 100) ^ (uint32_t)(final.axisX[1] * 100)
            ^ (uint32_t)(final.axisX[2] * 100));

        // Always send at least once per mapping (to initialise virtual controller's components)
        bool sentOnce = false;
        for (uint32_t k = 0; k < MAX_DEVICES; k++) {
            if (s_sentOnceFor[k] == physIdx) { sentOnce = true; break; }
        }
        bool stateChanged = (stateHash != g_lastSentVersion[physIdx]);
        if (!sentOnce || stateChanged) {
            g_lastSentVersion[physIdx] = stateHash;
            if (!sentOnce) {
                for (uint32_t k = 0; k < MAX_DEVICES; k++) {
                    if (s_sentOnceFor[k] == 0xFFFFFFFF || s_sentOnceFor[k] == 0) {
                        s_sentOnceFor[k] = physIdx; break;
                    }
                }
            }

            ++s_sendCount;
            if (s_sendCount <= 10 || s_sendCount % 200 == 0)
                LOG_INFO("SendInput #%d ctrl=%u side=%u pathA=%d pressed=%llu trig=%.2f grip=%.2f stk=%.2f,%.2f",
                         s_sendCount, physIdx, m.side, (int)pathAUsed,
                         (unsigned long long)final.pressed,
                         final.axisX[1], final.axisX[2],
                         final.axisX[0], final.axisY[0]);

            Msg_InputUpdate msg{};
            msg.controllerIndex = physIdx;
            msg.ulButtonPressed = final.pressed;
            msg.ulButtonTouched = final.touched;
            for (int a = 0; a < 5; a++) { msg.axisX[a] = final.axisX[a]; msg.axisY[a] = final.axisY[a]; }
            g_ipc.SendInputUpdate(msg);
        }
    }
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(780, 580, "OVR Dancers Tool", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Set window icon from the logo PNG
    {
        int iw, ih, ic;
        unsigned char* pixels = stbi_load("resources/icons/logo.png", &iw, &ih, &ic, 4);
        if (pixels) {
            GLFWimage img{ iw, ih, pixels };
            glfwSetWindowIcon(window, 1, &img);
            stbi_image_free(pixels);
        }
    }

    // Capture framebuffer size BEFORE iconify — glfwGetFramebufferSize returns (0,0)
    // once the window is minimized, which would size the FBO to zero.
    int w = 780, h = 580;
    glfwGetFramebufferSize(window, &w, &h);

    // Start minimized — the tool lives in the SteamVR dashboard overlay.
    // Iconify here so it appears in the taskbar but doesn't steal focus on launch.
    glfwIconifyWindow(window);
    LoadGL3Extensions(); // must be after context is current

    BLog::Init();
    LOG_INFO("OVRDancers overlay starting");
    OSCInit();

    g_ui.Init(window);

    // Wire UI callbacks to IPC — these run on the render thread, safe to call IPC send
    g_ui.callbacks.onSetMapping = [](const DeviceMapping& mapping) {
        DeviceMapping m = mapping;
        // Auto-detect hand side if not already set
        if (m.side == 0) {
            if      (strstr(m.controllerSerial, "VirtCtrl_L")) m.side = 1;
            else if (strstr(m.controllerSerial, "VirtCtrl_R")) m.side = 2;
            else if (g_vrSystem) {
                auto role = g_vrSystem->GetControllerRoleForTrackedDeviceIndex(m.controllerIndex);
                if      (role == vr::TrackedControllerRole_LeftHand)  m.side = 1;
                else if (role == vr::TrackedControllerRole_RightHand) m.side = 2;
            }
        }
        g_ipc.SendSetMapping(m);
    };
    g_ui.callbacks.onClearMapping = [](uint32_t idx) {
        // Find the serial before removing (needed to purge from saved mappings)
        char removedSerial[32] = {};
        for (uint32_t i = 0; i < g_activeState.mappingCount; i++) {
            if (g_activeState.mappings[i].controllerIndex == idx) {
                strncpy_s(removedSerial, sizeof(removedSerial),
                          g_activeState.mappings[i].controllerSerial, _TRUNCATE);
                break;
            }
        }

        g_ipc.SendClearMapping(idx);

        // Remove from local active state immediately — don't wait for driver echo
        uint32_t n = 0;
        for (uint32_t i = 0; i < g_activeState.mappingCount; i++)
            if (g_activeState.mappings[i].controllerIndex != idx)
                g_activeState.mappings[n++] = g_activeState.mappings[i];
        g_activeState.mappingCount = n;
        g_ui.UpdateMappings(g_activeState.mappings, g_activeState.mappingCount);

        // Also remove from saved mappings so TryRestoreSavedMappings doesn't re-add it
        if (removedSerial[0]) {
            for (auto it = g_savedMappings.begin(); it != g_savedMappings.end(); ) {
                if (strcmp(it->controllerSerial, removedSerial) == 0)
                    it = g_savedMappings.erase(it);
                else
                    ++it;
            }
            g_ui.UpdateSavedMappings(g_savedMappings);
        }
    };
    g_ui.callbacks.onSetOffset = [](uint32_t idx, const Offset6DOF& off) {
        g_ipc.SendSetOffset(idx, off);
    };
    g_ui.callbacks.onToggleMapping = [](uint32_t idx, bool enable) {
        if (enable) g_ipc.SendEnableMapping(idx);
        else        g_ipc.SendDisableMapping(idx);
        const OSCSettings& osc = g_ui.GetOSCSettings();
        if (osc.enabled) OSCSendBool(osc, osc.paramName, enable);
        const WSSettings& ws = g_ui.GetWSSettings();
        if (ws.enabled) WSBroadcast(ws.paramName, enable);
    };
    g_ui.callbacks.onToggleAll = [](bool enable) {
        for (auto& m : g_ui.GetMappings()) {
            if (enable) g_ipc.SendEnableMapping(m.controllerIndex);
            else        g_ipc.SendDisableMapping(m.controllerIndex);
        }
        const OSCSettings& osc = g_ui.GetOSCSettings();
        if (osc.enabled) OSCSendBool(osc, osc.paramName, enable);
        const WSSettings& ws = g_ui.GetWSSettings();
        if (ws.enabled) WSBroadcast(ws.paramName, enable);
    };
    g_ui.callbacks.onToggleHand = [](int side, bool enable) {
        for (auto& m : g_ui.GetMappings()) {
            if ((int)m.side != side) continue;
            if (enable) g_ipc.SendEnableMapping(m.controllerIndex);
            else        g_ipc.SendDisableMapping(m.controllerIndex);
        }
        const OSCSettings& osc = g_ui.GetOSCSettings();
        if (osc.enabled) OSCSendBool(osc, osc.paramName, enable);
        const WSSettings& ws = g_ui.GetWSSettings();
        if (ws.enabled) WSBroadcast(ws.paramName, enable);
    };
    g_ui.callbacks.onHideSettings = [](bool hideControllers, bool hideTrackers) {
        g_ipc.SendHideSettings(hideControllers, hideTrackers);
    };
    g_ui.callbacks.onOpenBindings = []() {
        if (!g_actionsReady || g_actionSet == vr::k_ulInvalidActionSetHandle) return;
        vr::EVRInputError err = vr::VRInput()->OpenBindingUI(
            "ovrdancers.dashboard", g_actionSet, vr::k_ulInvalidInputValueHandle, false);
        LOG_INFO("OpenBindingUI result: %d", (int)err);
    };
    // Deferred calibration — processed on the render thread to avoid IPC pipe races.
    struct CalibPending {
        bool     active     = false;
        uint32_t ctrlIdx    = 0;
        uint32_t trackerIdx = 0;
        bool     wasEnabled = false;
        double   disableAt  = 0.0;
    };
    static CalibPending s_calib;

    // Smooth Cal — real-time tracker-movement-to-offset adjustment.
    // While active, physically moving the tracker shifts the virtual controller
    // position by the same world-space delta, converted to tracker-local space.
    // No disable/re-enable needed — the mapping stays active and offsets update live.
    struct CalibSmooth {
        bool       active      = false;
        uint32_t   ctrlIdx     = 0;
        uint32_t   trackerIdx  = 0;
        bool       initialized = false;
        float      startTX=0, startTY=0, startTZ=0;         // tracker world pos at activation
        float      startRW=1, startRX=0, startRY=0, startRZ=0; // tracker rotation at activation
        Offset6DOF originalOffset;
    };
    static CalibSmooth s_smooth;

    g_ui.callbacks.onCalibratePosition = [](uint32_t ctrlIdx, uint32_t trackerIdx, bool wasEnabled) {
        if (s_calib.active || s_smooth.active) {
            LOG_WARN("Calibrate: previous calibration still pending — ignoring");
            return;
        }
        LOG_INFO("Calibrate: queued ctrl=%u tracker=%u wasEnabled=%d", ctrlIdx, trackerIdx, (int)wasEnabled);
        if (wasEnabled) g_ipc.SendDisableMapping(ctrlIdx);
        s_calib = { true, ctrlIdx, trackerIdx, wasEnabled, glfwGetTime() };
    };

    g_ui.callbacks.onCalibrateSmooth = [](uint32_t ctrlIdx, uint32_t trackerIdx, bool /*wasEnabled*/) {
        if (s_calib.active) {
            LOG_WARN("CalibrateSmooth: quick calibrate still pending — ignoring");
            return;
        }
        // Toggle off
        if (s_smooth.active && s_smooth.ctrlIdx == ctrlIdx) {
            LOG_INFO("CalibrateSmooth: stopped — offset already applied live");
            s_smooth.active = false;
            g_ui.smoothCalibCtrlIdx = 0xFFFFFFFF;
            return;
        }
        // Start: capture current offset from active mapping; start pose captured on first frame
        s_smooth = {};
        s_smooth.active     = true;
        s_smooth.ctrlIdx    = ctrlIdx;
        s_smooth.trackerIdx = trackerIdx;
        {
            std::lock_guard<std::mutex> lg(g_stateMutex);
            for (uint32_t mi = 0; mi < g_activeState.mappingCount; mi++) {
                if (g_activeState.mappings[mi].controllerIndex == ctrlIdx) {
                    s_smooth.originalOffset = g_activeState.mappings[mi].offset;
                    break;
                }
            }
        }
        g_ui.smoothCalibCtrlIdx = ctrlIdx;
        LOG_INFO("CalibrateSmooth: started ctrl=%u tracker=%u", ctrlIdx, trackerIdx);
    };

    // ── Persistence setup ─────────────────────────────────────────────────────
    {
        // Save in the user data directory — not next to the exe, so it survives reinstalls.
#ifdef _WIN32
        g_savePath = GetDataDir() + "\\ovrdancers_mappings.json";
#else
        g_savePath = GetDataDir() + "/ovrdancers_mappings.json";
#endif
        AppSettings appSettings;
        if (LoadMappings(g_savePath, g_savedMappings, &appSettings))
            LOG_INFO("Loaded %u saved mapping(s) from %s", (unsigned)g_savedMappings.size(), g_savePath.c_str());
        g_ui.UpdateSavedMappings(g_savedMappings);
        // Restore app settings
        {
            OSCSettings osc;
            osc.enabled = appSettings.oscEnabled;
            strncpy_s(osc.ip,        sizeof(osc.ip),        appSettings.oscIP,    _TRUNCATE);
            osc.port    = appSettings.oscPort;
            strncpy_s(osc.paramName, sizeof(osc.paramName), appSettings.oscParam, _TRUNCATE);
            g_ui.SetOSCSettings(osc);
        }
        if (appSettings.virtualControllers)
            g_ui.SetVirtualCtrlRegistered(true);
        g_ui.SetHtLocked(appSettings.htLocked);
        g_ui.SetLanguage((Language)appSettings.language);
        {
            WSSettings ws;
            ws.enabled = appSettings.wsEnabled;
            ws.port    = appSettings.wsPort;
            strncpy_s(ws.paramName, sizeof(ws.paramName), appSettings.wsParam, _TRUNCATE);
            g_ui.SetWSSettings(ws);
            if (ws.enabled) WSInit(ws);
        }
        // onSave callback is wired below, but WSUpdateSettings is called from UI when changed
    }

    g_ui.callbacks.onSave = []() {
        // Merge active mappings + any pending saved mappings that aren't active
        std::vector<SavedMapping> toSave;
        for (uint32_t mi = 0; mi < g_activeState.mappingCount; mi++) {
            const auto& am = g_activeState.mappings[mi];
            SavedMapping sm{};
            strncpy_s(sm.controllerSerial, sizeof(sm.controllerSerial), am.controllerSerial, _TRUNCATE);
            strncpy_s(sm.trackerSerial,    sizeof(sm.trackerSerial),    am.trackerSerial,    _TRUNCATE);
            sm.enabled      = am.enabled;
            sm.redirectMode = am.redirectMode;
            sm.side         = am.side;
            sm.offset       = am.offset;
            toSave.push_back(sm);
        }
        for (const auto& pm : g_savedMappings) {
            bool found = false;
            for (const auto& s : toSave)
                if (strcmp(s.controllerSerial, pm.controllerSerial) == 0 &&
                    strcmp(s.trackerSerial,    pm.trackerSerial)    == 0)
                    { found = true; break; }
            if (!found) toSave.push_back(pm);
        }
        g_savedMappings = toSave;
        AppSettings settings{};
        settings.virtualControllers = g_ui.GetVirtualCtrlRegistered();
        const auto& osc = g_ui.GetOSCSettings();
        settings.oscEnabled = osc.enabled;
        strncpy_s(settings.oscIP,    sizeof(settings.oscIP),    osc.ip,        _TRUNCATE);
        settings.oscPort    = osc.port;
        strncpy_s(settings.oscParam, sizeof(settings.oscParam), osc.paramName, _TRUNCATE);
        const auto& ws = g_ui.GetWSSettings();
        settings.wsEnabled  = ws.enabled;
        settings.wsPort     = ws.port;
        strncpy_s(settings.wsParam, sizeof(settings.wsParam), ws.paramName, _TRUNCATE);
        settings.htLocked   = g_ui.GetHtLocked();
        settings.language   = (uint8_t)g_ui.GetLanguage();
        // Apply WebSocket settings change (restarts server if needed)
        WSUpdateSettings(ws);
        if (SaveMappings(g_savePath, toSave, settings))
            LOG_INFO("Saved %u mapping(s) to %s", (unsigned)toSave.size(), g_savePath.c_str());
        else
            LOG_WARN("Failed to save mappings to %s", g_savePath.c_str());
        g_ui.UpdateSavedMappings(g_savedMappings);
    };

    g_ui.callbacks.onCreateVirtualControllers = []() {
        g_ipc.SendCreateVirtualControllers();
        LOG_INFO("Sent CreateVirtualControllers to driver");
    };

    g_ui.callbacks.onSetVirtCtrlActive = [](uint8_t side, bool active) {
        g_ipc.SendSetVirtCtrlActive(side, active);
        LOG_INFO("Sent SetVirtCtrlActive side=%u active=%d", (unsigned)side, (int)active);
    };

    g_ui.callbacks.onGetHandRoleDevice = [](uint32_t side) -> uint32_t {
        if (!g_vrSystem) return vr::k_unTrackedDeviceIndexInvalid;
        auto role = (side == 1)
            ? vr::TrackedControllerRole_LeftHand
            : vr::TrackedControllerRole_RightHand;
        return g_vrSystem->GetTrackedDeviceIndexForControllerRole(role);
    };

    g_ui.callbacks.onDeleteAllSettings = []() {
        // Tell the driver to clear each active mapping before wiping local state
        if (g_driverConnected) {
            for (const auto& m : g_ui.GetMappings())
                g_ipc.SendClearMapping(m.controllerIndex);
        }
        // Clear in-memory state
        g_savedMappings.clear();
        g_activeState = {};
        g_ui.UpdateMappings(nullptr, 0);
        g_ui.UpdateSavedMappings(g_savedMappings);
        // Overwrite save file with empty list
        SaveMappings(g_savePath, g_savedMappings);
        LOG_INFO("All settings deleted");
    };

    g_ui.callbacks.onReload = []() {
        g_savedMappings.clear();
        if (LoadMappings(g_savePath, g_savedMappings)) {
            LOG_INFO("Reloaded %u mapping(s) from %s", (unsigned)g_savedMappings.size(), g_savePath.c_str());
            TryRestoreSavedMappings();
        } else {
            LOG_INFO("No save file at %s", g_savePath.c_str());
            g_ui.UpdateSavedMappings(g_savedMappings);
        }
    };

    g_ui.callbacks.onRemoveSavedMapping = [](const char* controllerSerial) {
        for (auto it = g_savedMappings.begin(); it != g_savedMappings.end(); ) {
            if (strcmp(it->controllerSerial, controllerSerial) == 0)
                it = g_savedMappings.erase(it);
            else
                ++it;
        }
        SaveMappings(g_savePath, g_savedMappings);
        g_ui.UpdateSavedMappings(g_savedMappings);
        LOG_INFO("Removed saved mapping for controller %s", controllerSerial);
    };

    g_ui.callbacks.onRetryRestore = []() {
        TryRestoreSavedMappings();
    };

    InitVROverlay(window, w, h); // uses pre-iconify framebuffer size

    double lastConnectAttempt = -999.0;

    while (!glfwWindowShouldClose(window) && !g_ui.wantsQuit) {
        glfwPollEvents();

        // Rebuild font atlas when language changes (must happen outside Begin/EndFrame)
        if (g_ui.NeedsFontRebuild())
            g_ui.RebuildFonts();

        // Kick off async connection attempt every 3 seconds (non-blocking)
        double now = glfwGetTime();
        if (!g_driverConnected && !g_connectingNow && now - lastConnectAttempt > 3.0) {
            lastConnectAttempt = now;
            TryConnectAsync();
        }

        // Apply pending state from IPC receive thread
        if (g_hasPendingState.exchange(false)) {
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                g_activeState = g_pendingState;
            }
            g_ui.UpdateDevices(g_activeState.devices, g_activeState.deviceCount);
            g_ui.UpdateMappings(g_activeState.mappings, g_activeState.mappingCount);
            TryRestoreSavedMappings();

            // ── Detect controller connect/disconnect and auto-remap ──────────────
            // Tracks ALL connected controllers+hand-trackers that have a hand role.
            // Quest keeps old controllers connected when HT activates, so we can't
            // rely on class=5 or role-change polling — we just watch for any new device
            // with a recognisable side role and switch to it. On disconnect we fall back
            // to whoever holds the role now.
            if (g_vrSystem && g_driverConnected && !g_ui.GetHtLocked()) {
                static std::unordered_map<uint32_t, uint32_t> s_lastRoleDev; // index → side
                std::unordered_map<uint32_t, uint32_t> curRoleDev;

                for (uint32_t di = 0; di < g_activeState.deviceCount; di++) {
                    const auto& dev = g_activeState.devices[di];
                    if (!dev.connected) continue;
                    if (dev.deviceClass != 2 && dev.deviceClass != 5) continue; // controllers + hand trackers
                    vr::ETrackedPropertyError perr;
                    int roleHint = (int)g_vrSystem->GetInt32TrackedDeviceProperty(
                        dev.index, vr::Prop_ControllerRoleHint_Int32, &perr);
                    uint32_t side = (roleHint == (int)vr::TrackedControllerRole_LeftHand)  ? 1u :
                                    (roleHint == (int)vr::TrackedControllerRole_RightHand) ? 2u : 0u;
                    if (side == 0) continue;
                    curRoleDev[dev.index] = side;
                }

                // Helper: remap side-tagged mappings (or any mapping on fromIdx) to toIdx
                auto remapTo = [&](uint32_t toIdx, uint32_t side, uint32_t fromIdx) {
                    char serial[32]{};
                    g_vrSystem->GetStringTrackedDeviceProperty(toIdx,
                        vr::Prop_SerialNumber_String, serial, sizeof(serial));
                    for (uint32_t mi = 0; mi < g_activeState.mappingCount; mi++) {
                        const DeviceMapping& m = g_activeState.mappings[mi];
                        if (strstr(m.controllerSerial, "VirtCtrl_")) continue;
                        if (m.controllerIndex == toIdx) continue;
                        bool taggedSide = (m.side == side);
                        bool onOldDev   = (fromIdx != vr::k_unTrackedDeviceIndexInvalid
                                          && m.controllerIndex == fromIdx);
                        if (!taggedSide && !onOldDev) continue;

                        LOG_INFO("AutoSwitch: side=%u remap ctrl=%u -> dev=%u (%s)", side, m.controllerIndex, toIdx, serial);
                        g_suppressedCtrlSerials.insert(m.controllerSerial); // prevent TryRestore re-adding old device
                        g_ipc.SendClearMapping(m.controllerIndex);
                        DeviceMapping newM = m;
                        newM.controllerIndex = toIdx;
                        newM.side = side;
                        memset(newM.controllerSerial, 0, sizeof(newM.controllerSerial));
                        strncpy_s(newM.controllerSerial, serial, sizeof(newM.controllerSerial)-1);
                        g_ipc.SendSetMapping(newM);
                        if (newM.enabled) {
                            g_ipc.SendEnableMapping(toIdx);
                            if (!s_calib.active && !s_smooth.active && g_ui.callbacks.onCalibratePosition)
                                g_ui.callbacks.onCalibratePosition(toIdx, m.trackerIndex, true);
                        }
                    }
                };

                // New device with a hand role appeared — switch to it
                for (auto& [idx, side] : curRoleDev) {
                    if (s_lastRoleDev.count(idx)) continue;
                    LOG_INFO("AutoSwitch: new role dev=%u side=%u", idx, side);
                    // find the previous device for this side (if any) so we can remap "onOldDev" mappings
                    uint32_t prevIdx = vr::k_unTrackedDeviceIndexInvalid;
                    for (auto& [pidx, pside] : s_lastRoleDev)
                        if (pside == side) { prevIdx = pidx; break; }
                    remapTo(idx, side, prevIdx);
                }

                // Device disappeared — fall back to current role holder
                for (auto& [idx, side] : s_lastRoleDev) {
                    if (curRoleDev.count(idx)) continue;
                    LOG_INFO("AutoSwitch: role dev=%u side=%u disconnected", idx, side);
                    // Unsuppress the serial for this device so TryRestore can re-add it if needed
                    for (uint32_t di = 0; di < g_activeState.deviceCount; di++) {
                        if (g_activeState.devices[di].index == idx)
                            g_suppressedCtrlSerials.erase(g_activeState.devices[di].serial);
                    }
                    auto role = (side == 1) ? vr::TrackedControllerRole_LeftHand
                                            : vr::TrackedControllerRole_RightHand;
                    uint32_t newIdx = g_vrSystem->GetTrackedDeviceIndexForControllerRole(role);
                    if (newIdx != vr::k_unTrackedDeviceIndexInvalid)
                        remapTo(newIdx, side, idx);
                }

                s_lastRoleDev = curRoleDev;
            }
        }

        // ── Deferred calibration (render-thread only, avoids IPC pipe races) ────
        if (s_calib.active && g_vrSystem && g_driverConnected) {
            constexpr double kWaitSec = 0.50; // 500 ms — extra time for redirect mode controller to resume reporting
            if (glfwGetTime() - s_calib.disableAt >= kWaitSec) {
                vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
                g_vrSystem->GetDeviceToAbsoluteTrackingPose(
                    vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);

                uint32_t ci = s_calib.ctrlIdx, ti = s_calib.trackerIdx;
                bool ok = ci < vr::k_unMaxTrackedDeviceCount &&
                          ti < vr::k_unMaxTrackedDeviceCount &&
                          poses[ci].bPoseIsValid && poses[ti].bPoseIsValid;

                LOG_INFO("Calibrate: sample ctrl=%u tracker=%u ok=%d ctrlValid=%d trackerValid=%d",
                    ci, ti, (int)ok, (int)poses[ci].bPoseIsValid, (int)poses[ti].bPoseIsValid);

                if (ok) {
                    const auto& cm = poses[ci].mDeviceToAbsoluteTracking.m;
                    const auto& tm = poses[ti].mDeviceToAbsoluteTracking.m;
                    float cx=cm[0][3], cy=cm[1][3], cz=cm[2][3];
                    float tx=tm[0][3], ty=tm[1][3], tz=tm[2][3];
                    LOG_INFO("Calibrate: ctrlPos=(%.3f,%.3f,%.3f) trackerPos=(%.3f,%.3f,%.3f)",
                        cx,cy,cz,tx,ty,tz);

                    float dx=cx-tx, dy=cy-ty, dz=cz-tz;
                    Offset6DOF result{};
                    result.pos.x = tm[0][0]*dx + tm[1][0]*dy + tm[2][0]*dz;
                    result.pos.y = tm[0][1]*dx + tm[1][1]*dy + tm[2][1]*dz;
                    result.pos.z = tm[0][2]*dx + tm[1][2]*dy + tm[2][2]*dz;

                    float r00=tm[0][0]*cm[0][0]+tm[1][0]*cm[1][0]+tm[2][0]*cm[2][0];
                    float r01=tm[0][0]*cm[0][1]+tm[1][0]*cm[1][1]+tm[2][0]*cm[2][1];
                    float r02=tm[0][0]*cm[0][2]+tm[1][0]*cm[1][2]+tm[2][0]*cm[2][2];
                    float r10=tm[0][1]*cm[0][0]+tm[1][1]*cm[1][0]+tm[2][1]*cm[2][0];
                    float r11=tm[0][1]*cm[0][1]+tm[1][1]*cm[1][1]+tm[2][1]*cm[2][1];
                    float r12=tm[0][1]*cm[0][2]+tm[1][1]*cm[1][2]+tm[2][1]*cm[2][2];
                    float r20=tm[0][2]*cm[0][0]+tm[1][2]*cm[1][0]+tm[2][2]*cm[2][0];
                    float r21=tm[0][2]*cm[0][1]+tm[1][2]*cm[1][1]+tm[2][2]*cm[2][1];
                    float r22=tm[0][2]*cm[0][2]+tm[1][2]*cm[1][2]+tm[2][2]*cm[2][2];
                    float trace=r00+r11+r22, qw,qx,qy,qz;
                    if (trace>0.f) {
                        float s=0.5f/sqrtf(trace+1.f);
                        qw=0.25f/s; qx=(r21-r12)*s; qy=(r02-r20)*s; qz=(r10-r01)*s;
                    } else if (r00>r11&&r00>r22) {
                        float s=2.f*sqrtf(1.f+r00-r11-r22);
                        qw=(r21-r12)/s; qx=0.25f*s; qy=(r01+r10)/s; qz=(r02+r20)/s;
                    } else if (r11>r22) {
                        float s=2.f*sqrtf(1.f+r11-r00-r22);
                        qw=(r02-r20)/s; qx=(r01+r10)/s; qy=0.25f*s; qz=(r12+r21)/s;
                    } else {
                        float s=2.f*sqrtf(1.f+r22-r00-r11);
                        qw=(r10-r01)/s; qx=(r02+r20)/s; qy=(r12+r21)/s; qz=0.25f*s;
                    }
                    result.rot = {qw,qx,qy,qz};
                    {
                        float tr=tm[0][0]+tm[1][1]+tm[2][2],tw,tax,tay,taz;
                        if (tr>0.f) { float s=0.5f/sqrtf(tr+1.f); tw=0.25f/s; tax=(tm[2][1]-tm[1][2])*s; tay=(tm[0][2]-tm[2][0])*s; taz=(tm[1][0]-tm[0][1])*s; }
                        else if (tm[0][0]>tm[1][1]&&tm[0][0]>tm[2][2]) { float s=2.f*sqrtf(1.f+tm[0][0]-tm[1][1]-tm[2][2]); tw=(tm[2][1]-tm[1][2])/s; tax=0.25f*s; tay=(tm[0][1]+tm[1][0])/s; taz=(tm[0][2]+tm[2][0])/s; }
                        else if (tm[1][1]>tm[2][2]) { float s=2.f*sqrtf(1.f+tm[1][1]-tm[0][0]-tm[2][2]); tw=(tm[0][2]-tm[2][0])/s; tax=(tm[0][1]+tm[1][0])/s; tay=0.25f*s; taz=(tm[1][2]+tm[2][1])/s; }
                        else { float s=2.f*sqrtf(1.f+tm[2][2]-tm[0][0]-tm[1][1]); tw=(tm[1][0]-tm[0][1])/s; tax=(tm[0][2]+tm[2][0])/s; tay=(tm[1][2]+tm[2][1])/s; taz=0.25f*s; }
                        result.calibTrackerRot={tw,tax,tay,taz};
                    }
                    // Preserve origin offset/rotation — quick calibrate only updates the
                    // tracker-local pos/rot, leaving the user's world correction untouched.
                    {
                        std::lock_guard<std::mutex> lg(g_stateMutex);
                        for (uint32_t mi = 0; mi < g_activeState.mappingCount; mi++) {
                            if (g_activeState.mappings[mi].controllerIndex == ci) {
                                result.originOffset = g_activeState.mappings[mi].offset.originOffset;
                                result.originRot    = g_activeState.mappings[mi].offset.originRot;
                                break;
                            }
                        }
                    }
                    LOG_INFO("Calibrate done: pos=(%.3f,%.3f,%.3f) rot=(%.3f,%.3f,%.3f,%.3f)",
                        result.pos.x,result.pos.y,result.pos.z,
                        result.rot.w,result.rot.x,result.rot.y,result.rot.z);
                    g_ipc.SendSetOffset(ci, result);
                } else {
                    LOG_WARN("Calibrate: pose invalid — offset unchanged");
                }

                if (s_calib.wasEnabled) {
                    LOG_INFO("Calibrate: re-enabling ctrl=%u", ci);
                    g_ipc.SendEnableMapping(ci);
                }
                s_calib.active = false;
            }
        }

        // ── Smooth Cal — real-time tracker-movement offset adjustment ──────────
        if (s_smooth.active && g_vrSystem && g_driverConnected) {
            vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
            g_vrSystem->GetDeviceToAbsoluteTrackingPose(
                vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);

            // Use the tracker's real pose. In redirect mode the pose hook now
            // passes the tracker pose through (not disconnecting it) so that
            // GetDeviceToAbsoluteTrackingPose returns valid data here.
            uint32_t ti = s_smooth.trackerIdx;
            if (ti < vr::k_unMaxTrackedDeviceCount && poses[ti].bPoseIsValid) {
                const auto& tm = poses[ti].mDeviceToAbsoluteTracking.m;
                float tx = tm[0][3], ty = tm[1][3], tz = tm[2][3];

                // Extract quaternion from rotation matrix (Shepperd's method)
                float trace = tm[0][0]+tm[1][1]+tm[2][2];
                float rw,rx,ry,rz;
                if (trace > 0) {
                    float s=0.5f/sqrtf(trace+1.0f);
                    rw=0.25f/s; rx=(tm[2][1]-tm[1][2])*s; ry=(tm[0][2]-tm[2][0])*s; rz=(tm[1][0]-tm[0][1])*s;
                } else if (tm[0][0]>tm[1][1]&&tm[0][0]>tm[2][2]) {
                    float s=2.0f*sqrtf(1.0f+tm[0][0]-tm[1][1]-tm[2][2]);
                    rw=(tm[2][1]-tm[1][2])/s; rx=0.25f*s; ry=(tm[0][1]+tm[1][0])/s; rz=(tm[0][2]+tm[2][0])/s;
                } else if (tm[1][1]>tm[2][2]) {
                    float s=2.0f*sqrtf(1.0f+tm[1][1]-tm[0][0]-tm[2][2]);
                    rw=(tm[0][2]-tm[2][0])/s; rx=(tm[0][1]+tm[1][0])/s; ry=0.25f*s; rz=(tm[1][2]+tm[2][1])/s;
                } else {
                    float s=2.0f*sqrtf(1.0f+tm[2][2]-tm[0][0]-tm[1][1]);
                    rw=(tm[1][0]-tm[0][1])/s; rx=(tm[0][2]+tm[2][0])/s; ry=(tm[1][2]+tm[2][1])/s; rz=0.25f*s;
                }

                if (!s_smooth.initialized) {
                    s_smooth.startTX=tx; s_smooth.startTY=ty; s_smooth.startTZ=tz;
                    s_smooth.startRW=rw; s_smooth.startRX=rx; s_smooth.startRY=ry; s_smooth.startRZ=rz;
                    s_smooth.initialized = true;
                } else {
                    // Position: world delta → tracker-local
                    float dx=tx-s_smooth.startTX, dy=ty-s_smooth.startTY, dz=tz-s_smooth.startTZ;
                    float lx=tm[0][0]*dx+tm[1][0]*dy+tm[2][0]*dz;
                    float ly=tm[0][1]*dx+tm[1][1]*dy+tm[2][1]*dz;
                    float lz=tm[0][2]*dx+tm[1][2]*dy+tm[2][2]*dz;

                    // Rotation delta: startRot^{-1} * currentRot  (tracker-local rotation change)
                    float sw=s_smooth.startRW, sx=-s_smooth.startRX, sy=-s_smooth.startRY, sz=-s_smooth.startRZ; // conjugate
                    float dRw=sw*rw-sx*rx-sy*ry-sz*rz;
                    float dRx=sw*rx+sx*rw+sy*rz-sz*ry;
                    float dRy=sw*ry-sx*rz+sy*rw+sz*rx;
                    float dRz=sw*rz+sx*ry-sy*rx+sz*rw;
                    // Apply delta to original offset rotation: newRot = rotDelta * originalRot
                    float ow=s_smooth.originalOffset.rot.w, ox=s_smooth.originalOffset.rot.x,
                          oy=s_smooth.originalOffset.rot.y, oz=s_smooth.originalOffset.rot.z;
                    float nw=dRw*ow-dRx*ox-dRy*oy-dRz*oz;
                    float nx=dRw*ox+dRx*ow+dRy*oz-dRz*oy;
                    float ny=dRw*oy-dRx*oz+dRy*ow+dRz*ox;
                    float nz=dRw*oz+dRx*oy-dRy*ox+dRz*ow;

                    Offset6DOF result = s_smooth.originalOffset;
                    result.pos.x+=lx; result.pos.y+=ly; result.pos.z+=lz;
                    result.rot = {nw, nx, ny, nz};
                    g_ipc.SendSetOffset(s_smooth.ctrlIdx, result);
                }
            }
        }

        // Auto-remap mappings when the hand role migrates to a new device
        // (e.g. Quest hand tracking takes over from a controller or vice versa).
        // Runs once per second to avoid hammering the IPC pipe.
        {
            static double s_lastRoleCheck = 0.0;
            static uint32_t s_roleLeft  = vr::k_unTrackedDeviceIndexInvalid;
            static uint32_t s_roleRight = vr::k_unTrackedDeviceIndexInvalid;
            if (g_vrSystem && g_driverConnected && now - s_lastRoleCheck > 1.0
                && !g_ui.GetHtLocked()) {
                s_lastRoleCheck = now;
                uint32_t newLeft  = g_vrSystem->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
                uint32_t newRight = g_vrSystem->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);

                auto tryRemap = [&](uint32_t newIdx, uint32_t& lastIdx, uint32_t side) {
                    if (newIdx == vr::k_unTrackedDeviceIndexInvalid) return;
                    if (newIdx == lastIdx) return;
                    uint32_t prevIdx = lastIdx;
                    lastIdx = newIdx;

                    // Find a mapping to re-point to the new role-holder.
                    // Remap if: (a) explicitly tagged with this hand side (via "Left/Right Hand auto"),
                    // OR (b) currently mapped to the device that just LOST this role.
                    // This makes existing mappings (side=0) auto-follow when their device switches to hand tracking.
                    Msg_StateUpdate snap;
                    { std::lock_guard<std::mutex> lk(g_stateMutex); snap = g_activeState; }
                    for (uint32_t mi = 0; mi < snap.mappingCount; mi++) {
                        const DeviceMapping& m = snap.mappings[mi];
                        if (strstr(m.controllerSerial, "VirtCtrl_")) continue; // virtual controllers manage their own role
                        if (m.controllerIndex == newIdx) continue; // already on this device
                        bool taggedForSide = (m.side == side);
                        bool onOldDevice   = (prevIdx != vr::k_unTrackedDeviceIndexInvalid
                                              && m.controllerIndex == prevIdx);
                        if (!taggedForSide && !onOldDevice) continue;

                        char serial[32]{};
                        g_vrSystem->GetStringTrackedDeviceProperty(newIdx,
                            vr::Prop_SerialNumber_String, serial, sizeof(serial));

                        vr::ETrackedDeviceClass cls = g_vrSystem->GetTrackedDeviceClass(newIdx);
                        const char* clsName = (cls == vr::ETrackedDeviceClass(5)) ? "HandTracker" : "Controller";
                        LOG_INFO("HandRole: side=%u role moved to dev=%u (%s, %s) — remapping from %u",
                            side, newIdx, serial, clsName, m.controllerIndex);

                        // Remove old mapping first so the driver doesn't intercept
                        // both the old controller AND the new hand-tracking device.
                        g_ipc.SendClearMapping(m.controllerIndex);

                        DeviceMapping newM = m;
                        newM.controllerIndex = newIdx;
                        memset(newM.controllerSerial, 0, sizeof(newM.controllerSerial));
                        memcpy(newM.controllerSerial, serial,
                            strnlen(serial, sizeof(newM.controllerSerial) - 1));
                        g_ipc.SendSetMapping(newM);
                        if (newM.enabled) g_ipc.SendEnableMapping(newIdx);
                    }
                };

                tryRemap(newLeft,  s_roleLeft,  1);
                tryRemap(newRight, s_roleRight, 2);
            }
        }

        // Poll SteamVR system events to track physical controller button state.
        // Must run before ForwardControllerInputs so event state is current.
        PollControllerEvents();

        // Forward real controller button/axis state to virtual controllers each frame
        ForwardControllerInputs();

        g_ui.SetDriverConnected(g_driverConnected);

        // When VR is active render into the FBO directly — the window framebuffer returns
        // (0,0) when iconified, which would make ImGui render nothing and the overlay go blank.
        if (g_fbo) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
            glViewport(0, 0, g_fboW, g_fboH);
        } else {
            glfwGetFramebufferSize(window, &w, &h);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, w > 0 ? w : 1, h > 0 ? h : 1);
        }
        glClearColor(0.07f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // BeginFrame hook: also override DisplaySize when GLFW reports (0,0) for iconified window
        g_ui.BeginFrame([](){
            if (g_fboW > 0 && g_fboH > 0) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) {
                    io.DisplaySize             = ImVec2((float)g_fboW, (float)g_fboH);
                    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
                }
            }
            ForwardVRInput(g_fboW, g_fboH);
        });
        g_ui.EndFrame();

        // Submit the FBO (which we just rendered into) to the VR overlay
        if (g_dashHandle != vr::k_ulOverlayHandleInvalid && g_fboTex) {
            vr::Texture_t vrTex{};
            vrTex.handle      = (void*)(uintptr_t)g_fboTex;
            vrTex.eType       = vr::TextureType_OpenGL;
            vrTex.eColorSpace = vr::ColorSpace_Auto;
            vr::VROverlay()->SetOverlayTexture(g_dashHandle, &vrTex);
        }

        // Blit FBO → desktop window only when the window is visible (non-iconified)
        if (g_fbo) {
            glfwGetFramebufferSize(window, &w, &h);
            if (w > 0 && h > 0) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, g_fboW, g_fboH, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        glfwSwapBuffers(window);
    }

    g_ipc.Disconnect();
    g_ui.Shutdown();
    OSCShutdown();
    WSShutdown();
    ShutdownVROverlay();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
