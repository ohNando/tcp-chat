#include "settings_ui.h"

#include <algorithm>


namespace {

void section(const char* title) {
    ImGui::Spacing();
    ImGui::TextDisabled("%s", title);
    ImGui::Separator();
}

// A clickable miniature of a theme: page color with two small bubbles.
bool theme_card(const ThemeInfo& t, bool selected, const Palette& current) {
    Settings preview;
    preview.theme = t.id;
    Palette tp = resolve_theme(preview);

    const ImVec2 size(118, 74);
    ImGui::PushID(t.id);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("card", size);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);

    dl->AddRectFilled(p0, p1, to_u32(tp.bg), 10);
    dl->AddRectFilled(ImVec2(p0.x + 10, p0.y + 12), ImVec2(p0.x + 64, p0.y + 26), to_u32(tp.other_bubble), 7);
    dl->AddRectFilled(ImVec2(p1.x - 60, p0.y + 32), ImVec2(p1.x - 10, p0.y + 46), to_u32(tp.own_bubble), 7);
    dl->AddText(ImVec2(p0.x + 10, p1.y - ImGui::GetFontSize() - 6), to_u32(tp.text), t.name);
    if (selected)
        dl->AddRect(p0, p1, to_u32(current.accent), 10, 0, 3.0f);
    else
        dl->AddRect(p0, p1, to_u32(hovered ? current.muted : current.border), 10, 0, 1.0f);
    ImGui::PopID();
    return clicked;
}

// Color picker that falls back to the theme's color; "Reset" clears the
// override. Returns true when changed.
bool color_row(const char* label, int32_t& value, const ImVec4& theme_color) {
    bool changed = false;
    ImGui::PushID(label);
    float c[3];
    ImVec4 shown = value >= 0 ? rgb_to_vec(value) : theme_color;
    c[0] = shown.x, c[1] = shown.y, c[2] = shown.z;
    if (ImGui::ColorEdit3("##c", c, ImGuiColorEditFlags_NoInputs)) {
        value = vec_to_rgb(ImVec4(c[0], c[1], c[2], 1));
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    if (value >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            value = -1;
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

bool segmented(const char* id, std::string& value, const char* const* options, const char* const* labels, int n,
               const Palette& p) {
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < n; i++) {
        if (i) ImGui::SameLine(0, 4);
        bool on = value == options[i];
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.accent_hover);
            ImGui::PushStyleColor(ImGuiCol_Text, p.accent_text);
        }
        if (ImGui::Button(labels[i], ImVec2(64, 0)) && !on) {
            value = options[i];
            changed = true;
        }
        if (on) ImGui::PopStyleColor(3);
    }
    ImGui::PopID();
    return changed;
}

// A small preview of a wallpaper; "none" shows the plain chat color.
bool wallpaper_card(const char* id, const char* name, bool selected, const Settings& s, const Palette& p,
                    WallpaperRenderer& wr) {
    const ImVec2 size(76, 56);
    ImGui::PushID(id);
    ImGui::BeginGroup();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("wp", size);
    bool hovered = ImGui::IsItemHovered();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    Settings preview = s;
    preview.wallpaper = id;
    dl->AddRectFilled(p0, p1, to_u32(p.bg), 8);
    if (wr.draw(dl, p0, p1, preview, p)) {
        // Round the corners of the square preview by painting them over.
        dl->AddRect(ImVec2(p0.x - 3, p0.y - 3), ImVec2(p1.x + 3, p1.y + 3), to_u32(p.panel), 11, 0, 6.0f);
    }
    dl->AddRect(p0, p1, to_u32(selected ? p.accent : hovered ? p.muted : p.border), 8, 0, selected ? 3.0f : 1.0f);

    ImVec2 ts = ImGui::CalcTextSize(name);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (size.x - ts.x) / 2);
    ImGui::TextDisabled("%s", name);
    ImGui::EndGroup();
    ImGui::PopID();
    return clicked;
}

} // namespace

bool draw_settings_window(SettingsUi& ui, Settings& s, const Palette& p, WallpaperRenderer& wr) {
    bool* open = &ui.open;
    if (!*open) return false;
    bool changed = false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(420, vp->WorkSize.y - 40), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 20, vp->WorkPos.y + 20), ImGuiCond_Always,
                            ImVec2(1, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    bool visible = ImGui::Begin("Settings", open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    if (!visible) {
        ImGui::End();
        return false;
    }

    section("THEME");
    int n;
    const ThemeInfo* themes = theme_list(&n);
    for (int i = 0; i < n; i++) {
        if (i % 3) ImGui::SameLine();
        if (theme_card(themes[i], s.theme == themes[i].id, p) && s.theme != themes[i].id) {
            s.theme = themes[i].id;
            changed = true;
        }
    }

    section("COLORS");
    Settings base = s;
    base.accent = base.own_bubble = base.other_bubble = -1;
    Palette tp = resolve_theme(base);
    changed |= color_row("Accent", s.accent, tp.accent);
    changed |= color_row("My messages", s.own_bubble, tp.own_bubble);
    changed |= color_row("Their messages", s.other_bubble, tp.other_bubble);

    section("BACKGROUND");
    int nw;
    const WallpaperInfo* walls = wallpaper_list(&nw);
    int per_row = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 8) / 84));
    for (int i = -1; i < nw; i++) {
        const char* id = i < 0 ? "none" : walls[i].id;
        const char* name = i < 0 ? "None" : walls[i].name;
        if ((i + 1) % per_row) ImGui::SameLine(0, 8);
        if (wallpaper_card(id, name, s.wallpaper == id, s, p, wr) && s.wallpaper != id) {
            s.wallpaper = id;
            changed = true;
        }
    }
    if (!s.wallpaper_path.empty()) {
        if ((nw + 1) % per_row) ImGui::SameLine(0, 8);
        if (wallpaper_card("custom", "My image", s.wallpaper == "custom", s, p, wr) && s.wallpaper != "custom") {
            s.wallpaper = "custom";
            changed = true;
        }
    }

    ImGui::BeginDisabled(ui.picking);
    if (ImGui::Button(ui.picking ? "Choosing..." : "Choose image...")) ui.pick_image = true;
    ImGui::EndDisabled();
    if (!s.wallpaper_path.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Remove image")) {
            s.wallpaper_path.clear();
            if (s.wallpaper == "custom") s.wallpaper = "none";
            changed = true;
        }
    }
    // Typing a path works everywhere, also without zenity/kdialog.
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
    bool enter = ImGui::InputTextWithHint("##path", "or paste an image path", ui.path, sizeof(ui.path),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Use", ImVec2(60, 0)) || enter) && ui.path[0]) {
        s.wallpaper_path = ui.path;
        s.wallpaper = "custom";
        ui.path[0] = '\0';
        changed = true;
    }
    const std::string& err = wr.syncCustom(s);
    if (!err.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(p.danger, "%s", err.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::BeginDisabled(s.wallpaper == "none");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Dim");
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    float dim_pct = s.dim * 100;
    if (ImGui::SliderFloat("##dim", &dim_pct, 0, 80, "%.0f%%")) {
        s.dim = dim_pct / 100;
        changed = true;
    }
    ImGui::EndDisabled();

    section("TEXT & LAYOUT");
    static const char* sizes[] = { "small", "medium", "large" };
    static const char* size_labels[] = { "S", "M", "L" };
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Text size");
    ImGui::SameLine(140);
    changed |= segmented("size", s.font_size, sizes, size_labels, 3, p);
    changed |= ImGui::Checkbox("Compact messages", &s.compact);

    ImGui::End();
    return changed;
}
