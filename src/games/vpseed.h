#ifndef CASINO_VPSEED_H
#define CASINO_VPSEED_H

#include <stdbool.h>
#include <stdint.h>

#include "cards.h"

/*
 * Deterministic seed search for video poker.
 *
 * A seed is tried exactly as `casino videopoker --seed N` plays it: a
 * fresh seeded RNG, a fresh single-deck shoe and the game's own deal, so
 * a reported seed reproduces through ordinary gameplay.  Nothing here
 * knows how to shuffle, how to hold or how to rank a hand - the game's
 * deal, its solver and its evaluator are called directly.
 *
 * Seeds are tried in ascending order and the first match wins, so the
 * result is the lowest matching seed in the range.  The range is
 * INCLUSIVE at both ends: [start, end].
 *
 * Which seed produces which hand is a property of the current RNG and
 * shuffle; changing either changes the answers.
 */

typedef struct {
    uint64_t seed;
    card_t   initial[5];        /* the dealt hand */
    uint32_t hold;              /* bit i = card i+1 kept; all five bits in
                                   initial-deal mode, where nothing draws */
    card_t   final5[5];         /* == initial in initial-deal mode */
    int      cat;               /* category index (see vp_front_token) */
} vp_seed_hit_t;

/* Optional heartbeat: fn(checked, ctx) after every `every` seeds tried. */
typedef struct {
    uint64_t every;
    void   (*fn)(uint64_t checked, void *ctx);
    void    *ctx;
} vp_seed_report_t;

/*
 * Searches [start, end] for the first seed whose hand is `target` (a
 * category index).  With after_draw, the hand judged is the one left
 * after the game's optimal hold is applied and the replacements drawn;
 * otherwise it is the initial five cards.  Returns false when the range
 * holds no match (and when it is empty, i.e. start > end).
 */
bool vp_seed_find(int target, bool after_draw, uint64_t start, uint64_t end,
                  const vp_seed_report_t *report, vp_seed_hit_t *out);

#endif
