#include "app.h"

#include <cstdio>
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
    for (const ChatLine& l : client_.lines()) {
        if (l.kind == ChatLine::Chat)
            ImGui::Text("%s: %s", l.name.c_str(), l.text.c_str());
        else
            ImGui::TextUnformatted(l.text.c_str());
    }
}
