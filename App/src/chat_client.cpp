#include "chat_client.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static constexpr double CONNECT_TIMEOUT_S = 5.0;
static constexpr size_t MAX_LINES = 1000;
static constexpr double TYPING_RESEND_S = 2.0;
static constexpr double TYPING_SHOW_S = 5.0;

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static std::string fingerprint_of(const unsigned char pk[IDENTITY_PK_BYTES]) {
    char fp[FINGERPRINT_STR_LEN];
    identity_fingerprint(pk, fp);
    return fp;
}

ChatClient::ChatClient(const std::string& user_key_path)
    : scratch_(new message_t), plain_(RECORD_MAX_BODY) {
    framer_init(&in_, RECORD_MAX_BODY);
    char path[KNOWN_PATH_MAX];
    if (known_servers_path(path, sizeof(path)) == 0) known_path_ = path;

    char key_path[CONFIG_PATH_MAX];
    int created = 0;
    if (!user_key_path.empty())
        std::snprintf(key_path, sizeof(key_path), "%s", user_key_path.c_str());
    else if (::user_key_path(key_path, sizeof(key_path)) < 0)
        key_path[0] = '\0';
    have_key_ = key_path[0] && user_key_load_or_create(&my_key_, key_path, &created) == 0;
    if (!have_key_) addLine(ChatLine::Error, "Cannot load the private-message key; DMs are disabled.");
}

ChatClient::~ChatClient() {
    closeSocket();
    framer_free(&in_);
    user_key_wipe(&my_key_);
}

/* ------------------------------------------------------------------ */
/* Connection lifecycle                                                */
/* ------------------------------------------------------------------ */

bool ChatClient::connect(const std::string& host, int port, const std::string& name) {
    disconnect();
    host_ = host;
    port_ = port;
    name_ = name;
    hostport_ = host + ":" + std::to_string(port);
    last_error_.clear();
    trust_pending_ = identity_changed_ = false;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    // Note: getaddrinfo() itself blocks; fine for localhost and IP literals.
    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (rc != 0) {
        fail(std::string("Cannot resolve host: ") + gai_strerror(rc));
        return false;
    }

    fd_ = socket(res->ai_family, res->ai_socktype | SOCK_CLOEXEC, res->ai_protocol);
    if (fd_ < 0) {
        freeaddrinfo(res);
        fail(std::string("socket: ") + std::strerror(errno));
        return false;
    }
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    int one = 1; // small, latency sensitive messages: no Nagle
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    rc = ::connect(fd_, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc < 0 && errno != EINPROGRESS) {
        fail(std::string("connect: ") + std::strerror(errno));
        return false;
    }

    state_ = ConnState::Connecting;
    connect_started_ = now_s();
    return true;
}

void ChatClient::closeSocket() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    secure_wipe(&sec_);
    out_.clear();
    out_off_ = 0;
    framer_free(&in_);
    framer_init(&in_, RECORD_MAX_BODY);
    tx_seq_ = 0;
    users_.clear();
    peers_.clear();
    typing_.clear();
    typing_active_ = false;
}

void ChatClient::disconnect() {
    closeSocket();
    state_ = ConnState::Disconnected;
}

void ChatClient::fail(const std::string& why) {
    last_error_ = why;
    addLine(ChatLine::Error, why);
    disconnect();
}

void ChatClient::quit() {
    if (state_ == ConnState::Chatting) {
        message_init(scratch_.get(), FT_QUIT);
        sendMessage(*scratch_, true);
        flushOutput();
    }
    disconnect();
    addLine(ChatLine::System, "You left the chat.");
}

/* ------------------------------------------------------------------ */
/* Per-frame pump                                                      */
/* ------------------------------------------------------------------ */

void ChatClient::poll() {
    if (fd_ < 0) return;

    pollfd p{ fd_, POLLIN, 0 };
    if (state_ == ConnState::Connecting || out_off_ < out_.size()) p.events |= POLLOUT;
    if (::poll(&p, 1, 0) < 0) return; // zero timeout: never blocks the UI

    if (state_ == ConnState::Connecting) {
        if (p.revents & (POLLOUT | POLLERR | POLLHUP))
            finishConnect();
        else if (now_s() - connect_started_ > CONNECT_TIMEOUT_S)
            fail("Connection timed out.");
        return;
    }

    if (p.revents & (POLLIN | POLLERR | POLLHUP)) readSocket();
    if (fd_ >= 0 && (p.revents & POLLOUT)) flushOutput();
}

void ChatClient::finishConnect() {
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        fail(std::string("Cannot connect: ") + std::strerror(err));
        return;
    }
    state_ = ConnState::Handshaking;
    if (secure_client_init(&sec_, verifyThunk, emitThunk, this) < 0)
        fail("Cannot start the handshake.");
}

void ChatClient::readSocket() {
    size_t avail;
    uint8_t* space = framer_space(&in_, &avail);
    ssize_t n = ::recv(fd_, space, avail, 0);
    if (n == 0) {
        if (!trust_pending_ && !identity_changed_) fail("Server closed the connection.");
        else disconnect();
        return;
    }
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            fail(std::string("Read error: ") + std::strerror(errno));
        return;
    }
    framer_commit(&in_, static_cast<size_t>(n));

    const uint8_t* body;
    size_t len;
    int r;
    while (fd_ >= 0 && (r = framer_next(&in_, &body, &len)) == 1) handleRecord(body, len);
    if (fd_ >= 0 && r < 0) fail("Oversized record from server.");
}

void ChatClient::flushOutput() {
    while (fd_ >= 0 && out_off_ < out_.size()) {
        ssize_t n = ::send(fd_, out_.data() + out_off_, out_.size() - out_off_, MSG_NOSIGNAL);
        if (n > 0) {
            out_off_ += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        fail(std::string("Write error: ") + std::strerror(errno));
        return;
    }
    out_.clear();
    out_off_ = 0;
}

/* ------------------------------------------------------------------ */
/* Secure channel callbacks                                            */
/* ------------------------------------------------------------------ */

int ChatClient::emitThunk(void* ctx, const uint8_t* body, size_t len) {
    auto* self = static_cast<ChatClient*>(ctx);
    uint8_t hdr[RECORD_HEADER_SIZE];
    record_put_header(hdr, static_cast<uint32_t>(len));
    self->out_.insert(self->out_.end(), hdr, hdr + sizeof(hdr));
    self->out_.insert(self->out_.end(), body, body + len);
    return 0;
}

int ChatClient::verifyThunk(void* ctx, const unsigned char pk[IDENTITY_PK_BYTES]) {
    return static_cast<ChatClient*>(ctx)->verify(pk);
}

int ChatClient::verify(const unsigned char pk[IDENTITY_PK_BYTES]) {
    server_fp_ = fingerprint_of(pk);
    if (known_path_.empty()) {
        last_error_ = "Cannot locate the known_servers file (is HOME set?).";
        return -1;
    }

    unsigned char pinned[IDENTITY_PK_BYTES];
    int found = known_servers_lookup(known_path_.c_str(), hostport_.c_str(), pinned);
    if (found < 0) {
        last_error_ = "Cannot read " + known_path_;
        return -1;
    }
    if (found == 1) {
        if (sodium_memcmp(pinned, pk, IDENTITY_PK_BYTES) == 0) return 0;
        pinned_fp_ = fingerprint_of(pinned);
        identity_changed_ = true;
        return -1;
    }

    // Unknown server: refuse for now; the UI decides (the desktop app
    // remembers it right away, trust on first use like the web client).
    std::memcpy(pending_pk_, pk, IDENTITY_PK_BYTES);
    trust_pending_ = true;
    return -1;
}

void ChatClient::acceptPendingKey() {
    if (!trust_pending_) return;
    trust_pending_ = false;
    if (known_servers_add(known_path_.c_str(), hostport_.c_str(), pending_pk_) < 0) {
        fail("Cannot write " + known_path_);
        return;
    }
    connect(host_, port_, name_);
}

void ChatClient::rejectPendingKey() {
    trust_pending_ = identity_changed_ = false;
    disconnect();
}

/* ------------------------------------------------------------------ */
/* Records and messages                                                */
/* ------------------------------------------------------------------ */

void ChatClient::handleRecord(const uint8_t* body, size_t len) {
    size_t plen;
    int r = secure_recv(&sec_, body, len, plain_.data(), plain_.size(), &plen);
    if (r == SECURE_ERR) {
        if (trust_pending_ || identity_changed_) {
            disconnect(); // the UI explains what happened
            return;
        }
        fail(std::string("Secure channel error: ") + (sec_.error ? sec_.error : "unknown"));
        return;
    }
    if (r == SECURE_NONE) {
        if (state_ == ConnState::Handshaking && secure_established(&sec_)) {
            state_ = ConnState::Registering;
            message_init(scratch_.get(), FT_NAME);
            std::snprintf(scratch_->name, sizeof(scratch_->name), "%s", name_.c_str());
            sendMessage(*scratch_);
        }
        return;
    }
    if (message_decode(plain_.data(), plen, scratch_.get(), nullptr) < 0) {
        fail("Malformed frame from server.");
        return;
    }
    handleMessage(*scratch_);
}

void ChatClient::handleMessage(const message_t& m) {
    switch (m.type) {
        case FT_WELCOME:
            state_ = ConnState::Chatting;
            last_error_.clear();
            addLine(ChatLine::System, m.text);
            if (have_key_) {
                static message_t key;
                message_init(&key, FT_KEY);
                std::memcpy(key.key, my_key_.pk, DM_KEY_BYTES);
                sendMessage(key);
            }
            break;
        case FT_PEER_KEY:
            onPeerKey(m);
            break;
        case FT_DM_RECV:
            onDm(m);
            break;
        case FT_CHAT: {
            bool history = (m.flags & FLAG_HISTORY) != 0;
            // History replayed after a reconnect must not repeat lines.
            if (history && alreadyShown(m)) break;
            if (!history) typing_.erase(m.name); // the message ends their typing
            addLine(ChatLine::Chat, m.text, m.name, m.timestamp, name_ == m.name, history);
            break;
        }
        case FT_USER_TYPING:
            if (m.active)
                typing_[m.name] = now_s() + TYPING_SHOW_S;
            else
                typing_.erase(m.name);
            break;
        case FT_JOIN:
            if (std::find(users_.begin(), users_.end(), m.name) == users_.end())
                users_.emplace_back(m.name);
            addLine(ChatLine::System, std::string(m.name) + " joined the chat");
            break;
        case FT_LEAVE:
            typing_.erase(m.name);
            peers_.erase(m.name);
            users_.erase(std::remove(users_.begin(), users_.end(), m.name), users_.end());
            addLine(ChatLine::System, std::string(m.name) + " left the chat");
            break;
        case FT_USERS:
            users_.assign(m.users, m.users + m.user_count);
            break;
        case FT_PING: {
            // Heartbeat: the server drops connections that stay silent.
            static message_t pong;
            message_init(&pong, FT_PONG);
            sendMessage(pong);
            break;
        }
        case FT_ERR:
            last_error_ = m.text;
            addLine(ChatLine::Error, m.text);
            // A rejected NAME ends this attempt; the user picks another one.
            if (state_ == ConnState::Registering) disconnect();
            break;
        default:
            break;
    }
}

bool ChatClient::sendMessage(const message_t& m, bool final) {
    static uint8_t frame[FRAME_MAX_SIZE];
    if (!secure_established(&sec_)) return false;
    int n = message_encode(&m, tx_seq_++, frame, sizeof(frame));
    if (n < 0) return false;
    if (secure_send(&sec_, frame, static_cast<size_t>(n), final) < 0) return false;
    flushOutput();
    return true;
}

bool ChatClient::sendText(const std::string& text) {
    if (state_ != ConnState::Chatting) return false;
    if (!text_valid(text.c_str())) {
        addLine(ChatLine::Error, "Message is empty, too long or has control characters.");
        return false;
    }
    message_init(scratch_.get(), FT_MSG);
    std::snprintf(scratch_->text, sizeof(scratch_->text), "%s", text.c_str());
    typing_active_ = false; // the message itself tells others we stopped
    return sendMessage(*scratch_);
}

/* Keys are pinned per server and name (known_peers); a different key for
 * the same name gets a warning with the new safety number. */
void ChatClient::onPeerKey(const message_t& m) {
    std::array<uint8_t, DM_KEY_BYTES> key;
    std::memcpy(key.data(), m.key, DM_KEY_BYTES);

    char path[CONFIG_PATH_MAX];
    std::string who = hostport_ + "/" + m.name;
    if (config_path("known_peers", "TCPCHAT_KNOWN_PEERS", path, sizeof(path)) == 0) {
        unsigned char pinned[DM_KEY_BYTES];
        int found = known_servers_lookup(path, who.c_str(), pinned);
        if (found == 0) {
            known_servers_add(path, who.c_str(), m.key);
        } else if (found == 1 && sodium_memcmp(pinned, m.key, DM_KEY_BYTES) != 0) {
            peers_[m.name] = key;
            addLine(ChatLine::Error, std::string("The private-message key of ") + m.name +
                    " has CHANGED. They may have a new device, or the server is intercepting. "
                    "Compare the new safety number: " + safetyWith(m.name));
            known_servers_add(path, who.c_str(), m.key);
        }
    }
    peers_[m.name] = key;
}

void ChatClient::onDm(const message_t& m) {
    // Our own echo is "to" someone else; their key opens it too.
    std::string other = name_ == m.name ? m.to : m.name;
    auto it = peers_.find(other);
    uint64_t sent_at;
    static char text[TEXT_MAX_LEN + 1];
    if (!have_key_ || it == peers_.end() ||
        dm_open(&my_key_, it->second.data(), m.nonce, m.box, m.box_len, &sent_at, text) < 0) {
        addLine(ChatLine::Error, std::string("Could not decrypt a private message from ") + m.name + ".");
        return;
    }
    addLine(ChatLine::Dm, text, m.name, m.timestamp, name_ == m.name, false, m.to);
}

bool ChatClient::sendDm(const std::string& to, const std::string& text) {
    auto it = peers_.find(to);
    if (state_ != ConnState::Chatting || !have_key_ || it == peers_.end()) return false;
    message_init(scratch_.get(), FT_DM);
    std::snprintf(scratch_->to, sizeof(scratch_->to), "%s", to.c_str());
    if (dm_seal(&my_key_, it->second.data(), (uint64_t)std::time(nullptr) * 1000u, text.c_str(),
                scratch_->nonce, scratch_->box, &scratch_->box_len) < 0) {
        addLine(ChatLine::Error, "Message is empty, too long or has control characters.");
        return false;
    }
    return sendMessage(*scratch_);
}

std::string ChatClient::safetyWith(const std::string& name) const {
    auto it = peers_.find(name);
    if (!have_key_ || it == peers_.end()) return "";
    char out[DM_SAFETY_STR_LEN];
    dm_safety_number(my_key_.pk, it->second.data(), out);
    return out;
}

void ChatClient::notifyTyping(bool has_text) {
    if (state_ != ConnState::Chatting) return;
    double now = now_s();
    if (has_text && typing_active_ && now - typing_sent_at_ < TYPING_RESEND_S) return;
    if (!has_text && !typing_active_) return;

    static message_t m;
    message_init(&m, FT_TYPING);
    m.active = has_text ? 1 : 0;
    if (sendMessage(m)) {
        typing_active_ = has_text;
        typing_sent_at_ = now;
    }
}

std::vector<std::string> ChatClient::typingNames() {
    double now = now_s();
    std::vector<std::string> names;
    for (auto it = typing_.begin(); it != typing_.end();) {
        if (it->second <= now) {
            it = typing_.erase(it);
        } else {
            names.push_back(it->first);
            ++it;
        }
    }
    return names;
}

bool ChatClient::requestUsers() {
    if (state_ != ConnState::Chatting) return false;
    message_init(scratch_.get(), FT_USERS_REQ);
    return sendMessage(*scratch_);
}

bool ChatClient::alreadyShown(const message_t& chat) const {
    // Timestamps are not unique on their own (two messages can share a
    // millisecond), so compare the whole message.
    for (const ChatLine& l : lines_)
        if (l.kind == ChatLine::Chat && l.timestamp == chat.timestamp &&
            l.name == chat.name && l.text == chat.text)
            return true;
    return false;
}

void ChatClient::addLine(ChatLine::Kind kind, std::string text, std::string name,
                         uint64_t ts, bool own, bool history, std::string to) {
    if (lines_.size() >= MAX_LINES) lines_.erase(lines_.begin(), lines_.begin() + MAX_LINES / 10);
    // System lines have no server timestamp: use the time we got them, so
    // the chat view can put them under the right day.
    if (ts == 0)
        ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    lines_.push_back(ChatLine{ kind, std::move(name), std::move(to), std::move(text), ts, own, history });
    lines_version_++;
}
