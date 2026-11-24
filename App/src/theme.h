// Color themes for the desktop app. The palettes and the rules that derive
// text and hover colors mirror Web/src/theme/themes.js exactly.
#pragma once

#include <string>

#include "imgui.h"
#include "settings.h"

struct Palette {
    bool dark;
    ImVec4 bg, panel, panel_alt, border, text, muted;
    ImVec4 accent, accent_hover, accent_text, accent_soft;
    ImVec4 own_bubble, own_text, other_bubble, other_text;
    ImVec4 dm, danger, ok, warn;
};

struct ThemeInfo {
    const char* id;
    const char* name;
};

// The built-in themes, in display order.
const ThemeInfo* theme_list(int* count);

// "midnight" or "daylight", following the desktop's dark/light preference
// (like prefers-color-scheme in the browser). Used when Settings::theme is "".
const char* system_theme();

// The palette of one theme without user overrides (theme previews).
Palette theme_palette(const std::string& id);

// The selected theme plus the user's color overrides.
Palette resolve_theme(const Settings& s);

// Applies colors, rounding and text size to ImGui.
void apply_style(const Palette& p, const Settings& s);

ImU32 to_u32(const ImVec4& c, float alpha = 1.0f);
ImVec4 rgb_to_vec(int32_t rgb);
int32_t vec_to_rgb(const ImVec4& c);
ImVec4 with_alpha(ImVec4 c, float a);
