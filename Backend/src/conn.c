#include "conn.h"
#include "network.h"
#include "ws.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

static int emit_record(void* ctx, const uint8_t* body, size_t len) {
    return conn_queue_record(ctx, body, len);
}

conn_t* conn_create(int fd, int id, const struct sockaddr_in* addr,
                    const identity_t* identity, transport_t transport) {
    conn_t* c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    /* A WebSocket frame adds up to 14 header bytes around a record, and
     * the buffer must also hold a whole HTTP request. */
    size_t in_cap = RECORD_MAX_BODY;
    if (transport != TRANSPORT_TCP) in_cap += 2 * WS_MAX_HEADER_SIZE;
    if (in_cap < HTTP_MAX_REQUEST) in_cap = HTTP_MAX_REQUEST;

    if (framer_init(&c->in, in_cap) < 0) {
        free(c);
        return NULL;
    }
    secure_server_init(&c->sec, identity, emit_record, c);
    c->src.kind = SRC_CONN;
    c->transport = transport;
    c->file_fd = -1;
    c->src.fd = fd;
    c->id = id;
    if (addr)
        net_format_addr(addr, c->peer, sizeof(c->peer));
    else
        snprintf(c->peer, sizeof(c->peer), "fd:%d", fd);
    return c;
}

void conn_destroy(conn_t* c) {
    if (!c) return;
    secure_wipe(&c->sec);
    framer_free(&c->in);
    if (c->file_fd >= 0) close(c->file_fd);
    free(c->ws_msg);
    free(c->out);
    free(c);
}

size_t conn_pending_output(const conn_t* c) {
    return c->out_len - c->out_off;
}

/* Moves the unsent bytes to the front of the buffer. */
static void compact(conn_t* c) {
    size_t pending = conn_pending_output(c);
    if (pending) memmove(c->out, c->out + c->out_off, pending);
    c->out_off = 0;
    c->out_len = pending;
}

int conn_queue(conn_t* c, const void* data, size_t n) {
    if (conn_pending_output(c) + n > CONN_OUT_MAX) return -1;

    if (c->out_len + n > c->out_cap) {
        /* Out of room at the end. Reclaim the already-sent prefix only when
         * it is at least as large as what would have to move: every byte
         * copied is then paid for by a byte that was sent, so queueing stays
         * amortized O(1) even for a slow reader with a large backlog
         * (compacting on every call made that quadratic). */
        if (c->out_off > 0 && c->out_off >= conn_pending_output(c)) compact(c);
    }
    if (c->out_len + n > c->out_cap) {
        size_t cap = c->out_cap ? c->out_cap : 4096;
        while (cap < c->out_len + n) cap *= 2;
        if (cap > CONN_OUT_MAX) {
            /* At the size limit: compacting is the only way to make room. */
            compact(c);
            cap = CONN_OUT_MAX;
        }
        if (cap != c->out_cap) {
            uint8_t* p = realloc(c->out, cap);
            if (!p) return -1;
            c->out = p;
            c->out_cap = cap;
        }
    }

    memcpy(c->out + c->out_len, data, n);
    c->out_len += n;
    return 0;
}

int conn_queue_record(conn_t* c, const uint8_t* body, size_t len) {
    uint8_t hdr[WS_MAX_HEADER_SIZE];
    size_t hlen;

    if (len > RECORD_MAX_BODY) return -1;
    if (c->transport == TRANSPORT_WS) {
        hlen = ws_frame_header(hdr, WS_OP_BINARY, len);
    } else if (c->transport == TRANSPORT_TCP) {
        record_put_header(hdr, (uint32_t)len);
        hlen = RECORD_HEADER_SIZE;
    } else {
        return -1; /* no records over plain HTTP */
    }

    /* Both parts or nothing, so a full backlog never leaves half a record. */
    if (conn_pending_output(c) + hlen + len > CONN_OUT_MAX) return -1;
    if (conn_queue(c, hdr, hlen) < 0) return -1;
    return conn_queue(c, body, len);
}

int conn_send(conn_t* c, const message_t* m) {
    /* Single-threaded server: one static scratch buffer is enough and
     * keeps 64 KiB off the stack. */
    static uint8_t frame[FRAME_MAX_SIZE];

    if (!secure_established(&c->sec)) return -1;
    int n = message_encode(m, c->tx_seq, frame, sizeof(frame));
    if (n < 0) return -1;
    c->tx_seq++;
    return secure_send(&c->sec, frame, (size_t)n, 0);
}

int conn_has_pending(const conn_t* c) {
    return conn_pending_output(c) > 0 || c->file_fd >= 0;
}

void conn_attach_file(conn_t* c, int fd, off_t len) {
    if (c->file_fd >= 0) close(c->file_fd);
    c->file_fd = fd;
    c->file_off = 0;
    c->file_len = len;
}

/* sendfile(): the kernel copies file pages straight to the socket. */
static int flush_file(conn_t* c) {
    while (c->file_off < c->file_len) {
        ssize_t n = sendfile(c->src.fd, c->file_fd, &c->file_off,
                             (size_t)(c->file_len - c->file_off));
        if (n > 0) continue;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 1;
        return -1; /* error, or file shrank underneath us */
    }
    close(c->file_fd);
    c->file_fd = -1;
    return 0;
}

int conn_flush(conn_t* c) {
    while (conn_pending_output(c) > 0) {
        /* MSG_NOSIGNAL: a peer that already went away must produce
         * EPIPE here, not a SIGPIPE that kills the whole server. */
        ssize_t n = send(c->src.fd, c->out + c->out_off,
                         conn_pending_output(c), MSG_NOSIGNAL);
        if (n > 0) {
            c->out_off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 1;
        return -1;
    }
    c->out_off = 0;
    c->out_len = 0;
    return c->file_fd >= 0 ? flush_file(c) : 0;
}
