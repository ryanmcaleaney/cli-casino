#include "cards.h"

#include <stdio.h>
#include <stdlib.h>

void shoe_init(shoe_t *s, int ndecks)
{
    if (ndecks < 1)
        ndecks = 1;
    if (ndecks > SHOE_MAX_DECKS)
        ndecks = SHOE_MAX_DECKS;
    s->count = 0;
    s->pos = 0;
    for (int d = 0; d < ndecks; d++)
        for (uint8_t suit = 0; suit < 4; suit++)
            for (uint8_t rank = 1; rank <= 13; rank++)
                s->cards[s->count++] = (card_t){ rank, suit };
}

void shoe_shuffle(shoe_t *s, rng_t *r)
{
    rng_shuffle(r, s->cards, (size_t)s->count, sizeof s->cards[0]);
    s->pos = 0;
}

int shoe_remaining(const shoe_t *s)
{
    return s->count - s->pos;
}

card_t shoe_draw(shoe_t *s)
{
    if (s->pos >= s->count) {
        fprintf(stderr, "shoe_draw: shoe empty\n");
        exit(70);
    }
    return s->cards[s->pos++];
}

const char *card_rank_str(card_t c)
{
    static const char *ranks[] = {
        "?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    return (c.rank >= 1 && c.rank <= 13) ? ranks[c.rank] : "?";
}

const char *card_suit_str(card_t c)
{
    static const char *suits[] = { "c", "d", "h", "s" };
    return (c.suit < 4) ? suits[c.suit] : "?";
}

void card_name(card_t c, char *buf, size_t len)
{
    snprintf(buf, len, "%s%s", card_rank_str(c), card_suit_str(c));
}
