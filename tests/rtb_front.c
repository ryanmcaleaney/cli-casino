/*
 * Ride the Bus frontend/session regression tests.
 *
 * These drive the rtb_front_* API the GUI plays through.  Nothing here
 * re-implements a rule: the winning line for a deal is *found* by
 * replaying the same seed with every guess vector, which also proves the
 * options of each round are exact complements (exactly one wins) and that
 * a seeded deal is reproducible.
 */
#include <stdio.h>

#include "games/ridethebus.h"
#include "rng.h"

static int fails;

#define CHECK(c) do {                                                   \
        if (!(c)) {                                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c);                 \
            fails++;                                                    \
        }                                                               \
    } while (0)

#define BET 100

static const long WANT_PAYOUT[RTB_ROUNDS] = { 2 * BET, 3 * BET, 4 * BET,
                                              20 * BET };

/* Guess vector v: rounds 1-3 take one bit each, round 4 two bits. */
static void vector_guesses(int v, int out[RTB_ROUNDS])
{
    out[0] = v & 1;
    out[1] = (v >> 1) & 1;
    out[2] = (v >> 2) & 1;
    out[3] = (v >> 3) & 3;
}

static void start(rtb_game_t *g, uint64_t seed)
{
    rng_t rng;

    rng_init(&rng, true, seed);
    rtb_front_start(g, BET, &rng);
}

/* Play `stop` rounds of the vector, then cash out if asked. */
static void play(rtb_game_t *g, uint64_t seed, int vec, int stop, bool cash)
{
    int guess[RTB_ROUNDS];

    vector_guesses(vec, guess);
    start(g, seed);
    for (int r = 0; r < stop; r++) {
        if (rtb_front_over(g))
            break;
        rtb_front_guess(g, guess[r]);
    }
    if (cash && rtb_front_can_cash(g))
        rtb_front_cash_out(g);
}

/* The one vector out of 32 that wins all four rounds for this deal. */
static int winning_vector(uint64_t seed)
{
    rtb_game_t g;
    int found = -1, nfound = 0;

    for (int v = 0; v < 32; v++) {
        play(&g, seed, v, RTB_ROUNDS, false);
        if (g.outcome == RTB_BUS) {
            found = v;
            nfound++;
        }
    }
    CHECK(nfound == 1);         /* exactly one line rides the bus */
    return found;
}

int main(void)
{
    const uint64_t seed = 123;
    rtb_game_t g;
    int guess[RTB_ROUNDS];
    int vec = winning_vector(seed);

    CHECK(vec >= 0);
    if (vec < 0) {
        printf("no winning line found\n");
        return 1;
    }
    vector_guesses(vec, guess);

    /* a fresh session is at round 1 with nothing dealt or owed */
    start(&g, seed);
    CHECK(rtb_front_stage(&g) == RTB_RED_BLACK);
    CHECK(!rtb_front_over(&g));
    CHECK(!rtb_front_can_cash(&g));      /* nothing to cash before round 1 */
    CHECK(rtb_front_nchoices(RTB_RED_BLACK) == 2);
    CHECK(rtb_front_nchoices(RTB_HIGH_LOW) == 2);
    CHECK(rtb_front_nchoices(RTB_INSIDE_OUTSIDE) == 2);
    CHECK(rtb_front_nchoices(RTB_SUIT) == 4);
    CHECK(g.ncards == 0);
    CHECK(g.bet == BET);
    CHECK(g.payout == 0);

    /* round 1 -> round 2: one card, 2x, cash out now offered */
    CHECK(rtb_front_guess(&g, guess[0]));
    CHECK(g.ncards == 1);
    CHECK(g.rounds_won == 1);
    CHECK(g.payout == WANT_PAYOUT[0]);
    CHECK(rtb_front_stage(&g) == RTB_HIGH_LOW);
    CHECK(rtb_front_can_cash(&g));
    CHECK(!rtb_front_over(&g));

    /* payout ladder 2x -> 3x -> 4x -> 20x, one card per round played */
    for (int r = 1; r < RTB_ROUNDS; r++) {
        CHECK(rtb_front_guess(&g, guess[r]));
        CHECK(g.ncards == r + 1);
        CHECK(g.rounds_won == r + 1);
        CHECK(g.payout == WANT_PAYOUT[r]);
    }
    /* the fourth win rides the bus and ends the game */
    CHECK(g.outcome == RTB_BUS);
    CHECK(rtb_front_over(&g));
    CHECK(!rtb_front_can_cash(&g));
    CHECK(rtb_front_stage(&g) == RTB_COMPLETE);
    CHECK(g.payout == 20 * BET);
    CHECK(g.ncards == RTB_ROUNDS);

    /* cashing out after rounds 1, 2 and 3 keeps that round's payout */
    for (int r = 1; r <= 3; r++) {
        play(&g, seed, vec, r, true);
        CHECK(g.outcome == RTB_CASHOUT);
        CHECK(rtb_front_over(&g));
        CHECK(g.rounds_won == r);
        CHECK(g.ncards == r);            /* no extra card is dealt */
        CHECK(g.payout == WANT_PAYOUT[r - 1]);
    }

    /* losing a later round forfeits the wager and the payout won so far */
    for (int r = 1; r < RTB_ROUNDS; r++) {
        int lose = vec ^ (r == 3 ? (1 << 3) : (1 << r));  /* wrong guess */
        play(&g, seed, lose, RTB_ROUNDS, false);
        CHECK(g.outcome == RTB_LOSS);
        CHECK(rtb_front_over(&g));
        CHECK(g.payout == 0);
        CHECK(g.rounds_won == r);
        CHECK(g.ncards == r + 1);        /* the losing round dealt one card */
    }

    /* the same seed always deals the same cards */
    rtb_game_t a, b;
    play(&a, seed, vec, RTB_ROUNDS, false);
    play(&b, seed, vec, RTB_ROUNDS, false);
    CHECK(a.ncards == b.ncards && a.payout == b.payout &&
          a.outcome == b.outcome);
    for (int i = 0; i < a.ncards; i++)
        CHECK(a.cards[i].rank == b.cards[i].rank &&
              a.cards[i].suit == b.cards[i].suit);
    /* and a different seed deals a different game */
    play(&b, seed + 1, vec, RTB_ROUNDS, false);
    CHECK(a.cards[0].rank != b.cards[0].rank ||
          a.cards[0].suit != b.cards[0].suit);

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
