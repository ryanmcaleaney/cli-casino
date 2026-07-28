/*
 * Automatic basic-strategy play (--basic) at the engine level.
 *
 * The CLI driver asks the adviser for one action at a time and passes it
 * through bj_act(); this drives the same loop over many real rounds and
 * asserts the properties that make it safe to run unattended: every
 * recommendation is legal, every round reaches settlement, insurance is
 * always declined, and a bankroll too small for the chart's preferred
 * double or split falls back instead of stalling.
 */
#include <stdio.h>

#include "games/bj_strategy.h"
#include "games/blackjack.h"
#include "rng.h"

static int fails;

#define CHECK(c) do {                                                   \
        if (!(c)) {                                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c);                 \
            fails++;                                                    \
        }                                                               \
    } while (0)

/* What a --basic round did, so the sweep can assert the interesting
 * decisions are actually being reached. */
typedef struct {
    long rounds;
    long actions;
    long hits, stands, doubles, splits, surrenders;
    long insurance_offers;
    long multi_split_rounds;    /* rounds ending with three or more hands */
    long unsettled;
    long illegal;               /* adviser returned something bj_legal() rejects */
    long no_advice;             /* adviser had no opinion while a hand was live */
} sweep_t;

/* One round, exactly the way the CLI's basic mode plays it. */
static void basic_round(bj_session_t *s, rng_t *rng, sweep_t *sw)
{
    bj_deal(s, rng);
    sw->rounds++;

    if (bj_insurance_pending(s)) {
        sw->insurance_offers++;
        bj_insurance(s, false, rng);       /* basic strategy never insures */
    }

    while (s->round.phase == BJ_PHASE_PLAYER) {
        bj_strategy_action_t rec = bj_basic_strategy(s);
        bj_action_t a;

        if (!bj_strategy_action(rec, &a)) {
            sw->no_advice++;
            break;                          /* would stall the round */
        }
        if (!bj_legal(s, a)) {
            sw->illegal++;
            break;
        }
        sw->actions++;
        switch (a) {
        case BJ_HIT:       sw->hits++; break;
        case BJ_STAND:     sw->stands++; break;
        case BJ_DOUBLE:    sw->doubles++; break;
        case BJ_SPLIT:     sw->splits++; break;
        case BJ_SURRENDER: sw->surrenders++; break;
        }
        bj_act(s, a, rng);
    }

    if (s->round.phase != BJ_PHASE_SETTLED)
        sw->unsettled++;
    if (s->round.nhands >= 3)
        sw->multi_split_rounds++;
    CHECK(s->round.insurance == 0);
    s->round.phase = BJ_PHASE_BET;
}

/* A long unattended run: the properties the simulator relies on. */
static void test_sweep(void)
{
    rng_t rng;
    bj_session_t s;
    sweep_t sw = { 0 };

    rng_init(&rng, true, 20260728u);
    bj_session_start(&s, &rng);

    for (long i = 0; i < 20000; i++) {
        if (!bj_can_deal(&s))
            bj_bankroll_reset(&s);          /* re-buy, shoe untouched */
        basic_round(&s, &rng, &sw);
    }

    CHECK(sw.rounds == 20000);
    CHECK(sw.unsettled == 0);               /* automatic play always settles */
    CHECK(sw.illegal == 0);                 /* never rejected by bj_legal() */
    CHECK(sw.no_advice == 0);               /* an opinion on every live hand */

    /* the decisions the spec asks to see exercised */
    CHECK(sw.hits > 0);
    CHECK(sw.stands > 0);
    CHECK(sw.doubles > 0);
    CHECK(sw.splits > 0);
    CHECK(sw.surrenders > 0);
    CHECK(sw.multi_split_rounds > 0);       /* more than one split hand */
    CHECK(sw.insurance_offers > 0);         /* the offer came up and was declined */
}

/* A bankroll that cannot fund the extra wager must not stop the round:
 * the adviser owes a legal fallback. */
static void test_low_bankroll(void)
{
    rng_t rng;
    bj_session_t s;
    sweep_t sw = { 0 };
    long fallback_doubles = 0, fallback_splits = 0;

    rng_init(&rng, true, 99u);
    bj_session_start(&s, &rng);
    bj_set_bet(&s, BJ_BET_MAX);             /* 500 credits a hand */

    for (long i = 0; i < 4000; i++) {
        /* exactly the wager and not a half-credit more: every double and
         * every split the chart wants is unaffordable */
        s.bankroll = s.base_bet;
        bj_deal(&s, &rng);
        sw.rounds++;

        if (bj_insurance_pending(&s))
            bj_insurance(&s, false, &rng);

        while (s.round.phase == BJ_PHASE_PLAYER) {
            /* what the chart would ask for if the money were there */
            bj_session_t rich = s;
            bj_strategy_action_t want;
            bj_strategy_action_t rec = bj_basic_strategy(&s);
            bj_action_t a;

            rich.bankroll = BJ_BANKROLL_START;
            want = bj_basic_strategy(&rich);

            CHECK(bj_strategy_action(rec, &a));
            if (!bj_strategy_action(rec, &a))
                break;
            CHECK(bj_legal(&s, a));
            if (!bj_legal(&s, a))
                break;
            CHECK(a != BJ_DOUBLE);          /* nothing left to double with */
            CHECK(a != BJ_SPLIT);

            if (want == BJ_STRAT_DOUBLE) {  /* fell back to the chart's else */
                fallback_doubles++;
                CHECK(a == BJ_HIT || a == BJ_STAND);
            }
            if (want == BJ_STRAT_SPLIT) {
                fallback_splits++;
                CHECK(a == BJ_HIT || a == BJ_STAND || a == BJ_SURRENDER);
            }
            bj_act(&s, a, &rng);
        }
        CHECK(s.round.phase == BJ_PHASE_SETTLED);
        s.round.phase = BJ_PHASE_BET;
    }

    CHECK(sw.rounds == 4000);
    CHECK(fallback_doubles > 0);
    CHECK(fallback_splits > 0);             /* pairs came up and fell back */
}

/* The adviser is fed nothing but the session, so a shoe rich or poor in
 * high cards changes nothing: --basic cannot drift into count play. */
static void test_count_independent(void)
{
    rng_t rng;
    bj_session_t a, b;
    long same = 0;

    rng_init(&rng, true, 4242u);
    bj_session_start(&a, &rng);

    for (long i = 0; i < 500; i++) {
        bj_deal(&a, &rng);
        if (bj_insurance_pending(&a))
            bj_insurance(&a, false, &rng);

        while (a.round.phase == BJ_PHASE_PLAYER) {
            bj_action_t act;

            /* the identical table, three-quarters through the shoe */
            b = a;
            b.shoe.pos = b.shoe.count - 90;

            CHECK(bj_basic_strategy(&a) == bj_basic_strategy(&b));
            same++;

            CHECK(bj_strategy_action(bj_basic_strategy(&a), &act));
            bj_act(&a, act, &rng);
        }
        a.round.phase = BJ_PHASE_BET;
        if (!bj_can_deal(&a))
            bj_bankroll_reset(&a);
    }
    CHECK(same > 400);
}

int main(void)
{
    test_sweep();
    test_low_bankroll();
    test_count_independent();

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
