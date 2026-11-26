#ifndef CONN_H
#define CONN_H

/*
 * One client connection on the server.
 *
 * Input:  bytes from read() go into `in` (a framer) which yields records,
 *         which are passed to the secure channel `sec`.
 * Output: conn_send() encodes a message, the secure channel encrypts it
 *         and emits the ciphertext record into `out`. There is no
 *         plaintext send path: before the handshake, conn_send() fails.
 *         The event loop flushes `out` with nonblocking send(); whatever
 *         the kernel does not accept stays buffered until the socket
 *         becomes writable again.
 *         A client that never reads cannot block anyone else: once its
 *         backlog exceeds CONN_OUT_MAX it is disconnected.
 */

#include <netinet/in.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>

#include "framer.h"
#include "identity.h"
#include "message.h"
#include "secure.h"
#include "ratelimit.h"

#define CONN_OUT_MAX (1024 * 1024)

/* Everything registered in epoll starts with a source_t, so the event
 * loop can tell what kind of object `data.ptr` points to. */
typedef enum {
    SRC_LISTENER = 1,     /* raw TCP clients */
    SRC_WS_LISTENER,      /* HTTP / WebSocket clients */
    SRC_SIGNAL,
    SRC_CONN,
} source_kind_t;

typedef struct {
    source_kind_t kind;
    int fd;
} source_t;

/* How records travel on this connection. */
typedef enum {
    TRANSPORT_TCP,   /* u32 length prefix + body */
    TRANSPORT_HTTP,  /* waiting for an HTTP request (upgrade or file) */
    TRANSPORT_WS,    /* one WebSocket binary message per record */
} transport_t;

typedef struct conn {
    source_t src;                 /* must stay the first member */
    int id;
    char peer[32];                /* "ip:port" for logs */
    transport_t transport;

    framer_t in;

    uint8_t* out;
    size_t out_cap;
    size_t out_off;               /* first byte not yet sent */
    size_t out_len;               /* bytes buffered */
    int epoll_out;                /* EPOLLOUT currently requested */

    /* Static file being streamed with sendfile() after the headers. */
    int file_fd;
    off_t file_off;
    off_t file_len;

    /* WebSocket message being reassembled from fragments. */
    uint8_t* ws_msg;
    size_t ws_msg_len;
    int ws_in_message;

    secure_t sec;
    uint64_t created_ms;          /* for the handshake timeout */
    uint64_t last_rx_ms;          /* last time any byte arrived */
    uint64_t last_ping_ms;        /* last heartbeat PING we sent */
    bucket_t bucket;              /* flood protection, see ratelimit.h */
    uint32_t dropped;             /* frames dropped since the last accepted one */

    uint32_t tx_seq;
    int registered;
    char name[NAME_MAX_LEN + 1];
    int has_key;                  /* sent KEY: can send and receive DMs */
    uint8_t user_pk[DM_KEY_BYTES];

    int close_after_flush;        /* QUIT or protocol error */
    int want_close;               /* close at the end of this loop turn */
} conn_t;

/* Creates a server-side connection whose secure channel waits for a
 * CLIENT_HELLO signed off by `identity`. */
conn_t* conn_create(int fd, int id, const struct sockaddr_in* addr,
                    const identity_t* identity, transport_t transport);
/* Frees memory only; closing the fd is the caller's job. */
void conn_destroy(conn_t* c);

/* Appends raw bytes to the output buffer. -1 if over CONN_OUT_MAX. */
int conn_queue(conn_t* c, const void* data, size_t n);

/* Queues one record framed for the transport: a 4-byte length prefix on
 * TCP, a binary frame on WebSocket. */
int conn_queue_record(conn_t* c, const uint8_t* body, size_t len);

/* Encodes `m`, encrypts it and queues the record.
 * Fails (-1) before the handshake is complete. */
int conn_send(conn_t* c, const message_t* m);

/* Sends as much buffered output (then the attached file, if any) as the
 * socket accepts.
 * Returns 0 when everything is sent, 1 if data is still pending,
 * -1 on a socket error. */
int conn_flush(conn_t* c);

/* Bytes waiting in the output buffer (not counting a file being sent). */
size_t conn_pending_output(const conn_t* c);

/* True while buffered bytes or file contents are still to be sent. */
int conn_has_pending(const conn_t* c);

/* Streams an open file after the buffered output; takes ownership of fd. */
void conn_attach_file(conn_t* c, int fd, off_t len);

#endif
