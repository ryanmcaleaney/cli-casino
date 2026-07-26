/*
 * Engine regression: BJ_PHASE_SETTLED is a between-round state.
 *
 * The GUI shows the settled result of a hand and offers DEAL, BET +/- and
 * a re-buy from that screen, so the engine must accept all three there;
 * mid-round they must still be refused.  These transitions are not
 * reachable from the CLI, which drives one round per iteration, so they
 * are checked directly against the frontend API.
 */
#include <stdio.h>

#include "games/blackjack.h"
#include "rng.h"

static int fails;

#define CHECK(c) do {                                                   \
        if (!(c)) {                                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c);                 \
            fails++;                                                    \
        }                                                               \
    } while (0)

/* Stand every hand out so the round reaches BJ_PHASE_SETTLED. */
static void stand_out(bj_session_t *s, rng_t *rng)
{
    if (bj_insurance_pending(s))
        bj_insurance(s, false, rng);
    while (s->round.phase == BJ_PHASE_PLAYER)
        bj_act(s, BJ_STAND, rng);
}

int main(void)
{
    rng_t rng;
    bj_session_t s;

    rng_init(&rng, true, 1);
    bj_session_start(&s, &rng);
    CHECK(s.round.phase == BJ_PHASE_BET);
    CHECK(bj_can_deal(&s));

    /* ---- mid-round the table is locked ---- */
    bj_deal(&s, &rng);
    long bet = s.base_bet, bank = s.bankroll;
    CHECK(s.round.phase != BJ_PHASE_BET);
    CHECK(s.round.phase != BJ_PHASE_SETTLED || s.round.dealer_bj);
    if (s.round.phase == BJ_PHASE_INSURANCE ||
        s.round.phase == BJ_PHASE_PLAYER) {
        CHECK(!bj_can_deal(&s));
        bj_set_bet(&s, bet + 10 * BJ_HALF);
        CHECK(s.base_bet == bet);           /* bet change refused */
        bj_bankroll_reset(&s);
        CHECK(s.bankroll == bank);          /* re-buy refused */
    }

    /* ---- settled is a between-round state ---- */
    stand_out(&s, &rng);
    CHECK(s.round.phase == BJ_PHASE_SETTLED);

    /* 1. another hand can be dealt straight from the settled screen */
    CHECK(bj_can_deal(&s));

    /* 2. the bet can be changed while the result is still on display */
    bj_set_bet(&s, bet + 10 * BJ_HALF);
    CHECK(s.base_bet == bet + 10 * BJ_HALF);
    bj_set_bet(&s, BJ_BET_MAX + 1);
    CHECK(s.base_bet == BJ_BET_MAX);        /* still clamped */
    bj_set_bet(&s, bet);

    /* 3. and a broke player can re-buy without waiting for a new round */
    long saved = s.bankroll;
    s.bankroll = 0;
    CHECK(!bj_can_deal(&s));                /* no credits, no deal */
    bj_bankroll_reset(&s);
    CHECK(s.bankroll == BJ_BANKROLL_START);
    s.bankroll = saved;

    /* dealing from settled starts a clean round with the current bet */
    long remaining = shoe_remaining(&s.shoe);
    bj_deal(&s, &rng);
    CHECK(s.round.phase != BJ_PHASE_SETTLED || s.round.dealer_bj);
    CHECK(s.round.nhands == 1);
    CHECK(s.round.hands[0].n == 2);
    CHECK(s.round.ndealer == 2);
    CHECK(s.round.hands[0].wager == bet);
    CHECK(s.round.returned == 0 || s.round.dealer_bj);
    CHECK(s.bankroll == saved - bet);
    CHECK(s.shuffled || shoe_remaining(&s.shoe) == remaining - 4);

    /* a full session of back-to-back hands never stalls */
    for (int i = 0; i < 40; i++) {
        stand_out(&s, &rng);
        CHECK(s.round.phase == BJ_PHASE_SETTLED);
        if (s.bankroll < s.base_bet)
            bj_bankroll_reset(&s);
        CHECK(bj_can_deal(&s));
        bj_deal(&s, &rng);
        CHECK(s.round.hands[0].n == 2);
    }

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
