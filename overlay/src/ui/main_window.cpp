#include "main_window.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <cstring>
#include <cmath>
#include <algorithm>

// stb_image for PNG icon loading
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ──────────────────────────────────────────────────────────────────────────────
// Color palette — Breakers Tool dark neon theme
// ──────────────────────────────────────────────────────────────────────────────
// #F53F27 = (0.961, 0.247, 0.153) — Breakers red-orange brand color
static const ImVec4 COL_BG_DARK       = {0.07f, 0.05f, 0.05f, 1.00f};
static const ImVec4 COL_BG_MID        = {0.11f, 0.08f, 0.08f, 1.00f};
static const ImVec4 COL_BG_PANEL      = {0.15f, 0.11f, 0.10f, 1.00f};
static const ImVec4 COL_ACCENT        = {0.961f, 0.247f, 0.153f, 1.00f}; // #F53F27
static const ImVec4 COL_ACCENT_BRIGHT = {1.00f,  0.38f,  0.26f,  1.00f}; // lightened
static const ImVec4 COL_TEAL          = {1.00f,  0.65f,  0.20f,  1.00f}; // warm amber to complement
static const ImVec4 COL_GREEN         = {0.20f,  0.85f,  0.45f,  1.00f};
static const ImVec4 COL_RED           = {0.90f,  0.25f,  0.30f,  1.00f};
static const ImVec4 COL_TEXT          = {0.95f,  0.93f,  0.92f,  1.00f};
static const ImVec4 COL_TEXT_DIM      = {0.55f,  0.50f,  0.48f,  1.00f};
static const ImVec4 COL_BORDER        = {0.50f,  0.18f,  0.12f,  0.70f};

static ImVec4 Lerp(ImVec4 a, ImVec4 b, float t) {
    return {a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t};
}

// ──────────────────────────────────────────────────────────────────────────────

static unsigned int LoadTexture(const char* path) {
    int w, h, ch;
    unsigned char* data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) return 0;

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return tex;
}

static void ImageOrPlaceholder(unsigned int tex, ImVec2 size, ImVec4 tint = {1,1,1,1}) {
    if (tex)
        ImGui::ImageWithBg(ImTextureRef((ImTextureID)(uintptr_t)tex), size,
            ImVec2(0,0), ImVec2(1,1), ImVec4(0,0,0,0), tint);
    else
        ImGui::Dummy(size);
}

// ──────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() = default;

bool MainWindow::Init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "ovrdancers_ui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load font — falls back to built-in if file missing
    io.Fonts->AddFontFromFileTTF("resources/fonts/Inter-Regular.ttf", 15.0f);
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();

    ApplyTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    LoadIcons();
    return true;
}

void MainWindow::Shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void MainWindow::LoadIcons() {
    m_iconController = LoadTexture("resources/icons/controller.png");
    m_iconTracker    = LoadTexture("resources/icons/tracker.png");
    m_iconLink       = LoadTexture("resources/icons/link.png");
    m_iconSettings   = LoadTexture("resources/icons/settings.png");
    m_iconLogo       = LoadTexture("resources/icons/logo.png");
}

void MainWindow::ApplyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 10.0f;
    s.ChildRounding     = 8.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 8.0f;
    s.GrabRounding      = 4.0f;
    s.TabRounding       = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = {8, 6};
    s.FramePadding      = {10, 6};
    s.WindowPadding     = {14, 12};
    s.IndentSpacing     = 16.0f;
    s.ScrollbarSize     = 18.0f;

    auto* c = s.Colors;
    c[ImGuiCol_WindowBg]             = COL_BG_DARK;
    c[ImGuiCol_ChildBg]              = COL_BG_MID;
    c[ImGuiCol_PopupBg]              = COL_BG_PANEL;
    c[ImGuiCol_Border]               = COL_BORDER;
    c[ImGuiCol_FrameBg]              = {0.18f, 0.12f, 0.11f, 1.0f};
    c[ImGuiCol_FrameBgHovered]       = {0.30f, 0.14f, 0.10f, 1.0f};
    c[ImGuiCol_FrameBgActive]        = {0.45f, 0.18f, 0.12f, 1.0f};
    c[ImGuiCol_TitleBg]              = COL_BG_DARK;
    c[ImGuiCol_TitleBgActive]        = COL_BG_DARK;
    c[ImGuiCol_MenuBarBg]            = COL_BG_MID;
    c[ImGuiCol_ScrollbarBg]          = {0.0f, 0.0f, 0.0f, 0.0f};
    c[ImGuiCol_ScrollbarGrab]        = {0.45f, 0.14f, 0.09f, 1.0f};
    c[ImGuiCol_ScrollbarGrabHovered] = COL_ACCENT;
    c[ImGuiCol_CheckMark]            = COL_ACCENT_BRIGHT;
    c[ImGuiCol_SliderGrab]           = COL_ACCENT;
    c[ImGuiCol_SliderGrabActive]     = COL_ACCENT_BRIGHT;
    c[ImGuiCol_Button]               = {0.35f, 0.13f, 0.09f, 1.0f};
    c[ImGuiCol_ButtonHovered]        = COL_ACCENT;
    c[ImGuiCol_ButtonActive]         = COL_ACCENT_BRIGHT;
    c[ImGuiCol_Header]               = {0.35f, 0.13f, 0.09f, 0.8f};
    c[ImGuiCol_HeaderHovered]        = {0.55f, 0.18f, 0.12f, 1.0f};
    c[ImGuiCol_HeaderActive]         = COL_ACCENT;
    c[ImGuiCol_Tab]                  = {0.20f, 0.10f, 0.08f, 1.0f};
    c[ImGuiCol_TabHovered]           = COL_ACCENT;
    c[ImGuiCol_TabActive]            = {0.45f, 0.16f, 0.10f, 1.0f};
    c[ImGuiCol_Separator]            = COL_BORDER;
    c[ImGuiCol_Text]                 = COL_TEXT;
    c[ImGuiCol_TextDisabled]         = COL_TEXT_DIM;
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::UpdateDevices(const TrackedDeviceInfo* devices, uint32_t count) {
    m_controllers.clear();
    m_trackers.clear();
    for (uint32_t i = 0; i < count; i++) {
        if (devices[i].deviceClass == 2 /*Controller*/)
            m_controllers.push_back(devices[i]);
        else if (devices[i].deviceClass == 3 /*GenericTracker*/)
            m_trackers.push_back(devices[i]);
    }
    // Clamp selection indices so we never index out of bounds in the popup
    if (!m_controllers.empty()) m_addCtrlSel = std::min(m_addCtrlSel, (int)m_controllers.size()-1);
    else m_addCtrlSel = 0;
    if (!m_trackers.empty()) m_addTrkrSel = std::min(m_addTrkrSel, (int)m_trackers.size()-1);
    else m_addTrkrSel = 0;
}

void MainWindow::UpdateMappings(const DeviceMapping* mappings, uint32_t count) {
    m_mappings.assign(mappings, mappings + count);
    if (m_offsetExpanded.size() < count)
        m_offsetExpanded.resize(count, false);
    else
        m_offsetExpanded.resize(count);
}

void MainWindow::UpdateSavedMappings(const std::vector<SavedMapping>& saved) {
    m_savedMappings = saved;
}

void MainWindow::SetDriverConnected(bool connected) {
    m_driverConnected = connected;
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::BeginFrame(InputHook vrInputHook) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    // Hook runs here: after GLFW updates IO, before NewFrame processes the queue.
    // VR laser events added here will be processed this frame (not next frame).
    if (vrInputHook) vrInputHook();
    ImGui::NewFrame();
    m_animTime += ImGui::GetIO().DeltaTime;
}

void MainWindow::EndFrame() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowBgAlpha(1.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar();

    RenderHeader();
    ImGui::Spacing();
    RenderStatusBar();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Tab row — plain buttons with explicit state so VR clicks are reliable
    {
        const float tabH = 40.0f, tabW = 140.0f, gap = 4.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12.0f, 0.0f});

        auto tabBtn = [&](const char* label, int idx) -> bool {
            bool active = (m_activeTab == idx);
            ImVec4 bg  = active ? ImVec4{0.50f, 0.18f, 0.11f, 1.0f}
                                : ImVec4{0.18f, 0.10f, 0.08f, 1.0f};
            ImVec4 hov = active ? ImVec4{0.60f, 0.22f, 0.14f, 1.0f}
                                : ImVec4{0.32f, 0.15f, 0.10f, 1.0f};
            ImGui::PushStyleColor(ImGuiCol_Button,        bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_ACCENT);
            if (active) ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT_BRIGHT);
            bool clicked = ImGui::Button(label, {tabW, tabH});
            if (active) ImGui::PopStyleColor();
            ImGui::PopStyleColor(3);
            return clicked;
        };

        if (tabBtn("  Mappings", 0)) m_activeTab = 0;
        ImGui::SameLine(0, gap);
        if (tabBtn("  Settings", 1)) m_activeTab = 1;

        ImGui::PopStyleVar(2);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_activeTab == 0) {
        RenderDevicePanel();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("Device Visibility");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        bool changed = false;

        if (ImGui::Checkbox("Hide original controllers", &m_hideControllers))
            changed = true;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("Hides the physical controllers so only virtual ones appear in VR");
        ImGui::PopStyleColor();

        ImGui::Spacing();

        if (ImGui::Checkbox("Hide hand trackers", &m_hideTrackers))
            changed = true;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("Hides the wrist tracker pucks from the VR view");
        ImGui::PopStyleColor();

        if (changed && callbacks.onHideSettings)
            callbacks.onHideSettings(m_hideControllers, m_hideTrackers);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("Controller Bindings");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("Bind a VR controller button to toggle all virtual hands on/off");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.18f, 0.13f, 0.10f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.28f, 0.18f, 0.12f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.40f, 0.22f, 0.14f, 1.0f});
        if (ImGui::Button("Configure Binding##binding_btn", ImVec2(160, 0))) {
            if (callbacks.onOpenBindings) callbacks.onOpenBindings();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("Opens SteamVR binding UI in headset");
        ImGui::PopStyleColor();
    }
    RenderAddMappingPopup();

    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::RenderHeader() {
    // Logo + title row
    float headerY = ImGui::GetCursorPosY();
    ImGui::BeginGroup();
    if (m_iconLogo) {
        ImageOrPlaceholder(m_iconLogo, {40, 40});
        ImGui::SameLine(0, 12);
    }
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT_BRIGHT);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::Text("OVR DANCERS TOOL");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::Text("VR Tracker Controller Override");
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::EndGroup();

    // Determine global hands-active state: any mapping enabled = active
    bool anyEnabled = false;
    for (auto& m : m_mappings) if (m.enabled) { anyEnabled = true; break; }
    bool hasMappings = !m_mappings.empty();

    // Right-side buttons: [Hands: ON/OFF] [Reload Save] [Save] [+ Add Mapping]
    const float addW    = 140.0f, saveW = 68.0f, relW = 108.0f, handsW = 110.0f;
    const float gap     = 8.0f;
    float rightEdge     = ImGui::GetWindowContentRegionMax().x;
    float btnY          = headerY + 4.0f; // headerY saved before BeginGroup above

    // [Hands: ON / OFF] toggle — disabled when no mappings exist
    ImGui::SetCursorPos({rightEdge - handsW - gap - relW - gap - saveW - gap - addW, btnY});
    if (!hasMappings) ImGui::BeginDisabled();
    if (anyEnabled) {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.14f, 0.45f, 0.22f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.18f, 0.60f, 0.28f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f, 0.35f, 0.18f, 1.0f});
        if (ImGui::Button("  Hands: ON", {handsW, 32}) && callbacks.onToggleAll)
            callbacks.onToggleAll(false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Disable all virtual hands — real controllers take over");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.35f, 0.12f, 0.10f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.55f, 0.18f, 0.14f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_ACCENT);
        if (ImGui::Button("  Hands: OFF", {handsW, 32}) && callbacks.onToggleAll)
            callbacks.onToggleAll(true);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Re-enable all virtual hands");
    }
    ImGui::PopStyleColor(3);
    if (!hasMappings) ImGui::EndDisabled();

    ImGui::SetCursorPos({rightEdge - relW - gap - saveW - gap - addW, btnY});
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.20f, 0.17f, 0.15f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.32f, 0.26f, 0.22f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.45f, 0.34f, 0.28f, 1.0f});
    if (ImGui::Button("Reload Save", {relW, 32}) && callbacks.onReload)
        callbacks.onReload();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restore mappings from last save file");
    ImGui::PopStyleColor(3);

    ImGui::SetCursorPos({rightEdge - saveW - gap - addW, btnY});
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.28f, 0.22f, 0.10f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.50f, 0.36f, 0.14f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_TEAL);
    if (ImGui::Button("Save", {saveW, 32}) && callbacks.onSave)
        callbacks.onSave();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save all current mappings to disk");
    ImGui::PopStyleColor(3);

    ImGui::SetCursorPos({rightEdge - addW, btnY});
    ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT_BRIGHT);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_TEAL);
    if (ImGui::Button("  + Add Mapping  ", {addW, 32}))
        m_showAddPopup = true;
    ImGui::PopStyleColor(3);
}

void MainWindow::RenderStatusBar() {
    // Animated pulse dot
    float pulse = (sinf(m_animTime * 3.0f) + 1.0f) * 0.5f;
    ImVec4 dotCol = m_driverConnected
        ? Lerp(COL_GREEN, {0.10f, 0.70f, 0.35f, 1.0f}, pulse)
        : Lerp(COL_RED,   {0.70f, 0.10f, 0.12f, 1.0f}, pulse);

    ImGui::PushStyleColor(ImGuiCol_Text, dotCol);
    ImGui::Text(m_driverConnected ? "●  Driver connected" : "●  Driver not connected");
    ImGui::PopStyleColor();

    if (!m_driverConnected) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("— make sure SteamVR is running with OVRDancers driver installed");
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    char countBuf[64];
    snprintf(countBuf, sizeof(countBuf), "  %d controller(s)   %d tracker(s)",
        (int)m_controllers.size(), (int)m_trackers.size());
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::Text("%s", countBuf);
    ImGui::PopStyleColor();
}

void MainWindow::RenderDevicePanel() {

    // Build pending list: saved mappings whose serials don't match any active mapping
    std::vector<const SavedMapping*> pending;
    for (const auto& sm : m_savedMappings) {
        bool active = false;
        for (const auto& am : m_mappings)
            if (strcmp(am.controllerSerial, sm.controllerSerial) == 0 &&
                strcmp(am.trackerSerial,    sm.trackerSerial)    == 0)
                { active = true; break; }
        if (!active) pending.push_back(&sm);
    }

    if (m_mappings.empty() && pending.empty()) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.25f);
        float textW = 260.0f;
        ImGui::SetCursorPosX((avail.x - textW) * 0.5f);
        ImGui::BeginGroup();
        if (m_iconLink) {
            ImGui::SetCursorPosX((textW - 48) * 0.5f + ImGui::GetCursorPosX());
            ImageOrPlaceholder(m_iconLink, {48, 48}, {0.50f, 0.15f, 0.10f, 1.0f});
        }
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::SetCursorPosX((avail.x - ImGui::CalcTextSize("No mappings yet").x) * 0.5f);
        ImGui::Text("No mappings yet");
        ImGui::SetCursorPosX((avail.x - ImGui::CalcTextSize("Press  + Add Mapping  to begin").x) * 0.5f);
        ImGui::Text("Press  + Add Mapping  to begin");
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        return;
    }

    // Single scroll region for all cards — mouse wheel scrolls the whole page
    ImGui::BeginChild("##cards_scroll", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 10});

    for (int i = 0; i < (int)m_mappings.size(); i++)
        RenderMappingCard(m_mappings[i], i);

    if (!pending.empty()) {
        if (!m_mappings.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 2);
            ImGui::Text("-- Waiting for devices to connect --");
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
        for (int i = 0; i < (int)pending.size(); i++)
            RenderPendingCard(*pending[i], i);
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::RenderMappingCard(DeviceMapping& mapping, int idx) {
    ImGui::PushID(idx);

    bool expanded = (idx < (int)m_offsetExpanded.size()) && m_offsetExpanded[idx];
    float cardW   = ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_BG_PANEL);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    // AutoResizeY: child grows/shrinks with content; NoScrollWithMouse: wheel bubbles up
    ImGui::BeginChild("##card", {cardW, 0},
        ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Left accent bar
    ImVec2 p = ImGui::GetWindowPos();
    ImVec4 barCol = mapping.enabled ? COL_ACCENT : COL_TEXT_DIM;
    ImGui::GetWindowDrawList()->AddRectFilled(
        {p.x, p.y + 6}, {p.x + 4, p.y + 200},
        ImGui::ColorConvertFloat4ToU32(barCol), 2.0f);

    ImGui::Spacing();

    // ── Row 1: device icons + names + ON/Remove buttons ──
    // Capture row-top Y so both buttons can be placed at the same absolute Y
    float rowTopY = ImGui::GetCursorPosY();

    // Controller block
    ImGui::SetCursorPosX(14);
    ImGui::BeginGroup();
    ImageOrPlaceholder(m_iconController, {24, 24},
        mapping.enabled ? ImVec4{1,1,1,1} : ImVec4{0.5f,0.5f,0.5f,1});
    ImGui::SameLine(0, 6);
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::Text("CONTROLLER");
    ImGui::PopStyleColor();
    ImGui::Text("%s", mapping.controllerSerial[0] ? mapping.controllerSerial : "Unknown");
    ImGui::EndGroup();
    ImGui::EndGroup();

    ImGui::SameLine(0, 16);
    ImGui::SetCursorPosY(rowTopY + 10); // center link arrow in row
    if (m_iconLink)
        ImageOrPlaceholder(m_iconLink, {20, 20}, COL_ACCENT);
    else {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::Text("→");
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(0, 16);
    ImGui::SetCursorPosY(rowTopY);

    // Tracker block
    ImGui::BeginGroup();
    ImageOrPlaceholder(m_iconTracker, {24, 24},
        mapping.enabled ? ImVec4{1,1,1,1} : ImVec4{0.5f,0.5f,0.5f,1});
    ImGui::SameLine(0, 6);
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::Text("TRACKER");
    ImGui::PopStyleColor();
    ImGui::Text("%s", mapping.trackerSerial[0] ? mapping.trackerSerial : "Unknown");
    ImGui::EndGroup();
    ImGui::EndGroup();

    // ON/Remove buttons — both placed at identical Y (rowTopY + centered)
    float btnH   = 26.0f;
    float rowH   = 52.0f; // approx height of 2-line device block
    float btnY   = rowTopY + (rowH - btnH) * 0.5f;
    float rightEdge = ImGui::GetWindowContentRegionMax().x;

    // Remove button (rightmost)
    ImGui::SetCursorPos({rightEdge - 74.0f, btnY});
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.35f, 0.10f, 0.12f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_RED);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{1.0f, 0.40f, 0.40f, 1.0f});
    if (ImGui::Button("Remove", {70, btnH}))
        if (callbacks.onClearMapping)
            callbacks.onClearMapping(mapping.controllerIndex);
    ImGui::PopStyleColor(3);

    // ON/OFF button (left of Remove)
    ImGui::SetCursorPos({rightEdge - 74.0f - 64.0f, btnY});
    ImVec4 toggleCol = mapping.enabled ? COL_GREEN : ImVec4{0.25f, 0.25f, 0.30f, 1.0f};
    ImGui::PushStyleColor(ImGuiCol_Button, toggleCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mapping.enabled
        ? ImVec4{0.15f, 0.70f, 0.35f, 1.0f}
        : ImVec4{0.30f, 0.30f, 0.38f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, COL_TEAL);
    if (ImGui::Button(mapping.enabled ? " ON " : "OFF ", {60, btnH})) {
        mapping.enabled = !mapping.enabled;
        if (callbacks.onToggleMapping)
            callbacks.onToggleMapping(mapping.controllerIndex, mapping.enabled);
    }
    ImGui::PopStyleColor(3);

    // Advance cursor past the row
    ImGui::SetCursorPosY(rowTopY + rowH + 4);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, COL_BORDER);
    ImGui::SetCursorPosX(14);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ── Row 2: expand toggle + Quick Calibrate ──
    ImGui::SetCursorPosX(14);
    const char* toggleLabel = expanded ? "v  Manual Offset" : ">  Manual Offset";
    ImGui::PushStyleColor(ImGuiCol_Button,        COL_BG_MID);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f, 0.15f, 0.13f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.30f, 0.18f, 0.14f, 1.0f});
    if (ImGui::Button(toggleLabel, {130, 0})) {
        if (idx < (int)m_offsetExpanded.size())
            m_offsetExpanded[idx] = !m_offsetExpanded[idx];
    }
    ImGui::PopStyleColor(3);

    if (callbacks.onCalibratePosition) {
        float calBtnW = 140.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - calBtnW);
        if (ImGui::Button("Quick Calibrate", {calBtnW, 0})) {
            Offset6DOF cal = callbacks.onCalibratePosition(
                mapping.controllerIndex, mapping.trackerIndex);
            mapping.offset = cal;
            if (callbacks.onSetOffset)
                callbacks.onSetOffset(mapping.controllerIndex, cal);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Snaps the virtual controller to match\nthe physical controller's current position.");
    }

    // ── Expanded: offset sliders ──
    if (expanded) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Separator, COL_BORDER);
        ImGui::SetCursorPosX(14);
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        RenderOffsetEditor(mapping);
    }

    ImGui::Spacing();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::PopID();
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::RenderPendingCard(const SavedMapping& pm, int idx) {
    ImGui::PushID(idx + 10000);
    float cardW = ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.10f, 0.08f, 0.08f, 1.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild("##pcard", {cardW, 0}, ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Dim left accent bar
    ImVec2 p = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        {p.x, p.y + 4}, {p.x + 4, p.y + 68},
        ImGui::ColorConvertFloat4ToU32(ImVec4{0.40f, 0.37f, 0.35f, 1.0f}), 2.0f);

    ImGui::Spacing();
    ImGui::SetCursorPosX(14);

    // Device serials row
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImageOrPlaceholder(m_iconController, {18, 18}, ImVec4{0.4f, 0.4f, 0.4f, 1.0f});
    ImGui::SameLine(0, 5);
    ImGui::Text("%s", pm.controllerSerial[0] ? pm.controllerSerial : "?");
    ImGui::SameLine(0, 10);
    if (m_iconLink)
        ImageOrPlaceholder(m_iconLink, {14, 14}, ImVec4{0.4f, 0.4f, 0.4f, 1.0f});
    else
        ImGui::Text("->");
    ImGui::SameLine(0, 10);
    ImageOrPlaceholder(m_iconTracker, {18, 18}, ImVec4{0.4f, 0.4f, 0.4f, 1.0f});
    ImGui::SameLine(0, 5);
    ImGui::Text("%s", pm.trackerSerial[0] ? pm.trackerSerial : "?");

    // Status label at right edge
    float rightEdge = ImGui::GetWindowContentRegionMax().x;
    const char* statusLabel = "● Waiting for devices";
    float labelW = ImGui::CalcTextSize(statusLabel).x;
    if (ImGui::GetCursorPosX() < rightEdge - labelW - 8) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(rightEdge - labelW);
    }
    ImGui::Text("%s", statusLabel);
    ImGui::PopStyleColor();

    // Action buttons row
    ImGui::Spacing();
    ImGui::SetCursorPosX(14);

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.25f,0.18f,0.18f,1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.40f,0.22f,0.22f,1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.55f,0.18f,0.18f,1.0f});
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4{0.85f,0.45f,0.45f,1.0f});
    if (ImGui::SmallButton("Remove")) {
        if (callbacks.onRemoveSavedMapping)
            callbacks.onRemoveSavedMapping(pm.controllerSerial);
    }
    ImGui::PopStyleColor(4);

    ImGui::SameLine(0, 8);

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.15f,0.22f,0.28f,1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.20f,0.30f,0.38f,1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.15f,0.35f,0.50f,1.0f});
    if (ImGui::SmallButton("Retry")) {
        if (callbacks.onRetryRestore)
            callbacks.onRetryRestore();
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopID();
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::RenderOffsetEditor(DeviceMapping& mapping) {
    bool changed = false;
    float posArr[3] = { mapping.offset.pos.x, mapping.offset.pos.y, mapping.offset.pos.z };
    float rotArr[3]; // euler degrees for display

    // Convert quat to euler (yaw/pitch/roll) for display
    auto& q = mapping.offset.rot;
    float sinr_cosp = 2*(q.w*q.x + q.y*q.z);
    float cosr_cosp = 1 - 2*(q.x*q.x + q.y*q.y);
    float roll  = atan2f(sinr_cosp, cosr_cosp) * (180.0f / 3.14159265f);

    float sinp = 2*(q.w*q.y - q.z*q.x);
    float pitch = (fabsf(sinp) >= 1) ? copysignf(90.0f, sinp) : asinf(sinp) * (180.0f / 3.14159265f);

    float siny_cosp = 2*(q.w*q.z + q.x*q.y);
    float cosy_cosp = 1 - 2*(q.y*q.y + q.z*q.z);
    float yaw   = atan2f(siny_cosp, cosy_cosp) * (180.0f / 3.14159265f);
    rotArr[0] = pitch; rotArr[1] = yaw; rotArr[2] = roll;

    ImGui::SetCursorPosX(14);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
    ImGui::Text("Position (m)");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (ImGui::SmallButton("Reset pos")) {
        mapping.offset.pos = {0, 0, 0};
        changed = true;
    }

    ImGui::SetCursorPosX(14);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28);
    if (ImGui::SliderFloat3("##pos", posArr, -0.20f, 0.20f, "%.3f m")) {
        mapping.offset.pos = { posArr[0], posArr[1], posArr[2] };
        changed = true;
    }

    ImGui::SetCursorPosX(14);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
    ImGui::Text("Rotation (deg)");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (ImGui::SmallButton("Reset rot")) {
        mapping.offset.rot = {1, 0, 0, 0};
        changed = true;
    }

    ImGui::SetCursorPosX(14);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28);
    if (ImGui::SliderFloat3("##rot", rotArr, -180.0f, 180.0f, "%.1f°")) {
        // Convert euler back to quat
        float p2 = rotArr[0] * (3.14159265f / 180.0f) * 0.5f;
        float y2 = rotArr[1] * (3.14159265f / 180.0f) * 0.5f;
        float r2 = rotArr[2] * (3.14159265f / 180.0f) * 0.5f;
        mapping.offset.rot.w = cosf(r2)*cosf(p2)*cosf(y2) + sinf(r2)*sinf(p2)*sinf(y2);
        mapping.offset.rot.x = sinf(r2)*cosf(p2)*cosf(y2) - cosf(r2)*sinf(p2)*sinf(y2);
        mapping.offset.rot.y = cosf(r2)*sinf(p2)*cosf(y2) + sinf(r2)*cosf(p2)*sinf(y2);
        mapping.offset.rot.z = cosf(r2)*cosf(p2)*sinf(y2) - sinf(r2)*sinf(p2)*cosf(y2);
        changed = true;
    }

    if (changed && callbacks.onSetOffset)
        callbacks.onSetOffset(mapping.controllerIndex, mapping.offset);
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::RenderAddMappingPopup() {
    if (m_showAddPopup)
        ImGui::OpenPopup("Add Mapping##popup");

    ImGui::SetNextWindowSize({420, 280}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 18});

    if (ImGui::BeginPopupModal("Add Mapping##popup", &m_showAddPopup,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT_BRIGHT);
        ImGui::Text("New Tracker → Controller Mapping");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Controller selector
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("Select Controller");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##ctrl", m_controllers.empty() ? "(none found)"
            : m_controllers[m_addCtrlSel].serial)) {
            for (int i = 0; i < (int)m_controllers.size(); i++) {
                bool sel = (i == m_addCtrlSel);
                char label[96];
                snprintf(label, sizeof(label), "%s  —  %s",
                    m_controllers[i].serial, m_controllers[i].modelNumber);
                if (ImGui::Selectable(label, sel)) m_addCtrlSel = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();

        // Tracker selector
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("Select Wrist Tracker");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##trkr", m_trackers.empty() ? "(none found)"
            : m_trackers[m_addTrkrSel].serial)) {
            for (int i = 0; i < (int)m_trackers.size(); i++) {
                bool sel = (i == m_addTrkrSel);
                char label[96];
                snprintf(label, sizeof(label), "%s  —  %s",
                    m_trackers[i].serial, m_trackers[i].modelNumber);
                if (ImGui::Selectable(label, sel)) m_addTrkrSel = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool canAdd = !m_controllers.empty() && !m_trackers.empty();

        if (!canAdd) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_RED);
            ImGui::Text("No controllers or trackers detected. Start SteamVR first.");
            ImGui::PopStyleColor();
        }

        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 180 + ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("Cancel", {80, 30})) {
            m_showAddPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, 10);
        if (!canAdd) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT_BRIGHT);
        if (ImGui::Button("Add Mapping", {90, 30}) && canAdd) {
            DeviceMapping mapping = {};
            mapping.controllerIndex = m_controllers[m_addCtrlSel].index;
            mapping.trackerIndex    = m_trackers[m_addTrkrSel].index;
            strncpy_s(mapping.controllerSerial, m_controllers[m_addCtrlSel].serial, 31);
            strncpy_s(mapping.trackerSerial,    m_trackers[m_addTrkrSel].serial, 31);
            mapping.enabled = false;
            if (callbacks.onSetMapping) callbacks.onSetMapping(mapping);
            m_showAddPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        if (!canAdd) ImGui::EndDisabled();

        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}
