#include "bj_strategy.h"

/*
 * The chart is held as four tables, consulted in this order:
 *
 *   1. surrender   2. pair   3. soft total   4. hard total
 *
 * The order is what makes 8,8 a pair rather than hard 16, A,7 a soft 18
 * rather than hard 8, and A,A a pair rather than soft 12.  A pair of
 * fives is deliberately not in the pair table so that it falls through
 * to hard 10, which is what the chart says to play.
 *
 * Every row is written left to right for the dealer up-card columns
 *
 *      2  3  4  5  6  7  8  9  10  A
 *
 * with ten, jack, queen and king sharing the single ten column.
 */

/* Dealer column, 0 = 2 ... 7 = 9, 8 = any ten-value card, 9 = ace. */
#define DEALER_COLS 10

static int dealer_column(card_t up)
{
    if (up.rank == 1)
        return 9;
    if (up.rank >= 10)
        return 8;
    return up.rank - 2;
}

/*
 * A chart entry.  The D and R entries carry the fallback the chart itself
 * specifies, which is what gets played when the engine will not allow the
 * first choice (three cards already, no bankroll, a split hand, ...).
 */
typedef enum {
    ADV_H,      /* hit */
    ADV_S,      /* stand */
    ADV_DH,     /* double, otherwise hit */
    ADV_DS,     /* double, otherwise stand */
    ADV_P,      /* split */
    ADV_RH,     /* surrender, otherwise hit */
    ADV_RS      /* surrender, otherwise stand (unused under S17, kept so
                 * the representation covers every chart form) */
} advice_t;

/* ---- hard totals, 4 through 21 ------------------------------------------ */

#define HARD_MIN 4
#define HARD_MAX 21

static const advice_t HARD[HARD_MAX - HARD_MIN + 1][DEALER_COLS] = {
/*  2       3       4       5       6       7       8       9       10      A   */
{ ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /*  4 */
{ ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /*  5 */
{ ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /*  6 */
{ ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /*  7 */
{ ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /*  8 */
{ ADV_H,  ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /*  9 */
{ ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_H,  ADV_H }, /* 10 */
{ ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_H }, /* 11 */
{ ADV_H,  ADV_H,  ADV_S,  ADV_S,  ADV_S,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 12 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 13 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 14 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_H,  ADV_H,  ADV_H,  ADV_RH, ADV_H }, /* 15 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_H,  ADV_H,  ADV_RH, ADV_RH, ADV_RH },/* 16 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S }, /* 17 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S }, /* 18 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S }, /* 19 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S }, /* 20 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S }, /* 21 */
};

/* ---- soft totals, 12 (A,A) through 21 ----------------------------------- */

#define SOFT_MIN 12
#define SOFT_MAX 21

static const advice_t SOFT[SOFT_MAX - SOFT_MIN + 1][DEALER_COLS] = {
/*  2       3       4       5       6       7       8       9       10      A   */
{ ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 12 A,A */
{ ADV_H,  ADV_H,  ADV_H,  ADV_DH, ADV_DH, ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 13 A,2 */
{ ADV_H,  ADV_H,  ADV_H,  ADV_DH, ADV_DH, ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 14 A,3 */
{ ADV_H,  ADV_H,  ADV_DH, ADV_DH, ADV_DH, ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 15 A,4 */
{ ADV_H,  ADV_H,  ADV_DH, ADV_DH, ADV_DH, ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 16 A,5 */
{ ADV_H,  ADV_DH, ADV_DH, ADV_DH, ADV_DH, ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 17 A,6 */
{ ADV_S,  ADV_DS, ADV_DS, ADV_DS, ADV_DS, ADV_S,  ADV_S,  ADV_H,  ADV_H,  ADV_H }, /* 18 A,7 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S }, /* 19 A,8 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S }, /* 20 A,9 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S }, /* 21 */
};

/* ---- pairs -------------------------------------------------------------- */

/*
 * Rows are the paired rank: aces, then twos through nines, then any pair
 * of ten-value cards (the engine only calls exactly equal ranks a pair,
 * so K,K splits here while K,Q is simply a hard twenty).  Fives are
 * missing on purpose - see pair_advice().
 */
typedef enum {
    PR_ACES, PR_2, PR_3, PR_4, PR_6, PR_7, PR_8, PR_9, PR_TENS, PR_NONE
} pair_row_t;

static const advice_t PAIR[PR_NONE][DEALER_COLS] = {
/*  2       3       4       5       6       7       8       9       10      A   */
{ ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P }, /* A,A */
{ ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 2,2 */
{ ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 3,3 */
{ ADV_H,  ADV_H,  ADV_H,  ADV_P,  ADV_P,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 4,4 */
{ ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_H,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 6,6 */
{ ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_H,  ADV_H,  ADV_H,  ADV_H }, /* 7,7 */
{ ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P }, /* 8,8 */
{ ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_P,  ADV_S,  ADV_P,  ADV_P,  ADV_S,  ADV_S }, /* 9,9 */
{ ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S,  ADV_S }, /* T,T */
};

/* The pair row for a rank, or PR_NONE when the pair chart does not own
 * the decision.  Fives are played as hard ten, never split. */
static pair_row_t pair_row(int rank)
{
    switch (rank) {
    case 1:  return PR_ACES;
    case 2:  return PR_2;
    case 3:  return PR_3;
    case 4:  return PR_4;
    case 5:  return PR_NONE;        /* hard 10, not a pair decision */
    case 6:  return PR_6;
    case 7:  return PR_7;
    case 8:  return PR_8;
    case 9:  return PR_9;
    default: return PR_TENS;        /* 10, J, Q, K */
    }
}

/* ---- resolving an entry against the live engine state ------------------- */

static bj_strategy_action_t resolve(const bj_session_t *s, advice_t a)
{
    switch (a) {
    case ADV_S:
        return BJ_STRAT_STAND;
    case ADV_DH:
        return bj_legal(s, BJ_DOUBLE) ? BJ_STRAT_DOUBLE : BJ_STRAT_HIT;
    case ADV_DS:
        return bj_legal(s, BJ_DOUBLE) ? BJ_STRAT_DOUBLE : BJ_STRAT_STAND;
    case ADV_P:
        return bj_legal(s, BJ_SPLIT) ? BJ_STRAT_SPLIT : BJ_STRAT_NONE;
    case ADV_RH:
        return bj_legal(s, BJ_SURRENDER) ? BJ_STRAT_SURRENDER : BJ_STRAT_HIT;
    case ADV_RS:
        return bj_legal(s, BJ_SURRENDER) ? BJ_STRAT_SURRENDER : BJ_STRAT_STAND;
    case ADV_H:
    default:
        return BJ_STRAT_HIT;
    }
}

/* ---- the four layers ---------------------------------------------------- */

static bool is_pair_of(const bj_hand_t *h, int rank)
{
    return h->n == 2 && h->cards[0].rank == rank && h->cards[1].rank == rank;
}

/*
 * Late surrender, hard totals only, and only where the engine still
 * allows it (first decision on the unsplit two-card hand).  Under S17 the
 * surrender cells are 16 against 9, 10 and an ace, and 15 against a ten -
 * with the standing exception that a pair of eights is split instead
 * whenever the split is available.
 */
static bool surrender_advice(const bj_session_t *s, const bj_hand_t *h,
                             int total, bool soft, int col, advice_t *out)
{
    advice_t a;

    if (soft || !bj_legal(s, BJ_SURRENDER))
        return false;
    if (is_pair_of(h, 8) && bj_legal(s, BJ_SPLIT))
        return false;
    if (total < HARD_MIN || total > HARD_MAX)
        return false;

    a = HARD[total - HARD_MIN][col];
    if (a != ADV_RH && a != ADV_RS)
        return false;
    *out = a;
    return true;
}

/* The pair chart owns the decision only while the split is actually on
 * offer; without it the hand is just its total. */
static bool pair_advice(const bj_session_t *s, const bj_hand_t *h, int col,
                        advice_t *out)
{
    pair_row_t row;

    if (!bj_legal(s, BJ_SPLIT))
        return false;
    row = pair_row(h->cards[0].rank);
    if (row == PR_NONE)
        return false;
    *out = PAIR[row][col];
    /* a non-split entry falls through to the total tables, which say the
     * same thing and already know about doubling */
    return *out == ADV_P;
}

/* ---- public API --------------------------------------------------------- */

bj_strategy_action_t bj_basic_strategy(const bj_session_t *s)
{
    const bj_round_t *r = &s->round;
    const bj_hand_t  *h;
    advice_t          a;
    int               col, total;
    bool              soft;

    if (r->phase != BJ_PHASE_PLAYER || r->active < 0 ||
        r->active >= r->nhands || r->ndealer < 1)
        return BJ_STRAT_NONE;

    h = &r->hands[r->active];
    if (h->done || h->n < 2)
        return BJ_STRAT_NONE;

    col = dealer_column(r->dealer[0]);
    total = bj_total(h->cards, h->n);
    soft = bj_soft(h->cards, h->n);

    if (surrender_advice(s, h, total, soft, col, &a))
        return resolve(s, a);
    if (pair_advice(s, h, col, &a))
        return resolve(s, a);
    if (soft) {
        if (total < SOFT_MIN)
            total = SOFT_MIN;
        if (total > SOFT_MAX)
            total = SOFT_MAX;
        return resolve(s, SOFT[total - SOFT_MIN][col]);
    }
    if (total < HARD_MIN)
        total = HARD_MIN;
    if (total > HARD_MAX)
        total = HARD_MAX;
    return resolve(s, HARD[total - HARD_MIN][col]);
}

const char *bj_strategy_word(bj_strategy_action_t a)
{
    switch (a) {
    case BJ_STRAT_HIT:       return "HIT";
    case BJ_STRAT_STAND:     return "STAND";
    case BJ_STRAT_DOUBLE:    return "DOUBLE";
    case BJ_STRAT_SPLIT:     return "SPLIT";
    case BJ_STRAT_SURRENDER: return "SURRENDER";
    default:                 return "-";
    }
}

bool bj_strategy_action(bj_strategy_action_t rec, bj_action_t *out)
{
    switch (rec) {
    case BJ_STRAT_HIT:       *out = BJ_HIT;       return true;
    case BJ_STRAT_STAND:     *out = BJ_STAND;     return true;
    case BJ_STRAT_DOUBLE:    *out = BJ_DOUBLE;    return true;
    case BJ_STRAT_SPLIT:     *out = BJ_SPLIT;     return true;
    case BJ_STRAT_SURRENDER: *out = BJ_SURRENDER; return true;
    default:                 return false;
    }
}

bool bj_strategy_agrees(bj_strategy_action_t rec, bj_action_t chosen)
{
    bj_action_t want;

    return bj_strategy_action(rec, &want) && want == chosen;
}
