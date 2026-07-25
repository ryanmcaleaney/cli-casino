#ifndef CASINO_RNG_H
#define CASINO_RNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Two modes:
 *  - default: entropy pulled from Linux getrandom(2), buffered.
 *  - seeded:  deterministic xoshiro256** stream (for reproducible tests).
 * Bounded selection uses rejection sampling (no modulo bias).
 */

typedef struct rng {
    bool     seeded;
    uint64_t s[4];        /* xoshiro256** state (seeded mode) */
    uint8_t  buf[256];    /* entropy buffer (getrandom mode)  */
    size_t   buflen;
    size_t   bufpos;
} rng_t;

/* seeded=false ignores `seed` and uses getrandom(). */
void     rng_init(rng_t *r, bool seeded, uint64_t seed);

/* Uniform 64-bit value. */
uint64_t rng_next(rng_t *r);

/* Uniform value in [0, n). n must be >= 1. Unbiased. */
uint32_t rng_below(rng_t *r, uint32_t n);

/* Fisher-Yates shuffle of n elements of `size` bytes. */
void     rng_shuffle(rng_t *r, void *base, size_t n, size_t size);

#endif
