#include "test.h"
#include "room.h"
#include "peer.h"

/* Room logic only touches the output buffer, so no sockets are needed. */
static conn_t* member(int id, const char* name) {
    conn_t* c = conn_create(-1, id, NULL, &test_identity, TRANSPORT_TCP);
    establish(c);
    if (name) {
        strcpy(c->name, name);
        c->registered = 1;
    }
    return c;
}

TEST(add_until_full_then_reject) {
    room_t r;
    room_init(&r, 2);
    conn_t *a = member(1, NULL), *b = member(2, NULL), *c = member(3, NULL);

    CHECK_EQ_INT(room_add(&r, a), 0);
    CHECK_EQ_INT(room_add(&r, b), 0);
    CHECK_EQ_INT(room_add(&r, c), -1);
    CHECK_EQ_INT(r.count, 2);

    room_remove(&r, a);
    CHECK_EQ_INT(room_add(&r, c), 0);

    conn_destroy(a); conn_destroy(b); conn_destroy(c);
    room_free(&r);
}

TEST(find_name_ignores_case_and_unregistered) {
    room_t r;
    room_init(&r, 4);
    conn_t *a = member(1, "Ali"), *anon = member(2, NULL);
    room_add(&r, a);
    room_add(&r, anon);

    CHECK(room_find_name(&r, "ali") == a);
    CHECK(room_find_name(&r, "veli") == NULL);
    CHECK_EQ_INT(room_member_count(&r), 1);

    conn_destroy(a); conn_destroy(anon);
    room_free(&r);
}

TEST(broadcast_skips_sender_and_unregistered) {
    room_t r;
    room_init(&r, 4);
    conn_t *a = member(1, "ali"), *b = member(2, "veli"), *anon = member(3, NULL);
    room_add(&r, a); room_add(&r, b); room_add(&r, anon);

    message_t m;
    message_init(&m, FT_JOIN);
    strcpy(m.name, "ali");
    room_broadcast(&r, &m, a);

    CHECK_EQ_INT(conn_pending_output(a), 0);
    CHECK(conn_pending_output(b) > 0);
    CHECK_EQ_INT(conn_pending_output(anon), 0);

    conn_destroy(a); conn_destroy(b); conn_destroy(anon);
    room_free(&r);
}

TEST(full_backlog_flags_member_for_close) {
    room_t r;
    room_init(&r, 2);
    conn_t* a = member(1, "ali");
    room_add(&r, a);

    static uint8_t junk[CONN_OUT_MAX];
    conn_queue(a, junk, sizeof(junk));

    message_t m;
    message_init(&m, FT_PING);
    room_broadcast(&r, &m, NULL);
    CHECK(a->want_close);

    conn_destroy(a);
    room_free(&r);
}

TEST(users_message_lists_registered_names) {
    room_t r;
    room_init(&r, 4);
    conn_t *a = member(1, "ali"), *anon = member(2, NULL), *b = member(3, "veli");
    room_add(&r, a); room_add(&r, anon); room_add(&r, b);

    message_t m;
    room_fill_users(&r, &m);
    CHECK_EQ_INT(m.type, FT_USERS);
    CHECK_EQ_INT(m.user_count, 2);
    CHECK_EQ_STR(m.users[0], "ali");
    CHECK_EQ_STR(m.users[1], "veli");

    conn_destroy(a); conn_destroy(anon); conn_destroy(b);
    room_free(&r);
}

int main(void) {
    if (sodium_init() < 0) return 1;
    test_identity_init();
    RUN(add_until_full_then_reject);
    RUN(find_name_ignores_case_and_unregistered);
    RUN(broadcast_skips_sender_and_unregistered);
    RUN(full_backlog_flags_member_for_close);
    RUN(users_message_lists_registered_names);
    return TEST_REPORT();
}
