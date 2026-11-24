#include "theme.h"

#include <cmath>
#include <cstring>

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

const ImVec4 DARK_TEXT = rgb_to_vec(0x111827);
const ImVec4 WHITE(1, 1, 1, 1);

ImVec4 readable_on(const ImVec4& bg) {
    return luminance(bg) > 0.4f ? DARK_TEXT : WHITE;
}

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
}

ImVec4 with_alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
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

const ThemeInfo* theme_list(int* count) {
    *count = static_cast<int>(sizeof(INFOS) / sizeof(INFOS[0]));
    return INFOS;
}

Palette resolve_theme(const Settings& s) {
    const Def* d = &DEFS[0];
    for (const Def& def : DEFS)
        if (s.theme == def.id) d = &def;

    Palette p;
    p.dark = d->dark;
    p.bg = rgb_to_vec(d->bg);
    p.panel = rgb_to_vec(d->panel);
    p.panel_alt = rgb_to_vec(d->panel_alt);
    p.border = rgb_to_vec(d->border);
    p.text = rgb_to_vec(d->text);
    p.muted = rgb_to_vec(d->muted);
    p.accent = rgb_to_vec(s.accent >= 0 ? s.accent : d->accent);
    p.own_bubble = rgb_to_vec(s.own_bubble >= 0 ? s.own_bubble : d->own_bubble);
    p.other_bubble = rgb_to_vec(s.other_bubble >= 0 ? s.other_bubble : d->other_bubble);
    p.dm = rgb_to_vec(d->dm);
    p.danger = rgb_to_vec(0xef4444);
    p.ok = rgb_to_vec(0x22c55e);
    p.warn = rgb_to_vec(0xf59e0b);

    p.accent_text = readable_on(p.accent);
    p.accent_hover = mix(p.accent, p.dark ? WHITE : ImVec4(0, 0, 0, 1), 0.12f);
    p.own_text = readable_on(p.own_bubble);
    // Same rule as the web client: dark bubble on a dark theme uses the
    // theme's softer text color.
    p.other_text = luminance(p.other_bubble) > 0.4f ? DARK_TEXT : (p.dark ? p.text : WHITE);
    return p;
}

void apply_style(const Palette& p, const Settings& s) {
    ImGuiStyle& st = ImGui::GetStyle();
    const float font_size = st.FontSizeBase;
    st = ImGuiStyle(); // start from defaults so switching themes leaves no traces
    st.FontSizeBase = font_size;
    if (p.dark) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();

    const float k = s.compact ? 0.7f : 1.0f;
    st.WindowPadding = ImVec2(14, 12);
    st.FramePadding = ImVec2(12, 7 * k);
    st.ItemSpacing = ImVec2(10, 8 * k);
    st.WindowRounding = 0;
    st.ChildRounding = 10;
    st.FrameRounding = 10;
    st.PopupRounding = 12;
    st.GrabRounding = 8;
    st.ScrollbarRounding = 8;
    st.ScrollbarSize = 10;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.PopupBorderSize = 1;
    st.FontScaleMain = s.font_size == "small" ? 0.85f : s.font_size == "large" ? 1.15f : 1.0f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.muted;
    c[ImGuiCol_WindowBg] = p.panel;
    c[ImGuiCol_ChildBg] = with_alpha(p.panel, 0);
    c[ImGuiCol_PopupBg] = p.panel;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_FrameBg] = p.panel_alt;
    c[ImGuiCol_FrameBgHovered] = mix(p.panel_alt, p.accent, 0.12f);
    c[ImGuiCol_FrameBgActive] = mix(p.panel_alt, p.accent, 0.2f);
    c[ImGuiCol_TitleBg] = p.panel;
    c[ImGuiCol_TitleBgActive] = p.panel;
    c[ImGuiCol_Button] = mix(p.panel_alt, p.accent, 0.15f);
    c[ImGuiCol_ButtonHovered] = mix(p.panel_alt, p.accent, 0.3f);
    c[ImGuiCol_ButtonActive] = mix(p.panel_alt, p.accent, 0.45f);
    c[ImGuiCol_Header] = with_alpha(p.accent, 0.22f);
    c[ImGuiCol_HeaderHovered] = with_alpha(p.accent, 0.16f);
    c[ImGuiCol_HeaderActive] = with_alpha(p.accent, 0.3f);
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accent_hover;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_ScrollbarBg] = with_alpha(p.panel, 0);
    c[ImGuiCol_ScrollbarGrab] = p.border;
    c[ImGuiCol_ScrollbarGrabHovered] = p.muted;
    c[ImGuiCol_ScrollbarGrabActive] = p.muted;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
    c[ImGuiCol_NavHighlight] = p.accent;
    c[ImGuiCol_TextSelectedBg] = with_alpha(p.accent, 0.35f);
}

bool primary_button(const Palette& p, const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.accent_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.accent);
    ImGui::PushStyleColor(ImGuiCol_Text, p.accent_text);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}
