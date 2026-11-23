#include "test.h"
#include "conn.h"
#include "network.h"

#include <sys/socket.h>
#include <unistd.h>

/* A connected socket pair stands in for a real TCP connection. */
static conn_t* make_pair(int* peer) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) return NULL;
    net_set_nonblocking(sv[0]);
    *peer = sv[1];
    return conn_create(sv[0], 1, NULL);
}

static void close_pair(conn_t* c, int peer) {
    close(c->src.fd);
    close(peer);
    conn_destroy(c);
}

TEST(sent_message_arrives_as_one_record) {
    int peer;
    conn_t* c = make_pair(&peer);
    message_t m, got;

    message_init(&m, FT_JOIN);
    strcpy(m.name, "ali");
    CHECK_EQ_INT(conn_send(c, &m), 0);
    CHECK(conn_pending_output(c) > 0);
    CHECK_EQ_INT(conn_flush(c), 0);
    CHECK_EQ_INT(conn_pending_output(c), 0);

    uint8_t buf[256];
    ssize_t n = read(peer, buf, sizeof(buf));
    framer_t f;
    const uint8_t* body; size_t len;
    framer_init(&f, RECORD_MAX_BODY);
    framer_feed(&f, buf, (size_t)n);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_INT(message_decode(body, len, &got, NULL), 0);
    CHECK_EQ_INT(got.type, FT_JOIN);
    CHECK_EQ_STR(got.name, "ali");
    framer_free(&f);
    close_pair(c, peer);
}

TEST(sequence_number_increments_per_message) {
    int peer;
    conn_t* c = make_pair(&peer);
    message_t m;
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
    close(peer);

    message_t m;
    message_init(&m, FT_PING);
    conn_send(c, &m);
    /* No SIGPIPE thanks to MSG_NOSIGNAL; just an error. */
    CHECK_EQ_INT(conn_flush(c), -1);
    close(c->src.fd);
    conn_destroy(c);
}

int main(void) {
    RUN(sent_message_arrives_as_one_record);
    RUN(sequence_number_increments_per_message);
    RUN(flush_keeps_remainder_when_peer_is_slow);
    RUN(output_backlog_is_capped);
    RUN(flush_reports_error_when_peer_closed);
    return TEST_REPORT();
}
