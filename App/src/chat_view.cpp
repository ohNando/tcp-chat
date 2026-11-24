#include "chat_view.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <ctime>

static constexpr uint64_t GROUP_GAP_MS = 5 * 60 * 1000;

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

static uint32_t name_hash(const std::string& name) {
    uint32_t h = 2166136261u; // FNV-1a, same as nameHue() in the web client
    for (unsigned char ch : name) h = (h ^ ch) * 16777619u;
    return h;
}

// HSL -> RGB, so hue/saturation/lightness match the web's hsl() colors.
static ImVec4 hsl(float h, float s, float l) {
    auto f = [&](float n) {
        float k = std::fmod(n + h / 30.0f, 12.0f);
        float a = s * std::min(l, 1 - l);
        return l - a * std::max(-1.0f, std::min({ k - 3, 9 - k, 1.0f }));
    };
    return ImVec4(f(0), f(8), f(4), 1.0f);
}

ImU32 name_color(const std::string& name, float lightness) {
    return ImGui::ColorConvertFloat4ToU32(hsl(static_cast<float>(name_hash(name) % 360), 0.6f, lightness));
}

// First character of a name, upper-cased (including Turkish letters).
static std::string initial(const std::string& name) {
    if (name.empty()) return "?";
    unsigned char c = static_cast<unsigned char>(name[0]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
    std::string first = name.substr(0, len);
    static const char* map[][2] = {
        { "ç", "Ç" }, { "ğ", "Ğ" }, { "ı", "I" }, { "i", "İ" }, { "ö", "Ö" }, { "ş", "Ş" }, { "ü", "Ü" },
    };
    for (auto& m : map)
        if (first == m[0]) return m[1];
    if (len == 1 && c >= 'a' && c <= 'z') first[0] = static_cast<char>(c - 32);
    return first;
}

void draw_avatar(ImDrawList* dl, ImVec2 center, float radius, const std::string& name) {
    ImU32 bg = ImGui::ColorConvertFloat4ToU32(hsl(static_cast<float>(name_hash(name) % 360), 0.55f, 0.45f));
    dl->AddCircleFilled(center, radius, bg);
    std::string ch = initial(name);
    float size = radius * 1.05f;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, ch.c_str());
    dl->AddText(ImGui::GetFont(), size, ImVec2(center.x - ts.x / 2, center.y - ts.y / 2), IM_COL32_WHITE, ch.c_str());
}

/* ------------------------------------------------------------------ */
/* Days                                                                */
/* ------------------------------------------------------------------ */

static int day_number(uint64_t ms) {
    time_t t = static_cast<time_t>(ms / 1000);
    struct tm tm;
    localtime_r(&t, &tm);
    return (tm.tm_year + 1900) * 1000 + tm.tm_yday;
}

static std::string day_label(uint64_t ms) {
    uint64_t now = static_cast<uint64_t>(std::time(nullptr)) * 1000;
    if (day_number(ms) == day_number(now)) return "Today";
    if (day_number(ms) == day_number(now - 24ull * 3600 * 1000)) return "Yesterday";
    time_t t = static_cast<time_t>(ms / 1000);
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d %B %Y", &tm);
    return buf;
}

static std::string clock_text(uint64_t ms) {
    time_t t = static_cast<time_t>(ms / 1000);
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[8];
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

namespace {

struct Ctx {
    ImDrawList* dl;
    ImFont* font;
    float fs, small;   // body and small text sizes (px)
    float width;       // available width
    const Palette& p;
    bool compact;
};

ImU32 col(const ImVec4& c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

// Centered rounded label: day separators and system lines.
void pill(Ctx& c, const std::string& text, const ImVec4& fg, float margin) {
    ImVec2 cur = ImGui::GetCursorScreenPos();
    float max_w = c.width * 0.85f;
    ImVec2 ts = c.font->CalcTextSizeA(c.small, FLT_MAX, max_w, text.c_str());
    float w = ts.x + 24, h = ts.y + 8;
    ImVec2 p0(cur.x + (c.width - w) / 2, cur.y + margin);
    c.dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), col(c.p.panel, 0.92f), h / 2);
    c.dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), col(c.p.border, 1), h / 2);
    c.dl->AddText(c.font, c.small, ImVec2(p0.x + 12, p0.y + 4), col(fg, 1), text.c_str(), nullptr, max_w);
    ImGui::Dummy(ImVec2(c.width, h + margin + 2));
}

void bubble(Ctx& c, const ChatLine& l, bool first) {
    const float alpha = l.history ? 0.6f : 1.0f;
    const bool dm = l.kind == ChatLine::Dm;
    const float pad_x = c.compact ? 10 : 12, pad_y = c.compact ? 5 : 8;
    const float radius = c.compact ? 11 : 15;
    const float avatar_r = 17, avatar_gap = 8;
    const float left = l.own ? 0 : avatar_r * 2 + avatar_gap;
    const float max_inner = std::max(80.0f, (c.width - left) * 0.7f - 2 * pad_x);

    // Labels above the text: sender name (others, first of a group) or the
    // private-message marker.
    std::string label;
    ImVec4 label_color = c.p.dm;
    if (dm) label = l.own ? "🔒 Private to " + l.to : "🔒 Private from " + l.name;
    else if (first && !l.own) label = l.name;

    ImVec2 text_sz = c.font->CalcTextSizeA(c.fs, FLT_MAX, max_inner, l.text.c_str());
    std::string time = clock_text(l.timestamp);
    ImVec2 time_sz = c.font->CalcTextSizeA(c.small, FLT_MAX, 0, time.c_str());
    ImVec2 label_sz = label.empty() ? ImVec2(0, 0) : c.font->CalcTextSizeA(c.small * 1.05f, FLT_MAX, max_inner, label.c_str());

    // Time sits after the text when a one-line message leaves room for it,
    // otherwise on its own line at the bottom right.
    const float gap = 10;
    bool single_line = text_sz.y <= c.fs * 1.3f;
    bool inline_time = single_line && text_sz.x + gap + time_sz.x <= max_inner;
    float inner_w = std::max({ text_sz.x, label_sz.x, inline_time ? text_sz.x + gap + time_sz.x : time_sz.x });
    float label_h = label.empty() ? 0 : label_sz.y + 2;
    float inner_h = label_h + text_sz.y + (inline_time ? 0 : time_sz.y + 2);
    float bw = inner_w + 2 * pad_x, bh = inner_h + 2 * pad_y;

    ImVec2 cur = ImGui::GetCursorScreenPos();
    float top = cur.y + (first ? (c.compact ? 7 : 12) : 2);
    float x = l.own ? cur.x + c.width - bw - 8 : cur.x + left;
    ImVec2 p0(x, top), p1(x + bw, top + bh);

    // Bubble colors
    ImVec4 fill = l.own ? c.p.own_bubble : c.p.other_bubble;
    ImVec4 fg = l.own ? c.p.own_text : c.p.other_text;
    if (dm && l.own) {
        fill = c.p.dm;
        fg = ImVec4(1, 1, 1, 1);
        label_color = fg;
    }

    ImDrawFlags corners = ImDrawFlags_RoundCornersAll;
    bool tail = first && !dm;
    if (tail) corners = l.own ? (ImDrawFlags_RoundCornersAll & ~ImDrawFlags_RoundCornersTopRight)
                              : (ImDrawFlags_RoundCornersAll & ~ImDrawFlags_RoundCornersTopLeft);

    c.dl->AddRectFilled(ImVec2(p0.x, p0.y + 1.5f), ImVec2(p1.x, p1.y + 1.5f), col(ImVec4(0, 0, 0, 1), 0.12f * alpha),
                        radius, corners); // soft shadow
    c.dl->AddRectFilled(p0, p1, col(fill, alpha), radius, corners);
    if (tail) {
        if (l.own)
            c.dl->AddTriangleFilled(ImVec2(p1.x, p0.y), ImVec2(p1.x + 7, p0.y), ImVec2(p1.x, p0.y + 11), col(fill, alpha));
        else
            c.dl->AddTriangleFilled(ImVec2(p0.x, p0.y), ImVec2(p0.x - 7, p0.y), ImVec2(p0.x, p0.y + 11), col(fill, alpha));
    }
    if (dm && !l.own) c.dl->AddRect(p0, p1, col(c.p.dm, alpha), radius, 0, 2.0f);

    // Content
    float tx = p0.x + pad_x, ty = p0.y + pad_y;
    if (!label.empty()) {
        ImU32 lc = dm ? col(label_color, alpha) : (name_color(l.name, c.p.dark ? 0.62f : 0.42f) & 0x00FFFFFF) |
                   (static_cast<ImU32>(255 * alpha) << IM_COL32_A_SHIFT);
        c.dl->AddText(c.font, c.small * 1.05f, ImVec2(tx, ty), lc, label.c_str(), nullptr, max_inner);
        ty += label_h;
    }
    c.dl->AddText(c.font, c.fs, ImVec2(tx, ty), col(fg, alpha), l.text.c_str(), nullptr, max_inner);
    ImVec2 time_pos = inline_time
        ? ImVec2(p1.x - pad_x - time_sz.x, ty + (text_sz.y - time_sz.y))
        : ImVec2(p1.x - pad_x - time_sz.x, ty + text_sz.y + 2);
    c.dl->AddText(c.font, c.small, time_pos, col(fg, 0.7f * alpha), time.c_str());

    if (first && !l.own) draw_avatar(c.dl, ImVec2(cur.x + avatar_r, top + avatar_r), avatar_r, l.name);

    ImGui::Dummy(ImVec2(c.width, (p1.y - cur.y) + 0.5f));
}

bool same_lane(const ChatLine& a, const ChatLine& b) {
    return a.kind == b.kind && a.name == b.name && (a.kind != ChatLine::Dm || a.to == b.to) &&
           a.history == b.history && b.timestamp - a.timestamp <= GROUP_GAP_MS;
}

} // namespace

void draw_chat_view(const std::vector<ChatLine>& lines, const Palette& p, bool compact) {
    Ctx c{ ImGui::GetWindowDrawList(), ImGui::GetFont(), ImGui::GetFontSize(), ImGui::GetFontSize() * 0.8f,
           ImGui::GetContentRegionAvail().x, p, compact };

    int last_day = -1;
    const ChatLine* prev = nullptr; // previous bubble, if the previous item was one
    for (const ChatLine& l : lines) {
        int day = day_number(l.timestamp);
        if (day != last_day) {
            pill(c, day_label(l.timestamp), p.muted, 10);
            last_day = day;
            prev = nullptr;
        }
        switch (l.kind) {
            case ChatLine::System:
                pill(c, l.text, p.muted, 4);
                prev = nullptr;
                break;
            case ChatLine::Error:
                pill(c, l.text, p.danger, 4);
                prev = nullptr;
                break;
            case ChatLine::Chat:
            case ChatLine::Dm: {
                bool first = !(prev && same_lane(*prev, l));
                bubble(c, l, first);
                prev = &l;
                break;
            }
        }
    }
    ImGui::Dummy(ImVec2(c.width, 6));
}
