#ifndef CASINO_BJ_STRATEGY_H
#define CASINO_BJ_STRATEGY_H

#include <stdbool.h>

#include "blackjack.h"

/*
 * Basic strategy for the exact rule profile the blackjack engine already
 * implements:
 *
 *   six-deck shoe, dealer stands on all 17s (S17), dealer peeks for
 *   blackjack, double on any first two cards, double after split (DAS),
 *   split only on exactly equal ranks, up to four hands, split aces get
 *   one card and cannot be re-split or doubled, late surrender on the
 *   initial unsplit two-card hand only.
 *
 * The advice is total-dependent and count-independent: index deviations
 * (Illustrious 18, Fab 4) and count-based insurance are intentionally
 * outside this feature.  Insurance is never recommended and is not part
 * of the graded decisions.
 */
typedef enum {
    BJ_STRAT_NONE,          /* the engine is not waiting for an action */
    BJ_STRAT_HIT,
    BJ_STRAT_STAND,
    BJ_STRAT_DOUBLE,
    BJ_STRAT_SPLIT,
    BJ_STRAT_SURRENDER
} bj_strategy_action_t;

/*
 * The play for the active hand against the dealer's up-card.  The result
 * is resolved against the live engine state, so it is always an action
 * bj_legal() accepts: a double or a split the table asks for but the
 * table rules or the bankroll forbid falls back to the chart's own
 * alternative.  Returns BJ_STRAT_NONE whenever no decision is pending.
 */
bj_strategy_action_t bj_basic_strategy(const bj_session_t *s);

/* "HIT", "STAND", "DOUBLE", "SPLIT", "SURRENDER", "-" for NONE. */
const char *bj_strategy_word(bj_strategy_action_t a);

/* The engine action a recommendation asks for.  False for BJ_STRAT_NONE,
 * which is not a decision and maps to nothing. */
bool bj_strategy_action(bj_strategy_action_t rec, bj_action_t *out);

/* True when the player's action is the recommended one.  BJ_STRAT_NONE
 * agrees with nothing: there is no decision to grade. */
bool bj_strategy_agrees(bj_strategy_action_t rec, bj_action_t chosen);

#endif
