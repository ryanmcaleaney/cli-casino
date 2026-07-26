/*
 * Let It Ride session/frontend regression tests.
 *
 * These drive the lir_session_t API the GUI plays through: wager plumbing,
 * bankroll accounting across a round, when a pulled stake comes back, and
 * the reveal gate that stops a frontend seeing a community card early.
 * The pay table and hand ranking are checked by `casino letitride check`;
 * nothing here re-implements a rule.
 */
#include <stdio.h>

#include "games/letitride.h"
#include "rng.h"

static int fails;

#define CHECK(c) do {                                                   \
        if (!(c)) {                                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c);                 \
            fails++;                                                    \
        }                                                               \
    } while (0)

static void start(lir_session_t *s, rng_t *rng, uint64_t seed)
{
    rng_init(rng, true, seed);
    lir_session_start(s);
}

/* Bankroll must always be start - what gambled + what came back. */
static void check_bankroll(const lir_session_t *s)
{
    const lir_round_t *r = &s->round;

    CHECK(s->bankroll == LIR_BANKROLL_START - r->committed +
                         r->pulled_back + r->returned);
    CHECK(r->committed == LIR_BETS * r->bet);
    CHECK(r->pulled_back == (LIR_BETS - r->nriding) * r->bet);
    CHECK(r->wagered == r->nriding * r->bet);
}

int main(void)
{
    lir_session_t s;
    rng_t         rng;

    /* ---- opening state ---- */
    start(&s, &rng, 1);
    CHECK(s.bankroll == LIR_BANKROLL_START);
    CHECK(s.bet == LIR_BET_DEFAULT);
    CHECK(s.phase == LIR_PHASE_BET);
    CHECK(lir_can_deal(&s));

    /* ---- the wager clamps to the limits and to a third of the bankroll ---- */
    lir_set_bet(&s, 0);
    CHECK(s.bet == LIR_BET_MIN);
    lir_set_bet(&s, LIR_BET_MAX + 1000);
    CHECK(s.bet * LIR_BETS <= s.bankroll);      /* all three must be funded */
    s.bankroll = 60;
    lir_set_bet(&s, 100);
    CHECK(s.bet == 20);
    CHECK(lir_can_deal(&s));

    /* ---- ride,ride: three wagers gamble, nothing comes back early ---- */
    start(&s, &rng, 7);
    lir_set_bet(&s, 25);
    lir_deal(&s, &rng);
    CHECK(s.phase == LIR_PHASE_DECISION);
    CHECK(s.round.stage == LIR_STAGE_DECISION1);
    CHECK(s.bankroll == LIR_BANKROLL_START - 75);
    CHECK(s.round.nriding == 3);
    /* neither community card may be looked at yet */
    CHECK(lir_community_visible(&s.round, 0) == NULL);
    CHECK(lir_community_visible(&s.round, 1) == NULL);
    lir_decide(&s, false);
    CHECK(s.round.stage == LIR_STAGE_DECISION2);
    CHECK(s.bankroll == LIR_BANKROLL_START - 75);   /* nothing pulled */
    CHECK(lir_community_visible(&s.round, 0) != NULL);
    CHECK(lir_community_visible(&s.round, 1) == NULL);   /* still down */
    CHECK(!s.round.settled);
    lir_decide(&s, false);
    CHECK(s.phase == LIR_PHASE_SETTLED);
    CHECK(s.round.settled);
    CHECK(s.round.stage == LIR_STAGE_DONE);
    CHECK(lir_community_visible(&s.round, 1) != NULL);
    CHECK(s.round.nriding == 3);
    CHECK(s.round.riding[0] && s.round.riding[1] && s.round.riding[2]);
    CHECK(s.round.pulled_back == 0);
    CHECK(s.round.wagered == 75);
    check_bankroll(&s);

    /* ---- pull,ride: bet 1 comes straight back at the first decision ---- */
    start(&s, &rng, 7);
    lir_set_bet(&s, 25);
    lir_deal(&s, &rng);
    lir_decide(&s, true);
    CHECK(s.bankroll == LIR_BANKROLL_START - 75 + 25);  /* refunded at once */
    CHECK(!s.round.riding[0]);
    CHECK(s.round.nriding == 2);
    CHECK(s.round.pulled_back == 25);
    lir_decide(&s, false);
    CHECK(s.round.riding[1] && s.round.riding[2]);
    CHECK(s.round.wagered == 50);
    check_bankroll(&s);

    /* ---- pull,pull: only bet 3 is left, and it can never be pulled ---- */
    start(&s, &rng, 7);
    lir_set_bet(&s, 25);
    lir_deal(&s, &rng);
    lir_decide(&s, true);
    lir_decide(&s, true);
    CHECK(!s.round.riding[0] && !s.round.riding[1]);
    CHECK(s.round.riding[2]);                   /* bet 3 always rides */
    CHECK(s.round.nriding == 1);
    CHECK(s.round.pulled_back == 50);
    CHECK(s.round.wagered == 25);
    check_bankroll(&s);
    /* the settled round takes no further decisions */
    lir_decide(&s, true);
    CHECK(s.round.nriding == 1 && s.round.pulled_back == 50);

    /* ---- ride,pull ---- */
    start(&s, &rng, 7);
    lir_set_bet(&s, 25);
    lir_deal(&s, &rng);
    lir_decide(&s, false);
    lir_decide(&s, true);
    CHECK(s.round.riding[0] && !s.round.riding[1] && s.round.riding[2]);
    CHECK(s.round.nriding == 2);
    check_bankroll(&s);

    /* ---- the same deal pays every riding wager identically ---- */
    {
        lir_session_t a, b;
        rng_t ra, rb;

        start(&a, &ra, 21);
        lir_set_bet(&a, 25);
        lir_deal(&a, &ra);
        lir_decide(&a, false);
        lir_decide(&a, false);

        start(&b, &rb, 21);
        lir_set_bet(&b, 25);
        lir_deal(&b, &rb);
        lir_decide(&b, true);
        lir_decide(&b, true);

        /* same cards, same category: pulling changes nothing but stakes */
        CHECK(a.round.cat == b.round.cat);
        for (int i = 0; i < LIR_COMMUNITY; i++)
            CHECK(a.round.community[i].rank == b.round.community[i].rank &&
                  a.round.community[i].suit == b.round.community[i].suit);
        /* three riding wagers return exactly three times one riding wager */
        CHECK(a.round.returned == 3 * b.round.returned);
        CHECK(a.round.wagered == 3 * b.round.wagered);
    }

    /* ---- a losing hand costs only what was riding ---- */
    {
        card_t p[LIR_PLAYER_CARDS] = { { 9, 2 }, { 9, 1 }, { 2, 0 } };
        card_t c[LIR_COMMUNITY]    = { { 5, 3 }, { 7, 1 } };
        lir_round_t r;

        lir_round_deal_fixed(&r, p, c, 25);     /* pair of nines: no pay */
        lir_round_decide(&r, true);
        lir_round_decide(&r, true);
        CHECK(r.cat == LIR_NOTHING);
        CHECK(r.returned == 0);
        CHECK(r.wagered == 25);                 /* not the full 75 */
        CHECK(r.pulled_back == 50);
        CHECK(r.returned - r.wagered == -25);
    }

    /* ---- wagers are locked while a decision is pending ---- */
    start(&s, &rng, 9);
    lir_set_bet(&s, 25);
    lir_deal(&s, &rng);
    lir_set_bet(&s, 100);
    CHECK(s.bet == 25);
    CHECK(!lir_can_deal(&s));                   /* no re-deal mid-hand */

    /* a thin bankroll scales the wager down rather than blocking play */
    start(&s, &rng, 5);
    s.bankroll = 10;
    lir_set_bet(&s, 25);
    CHECK(s.bet == 3);                          /* 10 / 3 wagers */
    CHECK(lir_can_deal(&s));

    /* ---- a broken bankroll stops the deal; the rebuy restores it ---- */
    start(&s, &rng, 5);
    s.bankroll = 2;                             /* under one unit each */
    lir_set_bet(&s, 25);
    CHECK(!lir_can_deal(&s));
    lir_bankroll_reset(&s);
    CHECK(s.bankroll == LIR_BANKROLL_START);
    CHECK(lir_can_deal(&s));

    /* ---- the pay table the GUI reads is the engine's own ---- */
    CHECK(lir_front_payout(LIR_NOTHING) == 0);
    CHECK(lir_front_payout(LIR_PAIR_TENS) == 1);
    CHECK(lir_front_payout(LIR_TWO_PAIR) == 2);
    CHECK(lir_front_payout(LIR_THREE_OF_A_KIND) == 3);
    CHECK(lir_front_payout(LIR_STRAIGHT) == 5);
    CHECK(lir_front_payout(LIR_FLUSH) == 8);
    CHECK(lir_front_payout(LIR_FULL_HOUSE) == 11);
    CHECK(lir_front_payout(LIR_FOUR_OF_A_KIND) == 50);
    CHECK(lir_front_payout(LIR_STRAIGHT_FLUSH) == 200);
    CHECK(lir_front_payout(LIR_ROYAL_FLUSH) == 1000);

    /* ---- a seeded deal is reproducible, a different seed is not ---- */
    {
        lir_session_t a, b;
        rng_t ra, rb;

        start(&a, &ra, 33);
        lir_deal(&a, &ra);
        start(&b, &rb, 33);
        lir_deal(&b, &rb);
        for (int i = 0; i < LIR_PLAYER_CARDS; i++)
            CHECK(a.round.player[i].rank == b.round.player[i].rank &&
                  a.round.player[i].suit == b.round.player[i].suit);

        start(&b, &rb, 34);
        lir_deal(&b, &rb);
        CHECK(a.round.player[0].rank != b.round.player[0].rank ||
              a.round.player[0].suit != b.round.player[0].suit);
    }

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
