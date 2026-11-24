#include "chat_view.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <langinfo.h>

#include "ui.h"

using ui::Font;
using ui::col;
using ui::fonts;
using ui::rem;

static constexpr uint64_t GROUP_GAP_MS = 5 * 60 * 1000; // groupMessages.js

/* ------------------------------------------------------------------ */
/* Dates                                                               */
/* ------------------------------------------------------------------ */

static struct tm local(uint64_t ms) {
    time_t t = static_cast<time_t>(ms / 1000);
    struct tm tm;
    localtime_r(&t, &tm);
    return tm;
}

static int day_number(uint64_t ms) {
    struct tm tm = local(ms);
    return (tm.tm_year + 1900) * 1000 + tm.tm_yday;
}

std::string clock_text(uint64_t ms) {
    // 12-hour clock where the locale uses one (en_US: "%r"), like the browser.
    static const bool twelve = [] {
        const char* f = nl_langinfo(T_FMT);
        return std::strstr(f, "%r") || std::strstr(f, "%I") || std::strstr(f, "%p");
    }();
    struct tm tm = local(ms);
    char buf[16];
    std::strftime(buf, sizeof(buf), twelve ? "%I:%M %p" : "%H:%M", &tm);
    return buf;
}

std::string day_label(uint64_t ms) {
    uint64_t now = static_cast<uint64_t>(std::time(nullptr)) * 1000;
    if (day_number(ms) == day_number(now)) return "Today";
    if (day_number(ms) == day_number(now - 24ull * 3600 * 1000)) return "Yesterday";
    // { day: 'numeric', month: 'long', year only when not this year }
    struct tm tm = local(ms), today = local(now);
    bool us = std::strstr(nl_langinfo(D_FMT), "%m/%d") != nullptr; // month-first locales
    char buf[48];
    if (tm.tm_year == today.tm_year)
        std::strftime(buf, sizeof(buf), us ? "%B %-d" : "%-d %B", &tm);
    else
        std::strftime(buf, sizeof(buf), us ? "%B %-d, %Y" : "%-d %B %Y", &tm);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

namespace {

struct Ctx {
    ImDrawList* dl;
    const Palette& p;
    float x, width;    // content box of .messages-box
    bool compact, narrow;
    // CSS custom properties (index.css)
    float radius() const { return compact ? 12 : 16; }
    float pad_y() const { return compact ? 4 : 8; }
    float pad_x() const { return compact ? 10 : 12; }
    float group_gap() const { return compact ? 6 : 12; }
};

void advance(float h) {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + h);
}

// .day-separator / .system-line: a centered pill.
void pill(Ctx& c, const std::string& text, const ImVec4& fg, bool day) {
    const float margin_top = day ? 10 : 4, margin_bottom = day ? 6 : 4;
    Font f{ day ? fonts.semibold : fonts.regular, rem(0.78f) };
    float max_w = c.width - 24;
    std::vector<ui::Line> lines = ui::wrap(f, text, max_w);
    float tw = 0;
    for (auto& l : lines) tw = std::max(tw, l.width);
    float w = tw + 24, h = lines.size() * f.line() + 6;

    float top = ImGui::GetCursorScreenPos().y + margin_top;
    ImVec2 a(std::round(c.x + (c.width - w) / 2), top), b(a.x + w, top + h);
    ui::shadow(c.dl, a, b, h / 2, 1, 2, 0.12f);
    c.dl->AddRectFilled(a, b, col(c.p.panel), h / 2);
    float y = top + 3;
    for (auto& l : lines) {
        ui::text(c.dl, ImVec2(a.x + 12 + (tw - l.width) / 2, y), f, col(fg), l.text);
        y += f.line();
    }
    advance(margin_top + h + margin_bottom);
}

void bubble(Ctx& c, const ChatLine& l, bool first) {
    const float alpha = l.history ? 0.6f : 1.0f; // .msg-row.history
    const bool dm = l.kind == ChatLine::Dm;
    const bool own = l.own;
    const float avatar = 34, gap = 8;
    const float px = c.pad_x(), py = c.pad_y();

    Font body{ fonts.regular, rem(1) };
    Font name_f{ fonts.bold, rem(0.82f) };
    Font dm_f{ fonts.bold, rem(0.75f) };
    Font time_f{ fonts.regular, rem(0.68f), 1.0f };

    std::string label;
    if (dm) label = std::string("🔒 Private ") + (own ? "to " + l.to : "from " + l.name);
    bool show_name = first && !own && !dm;
    std::string time = clock_text(l.timestamp);
    float time_w = ui::text_width(time_f, time);
    const float time_gap = 10; // margin-left of the floated time

    // max-width: min(70%, 560px) of the row; the avatar column takes room too.
    float row_avail = c.width - (own ? 0 : avatar + gap);
    float max_outer = std::min({ c.width * (c.narrow ? 0.85f : 0.70f), 560.0f, row_avail });
    float max_inner = max_outer - 2 * px;

    // Shrink-to-fit: as wide as the content on one line, at most max_inner.
    std::vector<ui::Line> one = ui::wrap(body, l.text, 0);
    float content = 0;
    for (auto& ln : one) content = std::max(content, ln.width);
    content = std::max(content, one.back().width + time_gap + time_w);
    if (show_name) content = std::max(content, ui::text_width(name_f, l.name));
    if (dm) content = std::max(content, ui::text_width(dm_f, label));
    float inner_w = std::min(content, max_inner);
    std::vector<ui::Line> lines = content <= max_inner ? one : ui::wrap(body, l.text, inner_w);

    // The time floats right on the last line when it fits, else below it.
    bool time_inline = lines.back().width + time_gap + time_w <= inner_w + 0.5f;
    float label_h = show_name ? name_f.line() + 1 : dm ? dm_f.line() + 2 : 0;
    float text_h = lines.size() * body.line();
    float float_bottom = (time_inline ? text_h - body.line() : text_h) + 6 + time_f.px - 4;
    float inner_h = label_h + std::max(text_h, float_bottom);
    float bw = inner_w + 2 * px, bh = inner_h + 2 * py;

    ImVec2 cur = ImGui::GetCursorScreenPos();
    float top = cur.y + (first ? c.group_gap() : 2);
    float bx = own ? c.x + c.width - bw : c.x + avatar + gap;
    ImVec2 a(std::round(bx), std::round(top)), b(a.x + bw, a.y + bh);

    ImVec4 fill = own ? c.p.own_bubble : c.p.other_bubble;
    ImVec4 fg = own ? c.p.own_text : c.p.other_text;
    if (dm && own) { // .msg-row.own .bubble.dm
        fill = c.p.dm;
        fg = ImVec4(1, 1, 1, 1);
    }

    // First bubble of a group: a 4px corner toward the sender plus a tail.
    const float r = c.radius();
    bool tail = first && !dm;
    float tl = r, tr = r;
    if (first && !own) tl = 4;
    if (first && own) tr = 4;

    // box-shadow: 0 1px 1.5px rgba(0, 0, 0, 0.15)
    ui::shadow(c.dl, a, b, r, 1, 1.5f, 0.15f * alpha);
    ui::rounded_rect(c.dl, a, b, col(fill, alpha), tl, tr, r, r);
    if (tail) {
        if (own)
            c.dl->AddTriangleFilled(ImVec2(b.x - 4, a.y), ImVec2(b.x + 6, a.y), ImVec2(b.x - 4, a.y + 12),
                                    col(fill, alpha));
        else
            c.dl->AddTriangleFilled(ImVec2(a.x + 4, a.y), ImVec2(a.x - 6, a.y), ImVec2(a.x + 4, a.y + 12),
                                    col(fill, alpha));
    }
    if (dm && !own) // outline: 2px solid var(--dm); outline-offset: -2px
        c.dl->AddRect(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), col(c.p.dm, alpha), r - 1, 0, 2.0f);

    float tx = a.x + px, ty = a.y + py;
    if (show_name) {
        ui::text(c.dl, ImVec2(tx, ty), name_f, ui::name_color(l.name, alpha), l.name);
        ty += label_h;
    } else if (dm) {
        ui::text(c.dl, ImVec2(tx, ty), dm_f, col(fg, 0.9f * alpha), label);
        ty += label_h;
    }
    for (auto& ln : lines) {
        ui::text(c.dl, ImVec2(tx, ty), body, col(fg, alpha), ln.text);
        ty += body.line();
    }
    // .bubble-time: float right, margin-top 6px, line-height 1, opacity .7
    float time_top = (time_inline ? ty - body.line() : ty) + 6;
    ui::text(c.dl, ImVec2(a.x + px + inner_w - time_w, time_top), time_f, col(fg, 0.7f * alpha), time);

    if (first && !own) ui::avatar(c.dl, ImVec2(c.x, a.y), avatar, l.name, alpha);

    advance(b.y - cur.y);
}

bool same_lane(const ChatLine& a, const ChatLine& b) {
    return a.kind == b.kind && a.name == b.name && (a.kind != ChatLine::Dm || a.to == b.to) &&
           a.history == b.history && b.timestamp - a.timestamp <= GROUP_GAP_MS;
}

} // namespace

void draw_chat_view(const std::vector<ChatLine>& lines, const Palette& p, bool compact, bool narrow) {
    ImVec2 start = ImGui::GetCursorScreenPos();
    Ctx c{ ImGui::GetWindowDrawList(), p, start.x, ImGui::GetContentRegionAvail().x, compact, narrow };

    advance(12); // .messages-box padding-top
    int last_day = -1;
    const ChatLine* prev = nullptr; // previous bubble, if the previous item was one
    for (const ChatLine& l : lines) {
        int day = day_number(l.timestamp);
        if (day != last_day) {
            pill(c, day_label(l.timestamp), p.muted, true);
            last_day = day;
            prev = nullptr;
        }
        switch (l.kind) {
            case ChatLine::System:
                pill(c, l.text, p.muted, false);
                prev = nullptr;
                break;
            case ChatLine::Error:
                pill(c, l.text, p.danger, false);
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
    advance(8); // padding-bottom
    ImGui::Dummy(ImVec2(c.width, 0));
}
