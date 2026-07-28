#include "cs_strategy.h"

/*
 * The simplified optimal strategy for Caribbean Stud, as usually quoted
 * (it gives up about 0.02% against the exact play of every hand, and is
 * the rule set the game's own documentation describes):
 *
 *   1. raise with one pair or better
 *   2. fold with less than ace-king high
 *   3. holding exactly ace-king high, raise when any of these applies:
 *      a. the up-card is a 2 through queen and matches one of the
 *         player's own cards
 *      b. the up-card is an ace or a king and the player holds a queen
 *         or a jack
 *      c. the player holds a queen and the up-card ranks below the
 *         player's fourth-highest card
 *      otherwise fold.
 *
 * Rule (a) deliberately stops at the queen.  A hand that is exactly
 * ace-king high always contains an ace and a king, so an unrestricted
 * "matches one of your cards" would fire on every ace or king up-card
 * and leave rule (b) with nothing to decide; the 2-through-queen reading
 * is the only one in which all three rules do work.  Rule (c) needs no
 * range of its own: the fourth-highest card of an ace-king-queen hand is
 * a jack at best, so no ace or king up-card can rank below it.
 *
 * Ranks compare with the ace high (14).  Suits never enter into it
 * except through the hand category itself, which cs_eval() has already
 * settled.  Nothing here writes to anything.
 */

#define CS_RANK_ACE   14
#define CS_RANK_KING  13
#define CS_RANK_QUEEN 12
#define CS_RANK_JACK  11

/* 1 = ace .. 13 = king, as the ace-high comparison value. */
static int rank_value(card_t c)
{
    return c.rank == 1 ? CS_RANK_ACE : (int)c.rank;
}

cs_decision_t cs_basic_strategy(const card_t player[CS_CARDS],
                                card_t dealer_upcard)
{
    cs_eval_t ev = cs_eval(player);
    int up = rank_value(dealer_upcard);

    /* 1. one pair or better always plays */
    if (ev.cat > POKER_HIGH_CARD)
        return CS_DECISION_RAISE;

    /* 2. below ace-king high, fold.  rank[] is sorted high to low, so
     * "exactly ace-king high" is an ace over a king with no made hand -
     * an ace-queen or a bare king is not it. */
    if (ev.rank[0] != CS_RANK_ACE || ev.rank[1] != CS_RANK_KING)
        return CS_DECISION_FOLD;

    /* 3a. a 2-through-queen up-card the player also holds */
    if (up <= CS_RANK_QUEEN)
        for (int i = 0; i < CS_CARDS; i++)
            if (ev.rank[i] == up)
                return CS_DECISION_RAISE;

    /* 3b. an ace or king up-card against a queen or jack of our own */
    if (up == CS_RANK_ACE || up == CS_RANK_KING)
        for (int i = 2; i < CS_CARDS; i++)
            if (ev.rank[i] == CS_RANK_QUEEN || ev.rank[i] == CS_RANK_JACK)
                return CS_DECISION_RAISE;

    /* 3c. a queen of our own, with the up-card below our fourth card */
    if (ev.rank[2] == CS_RANK_QUEEN && up < ev.rank[3])
        return CS_DECISION_RAISE;

    return CS_DECISION_FOLD;
}
