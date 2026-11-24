#include "chat_client.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static constexpr double CONNECT_TIMEOUT_S = 5.0;
static constexpr size_t MAX_LINES = 1000;

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static std::string fingerprint_of(const unsigned char pk[IDENTITY_PK_BYTES]) {
    char fp[FINGERPRINT_STR_LEN];
    identity_fingerprint(pk, fp);
    return fp;
}

ChatClient::ChatClient() : scratch_(new message_t), plain_(RECORD_MAX_BODY) {
    framer_init(&in_, RECORD_MAX_BODY);
    char path[KNOWN_PATH_MAX];
    if (known_servers_path(path, sizeof(path)) == 0) known_path_ = path;
}

ChatClient::~ChatClient() {
    closeSocket();
    framer_free(&in_);
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

    // Unknown server: refuse for now and let the user decide in the UI.
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
    addLine(ChatLine::System, "Server key saved: " + server_fp_);
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
            break;
        case FT_CHAT:
            addLine(ChatLine::Chat, m.text, m.name, m.timestamp, name_ == m.name);
            break;
        case FT_JOIN:
            if (std::find(users_.begin(), users_.end(), m.name) == users_.end())
                users_.emplace_back(m.name);
            addLine(ChatLine::System, std::string(m.name) + " joined the chat");
            break;
        case FT_LEAVE:
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
    return sendMessage(*scratch_);
}

bool ChatClient::requestUsers() {
    if (state_ != ConnState::Chatting) return false;
    message_init(scratch_.get(), FT_USERS_REQ);
    return sendMessage(*scratch_);
}

void ChatClient::addLine(ChatLine::Kind kind, std::string text, std::string name,
                         uint64_t ts, bool own) {
    if (lines_.size() >= MAX_LINES) lines_.erase(lines_.begin(), lines_.begin() + MAX_LINES / 10);
    lines_.push_back(ChatLine{ kind, std::move(name), std::move(text), ts, own });
    lines_version_++;
}
