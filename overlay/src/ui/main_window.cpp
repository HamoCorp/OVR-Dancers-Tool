#include "main_window.h"
#include "translations.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

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

static void LoadCJKFonts(ImGuiIO& io);

// ──────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() = default;

bool MainWindow::Init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "ovrdancers_ui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Build fonts directly on init (no GPU objects exist yet to destroy)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        if (!io.Fonts->AddFontFromFileTTF("resources/fonts/Inter-Regular.ttf", 15.0f))
            io.Fonts->AddFontDefault();

        // Always merge CJK glyphs so language names render in any language
        LoadCJKFonts(io);

        io.Fonts->Build();
        // ImGui_ImplOpenGL3_Init already creates device objects; fonts are
        // uploaded the first time NewFrame is called, so nothing extra needed here.
    }
    LoadIcons();
    return true;
}

void MainWindow::Shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void MainWindow::RebuildFonts() {
    m_pendingFontRebuild = false;
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplOpenGL3_DestroyDeviceObjects();
    io.Fonts->Clear();

    if (!io.Fonts->AddFontFromFileTTF("resources/fonts/Inter-Regular.ttf", 15.0f))
        io.Fonts->AddFontDefault();

    LoadCJKFonts(io);

    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateDeviceObjects();
}

static void LoadCJKFonts(ImGuiIO& io) {
    // Japanese glyphs (covers 日本語 + CJK ideographs)
    {
        ImFontConfig cfg;
        cfg.MergeMode  = true;
        cfg.PixelSnapH = true;
        static const char* jaFonts[] = {
            "C:\\Windows\\Fonts\\YuGothR.ttc",
            "C:\\Windows\\Fonts\\meiryo.ttc",
            "C:\\Windows\\Fonts\\msgothic.ttc",
            nullptr
        };
        for (int i = 0; jaFonts[i]; i++) {
            if (io.Fonts->AddFontFromFileTTF(jaFonts[i], 15.0f, &cfg, io.Fonts->GetGlyphRangesJapanese()))
                break;
        }
    }
    // Korean glyphs (covers 한국어 Hangul)
    {
        ImFontConfig cfg;
        cfg.MergeMode  = true;
        cfg.PixelSnapH = true;
        static const char* koFonts[] = {
            "C:\\Windows\\Fonts\\malgun.ttf",
            "C:\\Windows\\Fonts\\gulim.ttc",
            nullptr
        };
        for (int i = 0; koFonts[i]; i++) {
            if (io.Fonts->AddFontFromFileTTF(koFonts[i], 15.0f, &cfg, io.Fonts->GetGlyphRangesKorean()))
                break;
        }
    }
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
    m_allDevices.clear();
    for (uint32_t i = 0; i < count; i++) {
        if (devices[i].deviceClass == 2 /*Controller*/ ||
            devices[i].deviceClass == 5 /*HandTracker*/) {
            m_controllers.push_back(devices[i]);
            m_allDevices.push_back(devices[i]);
        } else if (devices[i].deviceClass == 3 /*GenericTracker*/) {
            m_trackers.push_back(devices[i]);
            m_allDevices.push_back(devices[i]);
        }
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

        if (tabBtn(TR().tabMappings, 0)) m_activeTab = 0;
        ImGui::SameLine(0, gap);
        if (tabBtn(TR().tabSettings, 1)) m_activeTab = 1;

        ImGui::PopStyleVar(2);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_activeTab == 0) {
        RenderDevicePanel();
    } else {
        // ── Settings scroll region ─────────────────────────────────────────
        ImGui::BeginChild("##settings_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

        // Drag-to-scroll for VR laser pointer (no scroll wheel in VR)
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
            float dy = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y;
            ImGui::SetScrollY(ImGui::GetScrollY() - dy);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }

        // ── Language ───────────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("%s", TR().sectionLanguage);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        for (int li = 0; li < (int)LANG_COUNT; li++) {
            if (ImGui::RadioButton(k_langTable[li]->langName, (int)g_language == li)) {
                SetLanguage((Language)li);
                if (callbacks.onSave) callbacks.onSave();
            }
            if (li < (int)LANG_COUNT - 1) ImGui::SameLine(0, 20);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── About ──────────────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("%s", TR().sectionAbout);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("%s", TR().aboutVersion);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("%s", TR().aboutSourceLabel);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, {0.40f, 0.70f, 1.0f, 1.0f});
        ImGui::Text("github.com/HamoCorp/OVR-Dancers-Tool");
        bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", TR().aboutTooltip);
        }
        if (hovered && ImGui::IsMouseClicked(0)) {
#ifdef _WIN32
            ShellExecuteA(nullptr, "open", "https://github.com/HamoCorp/OVR-Dancers-Tool", nullptr, nullptr, SW_SHOWNORMAL);
#else
            system("xdg-open https://github.com/HamoCorp/OVR-Dancers-Tool &");
#endif
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Device Visibility ──────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("%s", TR().sectionDeviceVisibility);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (ImGui::Checkbox(TR().hideHandTrackers, &m_hideTrackers)) {
            if (callbacks.onHideSettings)
                callbacks.onHideSettings(m_hideControllers, m_hideTrackers);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("%s", TR().hideHandTrackersDesc);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Controller bindings ────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("%s", TR().sectionBindings);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("%s", TR().bindingsDesc);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.18f, 0.13f, 0.10f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.28f, 0.18f, 0.12f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.40f, 0.22f, 0.14f, 1.0f});
        if (ImGui::Button(TR().editBindings, ImVec2(160, 0)))
            if (callbacks.onOpenBindings) callbacks.onOpenBindings();
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("%s", TR().editBindingsDesc);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Hand Tracking ──────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("%s", TR().sectionHandTracking);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("%s", TR().handTrackingDesc);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        bool htChanged = false;
        if (ImGui::Checkbox(TR().lockHandTracking, &m_htLocked))
            htChanged = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR().lockHandTrackingTooltip);
        if (htChanged && callbacks.onSave) callbacks.onSave();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Virtual Hand Controllers ───────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("%s", TR().sectionVirtualControllers);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("%s", TR().virtualControllersDesc);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (m_virtualCtrlRegistered) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
            ImGui::Text("%s", TR().virtualControllersRegistered);
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // Per-side connect/disconnect buttons
            auto virtBtn = [&](uint8_t side, bool& active) {
                // L side uses translated labels (which embed ###vcl).
                // R side always uses English labels with ###vcr for stable IDs.
                const char* btnLbl;
                if (side == 1)
                    btnLbl = active ? TR().virtCtrlConnected : TR().virtCtrlDisconnected;
                else
                    btnLbl = active ? "R: Connected###vcr" : "R: Disconnected###vcr";
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        {0.14f, 0.45f, 0.22f, 1.0f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.18f, 0.60f, 0.28f, 1.0f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f, 0.35f, 0.18f, 1.0f});
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        {0.35f, 0.12f, 0.10f, 1.0f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.55f, 0.18f, 0.14f, 1.0f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_ACCENT);
                }
                if (ImGui::Button(btnLbl, {148, 0})) {
                    active = !active;
                    if (callbacks.onSetVirtCtrlActive) callbacks.onSetVirtCtrlActive(side, active);
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", active ? TR().virtCtrlDisconnectTooltip : TR().virtCtrlConnectTooltip);
            };

            virtBtn(1, m_virtCtrlLActive);
            ImGui::SameLine(0, 8);
            virtBtn(2, m_virtCtrlRActive);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.18f, 0.13f, 0.10f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.28f, 0.18f, 0.12f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.40f, 0.22f, 0.14f, 1.0f});
            if (ImGui::Button(TR().enableVirtualControllers, ImVec2(220, 0))) {
                m_virtualCtrlRegistered = true;
                if (callbacks.onCreateVirtualControllers) callbacks.onCreateVirtualControllers();
                if (callbacks.onSave) callbacks.onSave();
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── OSC Output ─────────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("%s", TR().sectionOSC);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("%s", TR().oscDesc);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        bool oscChanged = false;
        if (ImGui::Checkbox(TR().enableOSC, &m_osc.enabled)) oscChanged = true;
        if (m_osc.enabled) {
            ImGui::SetNextItemWidth(160);
            if (ImGui::InputText("IP##osc_ip", m_osc.ip, sizeof(m_osc.ip))) oscChanged = true;
            ImGui::SameLine(0, 12);
            int port = m_osc.port;
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputInt("Port##osc_port", &port, 0)) {
                m_osc.port = (uint16_t)std::max(1, std::min(65535, port));
                oscChanged = true;
            }
            ImGui::SetNextItemWidth(200);
            if (ImGui::InputText("Parameter##osc_param", m_osc.paramName, sizeof(m_osc.paramName)))
                oscChanged = true;
            ImGui::SameLine(0, 8);
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            ImGui::TextUnformatted(TR().oscAvatarParam);
            ImGui::PopStyleColor();
        }
        if (oscChanged && callbacks.onSave) callbacks.onSave();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── WebSocket Output ───────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("%s", TR().sectionWebSocket);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("%s", TR().wsDesc);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        bool wsChanged = false;
        if (ImGui::Checkbox(TR().enableWS, &m_ws.enabled)) wsChanged = true;
        if (m_ws.enabled) {
            int wsPort = m_ws.port;
            ImGui::SetNextItemWidth(100);
            if (ImGui::InputInt("Port##ws_port", &wsPort, 0)) {
                m_ws.port = (uint16_t)std::max(1, std::min(65535, wsPort));
                wsChanged = true;
            }
            ImGui::SameLine(0, 8);
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            ImGui::TextUnformatted(TR().wsPortHint);
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(200);
            if (ImGui::InputText("Parameter##ws_param", m_ws.paramName, sizeof(m_ws.paramName)))
                wsChanged = true;
            ImGui::SameLine(0, 8);
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            ImGui::TextUnformatted(TR().wsJsonParam);
            ImGui::PopStyleColor();
        }
        if (wsChanged && callbacks.onSave) callbacks.onSave();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Saved Data ─────────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
        ImGui::Text("%s", TR().sectionSavedData);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.30f, 0.10f, 0.10f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.55f, 0.14f, 0.14f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.70f, 0.18f, 0.18f, 1.0f});
        if (ImGui::Button(TR().deleteAllSettings, ImVec2(180, 0)))
            ImGui::OpenPopup(TR().deleteConfirmTitle);
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("%s", TR().deleteAllDesc);
        ImGui::PopStyleColor();

        if (ImGui::BeginPopupModal(TR().deleteConfirmTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", TR().deleteConfirmQ);
            ImGui::Text("%s", TR().deleteConfirmNote);
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.55f, 0.14f, 0.14f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.75f, 0.18f, 0.18f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.90f, 0.22f, 0.22f, 1.0f});
            if (ImGui::Button(TR().deleteBtn, ImVec2(100, 0))) {
                if (callbacks.onDeleteAllSettings) callbacks.onDeleteAllSettings();
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, 12);
            if (ImGui::Button(TR().cancel, ImVec2(80, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::EndChild(); // ##settings_scroll
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
    ImGui::Text("VR Tracker Controller Override"); // subtitle stays English (brand)
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::EndGroup();

    // Determine global hands-active state: any mapping enabled = active
    bool anyEnabled = false;
    for (auto& m : m_mappings) if (m.enabled) { anyEnabled = true; break; }
    bool hasMappings = !m_mappings.empty();

    // Right-side buttons: [Hands ON/OFF] [Reload Save] [Save] [+ Add Mapping]
    const float addW   = 140.0f, saveW = 68.0f, relW = 108.0f;
    const float handsW = 110.0f, gap   = 8.0f;
    float rightEdge    = ImGui::GetWindowContentRegionMax().x;
    float btnY         = headerY + 4.0f;

    float handsStartX = rightEdge - handsW - gap - relW - gap - saveW - gap - addW;
    ImGui::SetCursorPos({handsStartX, btnY});
    if (!hasMappings) ImGui::BeginDisabled();
    if (anyEnabled) {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.14f, 0.45f, 0.22f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.18f, 0.60f, 0.28f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f, 0.35f, 0.18f, 1.0f});
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.35f, 0.12f, 0.10f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.55f, 0.18f, 0.14f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_ACCENT);
    }
    if (ImGui::Button(anyEnabled ? TR().handsOn : TR().handsOff, {handsW, 32}))
        if (callbacks.onToggleAll) callbacks.onToggleAll(!anyEnabled);
    ImGui::PopStyleColor(3);
    if (!hasMappings) ImGui::EndDisabled();

    ImGui::SetCursorPos({rightEdge - relW - gap - saveW - gap - addW, btnY});
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.20f, 0.17f, 0.15f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.32f, 0.26f, 0.22f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.45f, 0.34f, 0.28f, 1.0f});
    if (ImGui::Button(TR().reloadSave, {relW, 32}) && callbacks.onReload)
        callbacks.onReload();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR().tooltipReload);
    ImGui::PopStyleColor(3);

    ImGui::SetCursorPos({rightEdge - saveW - gap - addW, btnY});
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.28f, 0.22f, 0.10f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.50f, 0.36f, 0.14f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_TEAL);
    if (ImGui::Button(TR().save, {saveW, 32}) && callbacks.onSave)
        callbacks.onSave();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR().tooltipSave);
    ImGui::PopStyleColor(3);

    ImGui::SetCursorPos({rightEdge - addW, btnY});
    ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT_BRIGHT);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_TEAL);
    if (ImGui::Button(TR().addMapping, {addW, 32}))
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
    ImGui::Text("%s", m_driverConnected ? TR().driverConnected : TR().driverNotConnected);
    ImGui::PopStyleColor();

    if (!m_driverConnected) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("%s", TR().driverHint);
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
        ImGui::SetCursorPosX((avail.x - ImGui::CalcTextSize(TR().noMappingsYet).x) * 0.5f);
        ImGui::Text("%s", TR().noMappingsYet);
        ImGui::SetCursorPosX((avail.x - ImGui::CalcTextSize(TR().pressAddToBegin).x) * 0.5f);
        ImGui::Text("%s", TR().pressAddToBegin);
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        return;
    }

    // Single scroll region for all cards — mouse wheel scrolls the whole page
    ImGui::BeginChild("##cards_scroll", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_None);
    // Drag-to-scroll for VR laser pointer
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
        float dy = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y;
        ImGui::SetScrollY(ImGui::GetScrollY() - dy);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 10});

    for (int i = 0; i < (int)m_mappings.size(); i++)
        RenderMappingCard(m_mappings[i], i);

    if (!pending.empty()) {
        if (!m_mappings.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 2);
            ImGui::Text("%s", TR().waitingForDevices);
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
    ImGui::Text("%s", TR().controller);
    ImGui::PopStyleColor();
    ImGui::Text("%s", mapping.controllerSerial[0] ? mapping.controllerSerial : "Unknown");
    // Hand side and hand tracking indicators
    {
        bool isHT = false;
        for (const auto& d : m_allDevices) {
            if (d.index == mapping.controllerIndex &&
                d.deviceClass == 5 /*TrackedDeviceClass_HandTracker*/)
                { isHT = true; break; }
        }
        if (mapping.side == 1 || mapping.side == 2) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
            ImGui::Text("%s", mapping.side == 1 ? TR().leftHand : TR().rightHand);
            ImGui::PopStyleColor();
        }
        if (isHT) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
            ImGui::Text("%s", TR().handTracking);
            ImGui::PopStyleColor();
        }
    }
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
    ImGui::Text("%s", TR().tracker);
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
    if (ImGui::Button(TR().remove, {70, btnH}))
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
    if (ImGui::Button(mapping.enabled ? TR().btnOn : TR().btnOff, {60, btnH})) {
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
    const char* toggleLabel = expanded ? TR().manualOffset : TR().manualOffsetOpen;
    ImGui::PushStyleColor(ImGuiCol_Button,        COL_BG_MID);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f, 0.15f, 0.13f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.30f, 0.18f, 0.14f, 1.0f});
    if (ImGui::Button(toggleLabel, {130, 0})) {
        if (idx < (int)m_offsetExpanded.size())
            m_offsetExpanded[idx] = !m_offsetExpanded[idx];
    }
    ImGui::PopStyleColor(3);

    // Row 2 right-side buttons: [Redirect] [Smooth(1s)] [Quick Cal]
    {
        float rightEdge2 = ImGui::GetWindowContentRegionMax().x;
        float quickW   = 118.0f;
        float smoothW  = 108.0f;
        float redirW   = 140.0f;
        float g2       = 6.0f;
        bool  isSmoothing = (smoothCalibCtrlIdx == mapping.controllerIndex);

        // Redirect Mode
        ImGui::SameLine();
        ImGui::SetCursorPosX(rightEdge2 - quickW - g2 - smoothW - g2 - redirW);
        bool redir = mapping.redirectMode;
        if (redir) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.10f,0.25f,0.45f,1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.15f,0.35f,0.60f,1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.20f,0.45f,0.75f,1.0f});
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.18f,0.13f,0.12f,1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.28f,0.18f,0.15f,1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.38f,0.22f,0.18f,1.0f});
        }
        char redirLbl[64];
        snprintf(redirLbl, sizeof(redirLbl), "%s###redir", TR().redirectMode);
        if (ImGui::Button(redirLbl, {redirW, 0})) {
            mapping.redirectMode = !mapping.redirectMode;
            if (callbacks.onSetMapping) callbacks.onSetMapping(mapping);
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR().tooltipRedirectMode);

        // Smooth Cal — real-time offset adjustment via tracker movement
        if (callbacks.onCalibrateSmooth) {
            ImGui::SameLine(0, g2);
            ImGui::SetCursorPosX(rightEdge2 - quickW - g2 - smoothW);
            char lbl[48];
            if (isSmoothing) {
                snprintf(lbl, sizeof(lbl), "%s###sm%u", TR().stopAdjust, mapping.controllerIndex);
                float pulse = 0.55f + 0.25f * sinf((float)ImGui::GetTime() * 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.0f, pulse*0.6f, 0.0f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, pulse*0.8f, 0.0f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.0f, 0.60f,      0.0f, 1.0f});
                if (ImGui::Button(lbl, {smoothW, 0}))
                    callbacks.onCalibrateSmooth(
                        mapping.controllerIndex, mapping.trackerIndex, mapping.enabled);
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TR().tooltipSmoothCalActive);
            } else {
                snprintf(lbl, sizeof(lbl), "%s###sm%u", TR().smoothCal, mapping.controllerIndex);
                if (ImGui::Button(lbl, {smoothW, 0}))
                    callbacks.onCalibrateSmooth(
                        mapping.controllerIndex, mapping.trackerIndex, mapping.enabled);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TR().tooltipSmoothCalIdle);
            }
        }

        // Quick Calibrate
        if (callbacks.onCalibratePosition) {
            ImGui::SameLine(0, g2);
            ImGui::SetCursorPosX(rightEdge2 - quickW);
            char qcLbl[64];
            snprintf(qcLbl, sizeof(qcLbl), "%s###qc", TR().quickCal);
            if (ImGui::Button(qcLbl, {quickW, 0}))
                callbacks.onCalibratePosition(
                    mapping.controllerIndex, mapping.trackerIndex, mapping.enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR().tooltipQuickCal);
        }
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
    const char* statusLabel = TR().waitingForDevicesStatus;
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
    if (ImGui::SmallButton(TR().remove)) {
        if (callbacks.onRemoveSavedMapping)
            callbacks.onRemoveSavedMapping(pm.controllerSerial);
    }
    ImGui::PopStyleColor(4);

    ImGui::SameLine(0, 8);

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.15f,0.22f,0.28f,1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.20f,0.30f,0.38f,1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.15f,0.35f,0.50f,1.0f});
    if (ImGui::SmallButton(TR().retry)) {
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
// Helper: one axis row for origin offset.
// Returns true if value changed.  Buttons: ±0.01, ±0.1, ±1 m, plus a typed input.
static bool OriginOffsetRow(const char* axisLabel, float& val) {
    bool changed = false;
    ImGui::PushID(axisLabel);

    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::Text("%s", axisLabel);
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 6);

    const float btnW = 36.0f, btnH = 22.0f;
    struct Step { float d; const char* lbl; };
    Step neg[] = { {-1.0f, "-1"}, {-0.1f, "-.1"}, {-0.01f, "-.01"} };
    for (auto& s : neg) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.28f,0.12f,0.10f,1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.50f,0.18f,0.14f,1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_ACCENT);
        if (ImGui::Button(s.lbl, {btnW, btnH})) { val += s.d; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 2);
    }

    ImGui::SetNextItemWidth(72.0f);
    if (ImGui::InputFloat("##v", &val, 0.0f, 0.0f, "%.3f")) changed = true;
    ImGui::SameLine(0, 2);

    Step pos[] = { {+0.01f, "+.01"}, {+0.1f, "+.1"}, {+1.0f, "+1"} };
    for (int i = 0; i < 3; i++) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.10f,0.22f,0.14f,1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.16f,0.40f,0.22f,1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_GREEN);
        if (ImGui::Button(pos[i].lbl, {btnW, btnH})) { val += pos[i].d; changed = true; }
        ImGui::PopStyleColor(3);
        if (i < 2) ImGui::SameLine(0, 2);
    }

    ImGui::PopID();
    return changed;
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
    ImGui::Text("%s", TR().positionM);
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (ImGui::SmallButton(TR().resetPos)) {
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
    ImGui::Text("%s", TR().rotationDeg);
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (ImGui::SmallButton(TR().resetRot)) {
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

    // ── Origin Offset ──────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, COL_BORDER);
    ImGui::SetCursorPosX(14);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::SetCursorPosX(14);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEAL);
    ImGui::Text("%s", TR().worldOffset);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::Text("%s", TR().worldOffsetDesc);
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 80);
    if (ImGui::SmallButton(TR().resetAll)) {
        mapping.offset.originOffset = {0, 0, 0};
        mapping.offset.originRot    = {1, 0, 0, 0};
        changed = true;
    }

    // Euler decomposition for originRot before entering groups
    auto& q2 = mapping.offset.originRot;
    float sinr2 = 2*(q2.w*q2.x + q2.y*q2.z);
    float cosr2 = 1 - 2*(q2.x*q2.x + q2.y*q2.y);
    float oRoll  = atan2f(sinr2, cosr2) * (180.0f / 3.14159265f);
    float sinp2  = 2*(q2.w*q2.y - q2.z*q2.x);
    float oPitch = (fabsf(sinp2) >= 1) ? copysignf(90.0f, sinp2) : asinf(sinp2) * (180.0f / 3.14159265f);
    float siny2  = 2*(q2.w*q2.z + q2.x*q2.y);
    float cosy2  = 1 - 2*(q2.y*q2.y + q2.z*q2.z);
    float oYaw   = atan2f(siny2, cosy2) * (180.0f / 3.14159265f);

    // Side-by-side: Position (left) | Rotation (right)
    ImGui::SetCursorPosX(14);
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::Text("%s", TR().positionM);
    ImGui::PopStyleColor();
    ImGui::PushID("wp");
    if (OriginOffsetRow("X", mapping.offset.originOffset.x)) changed = true;
    ImGui::Spacing();
    if (OriginOffsetRow("Y", mapping.offset.originOffset.y)) changed = true;
    ImGui::Spacing();
    if (OriginOffsetRow("Z", mapping.offset.originOffset.z)) changed = true;
    ImGui::PopID();
    ImGui::EndGroup();

    ImGui::SameLine(0, 20);

    // Rotation rows: buttons apply world-axis delta directly to the quat to avoid gimbal lock.
    // InputFloat accepts typed values and falls back to euler reconversion (rare in VR).
    bool rotChanged = false;

    // Apply delta degrees around a world-space axis (ax,ay,az) to q2
    auto applyRotDelta = [&](float deg, float ax, float ay, float az) {
        float a = deg * (3.14159265f / 180.0f) * 0.5f;
        float c = cosf(a), s = sinf(a);
        Quat dq = {c, s*ax, s*ay, s*az};
        Quat& q = q2;
        q2 = { dq.w*q.w - dq.x*q.x - dq.y*q.y - dq.z*q.z,
               dq.w*q.x + dq.x*q.w + dq.y*q.z - dq.z*q.y,
               dq.w*q.y - dq.x*q.z + dq.y*q.w + dq.z*q.x,
               dq.w*q.z + dq.x*q.y - dq.y*q.x + dq.z*q.w };
        rotChanged = true;
    };

    // One row: buttons apply world-axis rotation directly; InputFloat returns true if edited.
    // ax/ay/az = world-space rotation axis for this display angle.
    auto rotRowFn = [&](const char* label, float& displayDeg,
                        float ax, float ay, float az) -> bool {
        bool inputEdited = false;
        ImGui::PushID(label);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("%s", label);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 6);
        const float btnW = 34.0f, btnH = 22.0f;
        struct Step { float d; const char* l; };
        Step neg[] = {{-45.f,"-45"},{-5.f,"-5"},{-1.f,"-1"}};
        for (auto& st : neg) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.28f,0.12f,0.10f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.50f,0.18f,0.14f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_ACCENT);
            if (ImGui::Button(st.l, {btnW, btnH})) applyRotDelta(st.d, ax, ay, az);
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, 2);
        }
        ImGui::SetNextItemWidth(58.0f);
        if (ImGui::InputFloat("##v", &displayDeg, 0.f, 0.f, "%.1f")) inputEdited = true;
        ImGui::SameLine(0, 2);
        Step pos[] = {{+1.f,"+1"},{+5.f,"+5"},{+45.f,"+45"}};
        for (int i = 0; i < 3; i++) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.10f,0.22f,0.14f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.16f,0.40f,0.22f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_GREEN);
            if (ImGui::Button(pos[i].l, {btnW, btnH})) applyRotDelta(pos[i].d, ax, ay, az);
            ImGui::PopStyleColor(3);
            if (i < 2) ImGui::SameLine(0, 2);
        }
        ImGui::PopID();
        return inputEdited;
    };

    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::Text("%s", TR().rotationDeg);
    ImGui::PopStyleColor();
    // Pitch=Y axis, Yaw=Z axis, Roll=X axis (matches euler extraction convention)
    bool inputP = rotRowFn("P", oPitch, 0, 1, 0);
    ImGui::Spacing();
    bool inputY = rotRowFn("Y", oYaw,   0, 0, 1);
    ImGui::Spacing();
    bool inputR = rotRowFn("R", oRoll,  1, 0, 0);
    ImGui::EndGroup();

    if (inputP || inputY || inputR) {
        // Typed directly — reconvert euler to quat
        float p2 = oPitch * (3.14159265f / 180.0f) * 0.5f;
        float y2 = oYaw   * (3.14159265f / 180.0f) * 0.5f;
        float r2 = oRoll  * (3.14159265f / 180.0f) * 0.5f;
        q2.w = cosf(r2)*cosf(p2)*cosf(y2) + sinf(r2)*sinf(p2)*sinf(y2);
        q2.x = sinf(r2)*cosf(p2)*cosf(y2) - cosf(r2)*sinf(p2)*sinf(y2);
        q2.y = cosf(r2)*sinf(p2)*cosf(y2) + sinf(r2)*cosf(p2)*sinf(y2);
        q2.z = cosf(r2)*cosf(p2)*sinf(y2) - sinf(r2)*sinf(p2)*cosf(y2);
        rotChanged = true;
    }
    if (rotChanged) changed = true;
    ImGui::Spacing();

    if (changed && callbacks.onSetOffset)
        callbacks.onSetOffset(mapping.controllerIndex, mapping.offset);
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::RenderAddMappingPopup() {
    if (m_showAddPopup)
        ImGui::OpenPopup("###add_mapping_popup");

    ImGui::SetNextWindowSize({440, 420}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 18});

    // Triple-hash keeps a stable popup ID regardless of translated title
    char addPopupTitle[128];
    snprintf(addPopupTitle, sizeof(addPopupTitle), "%s###add_mapping_popup", TR().newDeviceMapping);
    if (ImGui::BeginPopupModal(addPopupTitle, &m_showAddPopup,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT_BRIGHT);
        ImGui::Text("%s", TR().newDeviceMapping);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Show-all-devices toggle
        ImGui::Checkbox(TR().showAllDevices, &m_showAllDevices);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When enabled, both dropdowns show all connected devices\n"
                              "(controllers and trackers) for non-standard setups.");
        ImGui::Spacing();

        // Active lists depend on toggle
        auto& ctrlList = m_showAllDevices ? m_allDevices : m_controllers;
        auto& trkrList = m_showAllDevices ? m_allDevices : m_trackers;
        // Clamp selections
        if (!ctrlList.empty()) m_addCtrlSel = std::min(m_addCtrlSel, (int)ctrlList.size()-1);
        else m_addCtrlSel = 0;
        if (!trkrList.empty()) m_addTrkrSel = std::min(m_addTrkrSel, (int)trkrList.size()-1);
        else m_addTrkrSel = 0;

        // Controller source selection: specific device or auto-follow hand role
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("%s", TR().controllerSource);
        ImGui::PopStyleColor();
        ImGui::RadioButton(TR().specificDevice, &m_addHandSide, 0);
        ImGui::RadioButton(TR().leftHandAuto,   &m_addHandSide, 1);
        ImGui::RadioButton(TR().rightHandAuto,  &m_addHandSide, 2);
        if (m_addHandSide != 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            ImGui::TextWrapped("%s", m_addHandSide == 1 ? TR().mapsToLeft : TR().mapsToRight);
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();

        // Helper: display name for a device (recognises virtual controllers by serial prefix)
        auto deviceDisplayName = [](const TrackedDeviceInfo& d, char* buf, size_t sz) {
            if (strncmp(d.serial, "VirtCtrl_", 9) == 0) {
                bool isLeft = d.serial[9] == 'L';
                snprintf(buf, sz, "[VIRTUAL] %s Hand (tracker as controller)",
                         isLeft ? "Left" : "Right");
            } else {
                snprintf(buf, sz, "%s  —  %s  [%s]", d.serial, d.modelNumber,
                         d.deviceClass == 2 ? "ctrl" : "tracker");
            }
        };

        // Controller selector (target device) — only when using specific device mode
        if (m_addHandSide == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            ImGui::Text("%s", m_showAllDevices ? TR().targetDevice : TR().selectController);
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            {
                char hdr[96] = "(none found)";
                if (!ctrlList.empty()) deviceDisplayName(ctrlList[m_addCtrlSel], hdr, sizeof(hdr));
                if (ImGui::BeginCombo("##ctrl", hdr)) {
                    for (int i = 0; i < (int)ctrlList.size(); i++) {
                        bool sel = (i == m_addCtrlSel);
                        char label[96];
                        deviceDisplayName(ctrlList[i], label, sizeof(label));
                        if (ImGui::Selectable(label, sel)) m_addCtrlSel = i;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::Spacing();
        }

        // Tracker selector (source device)
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("%s", m_showAllDevices ? TR().sourceDevice : TR().selectTracker);
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        {
            char hdr[96] = "(none found)";
            if (!trkrList.empty()) deviceDisplayName(trkrList[m_addTrkrSel], hdr, sizeof(hdr));
            if (ImGui::BeginCombo("##trkr", hdr)) {
                for (int i = 0; i < (int)trkrList.size(); i++) {
                    bool sel = (i == m_addTrkrSel);
                    char label[96];
                    deviceDisplayName(trkrList[i], label, sizeof(label));
                    if (ImGui::Selectable(label, sel)) m_addTrkrSel = i;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool canAdd = !trkrList.empty() && (m_addHandSide != 0 || !ctrlList.empty());

        if (!canAdd) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_RED);
            ImGui::Text("%s", m_addHandSide == 0 ? TR().noCtrlOrTracker : TR().noTracker);
            ImGui::PopStyleColor();
        }
        if (m_addHandError) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_RED);
            ImGui::TextWrapped("%s", TR().handRoleNotFound);
            ImGui::PopStyleColor();
        }

        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 180 + ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(TR().cancel, {80, 30})) {
            m_addHandError = false;
            m_showAddPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, 10);
        if (!canAdd) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT_BRIGHT);
        if (ImGui::Button(TR().addMappingBtn, {90, 30}) && canAdd) {
            if (m_addHandSide != 0) {
                // Resolve the device currently holding this hand role
                uint32_t handIdx = 0xFFFFFFFFu;
                if (callbacks.onGetHandRoleDevice)
                    handIdx = callbacks.onGetHandRoleDevice((uint32_t)m_addHandSide);
                if (handIdx == 0xFFFFFFFFu) {
                    m_addHandError = true; // keep popup open, show error
                } else {
                    char serial[32] = {};
                    for (const auto& d : m_allDevices)
                        if (d.index == handIdx) { strncpy_s(serial, d.serial, 31); break; }
                    DeviceMapping mapping;
                    mapping.offset.rot       = {1, 0, 0, 0};
                    mapping.controllerIndex  = handIdx;
                    mapping.trackerIndex     = trkrList[m_addTrkrSel].index;
                    strncpy_s(mapping.controllerSerial, serial, 31);
                    strncpy_s(mapping.trackerSerial, trkrList[m_addTrkrSel].serial, 31);
                    mapping.enabled = false;
                    mapping.side    = (uint32_t)m_addHandSide;
                    if (callbacks.onSetMapping) callbacks.onSetMapping(mapping);
                    m_addHandError = false;
                    m_showAddPopup = false;
                    ImGui::CloseCurrentPopup();
                }
            } else {
                DeviceMapping mapping;
                mapping.offset.rot = {1, 0, 0, 0};
                mapping.controllerIndex = ctrlList[m_addCtrlSel].index;
                mapping.trackerIndex    = trkrList[m_addTrkrSel].index;
                strncpy_s(mapping.controllerSerial, ctrlList[m_addCtrlSel].serial, 31);
                strncpy_s(mapping.trackerSerial,    trkrList[m_addTrkrSel].serial, 31);
                mapping.enabled = false;
                if (callbacks.onSetMapping) callbacks.onSetMapping(mapping);
                m_addHandError = false;
                m_showAddPopup = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::PopStyleColor(2);
        if (!canAdd) ImGui::EndDisabled();

        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}
