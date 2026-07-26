#ifndef CASINO_RNG_H
#define CASINO_RNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Two modes:
 *  - default: entropy pulled from the system, buffered.  getrandom(2) on
 *    Linux, arc4random_buf(3) on macOS and the BSDs; rng.c is the only
 *    place that knows the difference.
 *  - seeded:  deterministic xoshiro256** stream (for reproducible tests).
 * Bounded selection uses rejection sampling (no modulo bias), and is
 * shared by both modes.
 */

typedef struct rng {
    bool     seeded;
    uint64_t s[4];        /* xoshiro256** state (seeded mode) */
    uint8_t  buf[256];    /* system entropy buffer (unseeded mode) */
    size_t   buflen;
    size_t   bufpos;
} rng_t;

/* seeded=false ignores `seed` and draws from the system entropy source. */
void     rng_init(rng_t *r, bool seeded, uint64_t seed);

/* Uniform 64-bit value. */
uint64_t rng_next(rng_t *r);

/* Uniform value in [0, n). n must be >= 1. Unbiased. */
uint32_t rng_below(rng_t *r, uint32_t n);

/* Fisher-Yates shuffle of n elements of `size` bytes. */
void     rng_shuffle(rng_t *r, void *base, size_t n, size_t size);

#endif
