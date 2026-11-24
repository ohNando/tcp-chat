// Drawing helpers that let the desktop app reproduce the web client's CSS:
// sizes are CSS pixels, text uses the web's fonts and line heights, and the
// widgets look like the web's buttons, inputs and switches.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "theme.h"

namespace ui {

// Noto Sans in the weights the web CSS uses (system-ui is Noto Sans on
// Linux), plus Noto Sans Mono for fingerprints and safety numbers.
struct Fonts {
    ImFont* regular = nullptr;   // 400
    ImFont* medium = nullptr;    // 500
    ImFont* semibold = nullptr;  // 600
    ImFont* bold = nullptr;      // 700
    ImFont* italic = nullptr;
    ImFont* mono = nullptr;
};
extern Fonts fonts;
void load_fonts();

// ImGui sizes a font by ascent + descent, CSS by its em: for Noto Sans an
// ImGui size of 1.362 equals 1 CSS px of font-size.
constexpr float EM = 1.362f;
// Root font size in CSS px: 15 (13.5 / 17 for small / large text).
extern float root_px;
inline float rem(float r) { return r * root_px; }

// A CSS font: family/weight, size in px, line-height factor.
struct Font {
    ImFont* face;
    float px;
    float lh = 1.45f;
    float line() const { return px * lh; }
};

ImU32 col(const ImVec4& c, float alpha = 1.0f);

float text_width(const Font& f, std::string_view s);
// Draws one line; `top_left` is the top of the CSS line box.
void text(ImDrawList* dl, ImVec2 top_left, const Font& f, ImU32 color, std::string_view s);
// Uppercase headings with letter-spacing; returns the width. dl may be null.
float spaced(ImDrawList* dl, ImVec2 top_left, const Font& f, ImU32 color, std::string_view s, float spacing);

// Breaks text into lines like the browser: at spaces, long words anywhere,
// and at "\n" (white-space: pre-wrap).
struct Line {
    std::string_view text;
    float width;
};
std::vector<Line> wrap(const Font& f, std::string_view s, float width);

// Inline text with mixed fonts and colors, wrapped at spaces. Draws when dl
// is not null; returns the size.
struct Run {
    Font font;
    ImU32 color;
    std::string text;
};
ImVec2 flow(ImDrawList* dl, ImVec2 top_left, const std::vector<Run>& runs, float width, float line_height);

// Filled rounded rectangle with a radius per corner.
void rounded_rect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float tl, float tr, float br, float bl);
// box-shadow: 0 dy blur rgba(0, 0, 0, alpha), roughly.
void shadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float dy, float blur, float alpha);

// Avatars: initial on a circle colored by a hash of the name (web: Avatar.jsx).
uint32_t name_hue(const std::string& name);
ImU32 hsl(float h, float s, float l, float alpha = 1.0f);
ImU32 name_color(const std::string& name, float alpha = 1.0f);  // hsl(h 60% 55%)
void avatar(ImDrawList* dl, ImVec2 top_left, float size, const std::string& name, float alpha = 1.0f);

// ".status": 8 px dot, 6 px gap, text in .85rem muted. Returns the width.
float status(ImDrawList* dl, ImVec2 top_left, const Palette& p, const ImVec4& dot, std::string_view text);

/* ---------------- widgets (positioned at the ImGui cursor) ---------------- */

enum class Button { Primary, Link, Icon };
struct ButtonOpts {
    float width = 0;          // 0 = fit the label
    float px = 0;             // font size, 0 = 1rem
    ImVec2 pad = ImVec2(18, 10);
    float radius = 10;
    bool disabled = false;
};
bool button(const char* label, const Palette& p, Button kind, const ButtonOpts& o = ButtonOpts());
// button.icon: a 38 px circle with a glyph, muted until hovered.
bool icon_button(const char* id, const char* glyph, const Palette& p, bool disabled = false, float glyph_px = 0);
// input.toggle: 40 x 22 switch.
bool toggle(const char* id, bool* value, const Palette& p);
// .segmented: options as {value, label}; returns true when changed.
bool segmented(const char* id, std::string& value, const std::vector<std::pair<std::string, std::string>>& options,
               const Palette& p);
// input[type=text], with the accent focus ring. `pill` for the message box.
bool input(const char* id, const char* hint, char* buf, size_t size, float width, const Palette& p, bool pill,
           ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback cb = nullptr, void* user = nullptr);
// The height input() takes.
float input_height();

} // namespace ui
