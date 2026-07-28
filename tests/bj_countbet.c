/*
 * Hi-Lo true-count bet sizing (--count-bet) at the engine level.
 *
 * Two halves: the ramp itself, which is pure arithmetic over the true
 * count and the table and bankroll limits, and the sequencing the CLI
 * driver relies on - the count covering every exposed card of the round
 * before it, the shoe for the next round settled before the wager is
 * chosen, and a fresh shoe always betting one unit off a zero count.
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

/* Sum of the Hi-Lo tags over the first `upto` cards out of the shoe. */
static int tag_sum_dealt(const bj_session_t *s, int upto)
{
    int sum = 0;

    for (int i = 0; i < upto; i++)
        sum += bj_hilo(bj_dealt_card(s, i));
    return sum;
}

/* The hole card index worked out independently of bj_hole_index(): the
 * round's cards are the last ones dealt, and the hole is the fourth. */
static int hole_index(const bj_session_t *s)
{
    const bj_round_t *r = &s->round;
    int n = r->ndealer;

    if (n < 2)
        return -1;
    for (int i = 0; i < r->nhands; i++)
        n += r->hands[i].n;
    return bj_dealt_count(s) - n + 3;
}

/* ---- the ramp ---------------------------------------------------------- */

static void test_ramp_boundaries(void)
{
    /* the unrounded count picks the step, so +0.999 is still one unit */
    CHECK(bj_count_bet_units(-5.0)  == 1);
    CHECK(bj_count_bet_units(-0.001) == 1);
    CHECK(bj_count_bet_units(0.0)   == 1);
    CHECK(bj_count_bet_units(0.999) == 1);
    CHECK(bj_count_bet_units(1.0)   == 2);
    CHECK(bj_count_bet_units(1.999) == 2);
    CHECK(bj_count_bet_units(2.0)   == 4);
    CHECK(bj_count_bet_units(2.999) == 4);
    CHECK(bj_count_bet_units(3.0)   == 6);
    CHECK(bj_count_bet_units(3.999) == 6);
    CHECK(bj_count_bet_units(4.0)   == 8);
    CHECK(bj_count_bet_units(10.0)  == 8);
}

/* A table with a known true count: `running` over exactly `decks`. */
static void set_count(bj_session_t *s, bj_count_t *c, int running,
                      double decks)
{
    s->shoe.pos = s->shoe.count - (int)(decks * 52.0);
    bj_count_reset(c);
    c->running = running;
    c->counted = s->shoe.pos;
}

static void test_unit_sizes(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;
    bj_bet_plan_t p;

    rng_init(&rng, true, 1u);
    bj_session_start(&s, &rng);

    /* the default unit is the default wager, and a flat shoe bets one */
    set_count(&s, &c, 0, 6.0);
    bj_count_bet(&s, &c, BJ_BET_DEFAULT, &p);
    CHECK(p.units == 1);
    CHECK(p.wager == BJ_BET_DEFAULT);
    CHECK(p.true_count == 0.0);
    CHECK(!p.capped_table && !p.capped_bankroll);

    /* +6 over 3 decks is a true count of +2: four units */
    set_count(&s, &c, 6, 3.0);
    bj_count_bet(&s, &c, BJ_BET_DEFAULT, &p);
    CHECK(p.true_count == 2.0);
    CHECK(p.units == 4);
    CHECK(p.wager == 4 * BJ_BET_DEFAULT);       /* 100 credits */

    /* a user unit of 10 credits ramps 10/20/40/60/80 */
    static const long WANT[BJ_RAMP_STEPS] = { 10, 20, 40, 60, 80 };
    for (int step = 0; step < BJ_RAMP_STEPS; step++) {
        set_count(&s, &c, step * 2, 2.0);       /* true count 0, 1, 2, 3, 4 */
        bj_count_bet(&s, &c, 10 * BJ_HALF, &p);
        CHECK(p.step == step);
        CHECK(p.true_count == (double)step);
        CHECK(p.wager == WANT[step] * BJ_HALF);
        CHECK(!p.capped_table && !p.capped_bankroll);
    }

    /* a negative count never ramps up */
    set_count(&s, &c, -20, 2.0);
    bj_count_bet(&s, &c, 10 * BJ_HALF, &p);
    CHECK(p.true_count == -10.0);
    CHECK(p.units == 1);
    CHECK(p.wager == 10 * BJ_HALF);

    /* the unit itself is held inside the table limits */
    set_count(&s, &c, 0, 6.0);
    bj_count_bet(&s, &c, 1, &p);
    CHECK(p.wager == BJ_BET_MIN);
    bj_count_bet(&s, &c, 100 * BJ_BET_MAX, &p);
    CHECK(p.wager == BJ_BET_MAX);
}

static void test_table_cap(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;
    bj_bet_plan_t p;

    rng_init(&rng, true, 2u);
    bj_session_start(&s, &rng);
    s.bankroll = 100000 * BJ_HALF;      /* money is not the constraint */

    /* 100 x 8 = 800 credits, but the table stops at 500 */
    set_count(&s, &c, 8, 2.0);
    bj_count_bet(&s, &c, 100 * BJ_HALF, &p);
    CHECK(p.units == 8);
    CHECK(p.wager == BJ_BET_MAX);
    CHECK(p.capped_table);
    CHECK(!p.capped_bankroll);

    /* the same unit at a flat count is nowhere near the cap */
    set_count(&s, &c, 0, 6.0);
    bj_count_bet(&s, &c, 100 * BJ_HALF, &p);
    CHECK(p.wager == 100 * BJ_HALF);
    CHECK(!p.capped_table);
}

static void test_bankroll_cap(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;
    bj_bet_plan_t p;

    rng_init(&rng, true, 3u);
    bj_session_start(&s, &rng);

    /* wants 8 x 25 = 200, has 125: bets the 125 */
    set_count(&s, &c, 8, 1.0);
    s.bankroll = 125 * BJ_HALF;
    bj_count_bet(&s, &c, BJ_BET_DEFAULT, &p);
    CHECK(p.units == 8);
    CHECK(p.wager == 125 * BJ_HALF);
    CHECK(p.capped_bankroll);
    CHECK(!p.capped_table);
    CHECK(p.wager <= s.bankroll);

    /* half credits are not wagered: a 40.5 credit bankroll bets 40 */
    s.bankroll = 81;                    /* 40.5 credits */
    bj_count_bet(&s, &c, BJ_BET_DEFAULT, &p);
    CHECK(p.wager == 40 * BJ_HALF);
    CHECK(p.wager % BJ_HALF == 0);
    CHECK(p.capped_bankroll);

    /* enough for the whole spread: no cap at all */
    s.bankroll = BJ_BANKROLL_START;
    bj_count_bet(&s, &c, BJ_BET_DEFAULT, &p);
    CHECK(p.wager == 8 * BJ_BET_DEFAULT);
    CHECK(!p.capped_bankroll);

    /* below the table minimum nothing legal is left to bet: the driver
     * owes a re-buy before it gets here, and the table says so */
    s.bankroll = BJ_BET_MIN - BJ_HALF;
    bj_count_bet(&s, &c, BJ_BET_DEFAULT, &p);
    CHECK(p.wager == BJ_BET_MIN);
    bj_set_bet(&s, p.wager);
    CHECK(!bj_can_deal(&s));            /* ...and the deal is refused */
    bj_bankroll_reset(&s);
    CHECK(bj_can_deal(&s));
}

/* A re-buy is money only: the shoe and the count carry on. */
static void test_rebuy_keeps_shoe_and_count(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;
    bj_bet_plan_t p;

    rng_init(&rng, true, 17u);
    bj_session_start(&s, &rng);
    bj_count_reset(&c);

    bj_deal(&s, &rng);
    bj_count_update(&c, &s, bj_dealt_count(&s), bj_hole_index(&s),
                    !s.round.hole_hidden);
    while (s.round.phase == BJ_PHASE_PLAYER)
        bj_act(&s, BJ_STAND, &rng);
    bj_count_update(&c, &s, bj_dealt_count(&s), bj_hole_index(&s), true);
    s.round.phase = BJ_PHASE_BET;

    int pos = bj_dealt_count(&s);
    int running = c.running;

    s.bankroll = BJ_BET_MIN - BJ_HALF;
    bj_bankroll_reset(&s);
    CHECK(s.bankroll == BJ_BANKROLL_START);
    CHECK(bj_dealt_count(&s) == pos);       /* the shoe did not restart */
    CHECK(c.running == running);            /* nor did the count */
    CHECK(!bj_prepare_round(&s, &rng));     /* and no shuffle is due */

    bj_count_bet(&s, &c, BJ_BET_DEFAULT, &p);
    CHECK(p.true_count == bj_true_count(&c, &s));
}

/* ---- sequencing -------------------------------------------------------- */

/* Play out whatever is on the felt and leave the table between rounds. */
static void stand_out(bj_session_t *s, rng_t *rng)
{
    if (bj_insurance_pending(s))
        bj_insurance(s, false, rng);
    while (s->round.phase == BJ_PHASE_PLAYER)
        bj_act(s, BJ_STAND, rng);
    s->round.phase = BJ_PHASE_BET;
}

/* Preparing a round twice is one reshuffle, and bj_deal() still sees it. */
static void test_prepare_once(void)
{
    rng_t rng;
    bj_session_t s;

    rng_init(&rng, true, 23u);
    bj_session_start(&s, &rng);

    CHECK(!bj_prepare_round(&s, &rng));     /* a fresh shoe needs nothing */
    CHECK(!bj_prepare_round(&s, &rng));     /* twice is still nothing */
    bj_deal(&s, &rng);
    CHECK(!s.shuffled);
    stand_out(&s, &rng);

    s.shoe.pos = s.shoe.count - BJ_RESHUFFLE_AT;    /* at the cut card */
    CHECK(bj_prepare_round(&s, &rng));
    CHECK(bj_dealt_count(&s) == 0);
    CHECK(bj_remaining(&s) == BJ_SHOE_CARDS);
    CHECK(bj_prepare_round(&s, &rng));      /* idempotent, still shuffled */
    CHECK(bj_remaining(&s) == BJ_SHOE_CARDS);

    /* the flag survives into the round the preparation was for */
    bj_deal(&s, &rng);
    CHECK(s.shuffled);
    stand_out(&s, &rng);

    /* and the next round, off the same shoe, is not a shuffle */
    CHECK(!bj_prepare_round(&s, &rng));
    bj_deal(&s, &rng);
    CHECK(!s.shuffled);

    /* an unprepared table still deals correctly: bj_deal() prepares it */
    stand_out(&s, &rng);
    s.shoe.pos = s.shoe.count - BJ_RESHUFFLE_AT;
    bj_deal(&s, &rng);
    CHECK(s.shuffled);
    CHECK(bj_dealt_count(&s) >= 4);
}

typedef struct {
    long rounds;
    long shuffles;
    long fresh_shoe_units;      /* units bet on the first hand of a shoe */
    long ramp[BJ_RAMP_STEPS];
    long insurance_offers;
    long hidden_checks;         /* rounds where the hole card was checked */
} run_t;

/*
 * One round exactly the way the CLI driver plays it with --basic
 * --count-bet: prepare, count, bet, deal, play, count through settlement.
 */
static void count_bet_round(bj_session_t *s, bj_count_t *c, rng_t *rng,
                            long unit, run_t *r)
{
    bj_bet_plan_t p;
    bool fresh;

    if (s->bankroll < BJ_BET_MIN)
        bj_bankroll_reset(s);

    fresh = bj_prepare_round(s, rng);
    if (fresh) {
        bj_count_reset(c);
        r->shuffles++;
    }

    /* the wager comes from the count as it stands before the deal */
    bj_count_bet(s, c, unit, &p);
    CHECK(p.wager % BJ_HALF == 0);          /* whole credits only */
    CHECK(p.wager <= s->bankroll);
    CHECK(p.wager >= BJ_BET_MIN && p.wager <= BJ_BET_MAX);
    CHECK(p.true_count == bj_true_count(c, s));
    if (fresh) {
        /* a fresh shoe is a zero count, so it is always one unit */
        CHECK(c->running == 0);
        CHECK(p.true_count == 0.0);
        CHECK(p.units == 1);
        r->fresh_shoe_units += p.units;
    }
    r->ramp[p.step]++;

    bj_set_bet(s, p.wager);
    CHECK(s->base_bet == p.wager);

    int before = bj_dealt_count(s);
    bj_deal(s, rng);
    /* the shoe was settled before the bet, so the deal cannot have
     * changed it underneath the wager (a round that settles at the peek
     * draws the dealer's own cards straight away, hence >=) */
    CHECK(s->shuffled == fresh);
    CHECK(bj_dealt_count(s) >= before + 4);
    CHECK(s->round.hands[0].wager == p.wager);

    bj_count_update(c, s, bj_dealt_count(s), bj_hole_index(s),
                    !s->round.hole_hidden);

    /* the hole card stays out of the count while it is face down */
    if (s->round.hole_hidden) {
        int hole = bj_hole_index(s);
        CHECK(hole == hole_index(s));
        CHECK(c->running == tag_sum_dealt(s, bj_dealt_count(s)) -
                            bj_hilo(bj_dealt_card(s, hole)));
        r->hidden_checks++;
    }

    if (bj_insurance_pending(s)) {
        r->insurance_offers++;
        bj_insurance(s, false, rng);        /* declined, always */
        bj_count_update(c, s, bj_dealt_count(s), bj_hole_index(s),
                        !s->round.hole_hidden);
    }

    while (s->round.phase == BJ_PHASE_PLAYER) {
        bj_action_t a;

        CHECK(bj_strategy_action(bj_basic_strategy(s), &a));
        if (!bj_legal(s, a))
            break;
        bj_act(s, a, rng);
        bj_count_update(c, s, bj_dealt_count(s), bj_hole_index(s),
                        !s->round.hole_hidden);
    }
    bj_count_update(c, s, bj_dealt_count(s), bj_hole_index(s),
                    !s->round.hole_hidden);

    /* settled: every card of the round is face up and counted once, so
     * hits, doubles, split draws, the hole card and the dealer's own
     * draws have all landed exactly once */
    CHECK(s->round.phase == BJ_PHASE_SETTLED);
    CHECK(s->round.insurance == 0);
    CHECK(c->running == tag_sum_dealt(s, bj_dealt_count(s)));
    CHECK(c->counted == bj_dealt_count(s));

    s->round.phase = BJ_PHASE_BET;
    r->rounds++;
}

static void test_session(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;
    run_t r = { 0 };

    rng_init(&rng, true, 20260728u);
    bj_session_start(&s, &rng);
    bj_count_reset(&c);

    for (long i = 0; i < 8000; i++)
        count_bet_round(&s, &c, &rng, BJ_BET_DEFAULT, &r);

    CHECK(r.rounds == 8000);
    CHECK(r.shuffles > 0);                  /* shoes were replaced */
    CHECK(r.fresh_shoe_units == r.shuffles);/* every one of them bet one unit */
    CHECK(r.insurance_offers > 0);          /* and every offer was declined */
    CHECK(r.hidden_checks > 0);

    /* the ramp is exercised end to end, and the buckets are the rounds */
    long total = 0;
    for (int i = 0; i < BJ_RAMP_STEPS; i++) {
        CHECK(r.ramp[i] > 0);
        total += r.ramp[i];
    }
    CHECK(total == r.rounds);
    CHECK(r.ramp[0] > r.ramp[4]);           /* the shoe is flat far more often */
}

/* A bankroll that cannot cover the spread bets what it has, and the small
 * wager never stops the session. */
static void test_session_broke(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;
    run_t r = { 0 };
    long capped = 0;

    rng_init(&rng, true, 77u);
    bj_session_start(&s, &rng);
    bj_count_reset(&c);

    for (long i = 0; i < 2000; i++) {
        bj_bet_plan_t p;

        /* just over the table minimum: the ramp can never have its way */
        s.bankroll = BJ_BET_MIN + 1;        /* 5.5 credits */
        bj_count_bet(&s, &c, BJ_BET_MAX, &p);
        CHECK(p.wager == BJ_BET_MIN);       /* 5.5 credits bets 5 */
        CHECK(p.capped_bankroll);
        capped++;

        s.bankroll = BJ_BANKROLL_START;
        count_bet_round(&s, &c, &rng, BJ_BET_MAX, &r);
    }
    CHECK(capped == 2000);
    CHECK(r.rounds == 2000);
}

int main(void)
{
    test_ramp_boundaries();
    test_unit_sizes();
    test_table_cap();
    test_bankroll_cap();
    test_rebuy_keeps_shoe_and_count();
    test_prepare_once();
    test_session();
    test_session_broke();

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
