#ifndef CASINO_VIDEOPOKER_H
#define CASINO_VIDEOPOKER_H

#include <stddef.h>

#include "cards.h"
#include "cli.h"
#include "rng.h"

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

#endif
