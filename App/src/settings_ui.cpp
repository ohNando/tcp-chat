#include "settings_ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>

#include "ui.h"

using ui::Font;
using ui::col;
using ui::fonts;
using ui::rem;

namespace {

const float PANEL_W = 380, PAD_X = 20, PAD_TOP = 16, PAD_BOTTOM = 32;

// Layout state: a cursor in panel coordinates (CSS px from the panel's top
// left, scrolling included).
struct Ctx {
    const Palette& p;
    Settings& s;
    WallpaperRenderer& wr;
    float y = PAD_TOP;
    float width = PANEL_W - 2 * PAD_X;
    bool changed = false;
    bool popup_open = false; // a color picker is open: clicks outside close it, not the panel

    ImVec2 at(float x, float yy) const {
        ImVec2 w = ImGui::GetWindowPos();
        return ImVec2(w.x + PAD_X + x, w.y + yy - ImGui::GetScrollY());
    }
    void cursor(float x, float yy) const { ImGui::SetCursorScreenPos(at(x, yy)); }
    ImDrawList* dl() const { return ImGui::GetWindowDrawList(); }
};

// <section>: border-top, padding 14px 0, an uppercase h3.
void section(Ctx& c, const char* title) {
    ImVec2 a = c.at(0, c.y);
    c.dl()->AddLine(ImVec2(a.x, a.y + 0.5f), ImVec2(a.x + c.width, a.y + 0.5f), col(c.p.border));
    c.y += 1 + 14;
    Font h3{ fonts.bold, rem(0.78f) };
    std::string upper(title);
    for (char& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    ui::spaced(c.dl(), c.at(0, c.y), h3, col(c.p.muted), upper, h3.px * 0.06f);
    c.y += h3.line() + 10;
}
void end_section(Ctx& c) { c.y += 14; }

void note(Ctx& c, const char* text) {
    Font f{ fonts.regular, rem(0.78f) };
    c.y += 8;
    for (auto& l : ui::wrap(f, text, c.width)) {
        ui::text(c.dl(), c.at(0, c.y), f, col(c.p.muted), l.text);
        c.y += f.line();
    }
}

/* ---------------- Theme ---------------- */

float card_height() {
    return 2 + 6 + 58 + 6 + Font{ fonts.medium, rem(0.8f) }.line() + 6 + 2;
}

bool theme_card(Ctx& c, float x, float y, float w, const char* id, const char* label, bool selected) {
    Palette t = theme_palette(id); // "" previews whatever the system picks now
    Font name{ fonts.medium, rem(0.8f) };
    float h = card_height();
    c.cursor(x, y);
    ImGui::PushID(id[0] ? id : "system");
    bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = c.dl();
    ImVec2 a = c.at(x, y), b(a.x + w, a.y + h);
    if (hovered) dl->AddRectFilled(a, b, col(c.p.accent_soft), 12);
    if (selected) dl->AddRect(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), col(c.p.accent), 11, 0, 2.0f);

    // .theme-preview: 58 px, 1px border, radius 8, bubbles and an accent bar.
    ImVec2 pa(a.x + 8, a.y + 8), pb(b.x - 8, pa.y + 58);
    dl->AddRectFilled(pa, pb, col(t.panel_alt), 8);
    dl->AddRect(pa, pb, col(t.border), 8);
    float iw = pb.x - pa.x - 2;
    ImVec2 in(pa.x + 1, pa.y + 1);
    dl->AddRectFilled(ImVec2(in.x + 8, in.y + 10), ImVec2(in.x + 8 + iw * 0.55f, in.y + 22), col(t.other_bubble), 6);
    dl->AddRectFilled(ImVec2(in.x + iw - 8 - iw * 0.45f, in.y + 28), ImVec2(in.x + iw - 8, in.y + 40),
                      col(t.own_bubble), 6);
    dl->AddRectFilled(ImVec2(in.x + 8, pb.y - 1 - 6 - 6), ImVec2(in.x + 22, pb.y - 1 - 6), col(t.accent), 3);

    float tw = ui::text_width(name, label);
    ui::text(dl, ImVec2(a.x + (w - tw) / 2, pb.y + 6), name, col(c.p.text), label);
    return clicked;
}

void themes(Ctx& c) {
    section(c, "Theme");
    int n;
    const ThemeInfo* list = theme_list(&n);
    const float gap = 10, w = (c.width - 2 * gap) / 3, h = card_height();
    for (int i = -1; i < n; i++) {
        int k = i + 1;
        float x = (k % 3) * (w + gap), y = c.y + (k / 3) * (h + gap);
        const char* id = i < 0 ? "" : list[i].id;
        const char* label = i < 0 ? "Match system" : list[i].name;
        if (theme_card(c, x, y, w, id, label, c.s.theme == id) && c.s.theme != id) {
            c.s.theme = id;
            c.changed = true;
        }
    }
    c.y += ((n + 1 + 2) / 3) * (h + gap) - gap;
    end_section(c);
}

/* ---------------- Colors ---------------- */

void color_row(Ctx& c, const char* label, int32_t& value, const ImVec4& theme_color) {
    Font body{ fonts.regular, rem(1) };
    Font link{ fonts.medium, rem(0.8f) };
    const float link_w = ui::text_width(link, "Reset") + 12, link_h = link.line() + 8;
    const float row_h = std::max({ body.line(), 30.0f, link_h }) + 12;
    float mid = c.y + row_h / 2;
    ImDrawList* dl = c.dl();
    ui::text(dl, c.at(0, mid - body.line() / 2), body, col(c.p.text), label);

    // input[type=color]: 44 x 30, 1px border, radius 8, the color inside.
    float sx = c.width - link_w - 10 - 44;
    c.cursor(sx, mid - 15);
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("##swatch", ImVec2(44, 30))) ImGui::OpenPopup("picker");
    ImVec2 a = c.at(sx, mid - 15), b(a.x + 44, a.y + 30);
    ImVec4 shown = value >= 0 ? rgb_to_vec(value) : theme_color;
    dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, b.y - 2), col(shown), 6);
    dl->AddRect(a, b, col(c.p.border), 8);

    ImGui::SetNextWindowPos(ImVec2(a.x, b.y + 4), ImGuiCond_Appearing, ImVec2(0.6f, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    if (ImGui::BeginPopup("picker", ImGuiWindowFlags_NoMove)) {
        c.popup_open = true;
        float rgb[3] = { shown.x, shown.y, shown.z };
        ImGui::SetNextItemWidth(220);
        if (ImGui::ColorPicker3("##pick", rgb,
                                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs |
                                    ImGuiColorEditFlags_PickerHueBar)) {
            value = vec_to_rgb(ImVec4(rgb[0], rgb[1], rgb[2], 1));
            c.changed = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    // button.link "Reset", disabled while the theme's color is used.
    c.cursor(c.width - link_w, mid - link_h / 2);
    ui::ButtonOpts o;
    o.px = link.px;
    o.pad = ImVec2(6, 4);
    o.disabled = value < 0;
    if (ui::button("Reset", c.p, ui::Button::Link, o)) {
        value = -1;
        c.changed = true;
    }
    ImGui::PopID();
    c.y += row_h;
}

void colors(Ctx& c) {
    section(c, "Colors");
    Settings base = c.s;
    base.accent = base.own_bubble = base.other_bubble = -1;
    Palette tp = resolve_theme(base);
    color_row(c, "Accent", c.s.accent, tp.accent);
    color_row(c, "My messages", c.s.own_bubble, tp.own_bubble);
    color_row(c, "Their messages", c.s.other_bubble, tp.other_bubble);
    note(c, "Text colors adjust automatically so messages stay readable.");
    end_section(c);
}

/* ---------------- Background ---------------- */

float swatch_height() {
    return 2 + 4 + 48 + 4 + Font{ fonts.medium, rem(0.72f) }.line() + 4 + 2;
}

bool swatch(Ctx& c, float x, float y, float w, const char* id, const char* label) {
    Font f{ fonts.medium, rem(0.72f) };
    float h = swatch_height();
    c.cursor(x, y);
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##wp", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = c.dl();
    ImVec2 a = c.at(x, y), b(a.x + w, a.y + h);
    bool selected = c.s.wallpaper == id;
    if (hovered) dl->AddRectFilled(a, b, col(c.p.accent_soft), 10);
    if (selected) dl->AddRect(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), col(c.p.accent), 9, 0, 2.0f);

    ImVec2 sa(a.x + 6, a.y + 6), sb(b.x - 6, sa.y + 48);
    Settings preview = c.s;
    preview.wallpaper = id;
    if (!c.wr.draw(dl, sa, sb, preview, c.p, false, 6)) dl->AddRectFilled(sa, sb, col(c.p.panel_alt), 6);
    dl->AddRect(sa, sb, col(c.p.border), 6);
    float tw = ui::text_width(f, label);
    ui::text(dl, ImVec2(a.x + (w - tw) / 2, sb.y + 4), f, col(c.p.text), label);
    return clicked;
}

void slider(Ctx& c, const char* label, float* value, float min, float max, float step, const char* fmt, float shown) {
    Font body{ fonts.regular, rem(1) };
    Font out{ fonts.regular, rem(0.8f) };
    const float row_h = std::max(body.line(), 20.0f) + 12, mid = c.y + row_h / 2;
    ImDrawList* dl = c.dl();
    ui::text(dl, c.at(0, mid - body.line() / 2), body, col(c.p.text), label);

    // grid: 60px | 1fr | 48px, gap 10
    float x0 = 70, x1 = c.width - 58, tw = x1 - x0;
    c.cursor(x0, mid - 10);
    ImGui::PushID(label);
    ImGui::InvisibleButton("##range", ImVec2(tw, 20));
    if (ImGui::IsItemActive()) {
        float t = std::clamp((ImGui::GetIO().MousePos.x - c.at(x0, 0).x - 9) / (tw - 18), 0.0f, 1.0f);
        float v = std::round((min + t * (max - min)) / step) * step;
        if (v != *value) {
            *value = v;
            c.changed = true;
        }
    }
    ImGui::PopID();
    // Firefox's range with accent-color: a 4px track (accent up to the
    // thumb, #e9e9ed after it) and an 18px gray thumb with a white ring.
    float t = (*value - min) / (max - min);
    ImVec2 a = c.at(x0, mid);
    float knob = a.x + 9 + t * (tw - 18);
    dl->AddRectFilled(ImVec2(a.x, a.y - 2), ImVec2(a.x + tw, a.y + 2), IM_COL32(0xe9, 0xe9, 0xed, 255), 2);
    if (!c.p.dark) dl->AddRect(ImVec2(a.x, a.y - 2.5f), ImVec2(a.x + tw, a.y + 2.5f), IM_COL32(0x8f, 0x8f, 0x9d, 160), 2.5f);
    dl->AddRectFilled(ImVec2(a.x, a.y - 2), ImVec2(knob, a.y + 2), col(c.p.accent), 2);
    dl->AddCircleFilled(ImVec2(knob, a.y + 0.5f), 10, IM_COL32(0, 0, 0, 40), 32); // soft shadow
    dl->AddCircleFilled(ImVec2(knob, a.y), 9.5f, IM_COL32_WHITE, 32);
    dl->AddCircleFilled(ImVec2(knob, a.y), 7.5f, IM_COL32(0x67, 0x67, 0x74, 255), 32);

    char buf[16];
    std::snprintf(buf, sizeof(buf), fmt, shown);
    float ow = ui::text_width(out, buf);
    ui::text(dl, c.at(c.width - ow, mid - out.line() / 2), out, col(c.p.muted), buf);
    c.y += row_h;
}

void background(Ctx& c, SettingsUi& ui) {
    section(c, "Chat background");
    int n;
    const WallpaperInfo* list = wallpaper_list(&n);
    std::vector<std::pair<const char*, const char*>> items = { { "none", "None" } };
    for (int i = 0; i < n; i++) items.push_back({ list[i].id, list[i].name });
    c.wr.syncCustom(c.s);
    if (c.wr.hasCustom()) items.push_back({ "custom", "My image" });

    const float gap = 8, w = (c.width - 3 * gap) / 4, h = swatch_height();
    for (size_t i = 0; i < items.size(); i++) {
        float x = (i % 4) * (w + gap), y = c.y + (i / 4) * (h + gap);
        if (swatch(c, x, y, w, items[i].first, items[i].second) && c.s.wallpaper != items[i].first) {
            c.s.wallpaper = items[i].first;
            c.changed = true;
        }
    }
    c.y += ((items.size() + 3) / 4) * (h + gap) - gap;

    // .wallpaper-actions: margin 12px 0 4px
    c.y += 12;
    ui::ButtonOpts up;
    up.px = rem(0.85f);
    up.pad = ImVec2(14, 8);
    up.disabled = ui.picking;
    Font up_f{ fonts.semibold, up.px };
    const char* up_label = ui.picking ? "Choosing…" : "Upload image…";
    float up_h = up_f.line() + 16, up_w = ui::text_width(up_f, up_label) + 28;
    c.cursor(0, c.y);
    if (ui::button(up_label, c.p, ui::Button::Primary, up)) ui.pick_image = true;
    if (c.wr.hasCustom()) {
        Font link{ fonts.medium, rem(0.8f) };
        ui::ButtonOpts o;
        o.px = link.px;
        o.pad = ImVec2(6, 4);
        c.cursor(up_w + 10, c.y + (up_h - (link.line() + 8)) / 2);
        if (ui::button("Remove my image", c.p, ui::Button::Link, o)) {
            c.s.wallpaper_path.clear();
            if (c.s.wallpaper == "custom") c.s.wallpaper = "none";
            c.changed = true;
        }
    }
    c.y += up_h + 4;

    // Without zenity or kdialog there is no file dialog: type the path.
    if (ui.no_dialog) {
        c.y += 6;
        c.cursor(0, c.y);
        bool enter = ui::input("##path", "Image file path", ui.path, sizeof(ui.path), c.width - 80, c.p, false,
                               ImGuiInputTextFlags_EnterReturnsTrue);
        c.cursor(c.width - 70, c.y + (ui::input_height() - (rem(1) * 1.45f + 20)) / 2);
        ui::ButtonOpts o;
        o.width = 70;
        if ((ui::button("Use", c.p, ui::Button::Primary, o) || enter) && ui.path[0]) {
            c.s.wallpaper_path = ui.path;
            c.s.wallpaper = "custom";
            ui.path[0] = '\0';
            c.changed = true;
        }
        c.y += ui::input_height() + 4;
    }

    const std::string& err = c.wr.syncCustom(c.s);
    if (!err.empty()) {
        Font ef{ fonts.regular, rem(0.9f) };
        for (auto& l : ui::wrap(ef, err, c.width)) {
            ui::text(c.dl(), c.at(0, c.y), ef, col(c.p.danger), l.text);
            c.y += ef.line();
        }
    }

    if (c.s.wallpaper != "none") {
        slider(c, "Dim", &c.s.dim, 0, 0.8f, 0.05f, "%.0f%%", std::round(c.s.dim * 100));
        slider(c, "Blur", &c.s.blur, 0, 20, 1, "%.0fpx", c.s.blur);
    }
    end_section(c);
}

/* ---------------- Text & layout, notifications ---------------- */

void setting_row(Ctx& c, const char* label, float control_w, float control_h, const std::function<void()>& control) {
    Font body{ fonts.regular, rem(1) };
    std::vector<ui::Line> lines = ui::wrap(body, label, c.width - control_w - 10);
    float text_h = lines.size() * body.line();
    float row_h = std::max(text_h, control_h) + 12, mid = c.y + row_h / 2;
    float ty = mid - text_h / 2;
    for (auto& l : lines) {
        ui::text(c.dl(), c.at(0, ty), body, col(c.p.text), l.text);
        ty += body.line();
    }
    c.cursor(c.width - control_w, mid - control_h / 2);
    control();
    c.y += row_h;
}

void text_layout(Ctx& c) {
    section(c, "Text & layout");
    Font seg{ fonts.semibold, rem(0.85f) };
    float seg_h = seg.line() + 10 + 8;
    float seg_w = 8;
    for (const char* l : { "S", "M", "L" }) seg_w += std::max(38.0f, ui::text_width(seg, l) + 20);
    setting_row(c, "Text size", seg_w, seg_h, [&] {
        if (ui::segmented("size", c.s.font_size, { { "small", "S" }, { "medium", "M" }, { "large", "L" } }, c.p))
            c.changed = true;
    });
    setting_row(c, "Compact messages", 40, 22, [&] {
        if (ui::toggle("compact", &c.s.compact, c.p)) c.changed = true;
    });
    end_section(c);

    section(c, "Notifications");
    setting_row(c, "Notify me about private messages and mentions", 40, 22, [&] {
        if (ui::toggle("notify", &c.s.notifications, c.p)) c.changed = true;
    });
    note(c, "The window title always shows how many messages you missed.");
    end_section(c);
}

} // namespace

bool draw_settings_panel(SettingsUi& ui, Settings& s, const Palette& p, WallpaperRenderer& wr) {
    if (!ui.open) return false;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pw = std::min(PANEL_W, vp->WorkSize.x);

    // .settings-backdrop: rgba(0, 0, 0, 0.35); a click on it closes the panel.
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.35f));
    ImGui::Begin("##settings-backdrop", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    bool backdrop_click = ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    // box-shadow: -8px 0 24px rgba(0, 0, 0, 0.25)
    float x = vp->WorkPos.x + vp->WorkSize.x - pw;
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        ImVec2(x - 32, vp->WorkPos.y), ImVec2(x, vp->WorkPos.y + vp->WorkSize.y), IM_COL32(0, 0, 0, 0),
        IM_COL32(0, 0, 0, 40), IM_COL32(0, 0, 0, 40), IM_COL32(0, 0, 0, 0));
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(pw, vp->WorkSize.y));
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) ImGui::SetNextWindowFocus();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, p.panel);
    ImGui::Begin("##settings", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();

    Ctx c{ p, s, wr };
    ImDrawList* dl = c.dl();
    ImVec2 wp = ImGui::GetWindowPos();
    dl->AddLine(ImVec2(wp.x + 0.5f, wp.y), ImVec2(wp.x + 0.5f, wp.y + vp->WorkSize.y), col(p.border)); // border-left

    // header: h2 "Appearance" and a close button, margin-bottom 8px
    Font h2{ fonts.bold, rem(1.15f) };
    ui::text(dl, c.at(0, c.y + (38 - h2.line()) / 2), h2, col(p.text), "Appearance");
    c.cursor(c.width - 38, c.y);
    bool close = ui::icon_button("close", "×", p, false, rem(1.15f));
    c.y += 38 + 8;

    themes(c);
    colors(c);
    background(c, ui);
    text_layout(c);
    c.y += PAD_BOTTOM;

    // Let the window know how tall the content is, for scrolling.
    ImGui::SetCursorPos(ImVec2(0, c.y));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::End();

    if (close || (backdrop_click && !c.popup_open) || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !c.popup_open))
        ui.open = false;
    return c.changed;
}
