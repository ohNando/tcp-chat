#ifndef RATELIMIT_H
#define RATELIMIT_H

/*
 * Token bucket for flood protection: every frame a client sends costs one
 * token, the bucket refills at `rate` tokens per second up to `burst`. A
 * person typing never notices it; a script flooding the room does.
 */

#include <stdint.h>

typedef struct {
    double tokens;
    uint64_t last_ms;   /* last refill; 0 = not started (bucket full) */
} bucket_t;

/* Takes one token at time `now_ms`. Returns 1 if allowed, 0 if the bucket
 * is empty. rate <= 0 disables the limit (always allowed). */
int bucket_take(bucket_t* b, double rate, double burst, uint64_t now_ms);

#endif
