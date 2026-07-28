/*
 * Blackjack basic-strategy regression tests.
 *
 * The chart is for the engine's own rule profile: six decks, dealer
 * stands on all 17s, dealer peeks, DAS, split only on equal ranks, four
 * hands, split aces get one card, late surrender on the initial unsplit
 * two-card hand.  Sessions are built by hand so every case is exact, and
 * the advice is always checked against bj_legal().
 */
#include <stdio.h>
#include <string.h>

#include "games/bj_strategy.h"
#include "games/blackjack.h"

static int fails;

#define CHECK(c) do {                                                   \
        if (!(c)) {                                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c);                 \
            fails++;                                                    \
        }                                                               \
    } while (0)

static card_t mk(int rank, int suit)
{
    return (card_t){ (uint8_t)rank, (uint8_t)suit };
}

/* A table waiting on one unsplit hand against the given up-card. */
static void setup(bj_session_t *s, const int *ranks, int n, int up)
{
    bj_round_t *r = &s->round;

    memset(s, 0, sizeof *s);
    s->bankroll = 1000 * BJ_HALF;
    s->base_bet = BJ_BET_DEFAULT;

    r->phase = BJ_PHASE_PLAYER;
    r->nhands = 1;
    r->active = 0;
    r->hands[0].n = n;
    r->hands[0].wager = BJ_BET_DEFAULT;
    for (int i = 0; i < n; i++)
        r->hands[0].cards[i] = mk(ranks[i], i % 4);
    r->dealer[0] = mk(up, 3);
    r->dealer[1] = mk(7, 0);            /* hole card, still face down */
    r->ndealer = 2;
    r->hole_hidden = true;
}

static const char *advice(const bj_session_t *s)
{
    return bj_strategy_word(bj_basic_strategy(s));
}

/* Whatever is recommended must be an action the engine will accept. */
static bool rec_legal(const bj_session_t *s)
{
    bj_strategy_action_t rec = bj_basic_strategy(s);

    if (rec == BJ_STRAT_NONE)
        return true;
    for (int a = 0; a < BJ_NACTIONS; a++)
        if (bj_strategy_agrees(rec, (bj_action_t)a))
            return bj_legal(s, (bj_action_t)a);
    return false;
}

/* One two-card hand against one up-card. */
static void two(const char *label, int r1, int r2, int up, const char *want)
{
    bj_session_t s;
    int ranks[2] = { r1, r2 };
    const char *got;

    setup(&s, ranks, 2, up);
    got = advice(&s);
    if (strcmp(got, want) != 0) {
        printf("FAIL %-22s got %-10s want %s\n", label, got, want);
        fails++;
    }
    if (!rec_legal(&s)) {
        printf("FAIL %-22s recommended an illegal action\n", label);
        fails++;
    }
}

#define ACE 1
#define TEN 10
#define JACK 11
#define QUEEN 12
#define KING 13

int main(void)
{
    bj_session_t s;

    /* ---- hard totals ---- */
    two("hard 8 vs 10",    5, 3, TEN,   "HIT");
    two("hard 9 vs 2",     5, 4, 2,     "HIT");
    two("hard 9 vs 3",     5, 4, 3,     "DOUBLE");
    two("hard 9 vs 6",     5, 4, 6,     "DOUBLE");
    two("hard 9 vs 7",     5, 4, 7,     "HIT");
    two("hard 10 vs 9",    6, 4, 9,     "DOUBLE");
    two("hard 10 vs 10",   6, 4, TEN,   "HIT");
    two("hard 11 vs 10",   6, 5, TEN,   "DOUBLE");
    two("hard 11 vs A",    6, 5, ACE,   "HIT");
    two("hard 12 vs 2",    TEN, 2, 2,   "HIT");
    two("hard 12 vs 3",    TEN, 2, 3,   "HIT");
    two("hard 12 vs 4",    TEN, 2, 4,   "STAND");
    two("hard 13 vs 2",    TEN, 3, 2,   "STAND");
    two("hard 14 vs 7",    TEN, 4, 7,   "HIT");
    two("hard 16 vs 6",    TEN, 6, 6,   "STAND");
    two("hard 16 vs 7",    TEN, 6, 7,   "HIT");
    two("hard 16 vs 10",   TEN, 6, TEN, "SURRENDER");
    two("hard 17 vs A",    TEN, 7, ACE, "STAND");
    two("hard 20 vs 10",   KING, QUEEN, TEN, "STAND");

    /* ---- late surrender, S17: 16 vs 9/10/A and 15 vs 10 ---- */
    two("hard 16 vs 9",    TEN, 6, 9,   "SURRENDER");
    two("hard 16 vs A",    TEN, 6, ACE, "SURRENDER");
    two("hard 15 vs 10",   TEN, 5, TEN, "SURRENDER");
    two("hard 15 vs 9",    TEN, 5, 9,   "HIT");
    /* S17 markers: an H17 chart would surrender both of these */
    two("hard 15 vs A",    TEN, 5, ACE, "HIT");
    two("hard 17 vs A",    TEN, 7, ACE, "STAND");
    /* a jack or a king is the same ten column */
    two("hard 16 vs J",    TEN, 6, JACK,  "SURRENDER");
    two("hard 16 vs K",    TEN, 6, KING,  "SURRENDER");

    /* ---- soft totals ---- */
    two("A,2 vs 4",        ACE, 2, 4,   "HIT");
    two("A,2 vs 5",        ACE, 2, 5,   "DOUBLE");
    two("A,2 vs 6",        ACE, 2, 6,   "DOUBLE");
    two("A,3 vs 5",        ACE, 3, 5,   "DOUBLE");
    two("A,4 vs 4",        ACE, 4, 4,   "DOUBLE");
    two("A,5 vs 3",        ACE, 5, 3,   "HIT");
    two("A,6 vs 2",        ACE, 6, 2,   "HIT");
    two("A,6 vs 3",        ACE, 6, 3,   "DOUBLE");
    two("A,6 vs 6",        ACE, 6, 6,   "DOUBLE");
    two("A,7 vs 2",        ACE, 7, 2,   "STAND");
    two("A,7 vs 3",        ACE, 7, 3,   "DOUBLE");
    two("A,7 vs 6",        ACE, 7, 6,   "DOUBLE");
    two("A,7 vs 7",        ACE, 7, 7,   "STAND");
    two("A,7 vs 8",        ACE, 7, 8,   "STAND");
    two("A,7 vs 9",        ACE, 7, 9,   "HIT");
    two("A,7 vs 10",       ACE, 7, TEN, "HIT");
    two("A,7 vs A",        ACE, 7, ACE, "HIT");
    /* S17 marker: an H17 chart doubles soft 19 against a six */
    two("A,8 vs 6",        ACE, 8, 6,   "STAND");
    two("A,9 vs 6",        ACE, 9, 6,   "STAND");

    /* ---- pairs ---- */
    two("A,A vs 5",        ACE, ACE, 5,   "SPLIT");
    two("A,A vs 10",       ACE, ACE, TEN, "SPLIT");
    two("A,A vs A",        ACE, ACE, ACE, "SPLIT");
    two("8,8 vs 5",        8, 8, 5,       "SPLIT");
    two("8,8 vs 9",        8, 8, 9,       "SPLIT");
    /* a pair of eights splits rather than surrendering under S17 */
    two("8,8 vs 10",       8, 8, TEN,     "SPLIT");
    two("8,8 vs A",        8, 8, ACE,     "SPLIT");
    two("10,10 vs 6",      TEN, TEN, 6,   "STAND");
    two("9,9 vs 6",        9, 9, 6,       "SPLIT");
    two("9,9 vs 7",        9, 9, 7,       "STAND");
    two("9,9 vs 9",        9, 9, 9,       "SPLIT");
    two("9,9 vs 10",       9, 9, TEN,     "STAND");
    two("9,9 vs A",        9, 9, ACE,     "STAND");
    /* fives follow hard ten, they are never a pair decision */
    two("5,5 vs 6",        5, 5, 6,       "DOUBLE");
    two("5,5 vs 9",        5, 5, 9,       "DOUBLE");
    two("5,5 vs 10",       5, 5, TEN,     "HIT");
    two("5,5 vs A",        5, 5, ACE,     "HIT");
    /* DAS opens up the small pairs */
    two("4,4 vs 4",        4, 4, 4,       "HIT");
    two("4,4 vs 5",        4, 4, 5,       "SPLIT");
    two("4,4 vs 6",        4, 4, 6,       "SPLIT");
    two("4,4 vs 7",        4, 4, 7,       "HIT");
    two("2,2 vs 2",        2, 2, 2,       "SPLIT");
    two("2,2 vs 7",        2, 2, 7,       "SPLIT");
    two("2,2 vs 8",        2, 2, 8,       "HIT");
    two("3,3 vs 7",        3, 3, 7,       "SPLIT");
    two("6,6 vs 2",        6, 6, 2,       "SPLIT");
    two("6,6 vs 7",        6, 6, 7,       "HIT");
    two("7,7 vs 7",        7, 7, 7,       "SPLIT");
    two("7,7 vs 8",        7, 7, 8,       "HIT");
    /* court cards split only on an exact rank match */
    two("K,K vs 6",        KING, KING, 6,   "STAND");
    two("Q,Q vs 6",        QUEEN, QUEEN, 6, "STAND");
    two("K,Q vs 6",        KING, QUEEN, 6,  "STAND");
    two("J,10 vs 6",       JACK, TEN, 6,    "STAND");

    /* ---- surrender is a first-decision, unsplit-hand play only ---- */
    {
        int three_card_16[3] = { TEN, 3, 3 };

        setup(&s, three_card_16, 3, TEN);
        CHECK(!bj_legal(&s, BJ_SURRENDER));
        CHECK(strcmp(advice(&s), "HIT") == 0);      /* not SURRENDER */
        CHECK(rec_legal(&s));
    }
    {
        int sixteen[2] = { TEN, 6 };

        setup(&s, sixteen, 2, TEN);
        s.round.hands[0].from_split = true;
        s.round.nhands = 2;
        s.round.hands[1].n = 2;
        s.round.hands[1].wager = BJ_BET_DEFAULT;
        s.round.hands[1].from_split = true;
        CHECK(!bj_legal(&s, BJ_SURRENDER));
        CHECK(strcmp(advice(&s), "HIT") == 0);
        CHECK(rec_legal(&s));
    }

    /* ---- double falls back to the chart's own alternative ---- */
    {
        int nine[3] = { 2, 3, 4 };                  /* hard 9, three cards */

        setup(&s, nine, 3, 3);
        CHECK(!bj_legal(&s, BJ_DOUBLE));
        CHECK(strcmp(advice(&s), "HIT") == 0);      /* DH -> hit */
        CHECK(rec_legal(&s));
    }
    {
        int soft18[3] = { ACE, 3, 4 };              /* soft 18, three cards */

        setup(&s, soft18, 3, 3);
        CHECK(!bj_legal(&s, BJ_DOUBLE));
        CHECK(strcmp(advice(&s), "STAND") == 0);    /* DS -> stand */
        CHECK(rec_legal(&s));
    }
    {
        int eleven[2] = { 6, 5 };

        setup(&s, eleven, 2, 5);
        s.bankroll = s.round.hands[0].wager - 1;    /* cannot fund it */
        CHECK(!bj_legal(&s, BJ_DOUBLE));
        CHECK(strcmp(advice(&s), "HIT") == 0);
        CHECK(rec_legal(&s));
    }
    {
        int soft18[2] = { ACE, 7 };

        setup(&s, soft18, 2, 4);
        s.bankroll = s.round.hands[0].wager - 1;
        CHECK(strcmp(advice(&s), "STAND") == 0);    /* DS -> stand */
        CHECK(rec_legal(&s));
    }

    /* ---- split falls back to the hand's total ---- */
    {
        int eights[2] = { 8, 8 };

        setup(&s, eights, 2, 5);
        s.bankroll = s.round.hands[0].wager - 1;
        CHECK(!bj_legal(&s, BJ_SPLIT));
        CHECK(strcmp(advice(&s), "STAND") == 0);    /* hard 16 vs 5 */
        CHECK(rec_legal(&s));

        /* with no split available, 16 vs 10 goes back to surrendering */
        setup(&s, eights, 2, TEN);
        s.bankroll = s.round.hands[0].wager - 1;
        CHECK(!bj_legal(&s, BJ_SPLIT));
        CHECK(bj_legal(&s, BJ_SURRENDER));
        CHECK(strcmp(advice(&s), "SURRENDER") == 0);
        CHECK(rec_legal(&s));
    }
    {
        int eights[2] = { 8, 8 };

        /* four hands already: no split, and no surrender either */
        setup(&s, eights, 2, 5);
        s.round.nhands = BJ_MAX_HANDS;
        for (int i = 1; i < BJ_MAX_HANDS; i++) {
            s.round.hands[i].n = 2;
            s.round.hands[i].wager = BJ_BET_DEFAULT;
            s.round.hands[i].from_split = true;
        }
        s.round.hands[0].from_split = true;
        CHECK(!bj_legal(&s, BJ_SPLIT));
        CHECK(strcmp(advice(&s), "STAND") == 0);    /* hard 16 vs 5 */
        CHECK(rec_legal(&s));

        setup(&s, eights, 2, TEN);
        s.round.nhands = BJ_MAX_HANDS;
        for (int i = 1; i < BJ_MAX_HANDS; i++) {
            s.round.hands[i].n = 2;
            s.round.hands[i].wager = BJ_BET_DEFAULT;
            s.round.hands[i].from_split = true;
        }
        s.round.hands[0].from_split = true;
        CHECK(!bj_legal(&s, BJ_SPLIT));
        CHECK(!bj_legal(&s, BJ_SURRENDER));
        CHECK(strcmp(advice(&s), "HIT") == 0);      /* RH -> hit */
        CHECK(rec_legal(&s));
    }

    /* ---- split aces never get an illegal recommendation ---- */
    {
        int ace_ten[2] = { ACE, TEN };

        setup(&s, ace_ten, 2, 6);
        s.round.hands[0].from_split = true;
        s.round.hands[0].split_aces = true;
        s.round.nhands = 2;
        s.round.hands[1].n = 2;
        s.round.hands[1].wager = BJ_BET_DEFAULT;
        CHECK(!bj_legal(&s, BJ_DOUBLE));
        CHECK(!bj_legal(&s, BJ_SPLIT));
        CHECK(!bj_legal(&s, BJ_SURRENDER));
        CHECK(rec_legal(&s));
        /* and once the engine closes the hand there is nothing to advise */
        s.round.hands[0].done = true;
        CHECK(bj_basic_strategy(&s) == BJ_STRAT_NONE);
    }

    /* ---- no advice outside a pending player decision ---- */
    {
        int sixteen[2] = { TEN, 6 };

        setup(&s, sixteen, 2, 9);
        s.round.phase = BJ_PHASE_BET;
        CHECK(bj_basic_strategy(&s) == BJ_STRAT_NONE);
        s.round.phase = BJ_PHASE_INSURANCE;
        CHECK(bj_basic_strategy(&s) == BJ_STRAT_NONE);
        s.round.phase = BJ_PHASE_SETTLED;
        CHECK(bj_basic_strategy(&s) == BJ_STRAT_NONE);
        s.round.phase = BJ_PHASE_PLAYER;
        s.round.active = s.round.nhands;            /* no active hand */
        CHECK(bj_basic_strategy(&s) == BJ_STRAT_NONE);
    }

    /* ---- nothing recommended is ever illegal, over every start ---- */
    {
        long bankrolls[3] = { 1000 * BJ_HALF, BJ_BET_DEFAULT,
                              BJ_BET_DEFAULT - 1 };

        for (int b = 0; b < 3; b++)
            for (int r1 = 1; r1 <= 13; r1++)
                for (int r2 = 1; r2 <= 13; r2++)
                    for (int up = 1; up <= 13; up++) {
                        int ranks[2] = { r1, r2 };

                        setup(&s, ranks, 2, up);
                        s.bankroll = bankrolls[b];
                        if (!rec_legal(&s)) {
                            printf("FAIL illegal advice %d,%d vs %d "
                                   "(bankroll %ld)\n", r1, r2, up,
                                   bankrolls[b]);
                            fails++;
                        }
                        if (bj_basic_strategy(&s) == BJ_STRAT_NONE) {
                            printf("FAIL no advice for %d,%d vs %d\n",
                                   r1, r2, up);
                            fails++;
                        }
                    }
    }

    /* ---- the advice is count independent ---- */
    {
        int sixteen[2] = { TEN, 6 };
        bj_session_t rich, poor;
        bj_count_t c;
        bj_strategy_action_t a, b;

        setup(&rich, sixteen, 2, TEN);
        setup(&poor, sixteen, 2, TEN);
        /* a shoe three quarters dealt, whatever the running count says */
        poor.shoe.count = BJ_SHOE_CARDS;
        poor.shoe.pos = BJ_SHOE_CARDS - 80;
        bj_count_reset(&c);
        c.running = 20;                 /* a count that would deviate */
        a = bj_basic_strategy(&rich);
        b = bj_basic_strategy(&poor);
        CHECK(a == b);
        c.running = -20;
        CHECK(bj_basic_strategy(&poor) == b);
        /* 16 vs 10 stays a surrender: no Fab 4 or Illustrious 18 here */
        CHECK(a == BJ_STRAT_SURRENDER);

        setup(&rich, (int[2]){ TEN, 6 }, 2, TEN);
        rich.round.hands[0].n = 2;
        CHECK(strcmp(advice(&rich), "SURRENDER") == 0);
    }

    /* ---- grading helper ---- */
    CHECK(bj_strategy_agrees(BJ_STRAT_HIT, BJ_HIT));
    CHECK(!bj_strategy_agrees(BJ_STRAT_HIT, BJ_STAND));
    CHECK(bj_strategy_agrees(BJ_STRAT_SURRENDER, BJ_SURRENDER));
    CHECK(bj_strategy_agrees(BJ_STRAT_SPLIT, BJ_SPLIT));
    CHECK(bj_strategy_agrees(BJ_STRAT_DOUBLE, BJ_DOUBLE));
    /* no decision grades as nothing at all */
    for (int a = 0; a < BJ_NACTIONS; a++)
        CHECK(!bj_strategy_agrees(BJ_STRAT_NONE, (bj_action_t)a));
    CHECK(strcmp(bj_strategy_word(BJ_STRAT_NONE), "-") == 0);

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
