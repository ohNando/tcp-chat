#define _GNU_SOURCE /* accept4 */

#include "server.h"
#include "identity.h"
#include "network.h"
#include "store.h"
#include "utility.h"
#include "validate.h"
#include "http.h"
#include "ws.h"

#include <sodium.h>

#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <time.h>

static int epfd = -1;
static source_t listener = { SRC_LISTENER, -1 };
static source_t ws_listener = { SRC_WS_LISTENER, -1 };
static source_t sigsrc = { SRC_SIGNAL, -1 };
static int running = 1;
static identity_t identity;
static size_t max_members = SERVER_DEFAULT_MAX_CLIENTS;
static uint64_t idle_timeout_ms = SERVER_DEFAULT_IDLE_TIMEOUT_S * 1000u;
static char web_root[PATH_MAX];   /* resolved; empty = no static files */
static room_t room;
static store_t store;
static uint64_t last_prune_ms;
static int next_conn_id = 1;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void send_error(conn_t* c, uint16_t code, const char* text) {
    message_t m;
    message_init(&m, FT_ERR);
    m.code = code;
    snprintf(m.text, sizeof(m.text), "%s", text);
    room_send_to(c, &m);
}

static void protocol_error(conn_t* c, const char* why) {
    log_info("conn %d (%s): protocol error: %s", c->id, c->peer, why);
    send_error(c, ERR_PROTOCOL, why);
    c->close_after_flush = 1;
}

/* ---------------------------------------------------------------- */
/* Message handlers                                                  */
/* ---------------------------------------------------------------- */

/* ---------------------------------------------------------------- */
/* History: kept in SQLite (store.c), sent in pages                  */
/* ---------------------------------------------------------------- */

typedef struct {
    conn_t* to;
    uint8_t flags;
} history_ctx_t;

static void send_stored(void* ctx, const stored_message_t* sm) {
    history_ctx_t* h = ctx;
    message_t m;
    message_init(&m, FT_CHAT);
    m.flags = h->flags;
    m.id = sm->id;
    m.timestamp = sm->timestamp;
    memcpy(m.name, sm->name, sizeof(m.name));
    memcpy(m.text, sm->text, sizeof(m.text));
    room_send_to(h->to, &m);
}

/* Sends up to `limit` messages older than `before_id` (0 = the newest),
 * oldest first, then HISTORY_END telling whether even older ones exist.
 * `page` marks an answer to HISTORY_REQ, as opposed to the join replay. */
static void history_send(conn_t* c, uint64_t before_id, int limit, int page) {
    history_ctx_t ctx = { c, (uint8_t)(FLAG_HISTORY | (page ? FLAG_PAGE : 0)) };
    int more = 0;
    store_page(&store, before_id, limit, send_stored, &ctx, &more);

    message_t end;
    message_init(&end, FT_HISTORY_END);
    end.flags = ctx.flags;
    end.more = (uint8_t)more;
    room_send_to(c, &end);
}

static void handle_name(conn_t* c, const message_t* in) {
    if (c->registered) {
        send_error(c, ERR_ALREADY_REGISTERED, "You already have a name.");
        return;
    }
    if (!name_valid(in->name)) {
        send_error(c, ERR_NAME_INVALID,
                   "Name must be 2-24 characters: letters, digits, '_', '-' or '.'.");
        return;
    }

    if (room_member_count(&room) >= max_members) {
        send_error(c, ERR_ROOM_FULL, "Chat room is full.");
        c->close_after_flush = 1;
        return;
    }
    if (room_find_name(&room, in->name)) {
        send_error(c, ERR_NAME_TAKEN, "That name is already taken.");
        return;
    }

    memcpy(c->name, in->name, sizeof(c->name));
    c->registered = 1;
    log_info("conn %d registered as '%s'", c->id, c->name);

    /* Past messages first: the welcome line then separates old from new. */
    history_send(c, 0, SERVER_HISTORY_MAX, 0);

    message_t m;
    message_init(&m, FT_WELCOME);
    snprintf(m.text, sizeof(m.text), "Welcome, %s! %zu user(s) online.",
             c->name, room_member_count(&room));
    room_send_to(c, &m);

    /* New members get the current list right away... */
    room_fill_users(&room, &m);
    room_send_to(c, &m);

    /* ...and the DM keys of everyone who already published one. */
    for (size_t i = 0; i < room.max; i++) {
        conn_t* o = room.slots[i];
        if (!o || o == c || !o->registered || !o->has_key) continue;
        message_init(&m, FT_PEER_KEY);
        memcpy(m.name, o->name, sizeof(m.name));
        memcpy(m.key, o->user_pk, DM_KEY_BYTES);
        room_send_to(c, &m);
    }

    message_init(&m, FT_JOIN);
    memcpy(m.name, c->name, sizeof(m.name));
    room_broadcast(&room, &m, c);
}

static void handle_chat(conn_t* c, const message_t* in) {
    if (!text_valid(in->text)) {
        send_error(c, ERR_TEXT_INVALID, "Message is empty, too long or has control characters.");
        return;
    }

    message_t out;
    message_init(&out, FT_CHAT);
    out.timestamp = now_ms();
    memcpy(out.name, c->name, sizeof(out.name));
    memcpy(out.text, in->text, sizeof(out.text));
    /* Saved first, so the broadcast carries the stored id. */
    out.id = store_add(&store, out.timestamp, out.name, out.text);

    /* Sender included: the server is the single source of truth, so
     * clients render their own messages only when they come back. */
    room_broadcast(&room, &out, NULL);
}

/* Stores the member's public DM key and announces it to everybody else. */
static void handle_key(conn_t* c, const message_t* in) {
    memcpy(c->user_pk, in->key, DM_KEY_BYTES);
    c->has_key = 1;

    message_t m;
    message_init(&m, FT_PEER_KEY);
    memcpy(m.name, c->name, sizeof(m.name));
    memcpy(m.key, c->user_pk, DM_KEY_BYTES);
    room_broadcast(&room, &m, c);
}

/* Relays an end-to-end encrypted private message. The server cannot read
 * it: it only checks who it is for, then hands the box to the recipient
 * and echoes it to the sender. Deliberately not logged. */
static void handle_dm(conn_t* c, const message_t* in) {
    if (!c->has_key) {
        send_error(c, ERR_NO_KEY, "Send your key before private messages.");
        return;
    }
    conn_t* to = room_find_name(&room, in->to);
    if (!to) {
        send_error(c, ERR_NO_SUCH_USER, "Nobody with that name is here.");
        return;
    }
    if (!to->has_key) {
        send_error(c, ERR_NO_KEY, "That user cannot receive private messages yet.");
        return;
    }

    message_t m;
    message_init(&m, FT_DM_RECV);
    m.timestamp = now_ms();
    memcpy(m.name, c->name, sizeof(m.name));
    memcpy(m.to, to->name, sizeof(m.to));
    memcpy(m.nonce, in->nonce, DM_NONCE_BYTES);
    m.box_len = in->box_len;
    memcpy(m.box, in->box, in->box_len);
    room_send_to(to, &m);
    if (to != c) room_send_to(c, &m);
}

static void handle_message(conn_t* c, const message_t* m) {
    message_t reply;

    /* Before NAME only a few frames make sense. */
    if (!c->registered && m->type != FT_NAME && m->type != FT_PING && m->type != FT_QUIT) {
        send_error(c, ERR_NOT_REGISTERED, "Send your name first.");
        return;
    }

    switch (m->type) {
        case FT_NAME:
            handle_name(c, m);
            break;
        case FT_MSG:
            handle_chat(c, m);
            break;
        case FT_USERS_REQ:
            room_fill_users(&room, &reply);
            room_send_to(c, &reply);
            break;
        case FT_PING:
            message_init(&reply, FT_PONG);
            room_send_to(c, &reply);
            break;
        case FT_PONG:
            break; /* answer to our heartbeat; receiving it is all that matters */
        case FT_KEY:
            handle_key(c, m);
            break;
        case FT_DM:
            handle_dm(c, m);
            break;
        case FT_TYPING:
            message_init(&reply, FT_USER_TYPING);
            memcpy(reply.name, c->name, sizeof(reply.name));
            reply.active = m->active ? 1 : 0;
            room_broadcast(&room, &reply, c); /* everyone but the typist */
            break;
        case FT_HISTORY_REQ:
            history_send(c, m->id, m->limit ? m->limit : SERVER_HISTORY_MAX, 1);
            break;
        case FT_QUIT:
            c->close_after_flush = 1;
            break;
        default:
            protocol_error(c, "Unexpected frame type from client.");
            break;
    }
}

/* ---------------------------------------------------------------- */
/* Socket events                                                     */
/* ---------------------------------------------------------------- */

/* One record from the transport (TCP length-prefixed record or one
 * WebSocket binary message): run it through the secure channel and, once
 * established, decode and handle the frame inside. */
static void process_record(conn_t* c, const uint8_t* body, size_t len) {
    static uint8_t plain[RECORD_MAX_BODY];
    static message_t m;
    size_t plen;

    int was_established = secure_established(&c->sec);
    int s = secure_recv(&c->sec, body, len, plain, sizeof(plain), &plen);

    if (s == SECURE_ERR) {
        /* No error reply: the channel itself is broken or untrusted. */
        log_info("conn %d (%s): secure channel error: %s", c->id, c->peer,
                 c->sec.error ? c->sec.error : "unknown");
        c->want_close = 1;
        return;
    }
    if (s == SECURE_NONE) {
        if (!was_established && secure_established(&c->sec))
            log_info("conn %d (%s): handshake complete", c->id, c->peer);
        return;
    }

    if (message_decode(plain, plen, &m, NULL) < 0) {
        protocol_error(c, "Malformed frame.");
        return;
    }
    handle_message(c, &m);
    if (c->sec.state == SECURE_PEER_FINISHED) c->close_after_flush = 1;
}

/* ---------------------------------------------------------------- */
/* HTTP and WebSocket                                                */
/* ---------------------------------------------------------------- */

static void http_error(conn_t* c, int status, const char* reason) {
    char resp[512];
    int n = snprintf(resp, sizeof(resp),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: text/plain; charset=utf-8\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n%s\n",
                     status, reason, strlen(reason) + 1, reason);
    conn_queue(c, resp, (size_t)n);
    c->close_after_flush = 1;
}

static const char* mime_type(const char* path) {
    static const struct { const char* ext; const char* type; } types[] = {
        { ".html", "text/html; charset=utf-8" },
        { ".js",   "text/javascript; charset=utf-8" },
        { ".mjs",  "text/javascript; charset=utf-8" },
        { ".css",  "text/css; charset=utf-8" },
        { ".json", "application/json" },
        { ".svg",  "image/svg+xml" },
        { ".png",  "image/png" },
        { ".ico",  "image/x-icon" },
        { ".wasm", "application/wasm" },
        { ".woff2","font/woff2" },
        { ".txt",  "text/plain; charset=utf-8" },
    };
    const char* dot = strrchr(path, '.');
    if (dot)
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
            if (strcmp(dot, types[i].ext) == 0) return types[i].type;
    return "application/octet-stream";
}

/* Maps a request target to a file inside web_root. Rejects anything that
 * could escape it: "..", percent-encoding, backslashes, and symlinks that
 * resolve outside (checked with realpath). Returns 0 or -1. */
static int resolve_static(const char* target, char out[PATH_MAX]) {
    char rel[1024];
    size_t n = strcspn(target, "?#");      /* ignore query and fragment */
    if (n == 0 || n >= sizeof(rel) - 16 || target[0] != '/') return -1;
    memcpy(rel, target, n);
    rel[n] = '\0';

    if (strchr(rel, '%') || strchr(rel, '\\') || strstr(rel, "/..")) return -1;
    if (rel[n - 1] == '/') strcat(rel, "index.html");

    char joined[PATH_MAX];
    if (snprintf(joined, sizeof(joined), "%s%s", web_root, rel) >= (int)sizeof(joined))
        return -1;
    if (!realpath(joined, out)) return -1;

    size_t root_len = strlen(web_root);
    if (strncmp(out, web_root, root_len) != 0 || out[root_len] != '/') return -1;
    return 0;
}

/* Opens path + suffix if it is a regular file (no symlinks), else -1. */
static int open_variant(const char* path, const char* suffix, struct stat* st) {
    char p[PATH_MAX];
    if (snprintf(p, sizeof(p), "%s%s", path, suffix) >= (int)sizeof(p)) return -1;
    int fd = open(p, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, st) < 0 || !S_ISREG(st->st_mode)) {
        close(fd);
        return -1;
    }
    return fd;
}

static void serve_static(conn_t* c, const http_request_t* req) {
    int head = strcmp(req->method, "HEAD") == 0;
    if (!head && strcmp(req->method, "GET") != 0) {
        http_error(c, 405, "Method Not Allowed");
        return;
    }

    char path[PATH_MAX];
    if (!web_root[0] || resolve_static(req->target, path) < 0) {
        http_error(c, 404, "Not Found");
        return;
    }

    struct stat st;
    int fd = open_variant(path, "", &st);
    if (fd < 0) {
        http_error(c, 404, "Not Found");
        return;
    }

    /* Prefer a precompressed copy (made by Web/scripts/compress.mjs) when
     * the browser accepts it: nothing is compressed at request time. */
    const char* encoding = NULL;
    struct stat zst;
    int zfd;
    if (http_header_has_token(req, "Accept-Encoding", "br") &&
        (zfd = open_variant(path, ".br", &zst)) >= 0) {
        encoding = "br";
    } else if (http_header_has_token(req, "Accept-Encoding", "gzip") &&
               (zfd = open_variant(path, ".gz", &zst)) >= 0) {
        encoding = "gzip";
    }
    if (encoding) {
        close(fd);
        fd = zfd;
        st = zst;
    }

    /* Vite puts a content hash in every file name under /assets/, so those
     * can be cached forever; index.html must always be revalidated. */
    int immutable = strncmp(req->target, "/assets/", 8) == 0;

    char hdr[640];
    int hn = snprintf(hdr, sizeof(hdr),
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %lld\r\n"
                      "%s%s%s"
                      "Vary: Accept-Encoding\r\n"
                      "Cache-Control: %s\r\n"
                      "X-Content-Type-Options: nosniff\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      mime_type(path), (long long)st.st_size,
                      encoding ? "Content-Encoding: " : "", encoding ? encoding : "",
                      encoding ? "\r\n" : "",
                      immutable ? "public, max-age=31536000, immutable" : "no-cache");
    conn_queue(c, hdr, (size_t)hn);

    if (head)
        close(fd);
    else
        conn_attach_file(c, fd, st.st_size);
    c->close_after_flush = 1;
    log_info("conn %d (%s): %s %s 200 (%lld bytes%s%s)", c->id, c->peer, req->method,
             req->target, (long long)st.st_size, encoding ? ", " : "", encoding ? encoding : "");
}

/* Handles the first (and only) HTTP request on a connection. */
static void handle_http(conn_t* c) {
    static http_request_t req;
    size_t len;
    uint8_t* data = framer_peek(&c->in, &len);

    int n = http_parse_request(data, len, &req);
    if (n == 0) return;
    if (n < 0) {
        http_error(c, 400, "Bad Request");
        return;
    }
    framer_consume(&c->in, (size_t)n);

    char accept[WS_ACCEPT_SIZE];
    if (ws_check_upgrade(&req, accept) == 0) {
        char resp[256];
        int rn = ws_upgrade_response(resp, sizeof(resp), accept);
        conn_queue(c, resp, (size_t)rn);
        c->transport = TRANSPORT_WS;
        log_info("conn %d (%s): websocket upgrade", c->id, c->peer);
        return;
    }

    if (http_header(&req, "Upgrade"))
        http_error(c, 400, "Bad WebSocket Upgrade");
    else
        serve_static(c, &req);
}

/* Sends a close frame and closes once it is flushed. */
static void ws_fail(conn_t* c, uint16_t code, const char* why) {
    uint8_t frame[4];
    log_info("conn %d (%s): websocket closed (%u): %s", c->id, c->peer, code, why);
    conn_queue(c, frame, ws_close_frame(frame, code));
    c->close_after_flush = 1;
}

/* Appends a fragment to the message being reassembled. */
static int ws_append(conn_t* c, const uint8_t* data, size_t len) {
    if (c->ws_msg_len + len > RECORD_MAX_BODY) return -1;
    if (!c->ws_msg) {
        c->ws_msg = malloc(RECORD_MAX_BODY);
        if (!c->ws_msg) return -1;
    }
    memcpy(c->ws_msg + c->ws_msg_len, data, len);
    c->ws_msg_len += len;
    return 0;
}

static void handle_ws_frame(conn_t* c, const ws_frame_t* f) {
    switch (f->opcode) {
        case WS_OP_BINARY:
            if (c->ws_in_message) {
                ws_fail(c, WS_CLOSE_PROTOCOL, "new message inside a fragmented one");
                return;
            }
            if (f->fin) {
                process_record(c, f->payload, f->len); /* common, zero-copy case */
                return;
            }
            c->ws_in_message = 1;
            c->ws_msg_len = 0;
            if (ws_append(c, f->payload, f->len) < 0)
                ws_fail(c, WS_CLOSE_TOO_BIG, "message too big");
            return;

        case WS_OP_CONT:
            if (!c->ws_in_message) {
                ws_fail(c, WS_CLOSE_PROTOCOL, "continuation without a message");
                return;
            }
            if (ws_append(c, f->payload, f->len) < 0) {
                ws_fail(c, WS_CLOSE_TOO_BIG, "message too big");
                return;
            }
            if (f->fin) {
                c->ws_in_message = 0;
                process_record(c, c->ws_msg, c->ws_msg_len);
            }
            return;

        case WS_OP_TEXT:
            ws_fail(c, WS_CLOSE_UNSUPPORTED, "text frames are not supported");
            return;

        case WS_OP_PING: {
            /* Pong must echo the ping payload (at most 125 bytes). */
            uint8_t hdr[WS_MAX_HEADER_SIZE];
            size_t hlen = ws_frame_header(hdr, WS_OP_PONG, f->len);
            if (conn_queue(c, hdr, hlen) < 0 || conn_queue(c, f->payload, f->len) < 0)
                c->want_close = 1;
            return;
        }

        case WS_OP_PONG:
            return; /* we never send pings, nothing to match */

        case WS_OP_CLOSE: {
            /* Answer with a close frame of our own, then hang up. */
            uint8_t frame[4];
            conn_queue(c, frame, ws_close_frame(frame, WS_CLOSE_NORMAL));
            c->close_after_flush = 1;
            return;
        }

        default:
            return;
    }
}

static void handle_ws(conn_t* c) {
    while (!c->close_after_flush && !c->want_close) {
        size_t len;
        uint8_t* data = framer_peek(&c->in, &len);
        ws_frame_t f;

        int n = ws_parse_frame(data, len, RECORD_MAX_BODY, &f);
        if (n == 0) return;
        if (n < 0) {
            ws_fail(c, WS_CLOSE_PROTOCOL, "invalid frame");
            return;
        }
        handle_ws_frame(c, &f);
        framer_consume(&c->in, (size_t)n);
    }
}

static void handle_readable(conn_t* c) {
    size_t avail;
    uint8_t* space = framer_space(&c->in, &avail);
    if (avail == 0) {
        protocol_error(c, "Input buffer full.");
        return;
    }

    ssize_t n = recv(c->src.fd, space, avail, 0);
    if (n == 0) {
        c->want_close = 1; /* orderly shutdown by peer */
        return;
    }
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            c->want_close = 1;
        return;
    }
    framer_commit(&c->in, (size_t)n);
    c->last_rx_ms = now_ms();

    if (c->transport == TRANSPORT_HTTP) {
        handle_http(c);
        if (c->transport != TRANSPORT_WS) return;
        /* Bytes after the upgrade request are already WebSocket frames. */
    }
    if (c->transport == TRANSPORT_WS) {
        handle_ws(c);
        return;
    }

    const uint8_t* body;
    size_t len;
    int r;
    while (!c->close_after_flush && !c->want_close &&
           (r = framer_next(&c->in, &body, &len)) == 1)
        process_record(c, body, len);
    if (r < 0) protocol_error(c, "Record too large.");
}

static void accept_clients(const source_t* l) {
    transport_t transport = l->kind == SRC_WS_LISTENER ? TRANSPORT_HTTP : TRANSPORT_TCP;

    for (;;) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int fd = accept4(l->fd, (SA*)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                log_info("accept failed: %s", strerror(errno));
            return;
        }

        net_set_nodelay(fd);
        conn_t* c = conn_create(fd, next_conn_id++, &addr, &identity, transport);
        if (!c) {
            close(fd);
            continue;
        }
        if (room_add(&room, c) < 0) {
            /* Even the spare slots are taken: nothing can be sent before a
             * handshake, so just hang up. */
            log_info("rejected %s: no free connection slots", c->peer);
            conn_destroy(c);
            close(fd);
            continue;
        }
        c->created_ms = c->last_rx_ms = now_ms();

        struct epoll_event ev = { .events = EPOLLIN, .data.ptr = c };
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            log_info("epoll_ctl add failed: %s", strerror(errno));
            room_remove(&room, c);
            conn_destroy(c);
            close(fd);
            continue;
        }

        log_info("conn %d connected from %s%s", c->id, c->peer,
                 transport == TRANSPORT_HTTP ? " (http)" : "");
    }
}

/* ---------------------------------------------------------------- */
/* End of loop turn: flush, update interest, close                   */
/* ---------------------------------------------------------------- */

static void close_conn(conn_t* c) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->src.fd, NULL);
    close(c->src.fd);
    room_remove(&room, c);
    log_info("conn %d (%s) closed", c->id, c->peer);

    if (c->registered) {
        message_t m;
        message_init(&m, FT_LEAVE);
        memcpy(m.name, c->name, sizeof(m.name));
        room_broadcast(&room, &m, NULL);
    }
    conn_destroy(c);
}

static void update_conn(conn_t* c) {
    if (conn_has_pending(c) && conn_flush(c) < 0) {
        c->want_close = 1;
        return;
    }
    if (c->close_after_flush && !conn_has_pending(c)) {
        c->want_close = 1;
        return;
    }

    int want_out = conn_has_pending(c);
    if (want_out != c->epoll_out) {
        struct epoll_event ev = {
            .events = EPOLLIN | (want_out ? EPOLLOUT : 0),
            .data.ptr = c,
        };
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->src.fd, &ev) == 0)
            c->epoll_out = want_out;
    }
}

/* Runs about once a second:
 *  - connections that never finish the handshake would otherwise hold a
 *    slot forever (a cheap way to fill the server);
 *  - established connections that went quiet get a PING, and are dropped
 *    when nothing at all arrives for the idle timeout. */
static void check_timeouts(void) {
    uint64_t now = now_ms();
    uint64_t ping_after = idle_timeout_ms * 2 / 5;

    for (size_t i = 0; i < room.max; i++) {
        conn_t* c = room.slots[i];
        if (!c || c->want_close || c->file_fd >= 0) continue;

        if (!secure_established(&c->sec)) {
            if (c->sec.state != SECURE_PEER_FINISHED &&
                now - c->created_ms > SERVER_HANDSHAKE_TIMEOUT_MS) {
                log_info("conn %d (%s): handshake timed out", c->id, c->peer);
                c->want_close = 1;
            }
            continue;
        }

        uint64_t idle = now - c->last_rx_ms;
        if (idle > idle_timeout_ms) {
            log_info("conn %d (%s): no response for %llu s, dropping", c->id, c->peer,
                     (unsigned long long)(idle / 1000));
            c->want_close = 1;
        } else if (idle > ping_after && now - c->last_ping_ms > ping_after) {
            message_t ping;
            message_init(&ping, FT_PING);
            room_send_to(c, &ping);
            c->last_ping_ms = now;
        }
    }
}

/* Old messages are deleted once an hour (when a retention is set). */
static void prune_history(void) {
    uint64_t now = now_ms();
    if (now - last_prune_ms < 3600u * 1000u) return;
    last_prune_ms = now;
    int n = store_prune(&store, now);
    if (n > 0) log_info("deleted %d message(s) past the retention period", n);
}

static void sweep(void) {
    int again;
    check_timeouts();
    prune_history();
    do {
        again = 0;
        for (size_t i = 0; i < room.max; i++) {
            conn_t* c = room.slots[i];
            if (!c) continue;
            if (!c->want_close) update_conn(c);
            if (c->want_close) {
                close_conn(c);
                again = 1; /* closing may have queued data for others */
            }
        }
    } while (again);
}

/* ---------------------------------------------------------------- */

/* SIGINT/SIGTERM arrive as readable data on a signalfd, so shutdown is
 * just another event in the loop instead of an async signal handler. */
static int setup_signals(void) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) return -1;

    sigsrc.fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigsrc.fd < 0) return -1;

    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = &sigsrc };
    return epoll_ctl(epfd, EPOLL_CTL_ADD, sigsrc.fd, &ev);
}

static void handle_signal(void) {
    struct signalfd_siginfo info;
    while (read(sigsrc.fd, &info, sizeof(info)) == sizeof(info)) {
        log_info("received %s, shutting down", strsignal((int)info.ssi_signo));
        running = 0;
    }
}

static void close_all(void) {
    for (size_t i = 0; i < room.max; i++) {
        conn_t* c = room.slots[i];
        if (!c) continue;
        conn_flush(c); /* best effort */
        close(c->src.fd);
        conn_destroy(c);
        room.slots[i] = NULL;
    }
    room.count = 0;
}

static void run(void) {
    struct epoll_event events[SERVER_MAX_EVENTS];

    while (running) {
        /* Wake up at least once a second to expire stale handshakes. */
        int n = epoll_wait(epfd, events, SERVER_MAX_EVENTS, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            log_info("epoll_wait failed: %s", strerror(errno));
            return;
        }

        for (int i = 0; i < n; i++) {
            source_t* src = events[i].data.ptr;
            switch (src->kind) {
                case SRC_LISTENER:
                case SRC_WS_LISTENER:
                    accept_clients(src);
                    break;
                case SRC_SIGNAL:
                    handle_signal();
                    break;
                case SRC_CONN: {
                    conn_t* c = (conn_t*)src;
                    if (c->want_close) break;
                    if (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR))
                        handle_readable(c);
                    /* EPOLLOUT needs no handler: sweep() flushes. */
                    break;
                }
                default:
                    break;
            }
        }
        sweep();
    }
}

static void usage(const char* prog) {
    fprintf(stderr,
            "Usage: %s [-p port] [-w ws_port] [-d web_root] [-m max_clients] [-k key_file] [-t seconds]\n"
            "          [-s history_db] [-H days]\n"
            "  -p  TCP port to listen on (default %d)\n"
            "  -w  HTTP/WebSocket port, 0 to disable (default %d)\n"
            "  -d  directory with the web client to serve over HTTP (default: none)\n"
            "  -m  maximum simultaneous clients, 1-%d (default %d)\n"
            "  -k  server identity key, created if missing (default %s)\n"
            "  -t  drop connections silent for this long, pinging them first (default %d)\n"
            "  -s  message history database, created if missing; :memory: keeps it in RAM (default %s)\n"
            "  -H  delete messages older than this many days, 0 = keep forever (default 0)\n",
            prog, SERVER_DEFAULT_PORT, SERVER_DEFAULT_WS_PORT, USERS_MAX, SERVER_DEFAULT_MAX_CLIENTS,
            IDENTITY_DEFAULT_PATH, SERVER_DEFAULT_IDLE_TIMEOUT_S, SERVER_DEFAULT_HISTORY_DB);
}

/* Parses a decimal integer in [min, max]; returns -1 if invalid. */
static long parse_int(const char* s, long min, long max) {
    char* end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || *s == '\0' || *end != '\0' || v < min || v > max) return -1;
    return v;
}

int main(int argc, char** argv) {
    int port = SERVER_DEFAULT_PORT;
    int ws_port = SERVER_DEFAULT_WS_PORT;
    int max_clients = SERVER_DEFAULT_MAX_CLIENTS;
    const char* key_path = IDENTITY_DEFAULT_PATH;
    const char* history_db = SERVER_DEFAULT_HISTORY_DB;
    int retention_days = 0;
    int opt;

    while ((opt = getopt(argc, argv, "p:w:d:m:k:t:s:H:h")) != -1) {
        long v;
        switch (opt) {
            case 'p':
                if ((v = parse_int(optarg, 1, 65535)) < 0) {
                    fprintf(stderr, "invalid port: %s\n", optarg);
                    return 1;
                }
                port = (int)v;
                break;
            case 'm':
                /* The USERS frame carries at most USERS_MAX names. */
                if ((v = parse_int(optarg, 1, USERS_MAX)) < 0) {
                    fprintf(stderr, "invalid max clients: %s\n", optarg);
                    return 1;
                }
                max_clients = (int)v;
                break;
            case 'w':
                if ((v = parse_int(optarg, 0, 65535)) < 0) {
                    fprintf(stderr, "invalid websocket port: %s\n", optarg);
                    return 1;
                }
                ws_port = (int)v;
                break;
            case 'd':
                if (!realpath(optarg, web_root)) {
                    fprintf(stderr, "invalid web root %s: %s\n", optarg, strerror(errno));
                    return 1;
                }
                break;
            case 't':
                if ((v = parse_int(optarg, 2, 86400)) < 0) {
                    fprintf(stderr, "invalid idle timeout: %s\n", optarg);
                    return 1;
                }
                idle_timeout_ms = (uint64_t)v * 1000u;
                break;
            case 'k':
                key_path = optarg;
                break;
            case 's':
                history_db = optarg;
                break;
            case 'H':
                if ((v = parse_int(optarg, 0, 36500)) < 0) {
                    fprintf(stderr, "invalid history retention: %s\n", optarg);
                    return 1;
                }
                retention_days = (int)v;
                break;
            default:
                usage(argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }

    /* conn_flush() already uses MSG_NOSIGNAL; ignoring SIGPIPE as well
     * protects any other write path from killing the process. */
    signal(SIGPIPE, SIG_IGN);

    if (sodium_init() < 0) {
        fprintf(stderr, "libsodium initialization failed\n");
        return 1;
    }

    int created;
    if (identity_load_or_create(&identity, key_path, &created) < 0) return 1;
    if (created) log_info("generated new identity key in %s", key_path);

    char fp[FINGERPRINT_STR_LEN];
    identity_fingerprint(identity.pk, fp);
    log_info("server fingerprint: %s", fp);

    if (store_open(&store, history_db, retention_days) < 0) return 1;
    log_info("message history in %s%s", history_db, retention_days ? "" : " (kept forever)");
    prune_history();

    /* Extra slots let clients beyond the limit finish the handshake and
     * receive a proper (encrypted) "room full" error. */
    max_members = (size_t)max_clients;
    if (room_init(&room, max_members + SERVER_SPARE_SLOTS) < 0) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    listener.fd = net_listen(port, SERVER_LISTEN_BACKLOG);
    if (listener.fd < 0) {
        perror("listen");
        return 1;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1");
        return 1;
    }
    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = &listener };
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listener.fd, &ev) < 0) {
        perror("epoll_ctl");
        return 1;
    }

    if (ws_port > 0) {
        ws_listener.fd = net_listen(ws_port, SERVER_LISTEN_BACKLOG);
        if (ws_listener.fd < 0) {
            perror("listen (websocket)");
            return 1;
        }
        struct epoll_event wev = { .events = EPOLLIN, .data.ptr = &ws_listener };
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, ws_listener.fd, &wev) < 0) {
            perror("epoll_ctl");
            return 1;
        }
        log_info("http/websocket on port %d", ws_port);
        if (web_root[0]) log_info("serving web client from %s", web_root);
    }

    if (setup_signals() < 0) {
        perror("signalfd");
        return 1;
    }

    log_info("listening on port %d (max %d clients)", port, max_clients);
    run();

    close_all();
    store_close(&store);
    close(sigsrc.fd);
    close(epfd);
    close(listener.fd);
    if (ws_listener.fd >= 0) close(ws_listener.fd);
    room_free(&room);
    identity_wipe(&identity);
    return 0;
}
