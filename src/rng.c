/* Feature macros must come before any header: each platform hides its own
 * entropy call behind one. */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE     /* glibc: exposes getrandom() in <sys/random.h> */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE    /* Darwin: exposes arc4random_buf() in <stdlib.h> */
#endif

#include "rng.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- system entropy ----------------------------------------------------
 *
 * Filling a buffer from the kernel is the only platform-dependent piece in
 * the project.  Everything built on it - the buffering below, the seeded
 * stream above and the unbiased bounded draw - is shared, so no game or
 * other module needs to know which system it is running on.
 */

#if defined(__linux__)
#include <sys/random.h>

static void sys_entropy(void *dst, size_t len)
{
    unsigned char *p = dst;
    size_t have = 0;

    while (have < len) {
        ssize_t got = getrandom(p + have, len - have, 0);
        if (got < 0) {
            if (errno == EINTR)
                continue;
            perror("getrandom");
            exit(70); /* EX_SOFTWARE-ish: cannot run without entropy */
        }
        have += (size_t)got;
    }
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
/* macOS and the BSDs seed arc4random from the kernel themselves: it is
 * declared in <stdlib.h>, always fills the whole buffer and cannot fail,
 * so it needs no retry loop or error path. */

static void sys_entropy(void *dst, size_t len)
{
    arc4random_buf(dst, len);
}

#else
#error "no system entropy source known for this platform (see src/rng.c)"
#endif

/* ---- system entropy mode ---- */

static void entropy_refill(rng_t *r)
{
    sys_entropy(r->buf, sizeof r->buf);
    r->buflen = sizeof r->buf;
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
