#ifndef CASINO_CS_STRATEGY_H
#define CASINO_CS_STRATEGY_H

#include "cards.h"
#include "caribbeanstud.h"

/*
 * Caribbean Stud basic strategy: the established simplified optimal rule
 * set.  It reads the player's five cards and the dealer's up-card, and
 * nothing else - no session, no bankroll, no deck.  The exact rule is
 * documented and implemented in cs_strategy.c.
 */
typedef enum {
    CS_DECISION_FOLD,
    CS_DECISION_RAISE
} cs_decision_t;

cs_decision_t cs_basic_strategy(const card_t player[CS_CARDS],
                                card_t dealer_upcard);

#endif
