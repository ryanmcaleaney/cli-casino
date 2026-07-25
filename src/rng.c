#define _DEFAULT_SOURCE
#include "rng.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

/* ---- seeded mode: splitmix64 seeding + xoshiro256** ---- */

static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static uint64_t rotl64(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro_next(rng_t *r)
{
    uint64_t *s = r->s;
    uint64_t result = rotl64(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl64(s[3], 45);
    return result;
}

/* ---- getrandom mode ---- */

static void entropy_refill(rng_t *r)
{
    size_t have = 0;
    while (have < sizeof r->buf) {
        ssize_t got = getrandom(r->buf + have, sizeof r->buf - have, 0);
        if (got < 0) {
            if (errno == EINTR)
                continue;
            perror("getrandom");
            exit(70); /* EX_SOFTWARE-ish: cannot run without entropy */
        }
        have += (size_t)got;
    }
    r->buflen = have;
    r->bufpos = 0;
}

static uint64_t entropy_next(rng_t *r)
{
    uint64_t v;
    if (r->bufpos + sizeof v > r->buflen)
        entropy_refill(r);
    memcpy(&v, r->buf + r->bufpos, sizeof v);
    r->bufpos += sizeof v;
    return v;
}

/* ---- public ---- */

void rng_init(rng_t *r, bool seeded, uint64_t seed)
{
    memset(r, 0, sizeof *r);
    r->seeded = seeded;
    if (seeded) {
        uint64_t x = seed;
        for (int i = 0; i < 4; i++)
            r->s[i] = splitmix64(&x);
    }
}

uint64_t rng_next(rng_t *r)
{
    return r->seeded ? xoshiro_next(r) : entropy_next(r);
}

uint32_t rng_below(rng_t *r, uint32_t n)
{
    if (n < 2)
        return 0;
    /* Reject the low (2^64 mod n) values; remainder is an exact
     * multiple of n, so `% n` is uniform. */
    uint64_t nn = n;
    uint64_t min = (0 - nn) % nn;
    for (;;) {
        uint64_t x = rng_next(r);
        if (x >= min)
            return (uint32_t)(x % nn);
    }
}

void rng_shuffle(rng_t *r, void *base, size_t n, size_t size)
{
    unsigned char *b = base;
    unsigned char tmp[64];
    if (size > sizeof tmp) {
        fprintf(stderr, "rng_shuffle: element too large\n");
        exit(70);
    }
    for (size_t i = n; i > 1; i--) {
        size_t j = rng_below(r, (uint32_t)i); /* [0, i) */
        if (j == i - 1)
            continue;
        memcpy(tmp, b + j * size, size);
        memcpy(b + j * size, b + (i - 1) * size, size);
        memcpy(b + (i - 1) * size, tmp, size);
    }
}
