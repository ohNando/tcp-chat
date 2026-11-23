#include "test.h"
#include "queue.h"

TEST(dequeue_returns_messages_in_fifo_order) {
    MessageQueue q;
    char out[MAX_BUFSIZE];
    init_queue(&q);

    CHECK_EQ_INT(enqueue(&q, "first"), 0);
    CHECK_EQ_INT(enqueue(&q, "second"), 0);

    CHECK_EQ_INT(dequeue(&q, out, sizeof(out)), 0);
    CHECK_EQ_STR(out, "first");
    CHECK_EQ_INT(dequeue(&q, out, sizeof(out)), 0);
    CHECK_EQ_STR(out, "second");
}

TEST(nonblocking_dequeue_fails_on_empty_queue) {
    MessageQueue q;
    char out[MAX_BUFSIZE];
    init_queue(&q);

    CHECK_EQ_INT(dequeue_nonblocking(&q, out, sizeof(out)), -1);
}

int main(void) {
    RUN(dequeue_returns_messages_in_fifo_order);
    RUN(nonblocking_dequeue_fails_on_empty_queue);
    return TEST_REPORT();
}
