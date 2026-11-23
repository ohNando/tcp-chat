#include "conn.h"
#include "network.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

conn_t* conn_create(int fd, int id, const struct sockaddr_in* addr) {
    conn_t* c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    if (framer_init(&c->in, RECORD_MAX_BODY) < 0) {
        free(c);
        return NULL;
    }
    c->src.kind = SRC_CONN;
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
    framer_free(&c->in);
    free(c->out);
    free(c);
}

size_t conn_pending_output(const conn_t* c) {
    return c->out_len - c->out_off;
}

int conn_queue(conn_t* c, const void* data, size_t n) {
    /* Drop already-sent bytes before growing. */
    if (c->out_off > 0) {
        size_t pending = conn_pending_output(c);
        memmove(c->out, c->out + c->out_off, pending);
        c->out_off = 0;
        c->out_len = pending;
    }

    if (c->out_len + n > CONN_OUT_MAX) return -1;

    if (c->out_len + n > c->out_cap) {
        size_t cap = c->out_cap ? c->out_cap : 4096;
        while (cap < c->out_len + n) cap *= 2;
        if (cap > CONN_OUT_MAX) cap = CONN_OUT_MAX;
        uint8_t* p = realloc(c->out, cap);
        if (!p) return -1;
        c->out = p;
        c->out_cap = cap;
    }

    memcpy(c->out + c->out_len, data, n);
    c->out_len += n;
    return 0;
}

int conn_send(conn_t* c, const message_t* m) {
    /* Single-threaded server: one static scratch buffer is enough and
     * keeps 64 KiB off the stack. */
    static uint8_t buf[RECORD_HEADER_SIZE + FRAME_MAX_SIZE];

    int n = message_encode(m, c->tx_seq, buf + RECORD_HEADER_SIZE,
                           sizeof(buf) - RECORD_HEADER_SIZE);
    if (n < 0) return -1;
    record_put_header(buf, (uint32_t)n);
    c->tx_seq++;
    return conn_queue(c, buf, RECORD_HEADER_SIZE + (size_t)n);
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
    return 0;
}
