#include "app.h"

#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>

#include "imgui.h"
#include "chat_view.h"
#include "settings_ui.h"

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static const char* state_text(ConnState s) {
    switch (s) {
        case ConnState::Disconnected: return "Disconnected";
        case ConnState::Connecting:   return "Connecting...";
        case ConnState::Handshaking:  return "Securing connection...";
        case ConnState::Registering:  return "Joining...";
        case ConnState::Chatting:     return "Online";
    }
    return "";
}

App::App(const AutoRun& autorun) : autorun_(autorun), settings_path_(settings_path()) {
    settings_load(settings_, settings_path_);
    applyLook();
    settings_open_ = autorun_.open_settings;
    std::snprintf(host_, sizeof(host_), "%s", settings_.host.c_str());
    port_ = settings_.port;
    std::snprintf(name_, sizeof(name_), "%s", settings_.name.c_str());

    if (autorun_.connect) {
        std::snprintf(host_, sizeof(host_), "%s", autorun_.host.c_str());
        port_ = autorun_.port;
        std::snprintf(name_, sizeof(name_), "%s", autorun_.name.c_str());
        startConnect();
    }
}

void App::applyLook() {
    palette_ = resolve_theme(settings_);
    apply_style(palette_, settings_);
}

void App::saveSettings() {
    settings_save(settings_, settings_path_);
}

void App::startConnect() {
    // Remember what the user typed for next time (not for scripted runs).
    if (!autorun_.connect) {
        settings_.host = host_;
        settings_.port = port_;
        settings_.name = name_;
        saveSettings();
    }
    client_.connect(host_, port_, name_);
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

    if (autorun_.trust && client_.trustPending()) client_.acceptPendingKey();
    if (!said_ && !autorun_.say.empty() && client_.state() == ConnState::Chatting) {
        client_.sendText(autorun_.say);
        said_ = true;
    }
}

void App::draw() {
    // One borderless window covering the whole viewport.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    bool inRoom = joined_;
    if (inRoom)
        drawChatScreen();
    else
        drawConnectScreen();

    drawTrustPrompt();
    drawIdentityWarning();
    ImGui::End();

    if (draw_settings_window(&settings_open_, settings_, palette_)) look_dirty_ = true;
}

void App::drawConnectScreen() {
    const float width = 360.0f;
    ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - 90.0f, ImGui::GetStyle().WindowPadding.y));
    if (ImGui::Button("Settings", ImVec2(90.0f, 0))) settings_open_ = !settings_open_;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPos(ImVec2((avail.x - width) * 0.5f, avail.y * 0.25f));
    ImGui::BeginGroup();

    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("TCP Chat");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();

    ImGui::PushItemWidth(width);
    ImGui::TextUnformatted("Server");
    ImGui::InputText("##host", host_, sizeof(host_));
    ImGui::TextUnformatted("Port");
    ImGui::InputInt("##port", &port_, 0);
    ImGui::TextUnformatted("Name");
    bool enter = ImGui::InputText("##name", name_, sizeof(name_),
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::Spacing();

    bool busy = client_.state() != ConnState::Disconnected;
    bool valid = name_valid(name_) && port_ > 0 && port_ < 65536 && host_[0];
    ImGui::BeginDisabled(busy || !valid);
    if (primary_button(palette_, busy ? "Connecting..." : "Connect", ImVec2(width, 0)) ||
        (enter && valid && !busy))
        startConnect();
    ImGui::EndDisabled();

    if (!busy && !client_.lastError().empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextColored(palette_.danger, "%s", client_.lastError().c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
}

void App::leave() {
    client_.quit();
    joined_ = false;
    reconnect_at_ = 0;
    reconnect_attempt_ = 0;
}

void App::drawStatusBar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFontSize() * 2.4f; // header height
    const float r = h * 0.42f;
    ImVec2 cur = ImGui::GetCursorScreenPos();

    // Room icon: "#" on an accent circle.
    ImVec2 center(cur.x + r, cur.y + h / 2);
    dl->AddCircleFilled(center, r, to_u32(palette_.accent));
    ImVec2 hs = ImGui::CalcTextSize("#");
    dl->AddText(ImVec2(center.x - hs.x / 2, center.y - hs.y / 2), to_u32(palette_.accent_text), "#");

    // Title and a status line under it.
    ImGui::SetCursorScreenPos(ImVec2(cur.x + 2 * r + 12, cur.y + h / 2 - ImGui::GetTextLineHeight()));
    ImGui::BeginGroup();
    ImGui::TextUnformatted("TCP Chat");
    ConnState s = client_.state();
    ImVec4 dot = s == ConnState::Chatting ? palette_.ok : s == ConnState::Disconnected ? palette_.danger : palette_.warn;
    float small = ImGui::GetFontSize() * 0.82f;
    ImVec2 sp = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(sp.x + 4, sp.y + small * 0.62f), 4, to_u32(dot));
    char status[160];
    if (s == ConnState::Disconnected && reconnect_at_ > 0)
        std::snprintf(status, sizeof(status), "Disconnected, retrying in %.0fs",
                      std::ceil(std::max(0.0, reconnect_at_ - now_s())));
    else if (s == ConnState::Chatting)
        std::snprintf(status, sizeof(status), "Online · %zu online · signed in as %s", client_.users().size(),
                      client_.name().c_str());
    else
        std::snprintf(status, sizeof(status), "%s", state_text(s));
    dl->AddText(ImGui::GetFont(), small, ImVec2(sp.x + 14, sp.y), to_u32(palette_.muted), status);
    ImGui::Dummy(ImVec2(ImGui::CalcTextSize(status).x, small));
    ImGui::EndGroup();

    const float button_w = 90.0f;
    ImGui::SetCursorScreenPos(ImVec2(cur.x + ImGui::GetContentRegionAvail().x - 2 * button_w -
                                     ImGui::GetStyle().ItemSpacing.x,
                                     cur.y + (h - ImGui::GetFrameHeight()) / 2));
    if (ImGui::Button("Settings", ImVec2(button_w, 0))) settings_open_ = !settings_open_;
    ImGui::SameLine();
    if (ImGui::Button("Leave", ImVec2(button_w, 0))) leave();
    ImGui::SetCursorScreenPos(ImVec2(cur.x, cur.y + h + 4));
    ImGui::Separator();
}

void App::drawChatScreen() {
    drawStatusBar();
    const float users_w = 200.0f;
    const float input_h = ImGui::GetFrameHeightWithSpacing() + 6.0f;
    const float typing_h = ImGui::GetTextLineHeightWithSpacing() * (dmTarget() ? 2.0f : 1.0f);
    const float body_h = ImGui::GetContentRegionAvail().y - input_h - typing_h;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginGroup();
    {
        ImGui::BeginChild("left", ImVec2(ImGui::GetContentRegionAvail().x - users_w - spacing, body_h));
        drawMessages(ImGui::GetContentRegionAvail().y);
        ImGui::EndChild();
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    drawUsers(body_h);
    drawTyping();
    drawDmBanner();
    drawInput();
}

void App::drawUsers(float height) {
    ImGui::BeginChild("users", ImVec2(0, height), ImGuiChildFlags_Borders);
    std::vector<std::string> users = client_.users();
    std::sort(users.begin(), users.end());

    ImGui::TextDisabled("ONLINE · %zu", users.size());
    ImGui::Spacing();
    bool any_dm = false;
    const float row_h = ImGui::GetFontSize() * 2.0f, r = row_h * 0.4f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const std::string& u : users) {
        bool self = u == client_.name();
        bool dm = !self && client_.canDm(u);
        any_dm |= dm;

        ImGui::PushID(u.c_str());
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        // Click a name to write privately (end-to-end encrypted); again to go back.
        ImGui::BeginDisabled(!dm);
        bool clicked = ImGui::Selectable("##row", dm_target_ == u, ImGuiSelectableFlags_None,
                                         ImVec2(0, row_h));
        ImGui::EndDisabled();
        if (clicked) {
            dm_target_ = dm_target_ == u ? "" : u;
            focus_input_ = true;
        }
        ImVec2 c(p0.x + r + 2, p0.y + row_h / 2);
        draw_avatar(dl, c, r, u);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.72f, c.y + r * 0.72f), 4.5f, to_u32(palette_.panel));
        dl->AddCircleFilled(ImVec2(c.x + r * 0.72f, c.y + r * 0.72f), 3.2f, to_u32(palette_.ok));

        float ty = p0.y + (row_h - ImGui::GetFontSize()) / 2;
        dl->AddText(ImVec2(c.x + r + 10, ty), to_u32(palette_.text), u.c_str());
        if (self) {
            float w = ImGui::CalcTextSize(u.c_str()).x;
            dl->AddText(ImVec2(c.x + r + 14 + w, ty), to_u32(palette_.muted), "(you)");
        } else if (dm) {
            ImVec2 ts = ImGui::CalcTextSize("DM");
            float x = p0.x + ImGui::GetContentRegionAvail().x - ts.x - 6;
            dl->AddText(ImVec2(x, ty), to_u32(palette_.dm), "DM");
        }
        ImGui::PopID();
    }
    if (any_dm) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Click a name for an end-to-end encrypted private message.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

void App::drawMessages(float height) {
    // The chat area uses the darker page color so bubbles stand out.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette_.bg);
    ImGui::BeginChild("messages", ImVec2(0, height), ImGuiChildFlags_Borders);
    ImGui::PopStyleColor();
    // Follow new messages only if the user is already at the bottom, so
    // scrolling up to read history is not interrupted.
    bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
    draw_chat_view(client_.lines(), palette_, settings_.compact);

    uint64_t v = client_.linesVersion();
    if (v != seen_version_) {
        const ChatLine* last = client_.lines().empty() ? nullptr : &client_.lines().back();
        if (at_bottom || (last && last->own)) ImGui::SetScrollHereY(1.0f);
        seen_version_ = v;
    }
    ImGui::EndChild();
}

const char* App::dmTarget() const {
    // The recipient may have left (or lost their key) since we picked them.
    return !dm_target_.empty() && client_.canDm(dm_target_) ? dm_target_.c_str() : nullptr;
}

void App::drawDmBanner() {
    const char* to = dmTarget();
    if (!to) return;
    ImGui::PushStyleColor(ImGuiCol_Text, palette_.dm);
    ImGui::Text("Private message to %s (end-to-end encrypted)   safety number %s", to,
                client_.safetyWith(to).c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("back to room")) dm_target_.clear();
}

void App::drawTyping() {
    std::vector<std::string> names = client_.typingNames();
    if (names.empty())
        ImGui::TextUnformatted(" "); // keep the line so the layout does not jump
    else if (names.size() == 1)
        ImGui::TextDisabled("%s is typing...", names[0].c_str());
    else if (names.size() == 2)
        ImGui::TextDisabled("%s and %s are typing...", names[0].c_str(), names[1].c_str());
    else
        ImGui::TextDisabled("%zu people are typing...", names.size());
}

void App::drawInput() {
    const float button_w = 90.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button_w - ImGui::GetStyle().ItemSpacing.x);
    if (focus_input_) {
        ImGui::SetKeyboardFocusHere();
        focus_input_ = false;
    }
    ImGui::BeginDisabled(client_.state() != ConnState::Chatting);
    bool enter = ImGui::InputTextWithHint("##input", "Type a message, /users or /quit",
                                          input_, sizeof(input_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited()) {
        bool has_text = std::strspn(input_, " \t") != std::strlen(input_);
        if (!dmTarget()) client_.notifyTyping(has_text); // never announce typing a DM
    }
    ImGui::SameLine();
    if (primary_button(palette_, "Send", ImVec2(button_w, 0)) || enter) submitInput();
    ImGui::EndDisabled();
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
}

static void fingerprint_text(const std::string& fp) {
    // Two rows of eight groups read better than one long line.
    std::string row1 = fp.substr(0, 39), row2 = fp.size() > 40 ? fp.substr(40) : "";
    ImGui::TextUnformatted(row1.c_str());
    ImGui::TextUnformatted(row2.c_str());
}

void App::drawTrustPrompt() {
    const char* id = "Unknown server";
    if (client_.trustPending() && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);

    ImGui::SetNextWindowSize(ImVec2(520, 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return;

    ImGui::TextWrapped("This is the first connection to %s:%d. Check that the fingerprint "
                       "below matches the one the server printed when it started.",
                       client_.host().c_str(), client_.port());
    ImGui::Spacing();
    ImGui::TextDisabled("Server fingerprint");
    fingerprint_text(client_.serverFingerprint());
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("It will be saved to %s", client_.knownServersPath().c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (ImGui::Button("Trust and connect", ImVec2(200, 0))) {
        client_.acceptPendingKey();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        client_.rejectPendingKey();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::drawIdentityWarning() {
    const char* id = "Server identity has changed";
    if (client_.identityChanged() && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);

    ImGui::SetNextWindowSize(ImVec2(560, 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.65f, 0.12f, 0.12f, 1.0f));
    bool open = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    if (!open) return;

    ImGui::TextWrapped("The server presented a different key than the one remembered for this "
                       "address. Someone may be intercepting the connection, or the server was "
                       "reinstalled. The connection was refused.");
    ImGui::Spacing();
    ImGui::TextDisabled("Expected");
    fingerprint_text(client_.pinnedFingerprint());
    ImGui::TextDisabled("Received");
    fingerprint_text(client_.serverFingerprint());
    ImGui::Spacing();
    ImGui::TextWrapped("If the change is legitimate, remove the line for %s:%d from %s.",
                       client_.host().c_str(), client_.port(), client_.knownServersPath().c_str());
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
        client_.rejectPendingKey(); // clears the warning state
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
