// The desktop UI: draws one ImGui frame from ChatClient state, laid out like
// the web client's screens (Web/src/App.jsx).
#pragma once

#include <future>
#include <string>

#include "chat_client.h"
#include "settings.h"
#include "settings_ui.h"
#include "theme.h"
#include "wallpaper.h"

// Optional start-up actions from the command line (also used for automated
// screenshots).
struct AutoRun {
    bool connect = false;
    std::string host;
    int port = 0;
    std::string name;
    std::string say;
    bool open_settings = false;
    bool open_emoji = false;
    std::string dm;          // open a private chat with this user once possible
    std::string theme;       // overrides the saved look for this run
    std::string wallpaper;   // wallpaper id, or an image path
};

class App {
public:
    explicit App(const AutoRun& autorun);
    void poll();
    void draw();
    ImVec4 clearColor() const { return palette_.bg; }

    // Unread messages while the window is not focused: shown in the title.
    void setFocused(bool focused) { focused_ = focused; }
    std::string windowTitle() const;
    // True once per DM or mention that arrived unfocused: the window should
    // ask for attention (taskbar flash).
    bool takeAttentionRequest();

private:
    // Screens and their parts (positions in screen pixels = CSS px).
    void drawLogin();
    void drawChat();
    float drawHeader(ImVec2 at, float width);
    void drawChatArea(ImVec2 a, ImVec2 b);
    float dmBannerHeight(float width) const;
    void drawDmBanner(ImVec2 at, float width);
    float inputFormHeight() const;
    void drawInputForm(ImVec2 at, float width);
    void drawEmojiPicker(ImVec2 form_top_left);
    void drawUsers(ImVec2 a, ImVec2 b);
    void drawIdentityWarning();

    std::string statusText() const;
    const char* dmTarget() const;
    void insertIntoInput(const char* text);
    void submitInput();
    void leave();
    void startConnect();

    void saveSettings();
    void applyLook();   // after any change to the look settings
    void pollImagePicker();
    void countUnread();
    void notify(const std::string& title, const std::string& body);

    ChatClient client_;
    AutoRun autorun_;
    Settings settings_;
    std::string settings_path_;
    Palette palette_{};
    SettingsUi settings_ui_;
    WallpaperRenderer wallpaper_;
    std::future<std::pair<std::string, bool>> image_pick_;   // file dialog running on a thread
    bool look_dirty_ = false;   // re-apply the style before the next frame
    bool said_ = false;
    bool tried_ = false;        // connected at least once (for the login status line)

    // Set once we got into the room; while set, a dropped connection is
    // retried with exponential backoff instead of going back to login.
    bool joined_ = false;
    int reconnect_attempt_ = 0;
    double reconnect_at_ = 0;

    char host_[256] = "127.0.0.1";
    char port_text_[8] = "8080";
    char name_[NAME_MAX_LEN + 1] = "";
    char input_[TEXT_MAX_LEN + 1] = "";
    bool focused_ = true;
    int unread_ = 0;
    bool attention_ = false;
    uint64_t unread_version_ = 0; // lines version counted last
    int input_cursor_ = 0;        // byte offset of the text cursor
    bool focus_input_ = true;
    bool emoji_open_ = false;
    uint64_t seen_version_ = 0;   // lines version drawn last frame
    uint64_t seen_prepend_ = 0;   // prepend version handled
    bool prepend_settled_ = false;
    float restore_from_bottom_ = -1; // scroll distance from the bottom to keep
    std::string dm_target_;       // private-message recipient, empty = room
};
