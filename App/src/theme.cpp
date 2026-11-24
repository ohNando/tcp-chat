#include "theme.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui.h"

namespace {

struct Def {
    const char* id;
    const char* name;
    bool dark;
    int32_t bg, panel, panel_alt, border, text, muted, accent, own_bubble, other_bubble, dm;
};

// Keep in sync with Web/src/theme/themes.js.
const Def DEFS[] = {
    { "midnight", "Midnight", true,
      0x0b1220, 0x111a2b, 0x0e1626, 0x1f2a3d, 0xe6ebf5, 0x8a96ab, 0x4f8cff, 0x2b5fd9, 0x1a2436, 0xa78bfa },
    { "daylight", "Daylight", false,
      0xe9edf3, 0xffffff, 0xf4f6fa, 0xdde2ea, 0x1b2333, 0x6b7280, 0x2563eb, 0x3b82f6, 0xffffff, 0x7c3aed },
    { "ocean", "Ocean", true,
      0x061a1f, 0x0b252c, 0x082027, 0x12363f, 0xe2f4f6, 0x7fa7ad, 0x14b8a6, 0x0f766e, 0x10313a, 0xc084fc },
    { "forest", "Forest", true,
      0x0c140e, 0x121d15, 0x0f1912, 0x1e2e22, 0xe5efe6, 0x8aa18f, 0x4ade80, 0x166534, 0x1a291e, 0xc4b5fd },
    { "sunset", "Sunset", false,
      0xfde9dc, 0xfffaf5, 0xfff1e6, 0xf3d9c8, 0x3b2a22, 0x9a7b6c, 0xf97316, 0xe11d48, 0xffffff, 0x8b5cf6 },
    { "lavender", "Lavender", true,
      0x140f1f, 0x1c1530, 0x181228, 0x2c2346, 0xede9fe, 0xa59cc4, 0xa78bfa, 0x7c3aed, 0x251c3d, 0xf0abfc },
};

const ThemeInfo INFOS[] = {
    { "midnight", "Midnight" }, { "daylight", "Daylight" }, { "ocean", "Ocean" },
    { "forest", "Forest" },     { "sunset", "Sunset" },     { "lavender", "Lavender" },
};

// WCAG relative luminance.
float luminance(const ImVec4& c) {
    auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.x) + 0.7152f * lin(c.y) + 0.0722f * lin(c.z);
}

ImVec4 readable_on(const ImVec4& bg) {
    return luminance(bg) > 0.4f ? rgb_to_vec(0x111827) : ImVec4(1, 1, 1, 1);
}

// shade() in themes.js: toward black (amount > 0) or white (amount < 0),
// rounded to whole 0..255 steps like its hex output.
ImVec4 shade(const ImVec4& c, float amount) {
    float target = amount > 0 ? 0.0f : 1.0f, t = std::fabs(amount);
    auto ch = [&](float v) { return std::round((v + (target - v) * t) * 255) / 255; };
    return ImVec4(ch(c.x), ch(c.y), ch(c.z), 1);
}

const Def& find(const std::string& id) {
    for (const Def& d : DEFS)
        if (id == d.id) return d;
    return DEFS[0];
}

Palette build(const Def& d, int32_t accent, int32_t own, int32_t other) {
    Palette p;
    p.dark = d.dark;
    p.bg = rgb_to_vec(d.bg);
    p.panel = rgb_to_vec(d.panel);
    p.panel_alt = rgb_to_vec(d.panel_alt);
    p.border = rgb_to_vec(d.border);
    p.text = rgb_to_vec(d.text);
    p.muted = rgb_to_vec(d.muted);
    p.accent = rgb_to_vec(accent >= 0 ? accent : d.accent);
    p.own_bubble = rgb_to_vec(own >= 0 ? own : d.own_bubble);
    p.other_bubble = rgb_to_vec(other >= 0 ? other : d.other_bubble);
    p.dm = rgb_to_vec(d.dm);
    p.danger = rgb_to_vec(0xef4444);
    p.ok = rgb_to_vec(0x22c55e);
    p.warn = rgb_to_vec(0xf59e0b);

    p.own_text = readable_on(p.own_bubble);
    // Light bubble -> dark text; dark bubble -> the theme's own text color on
    // dark themes (softer than pure white), white otherwise.
    p.other_text = luminance(p.other_bubble) > 0.4f ? rgb_to_vec(0x111827)
                                                    : (d.dark ? p.text : ImVec4(1, 1, 1, 1));
    p.accent_text = readable_on(p.accent);
    p.accent_hover = shade(p.accent, d.dark ? -0.12f : 0.12f);
    p.accent_soft = with_alpha(p.accent, 0.15f);
    return p;
}

} // namespace

ImVec4 rgb_to_vec(int32_t rgb) {
    return ImVec4(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, 1.0f);
}

int32_t vec_to_rgb(const ImVec4& c) {
    auto b = [](float v) { return static_cast<int32_t>(std::lround(std::fmin(1.0f, std::fmax(0.0f, v)) * 255)); };
    return (b(c.x) << 16) | (b(c.y) << 8) | b(c.z);
}

ImU32 to_u32(const ImVec4& c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

ImVec4 with_alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

const ThemeInfo* theme_list(int* count) {
    *count = static_cast<int>(sizeof(INFOS) / sizeof(INFOS[0]));
    return INFOS;
}

const char* system_theme() {
    // Read once: GNOME's color-scheme ("prefer-dark"), else GTK's theme name.
    static const char* cached = [] {
        const char* cmds[] = {
            "gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null",
            "gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null",
        };
        for (const char* cmd : cmds) {
            FILE* f = popen(cmd, "r");
            if (!f) continue;
            char buf[128] = "";
            bool got = std::fgets(buf, sizeof(buf), f) != nullptr;
            pclose(f);
            if (!got || !buf[0]) continue;
            if (std::strstr(buf, "dark") || std::strstr(buf, "Dark")) return "midnight";
            if (std::strstr(buf, "light") || std::strstr(buf, "default")) return "daylight";
        }
        return "midnight";
    }();
    return cached;
}

Palette theme_palette(const std::string& id) {
    return build(find(id.empty() ? system_theme() : id), -1, -1, -1);
}

Palette resolve_theme(const Settings& s) {
    return build(find(s.theme.empty() ? system_theme() : s.theme), s.accent, s.own_bubble, s.other_bubble);
}

void apply_style(const Palette& p, const Settings& s) {
    ui::root_px = s.font_size == "small" ? 13.5f : s.font_size == "large" ? 17.0f : 15.0f;

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle(); // start from defaults so switching themes leaves no traces
    if (p.dark) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();
    st.FontSizeBase = ui::root_px * ui::EM;
    st.FontScaleMain = 1.0f;

    // Layout is placed by hand in CSS pixels; ImGui only lays out popups.
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(14, 10);
    st.ItemSpacing = ImVec2(10, 10);
    st.WindowRounding = 0;
    st.ChildRounding = 0;
    st.FrameRounding = 10;
    st.PopupRounding = 14;
    st.GrabRounding = 8;
    st.ScrollbarRounding = 6;
    st.ScrollbarSize = 8;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 0;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 0;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.muted;
    c[ImGuiCol_WindowBg] = p.bg;
    c[ImGuiCol_ChildBg] = with_alpha(p.panel, 0);
    c[ImGuiCol_PopupBg] = p.panel;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_FrameBg] = p.panel;
    c[ImGuiCol_FrameBgHovered] = p.panel;
    c[ImGuiCol_FrameBgActive] = p.panel;
    c[ImGuiCol_Button] = p.accent;
    c[ImGuiCol_ButtonHovered] = p.accent_hover;
    c[ImGuiCol_ButtonActive] = p.accent_hover;
    c[ImGuiCol_Header] = p.accent_soft;
    c[ImGuiCol_HeaderHovered] = p.accent_soft;
    c[ImGuiCol_HeaderActive] = p.accent_soft;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accent_hover;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_ScrollbarBg] = with_alpha(p.panel, 0);
    c[ImGuiCol_ScrollbarGrab] = p.border;
    c[ImGuiCol_ScrollbarGrabHovered] = p.muted;
    c[ImGuiCol_ScrollbarGrabActive] = p.muted;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.35f);
    c[ImGuiCol_NavCursor] = with_alpha(p.accent, 0);
    c[ImGuiCol_TextSelectedBg] = with_alpha(p.accent, 0.35f);
}
