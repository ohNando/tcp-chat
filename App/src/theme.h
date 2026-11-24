// Color themes for the desktop app. The palettes mirror
// Web/src/theme/themes.js so both clients look alike.
#pragma once

#include "imgui.h"
#include "settings.h"

struct Palette {
    bool dark;
    ImVec4 bg, panel, panel_alt, border, text, muted;
    ImVec4 accent, accent_hover, accent_text;
    ImVec4 own_bubble, own_text, other_bubble, other_text;
    ImVec4 dm, danger, ok, warn;
};

struct ThemeInfo {
    const char* id;
    const char* name;
};

// The built-in themes, in display order.
const ThemeInfo* theme_list(int* count);

// The selected theme plus the user's color overrides.
Palette resolve_theme(const Settings& s);

// Applies colors, rounding, spacing and text size to ImGui.
void apply_style(const Palette& p, const Settings& s);

ImU32 to_u32(const ImVec4& c, float alpha = 1.0f);
ImVec4 rgb_to_vec(int32_t rgb);
int32_t vec_to_rgb(const ImVec4& c);

// A filled button in the accent color with readable text on it.
bool primary_button(const Palette& p, const char* label, const ImVec2& size = ImVec2(0, 0));
