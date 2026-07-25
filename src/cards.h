#ifndef CASINO_CARDS_H
#define CASINO_CARDS_H

#include <stddef.h>
#include <stdint.h>

#include "rng.h"

/* rank: 1 = Ace .. 13 = King.  suit: 0=clubs 1=diamonds 2=hearts 3=spades */
typedef struct card {
    uint8_t rank;
    uint8_t suit;
} card_t;

#define SHOE_MAX_DECKS 8

typedef struct shoe {
    card_t cards[52 * SHOE_MAX_DECKS];
    int    count;
    int    pos;
} shoe_t;

void        shoe_init(shoe_t *s, int ndecks);           /* ordered */
void        shoe_shuffle(shoe_t *s, rng_t *r);
int         shoe_remaining(const shoe_t *s);
card_t      shoe_draw(shoe_t *s);                       /* aborts if empty */

const char *card_rank_str(card_t c);                    /* "A","2",..,"K" */
const char *card_suit_str(card_t c);                    /* "c","d","h","s" */
void        card_name(card_t c, char *buf, size_t len); /* e.g. "Ah" */

#endif
