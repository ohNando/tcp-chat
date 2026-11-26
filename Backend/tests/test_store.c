#include "test.h"
#include "store.h"

#include <stdlib.h>
#include <unistd.h>

/* Collects what store_page() hands out. */
typedef struct {
    int n;
    stored_message_t m[STORE_PAGE_MAX];
} page_t;

static void collect(void* ctx, const stored_message_t* m) {
    page_t* p = ctx;
    p->m[p->n++] = *m;
}

static void fill(store_t* s, int count) {
    char text[32];
    for (int i = 1; i <= count; i++) {
        snprintf(text, sizeof(text), "message %d", i);
        store_add(s, 1000u + (uint64_t)i, i % 2 ? "alice" : "bob", text);
    }
}

TEST(ids_grow_and_newest_page_is_oldest_first) {
    store_t s;
    CHECK_EQ_INT(store_open(&s, ":memory:", 0), 0);
    uint64_t a = store_add(&s, 1, "alice", "one");
    uint64_t b = store_add(&s, 2, "bob", "two");
    CHECK(a > 0);
    CHECK(b > a);

    page_t p = { 0 };
    int more = -1;
    CHECK_EQ_INT(store_page(&s, 0, 50, collect, &p, &more), 2);
    CHECK_EQ_INT(more, 0);
    CHECK_EQ_INT(p.m[0].id, a);
    CHECK_EQ_STR(p.m[0].name, "alice");
    CHECK_EQ_STR(p.m[0].text, "one");
    CHECK_EQ_INT(p.m[1].timestamp, 2);
    store_close(&s);
}

TEST(pages_walk_back_without_gaps) {
    store_t s;
    store_open(&s, ":memory:", 0);
    fill(&s, 25);

    page_t p = { 0 };
    int more;
    CHECK_EQ_INT(store_page(&s, 0, 10, collect, &p, &more), 10);
    CHECK_EQ_INT(more, 1);
    CHECK_EQ_STR(p.m[0].text, "message 16");
    CHECK_EQ_STR(p.m[9].text, "message 25");

    page_t older = { 0 };
    CHECK_EQ_INT(store_page(&s, p.m[0].id, 10, collect, &older, &more), 10);
    CHECK_EQ_INT(more, 1);
    CHECK_EQ_STR(older.m[9].text, "message 15");

    page_t last = { 0 };
    CHECK_EQ_INT(store_page(&s, older.m[0].id, 10, collect, &last, &more), 5);
    CHECK_EQ_INT(more, 0);
    CHECK_EQ_STR(last.m[0].text, "message 1");
    store_close(&s);
}

TEST(page_size_is_capped) {
    store_t s;
    store_open(&s, ":memory:", 0);
    fill(&s, STORE_PAGE_MAX + 20);
    page_t p = { 0 };
    int more;
    CHECK_EQ_INT(store_page(&s, 0, 100000, collect, &p, &more), STORE_PAGE_MAX);
    CHECK_EQ_INT(more, 1);
    store_close(&s);
}

TEST(text_is_stored_verbatim) {
    /* Bound parameters: quotes and SQL in a message are just text. */
    store_t s;
    store_open(&s, ":memory:", 0);
    const char* evil = "'); DROP TABLE messages; -- Şükrü 👋";
    store_add(&s, 5, "mallory", evil);
    page_t p = { 0 };
    int more;
    CHECK_EQ_INT(store_page(&s, 0, 10, collect, &p, &more), 1);
    CHECK_EQ_STR(p.m[0].text, evil);
    store_close(&s);
}

TEST(history_survives_reopening) {
    char path[] = "/tmp/tcpchat-store-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);

    store_t s;
    CHECK_EQ_INT(store_open(&s, path, 0), 0);
    fill(&s, 3);
    store_close(&s);

    CHECK_EQ_INT(store_open(&s, path, 0), 0);
    page_t p = { 0 };
    int more;
    CHECK_EQ_INT(store_page(&s, 0, 10, collect, &p, &more), 3);
    CHECK_EQ_STR(p.m[2].text, "message 3");
    CHECK(store_add(&s, 99, "carol", "after reopen") > p.m[2].id);
    store_close(&s);

    char wal[64], shm[64];
    snprintf(wal, sizeof(wal), "%s-wal", path);
    snprintf(shm, sizeof(shm), "%s-shm", path);
    unlink(path);
    unlink(wal);
    unlink(shm);
}

TEST(flush_commits_the_turns_messages) {
    char path[] = "/tmp/tcpchat-store-XXXXXX";
    int fd = mkstemp(path);
    close(fd);
    store_t writer, reader;
    CHECK_EQ_INT(store_open(&writer, path, 0), 0);
    CHECK_EQ_INT(store_open(&reader, path, 0), 0);

    CHECK(store_add(&writer, 1, "alice", "one") > 0);
    CHECK(store_add(&writer, 2, "alice", "two") > 0);
    page_t p = { 0 };
    int more;
    /* The writer sees its own messages at once (for history requests)... */
    CHECK_EQ_INT(store_page(&writer, 0, 10, collect, &p, &more), 2);
    /* ...others only after the commit. */
    page_t q = { 0 };
    CHECK_EQ_INT(store_page(&reader, 0, 10, collect, &q, &more), 0);
    store_flush(&writer);
    CHECK_EQ_INT(store_page(&reader, 0, 10, collect, &q, &more), 2);

    store_close(&writer);
    store_close(&reader);
    char wal[64], shm[64];
    snprintf(wal, sizeof(wal), "%s-wal", path);
    snprintf(shm, sizeof(shm), "%s-shm", path);
    unlink(path);
    unlink(wal);
    unlink(shm);
}

TEST(prune_drops_only_old_messages) {
    const uint64_t day = 24ull * 3600 * 1000, now = 100 * day;
    store_t s;
    store_open(&s, ":memory:", 7);
    store_add(&s, now - 10 * day, "alice", "too old");
    store_add(&s, now - 1 * day, "bob", "recent");
    CHECK_EQ_INT(store_prune(&s, now), 1);

    page_t p = { 0 };
    int more;
    CHECK_EQ_INT(store_page(&s, 0, 10, collect, &p, &more), 1);
    CHECK_EQ_STR(p.m[0].text, "recent");
    store_close(&s);

    store_open(&s, ":memory:", 0); /* 0 = keep forever */
    store_add(&s, 1, "alice", "ancient");
    CHECK_EQ_INT(store_prune(&s, now), 0);
    store_close(&s);
}

TEST(bad_path_fails_cleanly) {
    store_t s;
    CHECK_EQ_INT(store_open(&s, "/nonexistent-dir/x/chat.db", 0), -1);
    CHECK_EQ_INT(store_add(&s, 1, "a", "b"), 0);
}

int main(void) {
    RUN(ids_grow_and_newest_page_is_oldest_first);
    RUN(pages_walk_back_without_gaps);
    RUN(page_size_is_capped);
    RUN(text_is_stored_verbatim);
    RUN(history_survives_reopening);
    RUN(flush_commits_the_turns_messages);
    RUN(prune_drops_only_old_messages);
    RUN(bad_path_fails_cleanly);
    return TEST_REPORT();
}
