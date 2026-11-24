#include "app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>

#include "chat_view.h"
#include "imgui.h"
#include "ui.h"

using ui::Font;
using ui::col;
using ui::fonts;
using ui::rem;

extern char** environ;

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

App::App(const AutoRun& autorun) : autorun_(autorun), settings_path_(settings_path()) {
    settings_load(settings_, settings_path_);
    if (!autorun_.theme.empty()) settings_.theme = autorun_.theme == "system" ? "" : autorun_.theme;
    if (!autorun_.wallpaper.empty()) {
        int n;
        const WallpaperInfo* list = wallpaper_list(&n);
        bool builtin = autorun_.wallpaper == "none";
        for (int i = 0; i < n; i++) builtin |= autorun_.wallpaper == list[i].id;
        if (builtin) {
            settings_.wallpaper = autorun_.wallpaper;
        } else {
            settings_.wallpaper = "custom";
            settings_.wallpaper_path = autorun_.wallpaper;
        }
    }
    applyLook();
    settings_ui_.open = autorun_.open_settings;
    std::snprintf(host_, sizeof(host_), "%s", settings_.host.c_str());
    std::snprintf(port_text_, sizeof(port_text_), "%d", settings_.port);
    std::snprintf(name_, sizeof(name_), "%s", settings_.name.c_str());

    if (autorun_.connect) {
        std::snprintf(host_, sizeof(host_), "%s", autorun_.host.c_str());
        std::snprintf(port_text_, sizeof(port_text_), "%d", autorun_.port);
        std::snprintf(name_, sizeof(name_), "%s", autorun_.name.c_str());
        startConnect();
    }
}

void App::applyLook() {
    palette_ = resolve_theme(settings_);
    apply_style(palette_, settings_);
}

static std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Same rule as isNotable() in the web client: DMs and mentions of our name.
static bool is_notable(const ChatLine& l, const std::string& me) {
    if (l.own || l.history) return false;
    if (l.kind == ChatLine::Dm) return true;
    return l.kind == ChatLine::Chat && !me.empty() && lower_ascii(l.text).find(lower_ascii(me)) != std::string::npos;
}

void App::countUnread() {
    uint64_t v = client_.linesVersion();
    if (focused_) unread_ = 0;
    if (v == unread_version_) return;
    // Each new line bumps the version by one, so the last (v - seen) lines
    // are new (old lines may have been trimmed from the front meanwhile).
    const std::vector<ChatLine>& lines = client_.lines();
    size_t fresh = std::min<uint64_t>(v - unread_version_, lines.size());
    unread_version_ = v;
    if (focused_) return;
    for (size_t i = lines.size() - fresh; i < lines.size(); i++) {
        const ChatLine& l = lines[i];
        if ((l.kind != ChatLine::Chat && l.kind != ChatLine::Dm) || l.own || l.history) continue;
        unread_++;
        if (!is_notable(l, client_.name())) continue;
        attention_ = true;
        if (settings_.notifications)
            notify(l.kind == ChatLine::Dm ? "🔒 " + l.name + " (private)" : l.name + " mentioned you", l.text);
    }
}

// Desktop notification through notify-send (libnotify), like the web's
// Notification. Arguments go straight to exec: no shell sees the text.
void App::notify(const std::string& title, const std::string& body) {
    const char* argv[] = { "notify-send", "--app-name=TCP Chat", title.c_str(), body.c_str(), nullptr };
    pid_t pid;
    if (posix_spawnp(&pid, "notify-send", nullptr, nullptr, const_cast<char* const*>(argv), environ) == 0) {
        int status;
        waitpid(pid, &status, WNOHANG); // reaped later if still running
    }
}

std::string App::windowTitle() const {
    return unread_ > 0 ? "(" + std::to_string(unread_) + ") TCP Chat" : "TCP Chat";
}

bool App::takeAttentionRequest() {
    bool a = attention_;
    attention_ = false;
    return a;
}

// The file dialog is a separate program; run it on a thread so the window
// keeps drawing (and the connection keeps its heartbeat) meanwhile.
void App::pollImagePicker() {
    if (settings_ui_.pick_image && !image_pick_.valid()) {
        image_pick_ = std::async(std::launch::async, [] {
            bool no_dialog = false;
            std::string path = pick_image_file(&no_dialog);
            return std::make_pair(path, no_dialog);
        });
        settings_ui_.picking = true;
    }
    settings_ui_.pick_image = false;
    if (image_pick_.valid() && image_pick_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto [path, no_dialog] = image_pick_.get();
        settings_ui_.picking = false;
        settings_ui_.no_dialog = no_dialog;
        if (!path.empty()) {
            settings_.wallpaper_path = path;
            settings_.wallpaper = "custom";
            look_dirty_ = true;
        }
    }
}

void App::saveSettings() {
    settings_save(settings_, settings_path_);
}

void App::startConnect() {
    int port = std::atoi(port_text_);
    // Remember what the user typed for next time (not for scripted runs).
    if (!autorun_.connect) {
        settings_.host = host_;
        settings_.port = port;
        settings_.name = name_;
        saveSettings();
    }
    tried_ = true;
    client_.connect(host_, port, name_);
}

void App::leave() {
    client_.quit();
    joined_ = false;
    reconnect_at_ = 0;
    reconnect_attempt_ = 0;
}

void App::poll() {
    // Style changes take effect between frames, never in the middle of one.
    if (look_dirty_) {
        applyLook();
        saveSettings();
        look_dirty_ = false;
    }
    client_.poll();

    if (client_.state() == ConnState::Chatting) {
        joined_ = true;
        reconnect_attempt_ = 0;
        reconnect_at_ = 0;
    }

    // Lost the connection while in the room: retry 1s, 2s, 4s ... 15s.
    bool blocked = client_.trustPending() || client_.identityChanged();
    if (joined_ && client_.state() == ConnState::Disconnected && !blocked) {
        if (reconnect_at_ == 0) {
            double delay = std::min(15.0, std::pow(2.0, reconnect_attempt_));
            reconnect_at_ = now_s() + delay;
            reconnect_attempt_++;
        } else if (now_s() >= reconnect_at_) {
            reconnect_at_ = 0;
            startConnect();
        }
    }

    countUnread();

    if (autorun_.trust && client_.trustPending()) client_.acceptPendingKey();
    if (!said_ && !autorun_.say.empty() && client_.state() == ConnState::Chatting) {
        client_.sendText(autorun_.say);
        said_ = true;
    }
    if (!autorun_.dm.empty() && client_.canDm(autorun_.dm)) {
        dm_target_ = autorun_.dm;
        autorun_.dm.clear();
    }
}

// STATUS_TEXT in App.jsx.
std::string App::statusText() const {
    switch (client_.state()) {
        case ConnState::Connecting:
        case ConnState::Handshaking: return "Connecting securely…";
        case ConnState::Registering: return "Joining…";
        case ConnState::Chatting: return "Online";
        case ConnState::Disconnected: break;
    }
    return tried_ ? "Disconnected" : "Not connected";
}

const char* App::dmTarget() const {
    // The recipient may have left (or lost their key) since we picked them.
    return !dm_target_.empty() && client_.canDm(dm_target_) ? dm_target_.c_str() : nullptr;
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void App::draw() {
    // One borderless window covering the whole viewport, in the page color.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    if (joined_)
        drawChat();
    else
        drawLogin();
    drawTrustPrompt();
    drawIdentityWarning();
    ImGui::End();

    if (draw_settings_panel(settings_ui_, settings_, palette_, wallpaper_)) look_dirty_ = true;
    pollImagePicker();
}

/* ------------------------------------------------------------------ */
/* Login (LoginScreen)                                                 */
/* ------------------------------------------------------------------ */

void App::drawLogin() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Palette& p = palette_;

    // .login-corner: the settings button, fixed 12px from the top right.
    ImGui::SetCursorScreenPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12 - 38, vp->WorkPos.y + 12));
    if (ui::icon_button("settings", "⚙️", p)) settings_ui_.open = true;

    // .login-screen: max-width 360px, padding 24px, a centered column with
    // 12px gaps. The desktop also asks for the server (the web page knows it).
    const float width = std::min(360.0f, vp->WorkSize.x) - 48, gap = 12;
    Font h2{ fonts.bold, rem(1.9f) };
    Font st{ fonts.regular, rem(0.85f) };
    Font err{ fonts.regular, rem(0.9f) };
    const float input_h = ui::input_height(), button_h = rem(1) * 1.45f + 20;
    const std::string& error = client_.lastError();
    bool busy = client_.state() != ConnState::Disconnected;
    std::vector<ui::Line> err_lines = !busy && !error.empty() ? ui::wrap(err, error, width) : std::vector<ui::Line>();

    float total = h2.line() + gap + st.line() + gap + input_h + gap + input_h + gap + button_h;
    if (!err_lines.empty()) total += gap + err_lines.size() * err.line();
    float x = std::round(vp->WorkPos.x + (vp->WorkSize.x - width) / 2);
    float y = std::round(vp->WorkPos.y + std::max(24.0f, (vp->WorkSize.y - total) / 2));

    float tw = ui::text_width(h2, "TCP Chat");
    ui::text(dl, ImVec2(x + (width - tw) / 2, y), h2, col(p.text), "TCP Chat");
    y += h2.line() + gap;

    ConnState s = client_.state();
    ImVec4 dot = s == ConnState::Chatting ? p.ok : (s == ConnState::Disconnected && tried_) ? p.danger : p.warn;
    std::string status = statusText();
    float sw = ui::status(nullptr, ImVec2(), p, dot, status);
    ui::status(dl, ImVec2(x + (width - sw) / 2, y), p, dot, status);
    y += st.line() + gap;

    // Server and port on one row.
    const float port_w = 96;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ui::input("##host", "Server", host_, sizeof(host_), width - port_w - 8, p, false);
    ImGui::SetCursorScreenPos(ImVec2(x + width - port_w, y));
    ui::input("##port", "Port", port_text_, sizeof(port_text_), port_w, p, false, ImGuiInputTextFlags_CharsDecimal);
    y += input_h + gap;

    ImGui::SetCursorScreenPos(ImVec2(x, y));
    bool enter = ui::input("##name", "Your name", name_, sizeof(name_), width, p, false,
                           ImGuiInputTextFlags_EnterReturnsTrue);
    y += input_h + gap;

    int port = std::atoi(port_text_);
    bool valid = name_valid(name_) && port > 0 && port < 65536 && host_[0];
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ui::ButtonOpts o;
    o.width = width;
    o.disabled = busy || !valid;
    if (ui::button("Join chat", p, ui::Button::Primary, o) || (enter && valid && !busy)) startConnect();
    y += button_h;

    if (!err_lines.empty()) {
        y += gap;
        for (auto& l : err_lines) {
            ui::text(dl, ImVec2(x, y), err, col(p.danger), l.text);
            y += err.line();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Chat (ChatScreen)                                                   */
/* ------------------------------------------------------------------ */

void App::drawChat() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Palette& p = palette_;

    // .chat-layout: max-width 1200px, centered, panel color, 1px side borders;
    // a 250px user list column unless the window is narrow.
    const bool narrow = vp->WorkSize.x <= 720;
    float w = std::min(1200.0f, vp->WorkSize.x);
    float x0 = std::round(vp->WorkPos.x + (vp->WorkSize.x - w) / 2), y0 = vp->WorkPos.y;
    float x1 = x0 + w, y1 = vp->WorkPos.y + vp->WorkSize.y;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col(p.panel));
    dl->AddLine(ImVec2(x0 + 0.5f, y0), ImVec2(x0 + 0.5f, y1), col(p.border));
    dl->AddLine(ImVec2(x1 - 0.5f, y0), ImVec2(x1 - 0.5f, y1), col(p.border));

    float left = x0 + 1, right = narrow ? x1 - 1 : x1 - 1 - 250;
    float cw = right - left;

    float header_h = drawHeader(ImVec2(left, y0), cw);
    float form_h = inputFormHeight();
    float banner_h = dmBannerHeight(cw);
    float form_y = y1 - form_h, banner_y = form_y - banner_h;
    drawChatArea(ImVec2(left, y0 + header_h), ImVec2(right, banner_y));
    if (banner_h > 0) drawDmBanner(ImVec2(left, banner_y), cw);
    drawInputForm(ImVec2(left, form_y), cw);
    if (!narrow) drawUsers(ImVec2(right, y0), ImVec2(x1 - 1, y1));
}

float App::drawHeader(ImVec2 at, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Palette& p = palette_;
    Font h1{ fonts.bold, rem(1.05f) };
    Font st{ fonts.regular, rem(0.85f) };
    const float titles_h = h1.line() + st.line();
    const float inner = std::max({ 40.0f, 38.0f, titles_h });
    const float h = 10 + inner + 10 + 1;
    const float mid = at.y + 10 + inner / 2;

    // .room-icon: 40px accent circle with "#" (1.3rem, 700)
    ImVec2 c(at.x + 16 + 20, mid);
    dl->AddCircleFilled(c, 20, col(p.accent), 48);
    Font icon{ fonts.bold, rem(1.3f) };
    float iw = ui::text_width(icon, "#");
    ui::text(dl, ImVec2(c.x - iw / 2, c.y - icon.line() / 2), icon, col(p.accent_text), "#");

    float tx = at.x + 16 + 40 + 12, ty = mid - titles_h / 2;
    ui::text(dl, ImVec2(tx, ty), h1, col(p.text), "TCP Chat");
    std::string status = statusText() + " · " + std::to_string(client_.users().size()) + " online · signed in as " +
                         client_.name();
    if (client_.state() == ConnState::Disconnected && reconnect_at_ > 0) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), " · retrying in %.0fs", std::ceil(std::max(0.0, reconnect_at_ - now_s())));
        status += buf;
    }
    ConnState s = client_.state();
    ImVec4 dot = s == ConnState::Chatting ? p.ok : s == ConnState::Disconnected ? p.danger : p.warn;
    ui::status(dl, ImVec2(tx, ty + h1.line()), p, dot, status);

    ImGui::SetCursorScreenPos(ImVec2(at.x + width - 16 - 38, mid - 19));
    if (ui::icon_button("settings", "⚙️", p)) settings_ui_.open = true;

    dl->AddLine(ImVec2(at.x, at.y + h - 0.5f), ImVec2(at.x + width, at.y + h - 0.5f), col(p.border));
    return h;
}

void App::drawChatArea(ImVec2 a, ImVec2 b) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Palette& p = palette_;
    // Wallpaper and dim behind everything in .chat-area, else panel-alt.
    if (!wallpaper_.draw(dl, a, b, settings_, p)) dl->AddRectFilled(a, b, col(p.panel_alt));

    // .typing at the bottom: min-height 1.6em of 0.8rem, padding 2px 20px.
    Font typing{ fonts.italic, rem(0.8f) };
    const float typing_h = std::max(typing.px * 1.6f, typing.line()) + 4;
    std::vector<std::string> names = client_.typingNames();
    std::string t;
    if (names.size() == 1) t = names[0] + " is typing…";
    else if (names.size() == 2) t = names[0] + " and " + names[1] + " are typing…";
    else if (names.size() > 2) t = std::to_string(names.size()) + " people are typing…";
    if (!t.empty()) {
        float box = typing_h - 4;
        ui::text(dl, ImVec2(a.x + 20, b.y - typing_h + 2 + (box - typing.line()) / 2), typing, col(p.muted), t);
    }

    // .messages-box: scrolls, padding 12px 16px 8px.
    ImGui::SetCursorScreenPos(a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 0));
    ImGui::BeginChild("messages", ImVec2(b.x - a.x, b.y - a.y - typing_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    // Follow new messages only if the user is already at the bottom, so
    // scrolling up to read history is not interrupted.
    bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
    draw_chat_view(client_.lines(), p, settings_.compact, ImGui::GetMainViewport()->WorkSize.x <= 720);
    uint64_t v = client_.linesVersion();
    if (v != seen_version_) {
        const ChatLine* last = client_.lines().empty() ? nullptr : &client_.lines().back();
        if (at_bottom || (last && last->own)) ImGui::SetScrollHereY(1.0f);
        seen_version_ = v;
    }
    ImGui::EndChild();
}

// .dm-banner: dm color, white .85rem text, padding 6px 16px, a "×" button.
static std::vector<ui::Run> banner_runs(const std::string& to, const std::string& safety) {
    Font base{ fonts.regular, rem(0.85f) };
    Font strong{ fonts.bold, rem(0.85f) };
    Font code{ fonts.mono, rem(0.8f) };
    ImU32 white = IM_COL32_WHITE;
    return {
        { base, white, "🔒 Private message to " },
        { strong, white, to },
        { base, white, ", end-to-end encrypted. Safety number " },
        { code, white, safety },
    };
}

float App::dmBannerHeight(float width) const {
    const char* to = dmTarget();
    if (!to) return 0;
    Font close{ fonts.semibold, rem(1.2f) };
    float close_w = ui::text_width(close, "×") + 12;
    ImVec2 size = ui::flow(nullptr, ImVec2(), banner_runs(to, client_.safetyWith(to)), width - 32 - 12 - close_w,
                           rem(0.85f) * 1.45f);
    return 6 + std::max(size.y, close.line()) + 6;
}

void App::drawDmBanner(ImVec2 at, float width) {
    const char* to = dmTarget();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float h = dmBannerHeight(width);
    dl->AddRectFilled(at, ImVec2(at.x + width, at.y + h), col(palette_.dm));
    Font close{ fonts.semibold, rem(1.2f) };
    float close_w = ui::text_width(close, "×") + 12;
    float text_w = width - 32 - 12 - close_w;
    std::vector<ui::Run> runs = banner_runs(to, client_.safetyWith(to));
    ImVec2 size = ui::flow(nullptr, ImVec2(), runs, text_w, rem(0.85f) * 1.45f);
    ui::flow(dl, ImVec2(at.x + 16, at.y + (h - size.y) / 2), runs, text_w, rem(0.85f) * 1.45f);

    // The close button: transparent, white "×", padding 0 6px.
    ImVec2 ba(at.x + width - 16 - close_w, at.y + (h - close.line()) / 2);
    ImGui::SetCursorScreenPos(ba);
    if (ImGui::InvisibleButton("##dm-close", ImVec2(close_w, close.line()))) dm_target_.clear();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to the room");
    ui::text(dl, ImVec2(ba.x + 6, ba.y), close, IM_COL32_WHITE, "×");
}

float App::inputFormHeight() const {
    return 1 + 10 + std::max({ 38.0f, ui::input_height(), rem(1) * 1.45f + 20 }) + 10;
}

void App::drawInputForm(ImVec2 at, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Palette& p = palette_;
    const float h = inputFormHeight();
    dl->AddRectFilled(at, ImVec2(at.x + width, at.y + h), col(p.panel));
    dl->AddLine(ImVec2(at.x, at.y + 0.5f), ImVec2(at.x + width, at.y + 0.5f), col(p.border));
    const bool online = client_.state() == ConnState::Chatting;
    const float mid = at.y + 1 + (h - 1) / 2;

    // Emoji button, the pill input, and "Send" (gap 8px, padding 10px 14px).
    float x = at.x + 14;
    ImGui::SetCursorScreenPos(ImVec2(x, mid - 19));
    if (ui::icon_button("emoji", "😊", p, !online) || autorun_.open_emoji) {
        emoji_open_ = !emoji_open_;
        autorun_.open_emoji = false;
    }
    x += 38 + 8;

    Font send_f{ fonts.semibold, rem(1) };
    float send_w = ui::text_width(send_f, "Send") + 36, send_h = send_f.line() + 20;
    float input_w = at.x + width - 14 - send_w - 8 - x;

    ImGui::SetCursorScreenPos(ImVec2(x, mid - ui::input_height() / 2));
    if (focus_input_ && online) {
        ImGui::SetKeyboardFocusHere();
        focus_input_ = false;
    }
    // The callback remembers the cursor so emoji are inserted where it was.
    auto track_cursor = [](ImGuiInputTextCallbackData* d) {
        *static_cast<int*>(d->UserData) = d->CursorPos;
        return 0;
    };
    std::string hint = dmTarget() ? std::string("Private message to ") + dmTarget() + "…" : "Type a message…";
    ImGui::BeginDisabled(!online);
    bool enter = ui::input("##message", hint.c_str(), input_, sizeof(input_), input_w, p, true,
                           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways, track_cursor,
                           &input_cursor_);
    if (ImGui::IsItemEdited()) {
        bool has_text = std::strspn(input_, " \t") != std::strlen(input_);
        if (!dmTarget()) client_.notifyTyping(has_text); // never announce typing a DM
    }
    ImGui::EndDisabled();

    bool has_text = std::strspn(input_, " \t") != std::strlen(input_);
    ImGui::SetCursorScreenPos(ImVec2(at.x + width - 14 - send_w, mid - send_h / 2));
    ui::ButtonOpts o;
    o.radius = send_h / 2;
    o.disabled = !online || !has_text;
    if (ui::button("Send", p, ui::Button::Primary, o) || (enter && online)) submitInput();

    if (emoji_open_) drawEmojiPicker(at);
}

// Common emoji: the same list as the web client's EmojiPicker.jsx.
static const char* const EMOJI_GROUPS[][2] = {
    { "Smileys", "😀 😃 😄 😁 😆 😅 😂 🤣 😊 😇 🙂 😉 😍 🥰 😘 😋 😜 🤪 😎 🤩 🥳 😏 😌 🤔 🤨 😐 😴 🥱 😮 😲 😳 🥺 "
                 "😢 😭 😤 😡 🤯 😱 🤗 🤭 🙄 😬" },
    { "Gestures", "👍 👎 👌 ✌️ 🤞 🤟 🤘 👏 🙌 👐 🤝 🙏 💪 👋 🤙 ☝️ 👉 👈 👆 👇 ✋ 🫶" },
    { "Hearts", "❤️ 🧡 💛 💚 💙 💜 🖤 🤍 💔 💕 💖 💯 ✨ 🔥 ⭐ 🌟 ⚡ 🎉 🎊 🎈" },
    { "Things", "☕ 🍕 🍔 🍟 🍰 🎂 🍿 🍺 🍷 ⚽ 🏀 🎮 🎧 🎵 📷 💻 📱 💡 📌 🔒 🔑 ⏰ 📅 ✅ ❌ ⚠️ 🚀 🌍 🌙 ☀️ 🌧️ 🌈 "
               "🐱 🐶 🌸 🌳" },
};

void App::insertIntoInput(const char* text) {
    size_t len = std::strlen(input_), add = std::strlen(text);
    if (len + add >= sizeof(input_)) return;
    size_t at = std::min(static_cast<size_t>(std::max(0, input_cursor_)), len);
    std::memmove(input_ + at + add, input_ + at, len - at + 1);
    std::memcpy(input_ + at, text, add);
    input_cursor_ = static_cast<int>(at + add);
}

// .emoji-picker: 340px wide, at most 300px tall, 6px above the input form.
void App::drawEmojiPicker(ImVec2 form) {
    const Palette& p = palette_;
    Font h4{ fonts.bold, rem(0.72f) };
    Font emoji{ fonts.regular, rem(1.35f), 1.3f };
    const float width = std::min(340.0f, ImGui::GetMainViewport()->WorkSize.x - 20);
    const float cell_w = (width - 2 - 20) / 8, cell_h = emoji.line() + 8;

    std::vector<std::pair<const char*, std::vector<std::string>>> groups;
    float content_h = 8;
    for (auto& g : EMOJI_GROUPS) {
        std::vector<std::string> list;
        std::string all = g[1];
        for (size_t i = 0; i < all.size();) {
            size_t sp = all.find(' ', i);
            if (sp == std::string::npos) sp = all.size();
            if (sp > i) list.push_back(all.substr(i, sp - i));
            i = sp + 1;
        }
        content_h += 6 + h4.line() + 4 + ((list.size() + 7) / 8) * cell_h;
        groups.push_back({ g[0], list });
    }
    content_h += 10;
    const float height = std::min(300.0f, content_h + 2);
    ImVec2 pos(form.x + 10, form.y - 6 - height);

    // Shadow (0 10px 28px rgba(0, 0, 0, 0.3)) in its own window under the picker.
    ImGui::SetNextWindowPos(ImVec2(pos.x - 30, pos.y - 20));
    ImGui::SetNextWindowSize(ImVec2(width + 60, height + 60));
    ImGui::Begin("##emoji-shadow", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    ui::shadow(ImGui::GetWindowDrawList(), ImVec2(pos.x + 4, pos.y + 4), ImVec2(pos.x + width - 4, pos.y + height),
               14, 10, 28, 0.3f);
    ImGui::End();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, p.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, p.border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::Begin("##emoji", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    bool hovered_picker = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 w0 = ImGui::GetWindowPos();
    float scroll = ImGui::GetScrollY();
    float y = 8;
    for (auto& [name, list] : groups) {
        y += 6;
        std::string upper(name);
        for (char& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        ui::spaced(dl, ImVec2(w0.x + 1 + 10 + 2, w0.y + y - scroll), h4, col(p.muted), upper, h4.px * 0.06f);
        y += h4.line() + 4;
        for (size_t i = 0; i < list.size(); i++) {
            ImVec2 a(w0.x + 1 + 10 + (i % 8) * cell_w, w0.y + y + (i / 8) * cell_h - scroll);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(list[i].c_str());
            bool picked = ImGui::InvisibleButton("##e", ImVec2(cell_w, cell_h));
            bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hovered) dl->AddRectFilled(a, ImVec2(a.x + cell_w, a.y + cell_h), col(p.accent_soft), 8);
            float ew = ui::text_width(emoji, list[i]);
            ui::text(dl, ImVec2(a.x + (cell_w - ew) / 2, a.y + 4), emoji, col(p.text), list[i]);
            // Stays open so several emoji can be added in a row.
            if (picked) {
                insertIntoInput(list[i].c_str());
                focus_input_ = true;
            }
        }
        y += ((list.size() + 7) / 8) * cell_h;
    }
    ImGui::SetCursorPos(ImVec2(0, content_h));
    ImGui::Dummy(ImVec2(1, 0));
    ImGui::End();

    // Closes on Escape or a click outside (like the web picker).
    bool clicked_outside = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered_picker;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || clicked_outside) emoji_open_ = false;
}

/* ------------------------------------------------------------------ */
/* Users (UserList)                                                    */
/* ------------------------------------------------------------------ */

// localeCompare-like order: letters with Turkish marks sort with their base.
static std::string sort_key(const std::string& s) {
    static const char* fold[][2] = {
        { "ç", "c~" }, { "Ç", "c~" }, { "ğ", "g~" }, { "Ğ", "g~" }, { "ı", "i~" }, { "İ", "i" },
        { "ö", "o~" }, { "Ö", "o~" }, { "ş", "s~" }, { "Ş", "s~" }, { "ü", "u~" }, { "Ü", "u~" },
    };
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool done = false;
        for (auto& f : fold) {
            size_t n = std::strlen(f[0]);
            if (s.compare(i, n, f[0]) == 0) {
                out += f[1];
                i += n;
                done = true;
                break;
            }
        }
        if (done) continue;
        char c = s[i++];
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return out;
}

void App::drawUsers(ImVec2 a, ImVec2 b) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Palette& p = palette_;
    dl->AddLine(ImVec2(a.x + 0.5f, a.y), ImVec2(a.x + 0.5f, b.y), col(p.border)); // border-left

    std::vector<std::string> users = client_.users();
    std::sort(users.begin(), users.end(),
              [](const std::string& x, const std::string& y) { return sort_key(x) < sort_key(y); });

    // .user-list: padding 14px 10px; scrolls.
    ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y));
    ImGui::BeginChild("users", ImVec2(b.x - a.x - 1, b.y - a.y));
    dl = ImGui::GetWindowDrawList();
    ImVec2 w0 = ImGui::GetWindowPos();
    float scroll = ImGui::GetScrollY();
    const float x = w0.x + 10, width = b.x - a.x - 1 - 20;
    float y = w0.y + 14 - scroll;

    Font h3{ fonts.bold, rem(0.78f) };
    ui::spaced(dl, ImVec2(x + 8, y), h3, col(p.muted), "ONLINE · " + std::to_string(users.size()), h3.px * 0.06f);
    y += h3.line() + 8;

    Font name_f{ fonts.medium, rem(1) };
    Font you_f{ fonts.regular, rem(0.85f) };
    Font lock_f{ fonts.regular, rem(0.8f) };
    bool any_dm = false;
    for (const std::string& u : users) {
        bool self = u == client_.name();
        bool dm = !self && client_.canDm(u);
        any_dm |= dm;
        const float row_h = 6 + 32 + 6;
        ImVec2 ra(x, y), rb(x + width, y + row_h);

        ImGui::SetCursorScreenPos(ra);
        ImGui::PushID(u.c_str());
        bool clicked = ImGui::InvisibleButton("##row", ImVec2(width, row_h)) && dm;
        bool hovered = ImGui::IsItemHovered() && dm;
        ImGui::PopID();
        if (hovered) ImGui::SetTooltip("Private message to %s", u.c_str());
        if (clicked) {
            dm_target_ = dm_target_ == u ? "" : u;
            focus_input_ = true;
        }
        bool selected = dmTarget() && dm_target_ == u;
        if (selected || hovered) dl->AddRectFilled(ra, rb, col(p.accent_soft), 10);
        if (selected) { // box-shadow: inset 3px 0 0 var(--dm)
            dl->PushClipRect(ra, ImVec2(ra.x + 3, rb.y), true);
            dl->AddRectFilled(ra, rb, col(p.dm), 10);
            dl->PopClipRect();
        }

        ImVec2 av(x + 8, y + 6);
        ui::avatar(dl, av, 32, u);
        // .online-dot: 10px with a 2px panel-colored ring, at right/bottom -1px
        ImVec2 dc(av.x + 32 + 1 - 5, av.y + 32 + 1 - 5);
        dl->AddCircleFilled(dc, 5, col(p.panel), 16);
        dl->AddCircleFilled(dc, 3, col(p.ok), 16);

        float tx = av.x + 32 + 10, ty = y + (row_h - name_f.line()) / 2;
        ui::text(dl, ImVec2(tx, ty), name_f, col(p.text), u);
        if (self) {
            float nw = ui::text_width(name_f, u) + ui::text_width(you_f, " ");
            Font you = you_f;
            you.lh = name_f.line() / you.px; // same line box: baselines line up
            ui::text(dl, ImVec2(tx + nw, ty), you, col(p.muted), "(you)");
        } else if (dm) {
            float lw = ui::text_width(lock_f, "🔒");
            ui::text(dl, ImVec2(rb.x - 8 - lw, y + (row_h - lock_f.line()) / 2), lock_f, col(p.text, 0.8f), "🔒");
        }
        y += row_h + 2;
    }

    if (any_dm) {
        Font hint{ fonts.regular, rem(0.75f) };
        y += 12 - 2;
        for (auto& l : ui::wrap(hint, "Click a 🔒 name for an encrypted private message.", width - 16)) {
            ui::text(dl, ImVec2(x + 8, y), hint, col(p.muted), l.text);
            y += hint.line();
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(w0.x, y + 14));
    ImGui::Dummy(ImVec2(1, 0));
    ImGui::EndChild();
}

void App::submitInput() {
    std::string text = input_;
    // Trim surrounding whitespace.
    size_t b = text.find_first_not_of(" \t");
    size_t e = text.find_last_not_of(" \t");
    text = b == std::string::npos ? "" : text.substr(b, e - b + 1);
    focus_input_ = true; // Enter takes focus away from the field; give it back

    if (text.empty()) return;
    if (text == "/quit" || text == "/exit")
        leave();
    else if (text == "/users")
        client_.requestUsers();
    else if (const char* to = dmTarget()) {
        if (!client_.sendDm(to, text)) return;
    } else if (!client_.sendText(text))
        return; // keep the text so the user can fix it
    input_[0] = '\0';
    input_cursor_ = 0;
}

/* ------------------------------------------------------------------ */
/* Server identity dialogs (like .security-warning)                    */
/* ------------------------------------------------------------------ */

namespace {

// A dialog body laid out top to bottom at a fixed width.
struct Dialog {
    ImDrawList* dl;
    ImVec2 at;
    float width;
    float y = 0;

    void heading(const char* t, const ImVec4& color) {
        Font f{ fonts.bold, rem(1.5f) };
        ui::text(dl, ImVec2(at.x, at.y + y), f, col(color), t);
        y += f.line() + 14;
    }
    void para(const std::string& t, const ImVec4& color, Font f = Font{ fonts.regular, rem(1) }) {
        for (auto& l : ui::wrap(f, t, width)) {
            ui::text(dl, ImVec2(at.x, at.y + y), f, col(color), l.text);
            y += f.line();
        }
        y += 14;
    }
    // A "dt / dd" pair: bold label and a monospace fingerprint.
    void field(const char* label, const std::string& value, const Palette& p) {
        Font dt{ fonts.semibold, rem(1) };
        Font code{ fonts.mono, rem(0.8f) };
        float lw = ui::text_width(dt, label) + 12;
        ui::text(dl, ImVec2(at.x, at.y + y), dt, col(p.text), label);
        // word-break: break-all
        std::string rest = value;
        float yy = y + (dt.line() - code.line()) / 2;
        while (!rest.empty()) {
            size_t n = rest.size();
            while (n > 1 && ui::text_width(code, std::string_view(rest).substr(0, n)) > width - lw) n--;
            ui::text(dl, ImVec2(at.x + lw, at.y + yy), code, col(p.text), std::string_view(rest).substr(0, n));
            rest.erase(0, n);
            yy += code.line();
        }
        y = std::max(y + dt.line(), yy) + 6;
    }
};

// Opens a modal sized to its content (measured in the previous frame).
bool begin_dialog(const char* id, bool want_open, float width, float* height, const ImVec4& border,
                  const Palette& p) {
    if (want_open && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGui::SetNextWindowSize(ImVec2(width, *height));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, p.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2);
    bool open = ImGui::BeginPopupModal(id, nullptr,
                                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    return open;
}

} // namespace

void App::drawTrustPrompt() {
    static float height = 300;
    const Palette& p = palette_;
    if (!begin_dialog("Unknown server", client_.trustPending(), 560, &height, p.accent, p)) return;
    Dialog d{ ImGui::GetWindowDrawList(), ImVec2(ImGui::GetWindowPos().x + 28, ImGui::GetWindowPos().y + 28), 504 };
    d.heading("Unknown server", p.text);
    d.para("This is the first connection to " + client_.host() + ":" + std::to_string(client_.port()) +
               ". Check that the fingerprint below matches the one the server printed when it started.",
           p.text);
    d.field("Fingerprint", client_.serverFingerprint(), p);
    d.y += 8;
    d.para("It will be saved to " + client_.knownServersPath(), p.muted, Font{ fonts.regular, rem(0.85f) });

    ImGui::SetCursorScreenPos(ImVec2(d.at.x, d.at.y + d.y));
    if (ui::button("Trust and connect", p, ui::Button::Primary)) {
        client_.acceptPendingKey();
        ImGui::CloseCurrentPopup();
    }
    float tw = ui::text_width(Font{ fonts.semibold, rem(1) }, "Trust and connect") + 36;
    ImGui::SetCursorScreenPos(ImVec2(d.at.x + tw + 10, d.at.y + d.y));
    if (ui::button("Cancel", p, ui::Button::Link)) {
        client_.rejectPendingKey();
        ImGui::CloseCurrentPopup();
    }
    height = d.y + rem(1) * 1.45f + 20 + 56;
    ImGui::EndPopup();
}

void App::drawIdentityWarning() {
    static float height = 360;
    const Palette& p = palette_;
    if (!begin_dialog("Server identity has changed", client_.identityChanged(), 560, &height, p.danger, p)) return;
    Dialog d{ ImGui::GetWindowDrawList(), ImVec2(ImGui::GetWindowPos().x + 28, ImGui::GetWindowPos().y + 28), 504 };
    d.heading("Server identity has changed", p.danger);
    d.para("The server presented a different key than the one remembered for this address. Someone may be "
           "intercepting the connection, or the server was reinstalled with a new key. The connection was refused.",
           p.text);
    d.field("Expected", client_.pinnedFingerprint(), p);
    d.field("Received", client_.serverFingerprint(), p);
    d.y += 8;
    d.para("If the server operator confirms the new fingerprint, remove the line for " + client_.host() + ":" +
               std::to_string(client_.port()) + " from " + client_.knownServersPath() + ".",
           p.text);
    ImGui::SetCursorScreenPos(ImVec2(d.at.x, d.at.y + d.y));
    if (ui::button("Close", p, ui::Button::Primary)) {
        client_.rejectPendingKey(); // clears the warning state
        ImGui::CloseCurrentPopup();
    }
    height = d.y + rem(1) * 1.45f + 20 + 56;
    ImGui::EndPopup();
}
