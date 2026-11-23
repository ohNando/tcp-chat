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
    void drawTrustPrompt();
    void drawIdentityWarning();
    void startConnect();

    ChatClient client_;
    AutoRun autorun_;
    bool said_ = false;

    char host_[256] = "127.0.0.1";
    int port_ = 8080;
    char name_[NAME_MAX_LEN + 1] = "";
};
