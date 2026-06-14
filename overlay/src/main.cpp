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
#include <windows.h>
#include "ipc_client.h"
#include "ui/main_window.h"
#include "persistence.h"

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

        // Poll toggle_hands — rising edge triggers an all-mappings toggle
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
    LoadGL3Extensions(); // must be after context is current

    BLog::Init();
    LOG_INFO("OVRDancers overlay starting");

    g_ui.Init(window);

    // Wire UI callbacks to IPC — these run on the render thread, safe to call IPC send
    g_ui.callbacks.onSetMapping = [](const DeviceMapping& m) {
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
    };
    g_ui.callbacks.onToggleAll = [](bool enable) {
        // Toggle every known mapping — the UI already knows which indices exist via m_mappings.
        // We send enable/disable for each via the existing per-mapping IPC messages.
        for (auto& m : g_ui.GetMappings()) {
            if (enable) g_ipc.SendEnableMapping(m.controllerIndex);
            else        g_ipc.SendDisableMapping(m.controllerIndex);
        }
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
    g_ui.callbacks.onCalibratePosition = [](uint32_t ctrlIdx, uint32_t trackerIdx) -> Offset6DOF {
        Offset6DOF result{};
        if (!g_vrSystem) return result;

        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
        g_vrSystem->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);

        if (ctrlIdx    >= vr::k_unMaxTrackedDeviceCount) return result;
        if (trackerIdx >= vr::k_unMaxTrackedDeviceCount) return result;

        // The pose hook redirects the physical controller's pose to the tracker position.
        // Reading poses[ctrlIdx] would give the already-redirected position, causing the
        // calibration offset to compound on each press.  Use the ghost virtual controller
        // (which always shows the real physical controller location) instead.
        uint32_t posIdx = ctrlIdx;
        for (uint32_t mi = 0; mi < g_activeState.mappingCount; mi++) {
            if (g_activeState.mappings[mi].controllerIndex == ctrlIdx) {
                uint32_t gIdx = g_activeState.mappings[mi].virtDevIdx;
                if (gIdx != 0xFFFFFFFF && gIdx < vr::k_unMaxTrackedDeviceCount)
                    posIdx = gIdx;
                break;
            }
        }

        LOG_INFO("Calibrate: ctrlIdx=%u trackerIdx=%u posIdx=%u (virtDevIdx=%u)",
            ctrlIdx, trackerIdx, posIdx,
            [&]() -> uint32_t {
                for (uint32_t mi = 0; mi < g_activeState.mappingCount; mi++)
                    if (g_activeState.mappings[mi].controllerIndex == ctrlIdx)
                        return g_activeState.mappings[mi].virtDevIdx;
                return 0xFFFFFFFFu;
            }());
        LOG_INFO("Calibrate: ghostValid=%d trackerValid=%d",
            (int)poses[posIdx].bPoseIsValid, (int)poses[trackerIdx].bPoseIsValid);

        const auto& cp = poses[posIdx];
        const auto& tp = poses[trackerIdx];
        if (!cp.bPoseIsValid || !tp.bPoseIsValid) {
            LOG_WARN("Calibrate: early exit — pose invalid");
            return result;
        }

        // Extract columns of each 3x4 pose matrix (row-major, col = m[col][row])
        const auto& cm = cp.mDeviceToAbsoluteTracking.m;
        const auto& tm = tp.mDeviceToAbsoluteTracking.m;

        // Controller world position
        float cx = cm[0][3], cy = cm[1][3], cz = cm[2][3];
        // Tracker world position
        float tx = tm[0][3], ty = tm[1][3], tz = tm[2][3];

        LOG_INFO("Calibrate: ghostPos=(%.3f,%.3f,%.3f) trackerPos=(%.3f,%.3f,%.3f)",
            cx, cy, cz, tx, ty, tz);

        // Delta in world space
        float dx = cx - tx, dy = cy - ty, dz = cz - tz;

        // Transform delta into tracker local space: p_local = R_tracker^T * delta
        // tm columns are the tracker's right/up/forward vectors in world space
        // R_tracker rows: [tm[0][0] tm[1][0] tm[2][0]], [tm[0][1] tm[1][1] tm[2][1]], [tm[0][2] tm[1][2] tm[2][2]]
        result.pos.x = tm[0][0]*dx + tm[1][0]*dy + tm[2][0]*dz;
        result.pos.y = tm[0][1]*dx + tm[1][1]*dy + tm[2][1]*dz;
        result.pos.z = tm[0][2]*dx + tm[1][2]*dy + tm[2][2]*dz;

        // Compute relative rotation: R_rel = R_tracker^T * R_controller
        // R_tracker^T * R_controller — multiply 3x3 blocks
        float r00 = tm[0][0]*cm[0][0] + tm[1][0]*cm[1][0] + tm[2][0]*cm[2][0];
        float r01 = tm[0][0]*cm[0][1] + tm[1][0]*cm[1][1] + tm[2][0]*cm[2][1];
        float r02 = tm[0][0]*cm[0][2] + tm[1][0]*cm[1][2] + tm[2][0]*cm[2][2];
        float r10 = tm[0][1]*cm[0][0] + tm[1][1]*cm[1][0] + tm[2][1]*cm[2][0];
        float r11 = tm[0][1]*cm[0][1] + tm[1][1]*cm[1][1] + tm[2][1]*cm[2][1];
        float r12 = tm[0][1]*cm[0][2] + tm[1][1]*cm[1][2] + tm[2][1]*cm[2][2];
        float r20 = tm[0][2]*cm[0][0] + tm[1][2]*cm[1][0] + tm[2][2]*cm[2][0];
        float r21 = tm[0][2]*cm[0][1] + tm[1][2]*cm[1][1] + tm[2][2]*cm[2][1];
        float r22 = tm[0][2]*cm[0][2] + tm[1][2]*cm[1][2] + tm[2][2]*cm[2][2];

        // Convert 3x3 rotation matrix to quaternion
        float trace = r00 + r11 + r22;
        float qw, qx, qy, qz;
        if (trace > 0.0f) {
            float s = 0.5f / sqrtf(trace + 1.0f);
            qw = 0.25f / s;
            qx = (r21 - r12) * s;
            qy = (r02 - r20) * s;
            qz = (r10 - r01) * s;
        } else if (r00 > r11 && r00 > r22) {
            float s = 2.0f * sqrtf(1.0f + r00 - r11 - r22);
            qw = (r21 - r12) / s;
            qx = 0.25f * s;
            qy = (r01 + r10) / s;
            qz = (r02 + r20) / s;
        } else if (r11 > r22) {
            float s = 2.0f * sqrtf(1.0f + r11 - r00 - r22);
            qw = (r02 - r20) / s;
            qx = (r01 + r10) / s;
            qy = 0.25f * s;
            qz = (r12 + r21) / s;
        } else {
            float s = 2.0f * sqrtf(1.0f + r22 - r00 - r11);
            qw = (r10 - r01) / s;
            qx = (r02 + r20) / s;
            qy = (r12 + r21) / s;
            qz = 0.25f * s;
        }
        result.rot = {qw, qx, qy, qz};
        LOG_INFO("Calibrate: worldDelta=(%.3f,%.3f,%.3f) localOffset=(%.3f,%.3f,%.3f) rot=(%.3f,%.3f,%.3f,%.3f)",
            dx, dy, dz,
            result.pos.x, result.pos.y, result.pos.z,
            result.rot.w, result.rot.x, result.rot.y, result.rot.z);
        return result;
    };

    // ── Persistence setup ─────────────────────────────────────────────────────
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
        std::string exeDir(exePath);
        auto slash = exeDir.rfind('\\');
        if (slash != std::string::npos) exeDir = exeDir.substr(0, slash);
        g_savePath = exeDir + "\\ovrdancers_mappings.json";
        if (LoadMappings(g_savePath, g_savedMappings))
            LOG_INFO("Loaded %u saved mapping(s) from %s", (unsigned)g_savedMappings.size(), g_savePath.c_str());
        g_ui.UpdateSavedMappings(g_savedMappings);
    }

    g_ui.callbacks.onSave = []() {
        // Merge active mappings + any pending saved mappings that aren't active
        std::vector<SavedMapping> toSave;
        for (uint32_t mi = 0; mi < g_activeState.mappingCount; mi++) {
            const auto& am = g_activeState.mappings[mi];
            SavedMapping sm{};
            strncpy_s(sm.controllerSerial, sizeof(sm.controllerSerial), am.controllerSerial, _TRUNCATE);
            strncpy_s(sm.trackerSerial,    sizeof(sm.trackerSerial),    am.trackerSerial,    _TRUNCATE);
            sm.enabled = am.enabled;
            sm.offset  = am.offset;
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
        if (SaveMappings(g_savePath, toSave))
            LOG_INFO("Saved %u mapping(s) to %s", (unsigned)toSave.size(), g_savePath.c_str());
        else
            LOG_WARN("Failed to save mappings to %s", g_savePath.c_str());
        g_ui.UpdateSavedMappings(g_savedMappings);
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

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    InitVROverlay(window, w, h); // hides window and creates dashboard tab if SteamVR running

    double lastConnectAttempt = -999.0;

    while (!glfwWindowShouldClose(window) && !g_ui.wantsQuit) {
        glfwPollEvents();

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
        }

        // Poll SteamVR system events to track physical controller button state.
        // Must run before ForwardControllerInputs so event state is current.
        PollControllerEvents();

        // Forward real controller button/axis state to virtual controllers each frame
        ForwardControllerInputs();

        g_ui.SetDriverConnected(g_driverConnected);

        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.07f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Pass VR input as a hook: runs after ImGui_ImplGlfw_NewFrame, before ImGui::NewFrame
        g_ui.BeginFrame([](){ ForwardVRInput(g_fboW, g_fboH); });
        g_ui.EndFrame();

        SubmitOverlayFrame(); // no-op if SteamVR not running

        glfwSwapBuffers(window);
    }

    g_ipc.Disconnect();
    g_ui.Shutdown();
    ShutdownVROverlay();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
