// The desktop UI: draws one ImGui frame from ChatClient state.
#pragma once

#include <string>

#include "chat_client.h"

// Optional start-up actions from the command line (also used for automated
// screenshots).
struct AutoRun {
    bool connect = false;
    std::string host;
    int port = 0;
    std::string name;
    std::string say;
    bool trust = false;
};

class App {
public:
    explicit App(const AutoRun& autorun);
    void poll();
    void draw();

private:
    void drawConnectScreen();
    void drawChatScreen();
    void drawMessages(float height);
    void drawInput();
    void drawTyping();
    void drawUsers(float height);
    void drawStatusBar();
    void leave();
    void submitInput();
    void drawTrustPrompt();
    void drawIdentityWarning();
    void startConnect();

    ChatClient client_;
    AutoRun autorun_;
    bool said_ = false;

    // Set once we got into the room; while set, a dropped connection is
    // retried with exponential backoff instead of going back to login.
    bool joined_ = false;
    int reconnect_attempt_ = 0;
    double reconnect_at_ = 0;

    char host_[256] = "127.0.0.1";
    int port_ = 8080;
    char name_[NAME_MAX_LEN + 1] = "";
    char input_[TEXT_MAX_LEN + 1] = "";
    bool focus_input_ = true;
    uint64_t seen_version_ = 0;   // lines version drawn last frame
};
