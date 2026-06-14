#pragma once
#include <vector>
#include <string>
#include <functional>
#include "../../../shared/protocol.h"
#include "../persistence.h"

struct GLFWwindow;

// Callbacks to the overlay app
struct UICallbacks {
    std::function<void(const DeviceMapping&)>    onSetMapping;
    std::function<void(uint32_t)>                onClearMapping;
    std::function<void(uint32_t, const Offset6DOF&)> onSetOffset;
    std::function<void(uint32_t, bool)>          onToggleMapping; // true=enable
    // Returns computed offset that snaps virtual controller onto physical controller
    std::function<Offset6DOF(uint32_t ctrlIdx, uint32_t trackerIdx)> onCalibratePosition;
    // Settings toggles
    std::function<void(bool hideControllers, bool hideTrackers)> onHideSettings;
    // Enable or disable ALL active mappings at once (quick mute / restore)
    std::function<void(bool enable)> onToggleAll;
    // Opens SteamVR controller binding UI for the toggle_hands action
    std::function<void()> onOpenBindings;
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

    bool GetHideControllers() const { return m_hideControllers; }
    bool GetHideTrackers()    const { return m_hideTrackers; }
    const std::vector<DeviceMapping>& GetMappings() const { return m_mappings; }

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
    std::vector<DeviceMapping>     m_mappings;
    std::vector<bool>              m_offsetExpanded; // per-mapping collapse state
    std::vector<SavedMapping>      m_savedMappings;  // from JSON; shown as pending when not active

    bool m_driverConnected  = false;
    bool m_showAddPopup     = false;
    bool m_hideControllers  = false;
    bool m_hideTrackers     = false;
    int  m_activeTab        = 0; // 0=Mappings, 1=Settings

    // Add-mapping popup state
    int  m_addCtrlSel  = 0;
    int  m_addTrkrSel  = 0;

    // Icon textures (OpenGL IDs)
    unsigned int m_iconController = 0;
    unsigned int m_iconTracker    = 0;
    unsigned int m_iconLink       = 0;
    unsigned int m_iconSettings   = 0;
    unsigned int m_iconLogo       = 0;

    float m_animTime = 0.0f;
};
