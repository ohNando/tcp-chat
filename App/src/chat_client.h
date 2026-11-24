// Nonblocking, single-threaded chat client for the desktop app.
//
// Call poll() once per UI frame: it finishes a pending connect(), reads and
// decrypts whatever arrived, and flushes queued output. Nothing blocks, so
// the UI never freezes and no threads or locks are needed.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core.h"

enum class ConnState {
    Disconnected,
    Connecting,   // TCP connect() in progress
    Handshaking,  // secure channel being set up
    Registering,  // NAME sent, waiting for WELCOME or ERR
    Chatting,
};

struct ChatLine {
    enum Kind { Chat, System, Error, Dm } kind;
    std::string name;   // Chat and Dm: the sender
    std::string to;     // Dm only
    std::string text;
    uint64_t timestamp; // ms since epoch, Chat only
    bool own;
    bool history;       // replayed by the server when we joined
};

class ChatClient {
public:
    // `user_key_path` overrides where the private-message key lives
    // (default: ~/.config/tcp-chat/user.key); tests use it for two users.
    explicit ChatClient(const std::string& user_key_path = "");
    ~ChatClient();
    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    // Starts connecting; the name is sent automatically after the handshake.
    bool connect(const std::string& host, int port, const std::string& name);
    void disconnect();
    void poll();

    bool sendText(const std::string& text);
    bool requestUsers();

    // End-to-end encrypted private messages.
    bool canDm(const std::string& name) const { return peers_.count(name) != 0; }
    bool sendDm(const std::string& to, const std::string& text);
    std::string safetyWith(const std::string& name) const;

    // Typing indicator. Call on every edit of the input box; it sends
    // TYPING at most every 2 s, and "stopped" when the box is cleared.
    void notifyTyping(bool has_text);
    // Members currently typing (indicators expire after 5 s).
    std::vector<std::string> typingNames();
    void quit();

    // ---- trust on first use -------------------------------------------
    // When the server key is unknown the handshake is refused and the key
    // is kept here until the user decides.
    bool trustPending() const { return trust_pending_; }
    bool identityChanged() const { return identity_changed_; }
    const std::string& serverFingerprint() const { return server_fp_; }
    const std::string& pinnedFingerprint() const { return pinned_fp_; }
    const std::string& knownServersPath() const { return known_path_; }
    void acceptPendingKey();   // saves the key and reconnects
    void rejectPendingKey();   // also dismisses the identity-changed state

    // ---- state for the UI ------------------------------------------------
    ConnState state() const { return state_; }
    const std::string& host() const { return host_; }
    int port() const { return port_; }
    const std::string& name() const { return name_; }
    const std::vector<std::string>& users() const { return users_; }
    const std::vector<ChatLine>& lines() const { return lines_; }
    const std::string& lastError() const { return last_error_; }
    uint64_t linesVersion() const { return lines_version_; } // bumps on change

private:
    static int emitThunk(void* ctx, const uint8_t* body, size_t len);
    static int verifyThunk(void* ctx, const unsigned char pk[IDENTITY_PK_BYTES]);
    int verify(const unsigned char pk[IDENTITY_PK_BYTES]);

    void finishConnect();
    void readSocket();
    void flushOutput();
    void handleRecord(const uint8_t* body, size_t len);
    void handleMessage(const message_t& m);
    bool sendMessage(const message_t& m, bool final = false);
    void fail(const std::string& why);
    bool alreadyShown(const message_t& chat) const;
    void addLine(ChatLine::Kind kind, std::string text, std::string name = {},
                 uint64_t ts = 0, bool own = false, bool history = false, std::string to = {});
    void onPeerKey(const message_t& m);
    void onDm(const message_t& m);
    void closeSocket();

    int fd_ = -1;
    ConnState state_ = ConnState::Disconnected;
    double connect_started_ = 0;

    std::string host_;
    int port_ = 0;
    std::string hostport_;
    std::string name_;

    framer_t in_{};
    std::vector<uint8_t> out_;
    size_t out_off_ = 0;
    secure_t sec_{};
    uint32_t tx_seq_ = 0;
    std::unique_ptr<message_t> scratch_;    // ~8.5 KB, kept off the stack
    std::vector<uint8_t> plain_;

    std::vector<std::string> users_;
    std::vector<ChatLine> lines_;
    uint64_t lines_version_ = 0;
    std::string last_error_;

    user_key_t my_key_{};
    bool have_key_ = false;
    std::map<std::string, std::array<uint8_t, DM_KEY_BYTES>> peers_;

    std::map<std::string, double> typing_;  // name -> expiry (steady seconds)
    bool typing_active_ = false;
    double typing_sent_at_ = 0;

    bool trust_pending_ = false;
    bool identity_changed_ = false;
    std::string server_fp_, pinned_fp_, known_path_;
    unsigned char pending_pk_[IDENTITY_PK_BYTES] = {};
};
