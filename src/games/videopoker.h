#ifndef CASINO_VIDEOPOKER_H
#define CASINO_VIDEOPOKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cards.h"
#include "cli.h"
#include "rng.h"
#include "vpsolve.h"

int  videopoker_run(const cli_t *cli, rng_t *rng);
void videopoker_list_bets(void);

/*
 * Read-only evaluation/pay-table interface for frontends (GUI, solver
 * displays).  Thin wrappers over the game's internal Jacks-or-Better
 * classification: there is exactly one evaluator and one pay table.
 * Categories are ordered low to high, 0 .. VP_FRONT_NCATS-1.
 */
#define VP_FRONT_NCATS 11
int         vp_front_category(const card_t hand[5]);
int         vp_front_payout(int cat);           /* per 1-unit bet */
const char *vp_front_token(int cat);            /* e.g. "ROYAL_FLUSH" */
/* Human name of a hand, e.g. "Pair of Jacks", "Flush". */
void        vp_front_describe(const card_t hand[5], char *buf, size_t len);

/*
 * Read-only strategy interface for frontends.  vp_front_solve() runs the
 * exhaustive solver once for a dealt hand using the game's own pay table;
 * the result is then queried by hold mask (bit i = keep card i).  EV
 * comparisons stay exact, so every mask that ties the optimum is optimal.
 */
typedef struct {
    vp_hold_ev_t evs[VP_NMASKS];
    int          best;          /* one deterministic optimal mask */
} vp_strategy_t;

void     vp_front_solve(const card_t hand[5], vp_strategy_t *out);
/* True for every mask whose EV equals the optimum, not just `best`. */
bool     vp_front_hold_optimal(const vp_strategy_t *s, uint32_t mask);
double   vp_front_hold_ev(const vp_strategy_t *s, uint32_t mask);
double   vp_front_best_ev(const vp_strategy_t *s);
uint32_t vp_front_best_mask(const vp_strategy_t *s);

#endif
