#ifndef CASINO_WAR_H
#define CASINO_WAR_H

#include <stdbool.h>

#include "cards.h"
#include "cli.h"
#include "rng.h"

int  war_run(const cli_t *cli, rng_t *rng);
void war_list_bets(void);

/* ---- rule profile ------------------------------------------------------- */

#define WAR_DECKS 6                 /* shoe, reshuffled every round */
#define WAR_BURN  3                 /* cards burned before a war */

/*
 * Money is held in halves so that a surrender (half the wager) stays exact
 * in integer arithmetic.  Wagers are always an even number of halves, so
 * wager/2 never truncates.
 */
#define WAR_HALF        2
#define WAR_BET_MIN     1
#define WAR_BET_MAX     100000
#define WAR_BET_DEFAULT 100

/* What the player does when the first two cards tie. */
typedef enum {
    WAR_TIE_ASK,        /* no scripted strategy: prompt for the decision */
    WAR_TIE_WAR,
    WAR_TIE_SURRENDER
} war_tie_t;

/* How a round ended, from the player's point of view. */
typedef enum {
    WAR_WIN,            /* higher card, or the war card won */
    WAR_LOSS,           /* lower card, or the war card lost */
    WAR_PUSH,           /* the war tied again: both wagers come back */
    WAR_SURRENDER       /* the tie was conceded for half the wager */
} war_result_t;

/* One complete round.  Money fields are halves. */
typedef struct {
    long         bet;               /* base wager */
    card_t       player, dealer;
    bool         tie;
    war_tie_t    decision;          /* WAR_TIE_ASK unless a tie was resolved */
    card_t       burn[WAR_BURN];
    card_t       war_player, war_dealer;
    bool         second_tie;
    war_result_t result;
    long         wagered;           /* committed this round (base + war) */
    long         returned;          /* paid back to the player */
} war_round_t;

#endif
