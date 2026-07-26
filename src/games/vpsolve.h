#ifndef CASINO_VPSOLVE_H
#define CASINO_VPSOLVE_H

#include <stdbool.h>
#include <stdint.h>

#include "cards.h"

/*
 * Exhaustive video poker hold solver.
 *
 * For a dealt 5-card hand there are 32 hold masks (bit i = keep card i).
 * For each mask every combination of replacement cards from the remaining
 * 47-card deck is enumerated and paid via the caller's pay function, so
 * the solver is independent of the variant/pay table.
 *
 * EVs are kept as exact rationals (total payout / draw combinations);
 * comparisons cross-multiply in 64-bit, so no floating-point tolerance
 * is ever needed and exact ties are recognised.
 */
#define VP_NMASKS 32

typedef int (*vp_payfn_t)(const card_t hand[5]);

typedef struct {
    long total;                 /* summed payout over all draw combos */
    long draws;                 /* number of draw combinations */
} vp_hold_ev_t;

void   vp_solve(const card_t hand[5], vp_payfn_t pay,
                vp_hold_ev_t evs[VP_NMASKS]);
/* Index of the best mask; exact ties prefer holding more cards. */
int    vp_solve_best(const vp_hold_ev_t evs[VP_NMASKS]);
bool   vp_ev_equal(const vp_hold_ev_t *a, const vp_hold_ev_t *b);
double vp_ev(const vp_hold_ev_t *e);

#endif
