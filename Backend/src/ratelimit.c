#include "ratelimit.h"

int bucket_take(bucket_t* b, double rate, double burst, uint64_t now_ms) {
    if (rate <= 0) return 1;
    if (b->last_ms == 0) {
        b->tokens = burst;
    } else if (now_ms > b->last_ms) {
        b->tokens += (double)(now_ms - b->last_ms) * rate / 1000.0;
        if (b->tokens > burst) b->tokens = burst;
    }
    b->last_ms = now_ms;
    if (b->tokens < 1.0) return 0;
    b->tokens -= 1.0;
    return 1;
}
