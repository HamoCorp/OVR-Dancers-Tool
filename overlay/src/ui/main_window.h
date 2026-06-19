#pragma once
#include <vector>
#include <string>
#include <functional>
#include "../../../shared/protocol.h"
#include "../persistence.h"
#include "../osc_sender.h"
#include "../websocket_server.h"
#include "translations.h"

struct GLFWwindow;

// Callbacks to the overlay app
struct UICallbacks {
    std::function<void(const DeviceMapping&)>    onSetMapping;
    std::function<void(uint32_t)>                onClearMapping;
    std::function<void(uint32_t, const Offset6DOF&)> onSetOffset;
    std::function<void(uint32_t, bool)>          onToggleMapping; // true=enable
    // Temporarily disables mapping, waits for real pose, computes & applies offset, re-enables.
    // wasEnabled: true if the mapping was active before the button was pressed.
    std::function<void(uint32_t ctrlIdx, uint32_t trackerIdx, bool wasEnabled)> onCalibratePosition;
    // Settings toggles
    std::function<void(bool hideControllers, bool hideTrackers)> onHideSettings;
    // Enable or disable ALL active mappings at once (quick mute / restore)
    std::function<void(bool enable)> onToggleAll;
    // Toggle only one hand side (side: 1=left, 2=right)
    std::function<void(int side, bool enable)> onToggleHand;
    // Opens SteamVR controller binding UI for the toggle_hands action
    std::function<void()> onOpenBindings;
    // Deletes all saved mappings from disk and clears active state
    std::function<void()> onDeleteAllSettings;
    // Enable VirtCtrl_L/R in SteamVR (one-shot, can't un-register)
    std::function<void()> onCreateVirtualControllers;
    // Connect or disconnect a virtual controller without restarting SteamVR (side: 1=L, 2=R)
    std::function<void(uint8_t side, bool active)> onSetVirtCtrlActive;
    // Returns the current device index holding the given hand role (1=left, 2=right).
    // Returns 0xFFFFFFFF if no device holds that role.
    std::function<uint32_t(uint32_t side)> onGetHandRoleDevice;
    // Smooth (movement-based) calibration: average offset over 5 seconds
    std::function<void(uint32_t ctrlIdx, uint32_t trackerIdx, bool wasEnabled)> onCalibrateSmooth;
    // Persistence
    std::function<void()> onSave;
    std::function<void()> onReload;
    // Remove a pending saved mapping by controller serial (no active mapping exists yet)
    std::function<void(const char* controllerSerial)> onRemoveSavedMapping;
    // Manually retry restoring all pending saved mappings against current device list
    std::function<void()> onRetryRestore;
};

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Init(GLFWwindow* window);
    void Shutdown();

    // Font rebuild for CJK language changes — call BEFORE BeginFrame
    bool NeedsFontRebuild() const { return m_pendingFontRebuild; }
    void RebuildFonts();

    // Language
    Language GetLanguage() const { return g_language; }
    void SetLanguage(Language l) { g_language = l; m_pendingFontRebuild = true; }

    // Split frame: GLFW/GL new-frame happens first, then caller injects extra
    // input (e.g. VR laser events), then EndFrame draws everything.
    // inputHook runs between ImGui_ImplGlfw_NewFrame and ImGui::NewFrame so
    // VR events land in the correct queue position (after GLFW, before processing).
    using InputHook = void(*)();
    void BeginFrame(InputHook vrInputHook = nullptr);
    void EndFrame();
    void Render() { BeginFrame(); EndFrame(); }

    // Data sync from driver (call from main thread only)
    void UpdateDevices(const TrackedDeviceInfo* devices, uint32_t count);
    void UpdateMappings(const DeviceMapping* mappings, uint32_t count);
    void SetDriverConnected(bool connected);
    // Saved (pending) mappings from JSON — shown as "waiting" cards when not yet active
    void UpdateSavedMappings(const std::vector<SavedMapping>& saved);

    UICallbacks callbacks;

    // True when the user closes the window
    bool wantsQuit = false;

    bool GetHideControllers()      const { return m_hideControllers; }
    bool GetHideTrackers()         const { return m_hideTrackers; }
    bool GetVirtualCtrlRegistered() const { return m_virtualCtrlRegistered; }
    const OSCSettings& GetOSCSettings() const { return m_osc; }
    void SetOSCSettings(const OSCSettings& s) { m_osc = s; }
    const WSSettings& GetWSSettings() const { return m_ws; }
    void SetWSSettings(const WSSettings& s) { m_ws = s; }
    void SetVirtualCtrlRegistered(bool v)      { m_virtualCtrlRegistered = v; }
    bool GetHtLocked()  const { return m_htLocked; }
    void SetHtLocked(bool v)  { m_htLocked  = v; }
    const std::vector<DeviceMapping>& GetMappings() const { return m_mappings; }

    // Smooth calibration progress (set by main.cpp, read by mapping card render)
    uint32_t smoothCalibCtrlIdx  = 0xFFFFFFFF;
    float    smoothCalibSecsLeft = 0.0f;

private:
    void ApplyTheme();
    void RenderHeader();
    void RenderStatusBar();
    void RenderDevicePanel();
    void RenderMappingCard(DeviceMapping& mapping, int idx);
    void RenderPendingCard(const SavedMapping& pm, int idx);
    void RenderOffsetEditor(DeviceMapping& mapping);
    void RenderAddMappingPopup();
    void LoadIcons();

    // Device lists
    std::vector<TrackedDeviceInfo> m_controllers;
    std::vector<TrackedDeviceInfo> m_trackers;
    std::vector<TrackedDeviceInfo> m_allDevices; // controllers + trackers combined
    std::vector<DeviceMapping>     m_mappings;
    std::vector<bool>              m_offsetExpanded; // per-mapping collapse state
    std::vector<SavedMapping>      m_savedMappings;  // from JSON; shown as pending when not active

    bool m_driverConnected          = false;
    bool m_showAddPopup             = false;
    bool m_hideControllers          = false;
    bool m_hideTrackers             = false;
    bool m_showAllDevices           = false; // show all devices in both dropdowns
    bool m_virtualCtrlRegistered    = false; // true once VirtCtrl_L/R are registered in SteamVR
    bool m_virtCtrlLActive          = false; // starts disconnected, user enables in Settings
    bool m_virtCtrlRActive          = false;
    bool m_htLocked                 = false; // lock hand tracking device
    int  m_activeTab                = 0; // 0=Mappings, 1=Settings

    // OSC / WebSocket settings (runtime-editable)
    OSCSettings m_osc;
    WSSettings  m_ws;

    // Add-mapping popup state
    int  m_addCtrlSel   = 0;
    int  m_addTrkrSel   = 0;
    int  m_addHandSide  = 0; // 0=specific device, 1=left hand auto, 2=right hand auto
    bool m_addHandError = false; // popup error: hand role not found

    // Icon textures (OpenGL IDs)
    unsigned int m_iconController = 0;
    unsigned int m_iconTracker    = 0;
    unsigned int m_iconLink       = 0;
    unsigned int m_iconSettings   = 0;
    unsigned int m_iconLogo       = 0;

    float m_animTime           = 0.0f;
    bool  m_pendingFontRebuild = false;
};
