#define _GNU_SOURCE /* accept4 */

#include "server.h"
#include "identity.h"
#include "network.h"
#include "utility.h"
#include "validate.h"

#include <sodium.h>

#include <getopt.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <time.h>

static int epfd = -1;
static source_t listener = { SRC_LISTENER, -1 };
static source_t sigsrc = { SRC_SIGNAL, -1 };
static int running = 1;
static identity_t identity;
static size_t max_members = SERVER_DEFAULT_MAX_CLIENTS;
static room_t room;
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

    message_t m;
    message_init(&m, FT_WELCOME);
    snprintf(m.text, sizeof(m.text), "Welcome, %s! %zu user(s) online.",
             c->name, room_member_count(&room));
    room_send_to(c, &m);

    /* New members get the current list right away. */
    room_fill_users(&room, &m);
    room_send_to(c, &m);

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

    /* Sender included: the server is the single source of truth, so
     * clients render their own messages only when they come back. */
    room_broadcast(&room, &out, NULL);
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

    const uint8_t* body;
    size_t len;
    int r;
    static uint8_t plain[RECORD_MAX_BODY];
    static message_t m;

    while (!c->close_after_flush && (r = framer_next(&c->in, &body, &len)) == 1) {
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
            continue;
        }

        if (message_decode(plain, plen, &m, NULL) < 0) {
            protocol_error(c, "Malformed frame.");
            return;
        }
        handle_message(c, &m);
        if (c->sec.state == SECURE_PEER_FINISHED) c->close_after_flush = 1;
    }
    if (r < 0) protocol_error(c, "Record too large.");
}

static void accept_clients(void) {
    for (;;) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int fd = accept4(listener.fd, (SA*)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                log_info("accept failed: %s", strerror(errno));
            return;
        }

        conn_t* c = conn_create(fd, next_conn_id++, &addr, &identity);
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
        c->created_ms = now_ms();

        struct epoll_event ev = { .events = EPOLLIN, .data.ptr = c };
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            log_info("epoll_ctl add failed: %s", strerror(errno));
            room_remove(&room, c);
            conn_destroy(c);
            close(fd);
            continue;
        }

        log_info("conn %d connected from %s", c->id, c->peer);
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
    if (conn_pending_output(c) > 0 && conn_flush(c) < 0) {
        c->want_close = 1;
        return;
    }
    if (c->close_after_flush && conn_pending_output(c) == 0) {
        c->want_close = 1;
        return;
    }

    int want_out = conn_pending_output(c) > 0;
    if (want_out != c->epoll_out) {
        struct epoll_event ev = {
            .events = EPOLLIN | (want_out ? EPOLLOUT : 0),
            .data.ptr = c,
        };
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->src.fd, &ev) == 0)
            c->epoll_out = want_out;
    }
}

static void sweep(void) {
    int again;
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
        int n = epoll_wait(epfd, events, SERVER_MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            log_info("epoll_wait failed: %s", strerror(errno));
            return;
        }

        for (int i = 0; i < n; i++) {
            source_t* src = events[i].data.ptr;
            switch (src->kind) {
                case SRC_LISTENER:
                    accept_clients();
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
            "Usage: %s [-p port] [-m max_clients] [-k key_file]\n"
            "  -p  TCP port to listen on (default %d)\n"
            "  -m  maximum simultaneous clients, 1-%d (default %d)\n"
            "  -k  server identity key, created if missing (default %s)\n",
            prog, SERVER_DEFAULT_PORT, USERS_MAX, SERVER_DEFAULT_MAX_CLIENTS,
            IDENTITY_DEFAULT_PATH);
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
    int max_clients = SERVER_DEFAULT_MAX_CLIENTS;
    const char* key_path = IDENTITY_DEFAULT_PATH;
    int opt;

    while ((opt = getopt(argc, argv, "p:m:k:h")) != -1) {
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
            case 'k':
                key_path = optarg;
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

    if (setup_signals() < 0) {
        perror("signalfd");
        return 1;
    }

    log_info("listening on port %d (max %d clients)", port, max_clients);
    run();

    close_all();
    close(sigsrc.fd);
    close(epfd);
    close(listener.fd);
    room_free(&room);
    identity_wipe(&identity);
    return 0;
}
