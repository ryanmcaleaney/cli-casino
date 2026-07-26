/*
 * Three Card Poker session/frontend regression tests.
 *
 * These drive the tc_session_t API the GUI plays through: wager plumbing,
 * bankroll accounting across a round, and the funding rule that decides
 * whether PLAY may be offered at all.  The rules themselves are checked by
 * `casino threecard check`; nothing here re-implements one.
 */
#include <stdio.h>

#include "games/threecard.h"
#include "rng.h"

static int fails;

#define CHECK(c) do {                                                   \
        if (!(c)) {                                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c);                 \
            fails++;                                                    \
        }                                                               \
    } while (0)

static void start(tc_session_t *s, rng_t *rng, uint64_t seed)
{
    rng_init(rng, true, seed);
    tc_session_start(s);
}

int main(void)
{
    tc_session_t s;
    rng_t        rng;

    /* ---- opening state ---- */
    start(&s, &rng, 1);
    CHECK(s.bankroll == TC_BANKROLL_START);
    CHECK(s.ante == TC_ANTE_DEFAULT);
    CHECK(s.pairplus == 0);          /* pair plus is off unless asked for */
    CHECK(s.phase == TC_PHASE_BET);
    CHECK(tc_can_deal(&s));

    /* ---- wagers clamp to the table limits and to the bankroll ---- */
    tc_set_ante(&s, 0);
    CHECK(s.ante == TC_ANTE_MIN);
    tc_set_ante(&s, TC_ANTE_MAX + 1000);
    CHECK(s.ante == TC_ANTE_MAX);
    tc_set_pairplus(&s, -5);
    CHECK(s.pairplus == 0);
    tc_set_pairplus(&s, TC_ANTE_MAX + 1000);
    CHECK(s.pairplus == TC_ANTE_MAX);
    /* the two wagers together can never exceed the bankroll */
    CHECK(s.ante + s.pairplus <= s.bankroll);

    /* ---- a played round moves exactly what the engine settled ---- */
    start(&s, &rng, 7);
    tc_set_ante(&s, 25);
    tc_set_pairplus(&s, 5);
    tc_deal(&s, &rng);
    CHECK(s.phase == TC_PHASE_DECISION);
    /* the ante and the pair plus are off the bankroll, the play is not */
    CHECK(s.bankroll == TC_BANKROLL_START - 30);
    CHECK(s.round.ante == 25 && s.round.pairplus == 5);
    CHECK(!s.round.settled);
    CHECK(tc_can_play(&s));
    tc_decide(&s, TC_ACT_PLAY);
    CHECK(s.phase == TC_PHASE_SETTLED);
    CHECK(s.round.settled);
    CHECK(s.round.play == s.round.ante);            /* play matches the ante */
    CHECK(s.round.wagered == 25 + 25 + 5);
    /* bankroll = start - everything staked + everything returned */
    CHECK(s.bankroll == TC_BANKROLL_START - s.round.wagered +
                        s.round.returned);
    CHECK(s.round.returned - s.round.wagered ==
          s.round.ante_net + s.round.play_net + s.round.ante_bonus +
          s.round.pairplus_net);

    /* ---- folding never stakes the play wager ---- */
    start(&s, &rng, 7);
    tc_set_ante(&s, 25);
    tc_set_pairplus(&s, 5);
    tc_deal(&s, &rng);
    tc_decide(&s, TC_ACT_FOLD);
    CHECK(s.round.play == 0);
    CHECK(s.round.wagered == 25 + 5);
    CHECK(s.round.ante_net == -25);
    CHECK(s.round.ante_bonus == 0);                 /* no bonus on a fold */
    CHECK(s.round.outcome == TC_OUT_FOLD);
    CHECK(s.bankroll == TC_BANKROLL_START - 30 + s.round.returned);

    /* the same deal, both ways: the cards and the pair plus agree, so the
     * side bet really is independent of the ante decision */
    {
        tc_session_t a, b;
        rng_t ra, rb;

        start(&a, &ra, 12);
        tc_set_ante(&a, 25);
        tc_set_pairplus(&a, 5);
        tc_deal(&a, &ra);
        tc_decide(&a, TC_ACT_PLAY);

        start(&b, &rb, 12);
        tc_set_ante(&b, 25);
        tc_set_pairplus(&b, 5);
        tc_deal(&b, &rb);
        tc_decide(&b, TC_ACT_FOLD);

        for (int i = 0; i < TC_CARDS; i++) {
            CHECK(a.round.player[i].rank == b.round.player[i].rank &&
                  a.round.player[i].suit == b.round.player[i].suit);
            CHECK(a.round.dealer[i].rank == b.round.dealer[i].rank &&
                  a.round.dealer[i].suit == b.round.dealer[i].suit);
        }
        CHECK(a.round.pairplus_net == b.round.pairplus_net);
        CHECK(a.round.dealer_qualifies == b.round.dealer_qualifies);
    }

    /* ---- PLAY needs a fundable second wager, FOLD never does ---- */
    start(&s, &rng, 3);
    s.bankroll = 30;                 /* enough to ante, not to play */
    tc_set_ante(&s, 25);
    tc_set_pairplus(&s, 5);
    CHECK(tc_can_deal(&s));
    tc_deal(&s, &rng);
    CHECK(s.bankroll == 0);
    CHECK(!tc_can_play(&s));         /* the GUI greys PLAY out here */
    tc_decide(&s, TC_ACT_PLAY);      /* ... and the engine refuses it */
    CHECK(s.phase == TC_PHASE_DECISION);
    CHECK(!s.round.settled);
    tc_decide(&s, TC_ACT_FOLD);      /* folding is always available */
    CHECK(s.phase == TC_PHASE_SETTLED);
    CHECK(s.round.outcome == TC_OUT_FOLD);

    /* exactly enough to fund the play wager */
    start(&s, &rng, 3);
    s.bankroll = 50;
    tc_set_ante(&s, 25);
    tc_deal(&s, &rng);
    CHECK(s.bankroll == 25);
    CHECK(tc_can_play(&s));

    /* ---- a broken bankroll stops the deal and the rebuy restores it ---- */
    start(&s, &rng, 5);
    s.bankroll = 0;
    tc_set_ante(&s, 25);
    CHECK(!tc_can_deal(&s));
    tc_bankroll_reset(&s);
    CHECK(s.bankroll == TC_BANKROLL_START);
    CHECK(tc_can_deal(&s));

    /* ---- wagers are locked while a decision is pending ---- */
    start(&s, &rng, 9);
    tc_set_ante(&s, 25);
    tc_deal(&s, &rng);
    tc_set_ante(&s, 100);
    tc_set_pairplus(&s, 100);
    CHECK(s.ante == 25 && s.pairplus == 0);
    CHECK(!tc_can_deal(&s));         /* no re-deal mid-hand */

    /* ---- the pay tables the GUI reads are the engine's own ---- */
    CHECK(tc_front_ante_bonus(TC_STRAIGHT) == 1);
    CHECK(tc_front_ante_bonus(TC_THREE_OF_A_KIND) == 4);
    CHECK(tc_front_ante_bonus(TC_STRAIGHT_FLUSH) == 5);
    CHECK(tc_front_ante_bonus(TC_PAIR) == 0);
    CHECK(tc_front_ante_bonus(TC_FLUSH) == 0);
    CHECK(tc_front_ante_bonus(TC_HIGH_CARD) == 0);
    CHECK(tc_front_pairplus(TC_PAIR) == 1);
    CHECK(tc_front_pairplus(TC_FLUSH) == 4);
    CHECK(tc_front_pairplus(TC_STRAIGHT) == 6);
    CHECK(tc_front_pairplus(TC_THREE_OF_A_KIND) == 30);
    CHECK(tc_front_pairplus(TC_STRAIGHT_FLUSH) == 40);
    CHECK(tc_front_pairplus(TC_HIGH_CARD) == 0);

    /* ---- a seeded deal is reproducible, a different seed is not ---- */
    {
        tc_session_t a, b;
        rng_t ra, rb;

        start(&a, &ra, 21);
        tc_deal(&a, &ra);
        start(&b, &rb, 21);
        tc_deal(&b, &rb);
        for (int i = 0; i < TC_CARDS; i++)
            CHECK(a.round.player[i].rank == b.round.player[i].rank &&
                  a.round.player[i].suit == b.round.player[i].suit);

        start(&b, &rb, 22);
        tc_deal(&b, &rb);
        CHECK(a.round.player[0].rank != b.round.player[0].rank ||
              a.round.player[0].suit != b.round.player[0].suit);
    }

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
