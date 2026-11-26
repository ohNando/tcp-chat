#include "test.h"
#include "message.h"

static uint8_t wire[FRAME_MAX_SIZE];
static message_t in, out;

/* Encodes `in`, decodes into `out`, returns decode result. */
static int round_trip(void) {
    int n = message_encode(&in, 7, wire, sizeof(wire));
    if (n < 0) return -1;
    uint32_t seq = 0;
    int r = message_decode(wire, (size_t)n, &out, &seq);
    if (r == 0 && seq != 7) return -1;
    return r;
}

TEST(name_round_trips) {
    message_init(&in, FT_NAME);
    strcpy(in.name, "emirhan");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.type, FT_NAME);
    CHECK_EQ_STR(out.name, "emirhan");
}

TEST(msg_and_welcome_round_trip) {
    message_init(&in, FT_MSG);
    strcpy(in.text, "merhaba dünya");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_STR(out.text, "merhaba dünya");

    message_init(&in, FT_WELCOME);
    strcpy(in.text, "welcome");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_STR(out.text, "welcome");
}

TEST(chat_round_trips) {
    message_init(&in, FT_CHAT);
    in.id = 0x0102030405060708ULL;
    in.timestamp = 1763900000123ULL;
    strcpy(in.name, "ali");
    strcpy(in.text, "selam");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK(out.id == 0x0102030405060708ULL);
    CHECK(out.timestamp == 1763900000123ULL);
    CHECK_EQ_STR(out.name, "ali");
    CHECK_EQ_STR(out.text, "selam");
}

TEST(join_and_leave_round_trip) {
    message_init(&in, FT_JOIN);
    strcpy(in.name, "veli");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_STR(out.name, "veli");

    message_init(&in, FT_LEAVE);
    strcpy(in.name, "veli");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.type, FT_LEAVE);
}

TEST(users_round_trip) {
    message_init(&in, FT_USERS);
    in.user_count = 3;
    strcpy(in.users[0], "a");
    strcpy(in.users[1], "bb");
    strcpy(in.users[2], "ccc");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.user_count, 3);
    CHECK_EQ_STR(out.users[2], "ccc");
}

TEST(err_round_trips) {
    message_init(&in, FT_ERR);
    in.code = ERR_NAME_TAKEN;
    strcpy(in.text, "name taken");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.code, ERR_NAME_TAKEN);
    CHECK_EQ_STR(out.text, "name taken");
}

TEST(empty_messages_round_trip) {
    const uint8_t types[] = { FT_USERS_REQ, FT_QUIT, FT_PING, FT_PONG };
    for (size_t i = 0; i < sizeof(types); i++) {
        message_init(&in, types[i]);
        int n = message_encode(&in, 0, wire, sizeof(wire));
        CHECK_EQ_INT(n, FRAME_HEADER_SIZE);
        CHECK_EQ_INT(message_decode(wire, (size_t)n, &out, NULL), 0);
        CHECK_EQ_INT(out.type, types[i]);
    }
}

TEST(decode_rejects_truncated_payload) {
    message_init(&in, FT_CHAT);
    strcpy(in.name, "ali");
    strcpy(in.text, "selam");
    int n = message_encode(&in, 0, wire, sizeof(wire));
    CHECK_EQ_INT(message_decode(wire, (size_t)n - 1, &out, NULL), -1);
}

TEST(decode_rejects_trailing_bytes) {
    message_init(&in, FT_NAME);
    strcpy(in.name, "ali");
    int n = message_encode(&in, 0, wire, sizeof(wire));
    /* Grow the declared payload length by one and append a byte. */
    wire[11] += 1;
    wire[n] = 0xFF;
    CHECK_EQ_INT(message_decode(wire, (size_t)n + 1, &out, NULL), -1);
}

TEST(decode_rejects_length_mismatch) {
    message_init(&in, FT_PING);
    int n = message_encode(&in, 0, wire, sizeof(wire));
    /* Frame buffer is longer than the header says. */
    CHECK_EQ_INT(message_decode(wire, (size_t)n + 4, &out, NULL), -1);
}

TEST(decode_rejects_overlong_name) {
    /* Hand-craft a NAME frame whose string is longer than NAME_MAX_LEN. */
    message_init(&in, FT_MSG);
    memset(in.text, 'x', NAME_MAX_LEN + 1);
    int n = message_encode(&in, 0, wire, sizeof(wire));
    wire[1] = FT_NAME;
    CHECK_EQ_INT(message_decode(wire, (size_t)n, &out, NULL), -1);
}

TEST(decode_rejects_too_many_users) {
    message_init(&in, FT_USERS);
    int n = message_encode(&in, 0, wire, sizeof(wire));
    /* Patch count to USERS_MAX + 1 (payload starts after the header). */
    wire[FRAME_HEADER_SIZE] = (uint8_t)((USERS_MAX + 1) >> 8);
    wire[FRAME_HEADER_SIZE + 1] = (uint8_t)(USERS_MAX + 1);
    CHECK_EQ_INT(message_decode(wire, (size_t)n, &out, NULL), -1);
}

TEST(encode_fails_when_buffer_too_small) {
    uint8_t small[FRAME_HEADER_SIZE + 2];
    message_init(&in, FT_MSG);
    strcpy(in.text, "too long for buffer");
    CHECK_EQ_INT(message_encode(&in, 0, small, sizeof(small)), -1);
}

TEST(flags_round_trip) {
    message_init(&in, FT_CHAT);
    in.flags = FLAG_HISTORY;
    strcpy(in.name, "ali");
    strcpy(in.text, "eski mesaj");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.flags, FLAG_HISTORY);

    message_init(&in, FT_CHAT);
    strcpy(in.name, "ali");
    strcpy(in.text, "yeni");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.flags, 0);
}

TEST(history_frames_round_trip) {
    message_init(&in, FT_HISTORY_REQ);
    in.id = 4242;
    in.limit = 50;
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.type, FT_HISTORY_REQ);
    CHECK(out.id == 4242);
    CHECK_EQ_INT(out.limit, 50);

    message_init(&in, FT_HISTORY_END);
    in.more = 1;
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.more, 1);

    /* An older page is marked with both flags. */
    message_init(&in, FT_CHAT);
    in.flags = FLAG_HISTORY | FLAG_PAGE;
    in.id = 7;
    strcpy(in.name, "ali");
    strcpy(in.text, "eski");
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.flags, FLAG_HISTORY | FLAG_PAGE);
}

TEST(typing_round_trips) {
    message_init(&in, FT_TYPING);
    in.active = 1;
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_INT(out.type, FT_TYPING);
    CHECK_EQ_INT(out.active, 1);

    message_init(&in, FT_USER_TYPING);
    strcpy(in.name, "ayşe");
    in.active = 0;
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_STR(out.name, "ayşe");
    CHECK_EQ_INT(out.active, 0);
}

TEST(key_and_dm_round_trip) {
    message_init(&in, FT_KEY);
    for (int i = 0; i < DM_KEY_BYTES; i++) in.key[i] = (uint8_t)i;
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_MEM(out.key, in.key, DM_KEY_BYTES);

    message_init(&in, FT_PEER_KEY);
    strcpy(in.name, "veli");
    memset(in.key, 0xAB, DM_KEY_BYTES);
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_STR(out.name, "veli");
    CHECK_EQ_MEM(out.key, in.key, DM_KEY_BYTES);

    message_init(&in, FT_DM_RECV);
    in.timestamp = 42;
    strcpy(in.name, "ali");
    strcpy(in.to, "veli");
    memset(in.nonce, 7, DM_NONCE_BYTES);
    in.box_len = 40;
    memset(in.box, 9, 40);
    CHECK_EQ_INT(round_trip(), 0);
    CHECK_EQ_STR(out.to, "veli");
    CHECK_EQ_INT(out.box_len, 40);
    CHECK_EQ_MEM(out.nonce, in.nonce, DM_NONCE_BYTES);
    CHECK_EQ_MEM(out.box, in.box, 40);
}

TEST(dm_box_length_is_limited) {
    message_init(&in, FT_DM);
    strcpy(in.to, "veli");
    in.box_len = DM_BOX_MAX + 1;
    CHECK_EQ_INT(message_encode(&in, 0, wire, sizeof(wire)), -1);

    /* A frame claiming a longer box than allowed is rejected. */
    in.box_len = 4;
    int n = message_encode(&in, 0, wire, sizeof(wire));
    size_t len_at = FRAME_HEADER_SIZE + 2 + 4 + DM_NONCE_BYTES;  /* str "veli" then nonce */
    wire[len_at] = 0xFF;
    wire[len_at + 1] = 0xFF;
    CHECK_EQ_INT(message_decode(wire, (size_t)n, &out, NULL), -1);
}

int main(void) {
    RUN(name_round_trips);
    RUN(msg_and_welcome_round_trip);
    RUN(chat_round_trips);
    RUN(flags_round_trip);
    RUN(typing_round_trips);
    RUN(key_and_dm_round_trip);
    RUN(dm_box_length_is_limited);
    RUN(join_and_leave_round_trip);
    RUN(users_round_trip);
    RUN(err_round_trips);
    RUN(empty_messages_round_trip);
    RUN(decode_rejects_truncated_payload);
    RUN(decode_rejects_trailing_bytes);
    RUN(decode_rejects_length_mismatch);
    RUN(decode_rejects_overlong_name);
    RUN(decode_rejects_too_many_users);
    RUN(encode_fails_when_buffer_too_small);
    RUN(history_frames_round_trip);
    return TEST_REPORT();
}
