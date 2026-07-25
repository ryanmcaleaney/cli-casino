#ifndef CASINO_POKER_H
#define CASINO_POKER_H

#include "cards.h"

/*
 * Generic 5-card poker hand evaluation (no jokers, no wilds).
 * Ace plays high (10-J-Q-K-A) or low (A-2-3-4-5); no wraparound.
 */
typedef enum {
    POKER_HIGH_CARD, POKER_PAIR, POKER_TWO_PAIR, POKER_THREE_OF_A_KIND,
    POKER_STRAIGHT, POKER_FLUSH, POKER_FULL_HOUSE, POKER_FOUR_OF_A_KIND,
    POKER_STRAIGHT_FLUSH, POKER_ROYAL_FLUSH
} poker_cat_t;

typedef struct {
    poker_cat_t cat;
    int pair_rank;      /* rank of the pair (1=A, 11=J, ...) when
                           cat == POKER_PAIR, else 0 */
} poker_eval_t;

poker_eval_t poker_eval5(const card_t c[5]);
const char  *poker_cat_str(poker_cat_t cat);    /* "Royal Flush", ... */

#endif
