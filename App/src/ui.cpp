#include "ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unistd.h>

#include "emoji_font.h"
#include "imgui_internal.h" // IM_PI, FindRenderedTextEnd

namespace ui {

Fonts fonts;
float root_px = 15.0f;

/* ------------------------------------------------------------------ */
/* Fonts                                                               */
/* ------------------------------------------------------------------ */

namespace {

bool exists(const std::string& path) {
    return access(path.c_str(), R_OK) == 0;
}

// Fonts next to the binary (installed copy) or in the build directory.
std::string font_file(const char* name) {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        std::string dir(exe);
        dir = dir.substr(0, dir.find_last_of('/'));
        if (exists(dir + "/fonts/" + name)) return dir + "/fonts/" + name;
    }
#ifdef FONT_DIR
    if (exists(std::string(FONT_DIR) + "/" + name)) return std::string(FONT_DIR) + "/" + name;
#endif
    return "";
}

ImFont* add_font(const char* file, const char* const* fallbacks, float size) {
    ImGuiIO& io = ImGui::GetIO();
    std::string path = font_file(file);
    for (const char* const* f = fallbacks; path.empty() && *f; f++)
        if (exists(*f)) path = *f;
    ImFont* font = path.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(path.c_str(), size);
    if (!font) {
        std::fprintf(stderr, "warning: font %s not found, using ImGui's default\n", file);
        font = io.Fonts->AddFontDefault();
    }

    // Emoji: Noto Color Emoji (bitmap build) like the browser; otherwise a
    // monochrome emoji font if one is installed.
    static const std::string color = font_file("NotoColorEmoji.ttf");
    if (!color.empty() && merge_color_emoji(color.c_str(), size, EM)) return font;
    static const char* mono[] = {
        "/usr/share/fonts/google-noto-emoji-fonts/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/gdouros-symbola/Symbola.ttf",
    };
    ImFontConfig merge;
    merge.MergeMode = true;
    for (const char* m : mono)
        if (exists(m) && io.Fonts->AddFontFromFileTTF(m, size, &merge)) break;
    return font;
}

} // namespace

void load_fonts() {
    const float size = 15.0f * EM;
    static const char* sans[] = {
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        nullptr,
    };
    static const char* sans_bold[] = {
        "/usr/share/fonts/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        nullptr,
    };
    static const char* sans_italic[] = {
        "/usr/share/fonts/noto/NotoSans-Italic.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Italic.ttf",
        "/usr/share/fonts/google-noto-vf/NotoSans-Italic[wght].ttf",
        nullptr,
    };
    static const char* mono[] = {
        "/usr/share/fonts/google-noto-vf/NotoSansMono[wght].ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        nullptr,
    };
    fonts.regular = add_font("NotoSans-Regular.ttf", sans, size); // first = ImGui's default
    fonts.medium = add_font("NotoSans-Medium.ttf", sans, size);
    fonts.semibold = add_font("NotoSans-SemiBold.ttf", sans_bold, size);
    fonts.bold = add_font("NotoSans-Bold.ttf", sans_bold, size);
    fonts.italic = add_font("NotoSans-Italic.ttf", sans_italic, size);
    fonts.mono = add_font("NotoSansMono-Regular.ttf", mono, size);
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

ImU32 col(const ImVec4& c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

float text_width(const Font& f, std::string_view s) {
    return f.face->CalcTextSizeA(f.px * EM, FLT_MAX, 0, s.data(), s.data() + s.size()).x;
}

void text(ImDrawList* dl, ImVec2 p, const Font& f, ImU32 color, std::string_view s) {
    // The glyph box (ascent + descent) is centered in the CSS line box.
    float size = f.px * EM;
    dl->AddText(f.face, size, ImVec2(std::round(p.x), std::round(p.y + (f.line() - size) / 2)), color, s.data(),
                s.data() + s.size());
}

float spaced(ImDrawList* dl, ImVec2 p, const Font& f, ImU32 color, std::string_view s, float spacing) {
    float x = p.x;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        std::string_view ch = s.substr(i, len);
        if (dl) text(dl, ImVec2(x, p.y), f, color, ch);
        x += text_width(f, ch) + spacing;
        i += len;
    }
    return x - p.x - (s.empty() ? 0 : spacing);
}

std::vector<Line> wrap(const Font& f, std::string_view s, float width) {
    std::vector<Line> out;
    const float size = f.px * EM;
    size_t start = 0;
    while (true) {
        size_t nl = s.find('\n', start);
        std::string_view para = s.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        const char* b = para.data();
        const char* e = b + para.size();
        if (b == e) out.push_back({ para, 0 });
        while (b < e) {
            const char* cut = width > 0 ? f.face->CalcWordWrapPosition(size, b, e, width) : e;
            if (cut == b) cut = b + 1; // always make progress
            std::string_view line(b, static_cast<size_t>(cut - b));
            while (!line.empty() && line.back() == ' ') line.remove_suffix(1); // trailing spaces hang
            out.push_back({ line, text_width(f, line) });
            b = cut;
            while (b < e && *b == ' ') b++; // next line starts at the next word
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return out;
}

ImVec2 flow(ImDrawList* dl, ImVec2 p, const std::vector<Run>& runs, float width, float line_h) {
    float x = 0, y = 0, max_x = 0;
    for (const Run& r : runs) {
        // Words keep their trailing space, so a line can break after it.
        size_t i = 0;
        while (i < r.text.size()) {
            size_t sp = r.text.find(' ', i);
            size_t end = sp == std::string::npos ? r.text.size() : sp + 1;
            std::string_view word(r.text.data() + i, end - i);
            std::string_view visible = word;
            while (!visible.empty() && visible.back() == ' ') visible.remove_suffix(1);
            float w = text_width(r.font, word), vw = text_width(r.font, visible);
            if (x > 0 && x + vw > width) {
                x = 0;
                y += line_h;
            }
            if (dl) {
                // Baselines line up across fonts: center each run's own line
                // box on the shared one.
                Font f = r.font;
                f.lh = line_h / f.px;
                text(dl, ImVec2(p.x + x, p.y + y), f, r.color, visible);
            }
            max_x = std::max(max_x, x + vw);
            x += w;
            i = end;
        }
    }
    return ImVec2(max_x, y + line_h);
}

/* ------------------------------------------------------------------ */
/* Shapes                                                              */
/* ------------------------------------------------------------------ */

void rounded_rect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float tl, float tr, float br, float bl) {
    auto corner = [&](ImVec2 c, float r, float a0, float a1, ImVec2 sharp) {
        if (r <= 0.5f) dl->PathLineTo(sharp);
        else dl->PathArcTo(c, r, a0, a1);
    };
    corner(ImVec2(a.x + tl, a.y + tl), tl, IM_PI, IM_PI * 1.5f, a);
    corner(ImVec2(b.x - tr, a.y + tr), tr, IM_PI * 1.5f, IM_PI * 2.0f, ImVec2(b.x, a.y));
    corner(ImVec2(b.x - br, b.y - br), br, 0, IM_PI * 0.5f, b);
    corner(ImVec2(a.x + bl, b.y - bl), bl, IM_PI * 0.5f, IM_PI, ImVec2(a.x, b.y));
    dl->PathFillConvex(color);
}

void shadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float dy, float blur, float alpha) {
    // A few expanding layers approximate the Gaussian edge.
    const int steps = std::max(1, static_cast<int>(std::ceil(blur)));
    for (int i = steps; i >= 1; i--) {
        float grow = blur * i / steps * 0.5f;
        float a_i = alpha / steps;
        dl->AddRectFilled(ImVec2(a.x - grow, a.y + dy - grow), ImVec2(b.x + grow, b.y + dy + grow),
                          IM_COL32(0, 0, 0, static_cast<int>(255 * a_i)), radius + grow);
    }
}

uint32_t name_hue(const std::string& name) {
    uint32_t h = 2166136261u; // FNV-1a, same as nameHue() in the web client
    for (unsigned char ch : name) h = (h ^ ch) * 16777619u;
    return h % 360;
}

ImU32 hsl(float h, float s, float l, float alpha) {
    auto f = [&](float n) {
        float k = std::fmod(n + h / 30.0f, 12.0f);
        float a = s * std::min(l, 1 - l);
        return l - a * std::max(-1.0f, std::min({ k - 3, 9 - k, 1.0f }));
    };
    return ImGui::ColorConvertFloat4ToU32(ImVec4(f(0), f(8), f(4), alpha));
}

ImU32 name_color(const std::string& name, float alpha) {
    return hsl(static_cast<float>(name_hue(name)), 0.60f, 0.55f, alpha);
}

// First character, upper-cased with Turkish rules (like toLocaleUpperCase('tr')).
static std::string initial(const std::string& name) {
    size_t b = name.find_first_not_of(' ');
    if (b == std::string::npos) return "?";
    unsigned char c = static_cast<unsigned char>(name[b]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    std::string first = name.substr(b, len);
    static const char* map[][2] = {
        { "ç", "Ç" }, { "ğ", "Ğ" }, { "ı", "I" }, { "i", "İ" }, { "ö", "Ö" }, { "ş", "Ş" }, { "ü", "Ü" },
    };
    for (auto& m : map)
        if (first == m[0]) return m[1];
    if (len == 1 && c >= 'a' && c <= 'z') first[0] = static_cast<char>(c - 32);
    return first;
}

void avatar(ImDrawList* dl, ImVec2 p, float size, const std::string& name, float alpha) {
    ImVec2 c(p.x + size / 2, p.y + size / 2);
    dl->AddCircleFilled(c, size / 2, hsl(static_cast<float>(name_hue(name)), 0.55f, 0.45f, alpha), 48);
    // font-size 0.45 * size, weight 700, line-height 1, centered.
    Font f{ fonts.bold, size * 0.45f, 1.0f };
    std::string ch = initial(name);
    float w = text_width(f, ch);
    text(dl, ImVec2(c.x - w / 2, c.y - f.line() / 2), f, IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), ch);
}

float status(ImDrawList* dl, ImVec2 p, const Palette& pal, const ImVec4& dot, std::string_view s) {
    Font f{ fonts.regular, rem(0.85f) };
    // inline-block 8 x 8 with vertical-align: middle sits on the x-height.
    float cy = p.y + f.line() / 2 + 0.5f;
    if (dl) {
        dl->AddCircleFilled(ImVec2(p.x + 4, cy), 4, col(dot), 16);
        text(dl, ImVec2(p.x + 14, p.y), f, col(pal.muted), s);
    }
    return 14 + text_width(f, s);
}

/* ------------------------------------------------------------------ */
/* Widgets                                                             */
/* ------------------------------------------------------------------ */

bool button(const char* label, const Palette& p, Button kind, const ButtonOpts& o) {
    const char* end = ImGui::FindRenderedTextEnd(label);
    std::string_view text_part(label, static_cast<size_t>(end - label));
    float px = o.px > 0 ? o.px : rem(1);
    Font f{ kind == Button::Primary ? fonts.semibold : fonts.medium, px };
    float w = o.width > 0 ? o.width : text_width(f, text_part) + 2 * o.pad.x;
    float h = f.line() + 2 * o.pad.y;

    ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    bool pressed = ImGui::InvisibleButton("##b", ImVec2(w, h)) && !o.disabled;
    bool hovered = ImGui::IsItemHovered() && !o.disabled;
    bool held = ImGui::IsItemActive() && !o.disabled;
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(a.x + w, a.y + h);
    float dy = held ? 1.0f : 0.0f; // :active moves the button down 1px
    ImU32 fg;
    if (kind == Button::Primary) {
        ImVec4 bg = o.disabled ? p.muted : hovered ? p.accent_hover : p.accent;
        dl->AddRectFilled(ImVec2(a.x, a.y + dy), ImVec2(b.x, b.y + dy), col(bg, o.disabled ? 0.5f : 1), o.radius);
        fg = col(p.accent_text, o.disabled ? 0.5f : 1);
    } else {
        if (hovered) dl->AddRectFilled(ImVec2(a.x, a.y + dy), ImVec2(b.x, b.y + dy), col(p.accent_soft), o.radius);
        fg = col(o.disabled ? p.muted : p.accent);
    }
    float tw = text_width(f, text_part);
    text(dl, ImVec2(a.x + (w - tw) / 2, a.y + o.pad.y + dy), f, fg, text_part);
    return pressed;
}

bool icon_button(const char* id, const char* glyph, const Palette& p, bool disabled, float glyph_px) {
    const float size = 38;
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    bool pressed = ImGui::InvisibleButton("##i", ImVec2(size, size)) && !disabled;
    bool hovered = ImGui::IsItemHovered() && !disabled;
    bool held = ImGui::IsItemActive() && !disabled;
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dy = held ? 1.0f : 0.0f;
    ImVec2 c(a.x + size / 2, a.y + size / 2 + dy);
    if (hovered) dl->AddCircleFilled(c, size / 2, col(p.accent_soft), 32);
    Font f{ fonts.regular, glyph_px > 0 ? glyph_px : rem(1.15f) };
    float w = text_width(f, glyph);
    // Emoji glyphs are colored and ignore the tint; text glyphs use it.
    ImVec4 fg = hovered ? p.text : p.muted;
    text(dl, ImVec2(c.x - w / 2, c.y - f.line() / 2), f, col(fg, disabled ? 0.5f : 1), glyph);
    return pressed;
}

bool toggle(const char* id, bool* value, const Palette& p) {
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    bool pressed = ImGui::InvisibleButton("##t", ImVec2(40, 22));
    ImGui::PopID();
    if (pressed) *value = !*value;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + 40, a.y + 22), col(*value ? p.accent : p.border), 11);
    float knob_x = a.x + 3 + 8 + (*value ? 18 : 0);
    dl->AddCircleFilled(ImVec2(knob_x, a.y + 11), 8, IM_COL32_WHITE, 24);
    return pressed;
}

bool segmented(const char* id, std::string& value, const std::vector<std::pair<std::string, std::string>>& options,
               const Palette& p) {
    Font f{ fonts.medium, rem(0.85f) };
    // padding 3px around buttons of min-width 38px, padding 5px 10px.
    std::vector<float> widths;
    float total = 6 + 2;
    for (auto& o : options) {
        widths.push_back(std::max(38.0f, text_width(Font{ fonts.semibold, f.px }, o.second) + 20));
        total += widths.back();
    }
    float bh = f.line() + 10, h = bh + 6 + 2;
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + total, a.y + h), col(p.panel_alt), 10);
    dl->AddRect(a, ImVec2(a.x + total, a.y + h), col(p.border), 10);
    bool changed = false;
    float x = a.x + 4;
    ImGui::PushID(id);
    for (size_t i = 0; i < options.size(); i++) {
        ImGui::SetCursorScreenPos(ImVec2(x, a.y + 4));
        ImGui::PushID(static_cast<int>(i));
        bool clicked = ImGui::InvisibleButton("##s", ImVec2(widths[i], bh));
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        bool on = value == options[i].first;
        if (clicked && !on) {
            value = options[i].first;
            changed = true;
            on = true;
        }
        if (on) dl->AddRectFilled(ImVec2(x, a.y + 4), ImVec2(x + widths[i], a.y + 4 + bh), col(p.accent), 7);
        Font lf{ fonts.semibold, f.px };
        float tw = text_width(lf, options[i].second);
        ImVec4 fg = on ? p.accent_text : hovered ? p.text : p.muted;
        text(dl, ImVec2(x + (widths[i] - tw) / 2, a.y + 4 + 5), lf, col(fg), options[i].second);
        x += widths[i];
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(a);
    ImGui::Dummy(ImVec2(total, h));
    return changed;
}

float input_height() {
    return rem(1) * 1.45f + 2 * 10 + 2;
}

bool input(const char* id, const char* hint, char* buf, size_t size, float width, const Palette& p, bool pill,
           ImGuiInputTextFlags flags, ImGuiInputTextCallback cb, void* user) {
    const float h = input_height();
    const float pad_x = pill ? 16 : 14;
    const float radius = pill ? h / 2 : 10;
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ImGui draws the text; the frame (border, focus ring) is drawn here.
    float glyph = ImGui::GetStyle().FontSizeBase;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad_x, (h - glyph) / 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, radius);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, p.muted);
    dl->AddRectFilled(a, ImVec2(a.x + width, a.y + h), col(p.panel), radius);
    ImGui::SetNextItemWidth(width);
    bool r = ImGui::InputTextWithHint(id, hint, buf, size, flags, cb, user);
    bool active = ImGui::IsItemActive();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    if (active) {
        // border-color: accent; box-shadow: 0 0 0 3px accent-soft
        dl->AddRect(ImVec2(a.x - 1.5f, a.y - 1.5f), ImVec2(a.x + width + 1.5f, a.y + h + 1.5f), col(p.accent_soft),
                    radius + 1.5f, 0, 3.0f);
        dl->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(a.x + width - 0.5f, a.y + h - 0.5f), col(p.accent),
                    radius, 0, 1.0f);
    } else {
        dl->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(a.x + width - 0.5f, a.y + h - 0.5f), col(p.border),
                    radius, 0, 1.0f);
    }
    return r;
}

} // namespace ui
