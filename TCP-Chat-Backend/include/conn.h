#ifndef CONN_H
#define CONN_H

/*
 * One client connection on the server.
 *
 * Input:  bytes from read() go into `in` (a framer) which yields records.
 * Output: encoded records are appended to `out`. The event loop flushes
 *         it with nonblocking send(); whatever the kernel does not accept
 *         stays buffered until the socket becomes writable again.
 *         A client that never reads cannot block anyone else: once its
 *         backlog exceeds CONN_OUT_MAX it is disconnected.
 */

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#include "framer.h"
#include "message.h"

#define CONN_OUT_MAX (1024 * 1024)

/* Everything registered in epoll starts with a source_t, so the event
 * loop can tell what kind of object `data.ptr` points to. */
typedef enum {
    SRC_LISTENER = 1,
    SRC_SIGNAL,
    SRC_CONN,
} source_kind_t;

typedef struct {
    source_kind_t kind;
    int fd;
} source_t;

typedef struct conn {
    source_t src;                 /* must stay the first member */
    int id;
    char peer[32];                /* "ip:port" for logs */

    framer_t in;

    uint8_t* out;
    size_t out_cap;
    size_t out_off;               /* first byte not yet sent */
    size_t out_len;               /* bytes buffered */
    int epoll_out;                /* EPOLLOUT currently requested */

    uint32_t tx_seq;
    int registered;
    char name[NAME_MAX_LEN + 1];

    int close_after_flush;        /* QUIT or protocol error */
    int want_close;               /* close at the end of this loop turn */
} conn_t;

conn_t* conn_create(int fd, int id, const struct sockaddr_in* addr);
/* Frees memory only; closing the fd is the caller's job. */
void conn_destroy(conn_t* c);

/* Appends raw bytes to the output buffer. -1 if over CONN_OUT_MAX. */
int conn_queue(conn_t* c, const void* data, size_t n);

/* Encodes `m` as a record (length prefix + frame) and queues it. */
int conn_send(conn_t* c, const message_t* m);

/* Sends as much buffered output as the socket accepts.
 * Returns 0 when everything is sent, 1 if data is still pending,
 * -1 on a socket error. */
int conn_flush(conn_t* c);

size_t conn_pending_output(const conn_t* c);

#endif
