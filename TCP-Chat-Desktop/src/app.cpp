#include "app.h"

#include <cstdio>
#include <algorithm>
#include <cstring>

#include "imgui.h"

App::App(const AutoRun& autorun) : autorun_(autorun) {
    if (autorun_.connect) {
        std::snprintf(host_, sizeof(host_), "%s", autorun_.host.c_str());
        port_ = autorun_.port;
        std::snprintf(name_, sizeof(name_), "%s", autorun_.name.c_str());
        startConnect();
    }
}

void App::startConnect() {
    client_.connect(host_, port_, name_);
}

void App::poll() {
    client_.poll();

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

    bool inRoom = client_.state() == ConnState::Chatting;
    if (inRoom)
        drawChatScreen();
    else
        drawConnectScreen();

    drawTrustPrompt();
    drawIdentityWarning();
    ImGui::End();
}

void App::drawConnectScreen() {
    const float width = 360.0f;
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
    if (ImGui::Button(busy ? "Connecting..." : "Connect", ImVec2(width, 0)) || (enter && valid && !busy))
        startConnect();
    ImGui::EndDisabled();

    if (!busy && !client_.lastError().empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", client_.lastError().c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
}

void App::drawChatScreen() {
    const float users_w = 200.0f;
    const float input_h = ImGui::GetFrameHeightWithSpacing() + 6.0f;
    const float body_h = ImGui::GetContentRegionAvail().y - input_h;
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
    drawInput();
}

void App::drawUsers(float height) {
    ImGui::BeginChild("users", ImVec2(0, height), ImGuiChildFlags_Borders);
    std::vector<std::string> users = client_.users();
    std::sort(users.begin(), users.end());

    ImGui::TextDisabled("ONLINE - %zu", users.size());
    ImGui::Separator();
    for (const std::string& u : users) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "*");
        ImGui::SameLine();
        if (u == client_.name())
            ImGui::Text("%s (you)", u.c_str());
        else
            ImGui::TextUnformatted(u.c_str());
    }
    ImGui::EndChild();
}

void App::drawMessages(float height) {
    ImGui::BeginChild("messages", ImVec2(0, height), ImGuiChildFlags_Borders);
    ImGui::PushTextWrapPos(0.0f); // wrap at the child's right edge

    for (const ChatLine& l : client_.lines()) {
        switch (l.kind) {
            case ChatLine::Chat:
                // "%s" keeps user text out of the format string.
                ImGui::TextWrapped("%s: %s", l.name.c_str(), l.text.c_str());
                break;
            case ChatLine::System:
                ImGui::TextDisabled("%s", l.text.c_str());
                break;
            case ChatLine::Error:
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", l.text.c_str());
                break;
        }
    }

    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void App::drawInput() {
    const float button_w = 90.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button_w - ImGui::GetStyle().ItemSpacing.x);
    if (focus_input_) {
        ImGui::SetKeyboardFocusHere();
        focus_input_ = false;
    }
    bool enter = ImGui::InputTextWithHint("##input", "Type a message, /users or /quit",
                                          input_, sizeof(input_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Send", ImVec2(button_w, 0)) || enter) submitInput();
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
        client_.quit();
    else if (text == "/users")
        client_.requestUsers();
    else if (!client_.sendText(text))
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
