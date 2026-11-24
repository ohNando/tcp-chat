#define _GNU_SOURCE /* memmem */
#include "test.h"
#include "conn.h"
#include "network.h"
#include "peer.h"

#include <sys/socket.h>
#include <unistd.h>

/* A connected socket pair stands in for a real TCP connection. */
static conn_t* make_pair(int* peer) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) return NULL;
    net_set_nonblocking(sv[0]);
    *peer = sv[1];
    return conn_create(sv[0], 1, NULL, &test_identity, TRANSPORT_TCP);
}

static void close_pair(conn_t* c, int peer) {
    close(c->src.fd);
    close(peer);
    conn_destroy(c);
}

TEST(send_fails_before_handshake) {
    int peer;
    conn_t* c = make_pair(&peer);
    message_t m;
    message_init(&m, FT_PING);
    CHECK_EQ_INT(conn_send(c, &m), -1);
    CHECK_EQ_INT(conn_pending_output(c), 0);
    close_pair(c, peer);
}

TEST(sent_message_arrives_encrypted_as_one_record) {
    int peer;
    conn_t* c = make_pair(&peer);
    message_t m, got;
    CHECK_EQ_INT(establish(c), 0);

    message_init(&m, FT_JOIN);
    strcpy(m.name, "ali");
    CHECK_EQ_INT(conn_send(c, &m), 0);
    CHECK_EQ_INT(conn_flush(c), 0);
    CHECK_EQ_INT(conn_pending_output(c), 0);

    uint8_t buf[256];
    ssize_t n = read(peer, buf, sizeof(buf));
    CHECK(memmem(buf, (size_t)n, "ali", 3) == NULL); /* no plaintext on the wire */

    framer_t f;
    const uint8_t* body; size_t len, plen;
    framer_init(&f, RECORD_MAX_BODY);
    framer_feed(&f, buf, (size_t)n);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_INT(secure_recv(&test_peer, body, len, test_plain, sizeof(test_plain), &plen),
                 SECURE_DATA);
    CHECK_EQ_INT(message_decode(test_plain, plen, &got, NULL), 0);
    CHECK_EQ_INT(got.type, FT_JOIN);
    CHECK_EQ_STR(got.name, "ali");
    framer_free(&f);
    close_pair(c, peer);
}

TEST(sequence_number_increments_per_message) {
    int peer;
    conn_t* c = make_pair(&peer);
    message_t m;
    establish(c);
    message_init(&m, FT_PING);
    conn_send(c, &m);
    conn_send(c, &m);
    CHECK_EQ_INT(c->tx_seq, 2);
    close_pair(c, peer);
}

TEST(flush_keeps_remainder_when_peer_is_slow) {
    int peer;
    conn_t* c = make_pair(&peer);
    static uint8_t chunk[64 * 1024];
    memset(chunk, 'x', sizeof(chunk));

    /* Queue more than the socket buffer can take; the peer never reads. */
    for (int i = 0; i < 8; i++)
        CHECK_EQ_INT(conn_queue(c, chunk, sizeof(chunk)), 0);
    CHECK_EQ_INT(conn_flush(c), 1);
    CHECK(conn_pending_output(c) > 0);
    close_pair(c, peer);
}

TEST(output_backlog_is_capped) {
    int peer;
    conn_t* c = make_pair(&peer);
    static uint8_t chunk[64 * 1024];

    int rc = 0;
    for (int i = 0; i < 17 && rc == 0; i++)
        rc = conn_queue(c, chunk, sizeof(chunk));
    CHECK_EQ_INT(rc, -1); /* 17 * 64 KiB > 1 MiB */
    close_pair(c, peer);
}

TEST(flush_reports_error_when_peer_closed) {
    int peer;
    conn_t* c = make_pair(&peer);
    establish(c);
    close(peer);

    message_t m;
    message_init(&m, FT_PING);
    conn_send(c, &m);
    /* No SIGPIPE thanks to MSG_NOSIGNAL; just an error. */
    CHECK_EQ_INT(conn_flush(c), -1);
    close(c->src.fd);
    conn_destroy(c);
}

TEST(interleaved_queue_and_partial_flush_keep_byte_order) {
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    int small = 4096;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
    net_set_nonblocking(sv[0]);
    net_set_nonblocking(sv[1]);
    conn_t* c = conn_create(sv[0], 1, NULL, &test_identity, TRANSPORT_TCP);

    /* Write a counting byte pattern in odd-sized chunks while the peer
     * drains slowly; everything must come out once and in order. */
    enum { TOTAL = 600000 };
    static uint8_t chunk[7919];
    uint32_t next_out = 0, next_in = 0;
    int ok = 1;
    while (next_in < TOTAL && ok) {
        if (next_out < TOTAL) {
            size_t n = sizeof(chunk);
            if (n > TOTAL - next_out) n = TOTAL - next_out;
            for (size_t i = 0; i < n; i++) chunk[i] = (uint8_t)((next_out + i) * 31);
            if (conn_queue(c, chunk, n) == 0) next_out += (uint32_t)n;
        }
        conn_flush(c);
        uint8_t buf[3000];
        ssize_t r = read(sv[1], buf, sizeof(buf));
        for (ssize_t i = 0; i < r && ok; i++, next_in++)
            ok = buf[i] == (uint8_t)(next_in * 31);
    }
    CHECK(ok);
    CHECK_EQ_INT(next_in, TOTAL);
    CHECK(c->out_cap <= CONN_OUT_MAX);

    close(sv[0]);
    close(sv[1]);
    conn_destroy(c);
}

int main(void) {
    if (sodium_init() < 0) return 1;
    test_identity_init();
    RUN(send_fails_before_handshake);
    RUN(sent_message_arrives_encrypted_as_one_record);
    RUN(sequence_number_increments_per_message);
    RUN(flush_keeps_remainder_when_peer_is_slow);
    RUN(output_backlog_is_capped);
    RUN(flush_reports_error_when_peer_closed);
    RUN(interleaved_queue_and_partial_flush_keep_byte_order);
    return TEST_REPORT();
}
