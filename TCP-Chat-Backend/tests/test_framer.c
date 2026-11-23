#include "test.h"
#include "framer.h"

/* Builds "len | body" into out, returns total size. */
static size_t make_record(uint8_t* out, const char* body) {
    size_t n = strlen(body);
    record_put_header(out, (uint32_t)n);
    memcpy(out + RECORD_HEADER_SIZE, body, n);
    return RECORD_HEADER_SIZE + n;
}

TEST(single_complete_record) {
    framer_t f;
    uint8_t wire[64];
    const uint8_t* body; size_t len;

    framer_init(&f, 128);
    size_t n = make_record(wire, "hello");
    CHECK_EQ_INT(framer_feed(&f, wire, n), n);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_INT(len, 5);
    CHECK_EQ_MEM(body, "hello", 5);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 0);
    CHECK_EQ_INT(framer_pending(&f), 0);
    framer_free(&f);
}

TEST(record_split_byte_by_byte) {
    framer_t f;
    uint8_t wire[64];
    const uint8_t* body; size_t len;

    framer_init(&f, 128);
    size_t n = make_record(wire, "split");
    for (size_t i = 0; i < n - 1; i++) {
        framer_feed(&f, wire + i, 1);
        CHECK_EQ_INT(framer_next(&f, &body, &len), 0);
    }
    framer_feed(&f, wire + n - 1, 1);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "split", 5);
    framer_free(&f);
}

TEST(merged_records_in_one_read) {
    framer_t f;
    uint8_t wire[128];
    const uint8_t* body; size_t len;

    framer_init(&f, 128);
    size_t n = make_record(wire, "one");
    n += make_record(wire + n, "two");
    n += make_record(wire + n, "");
    framer_feed(&f, wire, n);

    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "one", 3);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "two", 3);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_INT(len, 0);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 0);
    framer_free(&f);
}

TEST(record_followed_by_partial_record) {
    framer_t f;
    uint8_t wire[128];
    const uint8_t* body; size_t len;

    framer_init(&f, 128);
    size_t first = make_record(wire, "full");
    size_t second = make_record(wire + first, "later");
    framer_feed(&f, wire, first + 3);

    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "full", 4);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 0);

    framer_feed(&f, wire + first + 3, second - 3);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "later", 5);
    framer_free(&f);
}

TEST(oversized_record_is_rejected) {
    framer_t f;
    uint8_t hdr[RECORD_HEADER_SIZE];
    const uint8_t* body; size_t len;

    framer_init(&f, 16);
    record_put_header(hdr, 17);
    framer_feed(&f, hdr, sizeof(hdr));
    CHECK_EQ_INT(framer_next(&f, &body, &len), -1);
    framer_free(&f);
}

TEST(buffer_compacts_to_fit_max_record) {
    framer_t f;
    uint8_t wire[64];
    const uint8_t* body; size_t len;

    /* Capacity is exactly one max-size record. After consuming a small
     * record, a max-size record must still fit thanks to compaction. */
    framer_init(&f, 8);
    size_t n = make_record(wire, "abc");
    framer_feed(&f, wire, n);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);

    n = make_record(wire, "12345678");
    CHECK_EQ_INT(framer_feed(&f, wire, n), n);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "12345678", 8);
    framer_free(&f);
}

int main(void) {
    RUN(single_complete_record);
    RUN(record_split_byte_by_byte);
    RUN(merged_records_in_one_read);
    RUN(record_followed_by_partial_record);
    RUN(oversized_record_is_rejected);
    RUN(buffer_compacts_to_fit_max_record);
    return TEST_REPORT();
}
