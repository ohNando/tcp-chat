/*
 * Load generator for the chat server.
 *
 *   bench [-c clients] [-r rate] [-d seconds] [-s size] [-S slow] [-P server_pid] [host] [port]
 *
 * Opens `clients` encrypted connections from one thread (epoll), registers
 * them as bench-0, bench-1, ..., then every client sends `rate` messages
 * per second for `seconds`. Each message carries its send time, so when the
 * server broadcasts it back to the sender we get the round-trip latency.
 * Every broadcast copy received by any client counts as one delivery.
 *
 * With -S, that many extra clients join but never read: their backlog grows
 * on the server until it drops them, which exercises the output buffering.
 * With -P the server's CPU time and memory are read from /proc.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <sodium.h>

#include "framer.h"
#include "message.h"
#include "secure.h"

typedef enum { C_CONNECTING, C_HANDSHAKE, C_REGISTERING, C_READY, C_DEAD } cstate_t;

typedef struct {
    int fd;
    int id;
    cstate_t state;
    char name[NAME_MAX_LEN + 1];
    framer_t in;
    secure_t sec;
    uint8_t* out;
    size_t out_len, out_off, out_cap;
    int want_out;
    uint32_t seq;
    double next_send;
    long sent;
    int slow;          /* joins, then stops reading */
} client_t;

static int epfd;
static double* lat_ms;        /* round-trip samples */
static size_t lat_n, lat_cap;
static long deliveries;       /* CHAT frames received by any client */
static long errors;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void die(const char* msg) {
    perror(msg);
    exit(1);
}

/* ---------------- output ---------------- */

static void update_interest(client_t* c) {
    int want = c->state == C_CONNECTING || c->out_off < c->out_len;
    if (want == c->want_out) return;
    struct epoll_event ev = { .events = EPOLLIN | (want ? EPOLLOUT : 0), .data.ptr = c };
    epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);
    c->want_out = want;
}

static void flush(client_t* c) {
    while (c->out_off < c->out_len) {
        ssize_t n = send(c->fd, c->out + c->out_off, c->out_len - c->out_off, MSG_NOSIGNAL);
        if (n > 0) { c->out_off += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
        c->state = C_DEAD;
        errors++;
        return;
    }
    if (c->out_off == c->out_len) c->out_off = c->out_len = 0;
    update_interest(c);
}

static int emit_record(void* ctx, const uint8_t* body, size_t len) {
    client_t* c = ctx;
    size_t need = c->out_len + RECORD_HEADER_SIZE + len;
    if (need > c->out_cap) {
        size_t cap = c->out_cap ? c->out_cap : 4096;
        while (cap < need) cap *= 2;
        uint8_t* p = realloc(c->out, cap);
        if (!p) return -1;
        c->out = p;
        c->out_cap = cap;
    }
    record_put_header(c->out + c->out_len, (uint32_t)len);
    memcpy(c->out + c->out_len + RECORD_HEADER_SIZE, body, len);
    c->out_len = need;
    return 0;
}

static void send_msg(client_t* c, const message_t* m) {
    static uint8_t frame[FRAME_MAX_SIZE];
    int n = message_encode(m, c->seq++, frame, sizeof(frame));
    if (n < 0 || secure_send(&c->sec, frame, (size_t)n, 0) < 0) {
        errors++;
        return;
    }
    flush(c);
}

/* ---------------- input ---------------- */

static void record_latency(double ms) {
    if (lat_n == lat_cap) {
        lat_cap = lat_cap ? lat_cap * 2 : 1 << 16;
        lat_ms = realloc(lat_ms, lat_cap * sizeof(*lat_ms));
        if (!lat_ms) die("realloc");
    }
    lat_ms[lat_n++] = ms;
}

static void on_message(client_t* c, const message_t* m, int measuring) {
    switch (m->type) {
        case FT_WELCOME:
            c->state = C_READY;
            break;
        case FT_CHAT:
            if (!measuring) break;
            deliveries++;
            if (strcmp(m->name, c->name) == 0) {
                /* text starts with the send time in microseconds */
                double sent_us = strtod(m->text, NULL);
                record_latency((now_s() * 1e6 - sent_us) / 1000.0);
            }
            break;
        case FT_PING: {
            message_t pong;
            message_init(&pong, FT_PONG);
            send_msg(c, &pong);
            break;
        }
        case FT_ERR:
            fprintf(stderr, "%s: server error: %s\n", c->name, m->text);
            errors++;
            break;
        default:
            break;
    }
}

static void on_readable(client_t* c, int measuring) {
    static uint8_t plain[RECORD_MAX_BODY];
    static message_t m;

    for (;;) {
        size_t avail;
        uint8_t* space = framer_space(&c->in, &avail);
        ssize_t n = recv(c->fd, space, avail, 0);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            if (c->state != C_DEAD) errors++;
            c->state = C_DEAD;
            return;
        }
        if (n < 0) break;
        framer_commit(&c->in, (size_t)n);

        const uint8_t* body;
        size_t len, plen;
        while (framer_next(&c->in, &body, &len) == 1) {
            int r = secure_recv(&c->sec, body, len, plain, sizeof(plain), &plen);
            if (r == SECURE_ERR) {
                fprintf(stderr, "%s: secure error: %s\n", c->name, c->sec.error);
                c->state = C_DEAD;
                errors++;
                return;
            }
            if (r == SECURE_NONE) {
                if (c->state == C_HANDSHAKE && secure_established(&c->sec)) {
                    c->state = C_REGISTERING;
                    message_init(&m, FT_NAME);
                    snprintf(m.name, sizeof(m.name), "%s", c->name);
                    send_msg(c, &m);
                }
                continue;
            }
            if (message_decode(plain, plen, &m, NULL) == 0) on_message(c, &m, measuring);
        }
    }
}

static void on_writable(client_t* c) {
    if (c->state == C_CONNECTING) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err) {
            fprintf(stderr, "%s: connect: %s\n", c->name, strerror(err));
            c->state = C_DEAD;
            errors++;
            return;
        }
        c->state = C_HANDSHAKE;
        secure_client_init(&c->sec, NULL, emit_record, c);
    }
    flush(c);
}

/* ---------------- server stats ---------------- */

typedef struct { double user_s, sys_s; long rss_kb, hwm_kb; } proc_stat_t;

static int read_proc(int pid, proc_stat_t* s) {
    char path[64], buf[4096];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    /* fields after the ")" that ends the command name: 14=utime 15=stime */
    char* p = strrchr(buf, ')');
    unsigned long ut = 0, st = 0;
    if (!p || sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &st) != 2)
        return -1;
    s->user_s = (double)ut / (double)sysconf(_SC_CLK_TCK);
    s->sys_s = (double)st / (double)sysconf(_SC_CLK_TCK);

    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    f = fopen(path, "r");
    if (!f) return -1;
    s->rss_kb = s->hwm_kb = 0;
    while (fgets(buf, sizeof(buf), f)) {
        sscanf(buf, "VmRSS: %ld", &s->rss_kb);
        sscanf(buf, "VmHWM: %ld", &s->hwm_kb);
    }
    fclose(f);
    return 0;
}

/* ---------------- main ---------------- */

static int cmp_double(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

static double pct(double p) {
    if (lat_n == 0) return 0;
    size_t i = (size_t)(p / 100.0 * (double)(lat_n - 1) + 0.5);
    return lat_ms[i];
}

static void usage(const char* prog) {
    fprintf(stderr,
            "Usage: %s [-c clients] [-r rate] [-d seconds] [-s size] [-S slow] [-P server_pid] [host] [port]\n"
            "  -c  number of connections (default 50)\n"
            "  -r  messages per second per client (default 1)\n"
            "  -d  measuring time in seconds (default 10)\n"
            "  -s  message size in bytes (default 64)\n"
            "  -S  extra clients that join but never read (default 0)\n"
            "  -P  server pid, to report its CPU and memory use\n",
            prog);
}

int main(int argc, char** argv) {
    int nclients = 50, size = 64, pid = 0, nslow = 0;
    double rate = 1, duration = 10;
    int opt;

    while ((opt = getopt(argc, argv, "c:r:d:s:S:P:h")) != -1) {
        switch (opt) {
            case 'c': nclients = atoi(optarg); break;
            case 'r': rate = atof(optarg); break;
            case 'd': duration = atof(optarg); break;
            case 's': size = atoi(optarg); break;
            case 'S': nslow = atoi(optarg); break;
            case 'P': pid = atoi(optarg); break;
            default: usage(argv[0]); return opt == 'h' ? 0 : 2;
        }
    }
    const char* host = optind < argc ? argv[optind] : "127.0.0.1";
    const char* port = optind + 1 < argc ? argv[optind + 1] : "8080";
    if (nslow < 0 || nclients < 1 || rate <= 0 || duration <= 0 || size < 24 || size > TEXT_MAX_LEN) {
        usage(argv[0]);
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);
    if (sodium_init() < 0) return 1;

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *ai;
    if (getaddrinfo(host, port, &hints, &ai) != 0) die("getaddrinfo");

    epfd = epoll_create1(0);
    const int total = nclients + nslow;
    client_t* cl = calloc((size_t)total, sizeof(*cl));
    if (!cl) die("calloc");

    /* ---- connect everybody ---- */
    double t0 = now_s();
    for (int i = 0; i < total; i++) {
        client_t* c = &cl[i];
        c->id = i;
        c->slow = i >= nclients;
        snprintf(c->name, sizeof(c->name), c->slow ? "slow-%d" : "bench-%d", i);
        framer_init(&c->in, RECORD_MAX_BODY);
        c->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (c->fd < 0) die("socket");
        int one = 1;
        setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (connect(c->fd, ai->ai_addr, ai->ai_addrlen) < 0 && errno != EINPROGRESS) die("connect");
        c->state = C_CONNECTING;
        c->want_out = 1;
        struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT, .data.ptr = c };
        epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &ev);
    }
    freeaddrinfo(ai);

    struct epoll_event events[256];
    int ready = 0;
    while (ready + (int)errors < total && now_s() - t0 < 30) {
        int n = epoll_wait(epfd, events, 256, 100);
        for (int i = 0; i < n; i++) {
            client_t* c = events[i].data.ptr;
            if (c->state == C_DEAD) continue;
            if (events[i].events & EPOLLOUT) on_writable(c);
            if (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) on_readable(c, 0);
        }
        ready = 0;
        for (int i = 0; i < total; i++) ready += cl[i].state == C_READY;
    }
    double setup = now_s() - t0;
    printf("connected %d/%d clients in %.2f s (%.1f handshakes/s)\n",
           ready, total, setup, ready / setup);
    if (ready == 0) return 1;

    /* Slow clients stop reading from here on. */
    for (int i = nclients; i < total; i++) {
        struct epoll_event ev = { .events = 0, .data.ptr = &cl[i] };
        epoll_ctl(epfd, EPOLL_CTL_MOD, cl[i].fd, &ev);
    }

    /* ---- measure ---- */
    proc_stat_t before = { 0 }, after = { 0 };
    int have_proc = pid > 0 && read_proc(pid, &before) == 0;

    double start = now_s(), end = start + duration;
    double interval = 1.0 / rate;
    for (int i = 0; i < nclients; i++)  /* spread the first sends over one interval */
        cl[i].next_send = start + interval * i / nclients;

    char text[TEXT_MAX_LEN + 1];
    message_t m;
    while (now_s() < end + 1.0) {        /* one extra second to drain replies */
        double now = now_s();
        double next = end;
        for (int i = 0; i < nclients; i++) {
            client_t* c = &cl[i];
            if (c->state != C_READY || now >= end) continue;
            while (c->next_send <= now) {
                int len = snprintf(text, sizeof(text), "%.0f ", now_s() * 1e6);
                memset(text + len, 'x', (size_t)(size - len));
                text[size] = '\0';
                message_init(&m, FT_MSG);
                memcpy(m.text, text, (size_t)size + 1);
                send_msg(c, &m);
                c->sent++;
                c->next_send += interval;
            }
            if (c->next_send < next) next = c->next_send;
        }
        int timeout = (int)((next - now_s()) * 1000);
        if (timeout < 0) timeout = 0;
        if (now >= end) timeout = 50;
        int n = epoll_wait(epfd, events, 256, timeout);
        for (int i = 0; i < n; i++) {
            client_t* c = events[i].data.ptr;
            if (c->state == C_DEAD || c->slow) continue;
            if (events[i].events & EPOLLOUT) flush(c);
            if (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) on_readable(c, 1);
        }
    }
    if (have_proc) read_proc(pid, &after);

    /* ---- report ---- */
    long sent = 0;
    for (int i = 0; i < nclients; i++) sent += cl[i].sent;
    qsort(lat_ms, lat_n, sizeof(*lat_ms), cmp_double);

    printf("sent        %ld messages (%.0f/s), %d bytes each\n", sent, sent / duration, size);
    printf("delivered   %ld broadcast copies (%.0f/s)\n", deliveries, deliveries / duration);
    printf("echoes      %zu/%ld\n", lat_n, sent);
    printf("latency ms  p50 %.2f  p95 %.2f  p99 %.2f  max %.2f\n",
           pct(50), pct(95), pct(99), lat_n ? lat_ms[lat_n - 1] : 0.0);
    if (have_proc) {
        double wall = duration + 1.0;
        double user = 100.0 * (after.user_s - before.user_s) / wall;
        double sys = 100.0 * (after.sys_s - before.sys_s) / wall;
        printf("server      cpu %.0f%% (user %.0f%%, kernel %.0f%%)  rss %ld KiB  peak %ld KiB\n",
               user + sys, user, sys, after.rss_kb, after.hwm_kb);
    }
    if (nslow) printf("slow        %d clients never read\n", nslow);
    printf("errors      %ld\n", errors);

    for (int i = 0; i < total; i++) {
        close(cl[i].fd);
        framer_free(&cl[i].in);
        free(cl[i].out);
    }
    free(cl);
    free(lat_ms);
    return errors ? 1 : 0;
}
