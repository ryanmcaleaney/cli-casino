/*
 * Hi-Lo counting regression tests.
 *
 * Covers the tag table, counting each dealt card exactly once, the hidden
 * hole card, and the count restarting when the shoe is reshuffled.  The
 * frontend's notion of "visible" is fed in the same way the GUI feeds it,
 * so the awkward cases (hole card, splits, dealer draws, naturals) are
 * exercised against real rounds instead of hand-made states.
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

static card_t mk(int rank, int suit)
{
    return (card_t){ (uint8_t)rank, (uint8_t)suit };
}

/* Sum of the tags over every card dealt out of the current shoe. */
static int tag_sum_dealt(const bj_session_t *s, int upto)
{
    int sum = 0;

    for (int i = 0; i < upto; i++)
        sum += bj_hilo(bj_dealt_card(s, i));
    return sum;
}

/* Where the GUI says the hole card is: the round's fourth dealt card. */
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

static void test_tags(void)
{
    /* 2-6 = +1, 7-9 = 0, 10-A = -1, whatever the suit */
    for (int suit = 0; suit < 4; suit++) {
        CHECK(bj_hilo(mk(1, suit)) == -1);          /* ace is high */
        for (int r = 2; r <= 6; r++)
            CHECK(bj_hilo(mk(r, suit)) == 1);
        for (int r = 7; r <= 9; r++)
            CHECK(bj_hilo(mk(r, suit)) == 0);
        for (int r = 10; r <= 13; r++)
            CHECK(bj_hilo(mk(r, suit)) == -1);      /* 10, J, Q, K */
    }
    /* a full deck is balanced: 20 low, 20 high, 12 neutral */
    int deck = 0;
    for (int r = 1; r <= 13; r++)
        for (int suit = 0; suit < 4; suit++)
            deck += bj_hilo(mk(r, suit));
    CHECK(deck == 0);
}

/* Play one round to settlement, feeding visibility the way the GUI does
 * once its deal animation has finished (everything dealt is face up,
 * except the hole card while the engine keeps it hidden). */
static void play_round(bj_session_t *s, bj_count_t *c, rng_t *rng)
{
    bj_deal(s, rng);
    bj_count_update(c, s, bj_dealt_count(s), hole_index(s),
                    !s->round.hole_hidden);
    if (bj_insurance_pending(s)) {
        bj_insurance(s, false, rng);
        bj_count_update(c, s, bj_dealt_count(s), hole_index(s),
                        !s->round.hole_hidden);
    }
    while (s->round.phase == BJ_PHASE_PLAYER) {
        const bj_hand_t *h = &s->round.hands[s->round.active];
        bj_action_t a;

        /* split whenever it is offered, otherwise play like the dealer,
         * so live hands, busts and dealer draws all occur */
        if (bj_legal(s, BJ_SPLIT))
            a = BJ_SPLIT;
        else if (bj_total(h->cards, h->n) < 17)
            a = BJ_HIT;
        else
            a = BJ_STAND;
        bj_act(s, a, rng);
        bj_count_update(c, s, bj_dealt_count(s), hole_index(s),
                        !s->round.hole_hidden);
    }
    bj_count_update(c, s, bj_dealt_count(s), hole_index(s),
                    !s->round.hole_hidden);
}

static void test_hole_card(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;

    rng_init(&rng, true, 5);
    bj_session_start(&s, &rng);
    bj_count_reset(&c);
    CHECK(c.running == 0 && c.counted == 0 && c.hole_done == -1);

    bj_deal(&s, &rng);
    int hole = hole_index(&s);
    CHECK(hole >= 0);

    /* while the hole card is face down it must stay out of the count */
    if (s.round.hole_hidden) {
        bj_count_update(&c, &s, bj_dealt_count(&s), hole, false);
        int without = tag_sum_dealt(&s, bj_dealt_count(&s)) -
                      bj_hilo(bj_dealt_card(&s, hole));
        CHECK(c.running == without);
        CHECK(c.hole_done == -1);
        /* repeating the update must not move the count */
        bj_count_update(&c, &s, bj_dealt_count(&s), hole, false);
        CHECK(c.running == without);

        /* and it lands exactly once when it turns over */
        bj_count_update(&c, &s, bj_dealt_count(&s), hole, true);
        CHECK(c.running == tag_sum_dealt(&s, bj_dealt_count(&s)));
        CHECK(c.hole_done == hole);
        for (int i = 0; i < 5; i++)
            bj_count_update(&c, &s, bj_dealt_count(&s), hole, true);
        CHECK(c.running == tag_sum_dealt(&s, bj_dealt_count(&s)));
    }

    /* a partly dealt table only counts what has been turned over */
    bj_count_t p;
    bj_count_reset(&p);
    bj_count_update(&p, &s, 2, hole, false);        /* two cards on felt */
    CHECK(p.running == bj_hilo(bj_dealt_card(&s, 0)) +
                       bj_hilo(bj_dealt_card(&s, 1)));
}

/* Every card the player has seen, once, across a whole shoe. */
static void test_session(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;
    bool saw_shuffle = false, saw_split = false, saw_many_dealer = false;

    rng_init(&rng, true, 11);
    bj_session_start(&s, &rng);
    bj_count_reset(&c);

    for (int round = 0; round < 400; round++) {
        if (s.bankroll < s.base_bet)
            bj_bankroll_reset(&s);

        int before = bj_dealt_count(&s);
        play_round(&s, &c, &rng);

        if (bj_dealt_count(&s) < before) {          /* the shoe restarted */
            saw_shuffle = true;
            CHECK(s.shuffled);
        }
        if (s.round.nhands > 1)
            saw_split = true;
        if (s.round.ndealer > 3)
            saw_many_dealer = true;

        /* once a round is settled the whole hand is face up, so the
         * count must equal the tags of every card out of this shoe */
        CHECK(s.round.phase == BJ_PHASE_SETTLED);
        CHECK(!s.round.hole_hidden);
        CHECK(c.running == tag_sum_dealt(&s, bj_dealt_count(&s)));
        CHECK(c.counted == bj_dealt_count(&s));

        /* true count follows from the decks still in the shoe */
        double decks = bj_remaining(&s) / 52.0;
        CHECK(bj_decks_left(&s) == decks);
        if (decks > 0.0) {
            double want = c.running / decks;
            double got = bj_true_count(&c, &s);
            CHECK(got - want < 1e-9 && want - got < 1e-9);
        }
    }
    CHECK(saw_shuffle);         /* the shoe was reshuffled at the cut card */
    CHECK(saw_split);           /* split hands were counted */
    CHECK(saw_many_dealer);     /* multi-card dealer draws were counted */
}

/* A reshuffle wipes the count even if the frontend forgets to reset. */
static void test_shuffle_reset(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;

    rng_init(&rng, true, 3);
    bj_session_start(&s, &rng);
    bj_count_reset(&c);

    while (!s.shuffled || bj_dealt_count(&s) == 0) {
        if (s.bankroll < s.base_bet)
            bj_bankroll_reset(&s);
        play_round(&s, &c, &rng);
        if (s.shuffled)
            break;
    }
    CHECK(s.shuffled);
    /* the count covers this shoe only, not the one before it */
    CHECK(c.running == tag_sum_dealt(&s, bj_dealt_count(&s)));
    CHECK(c.counted == bj_dealt_count(&s));

    /* an explicit reset is the same thing */
    bj_count_reset(&c);
    CHECK(c.running == 0 && c.counted == 0 && c.hole_done == -1);
}

static void test_true_count(void)
{
    rng_t rng;
    bj_session_t s;
    bj_count_t c;

    rng_init(&rng, true, 7);
    bj_session_start(&s, &rng);
    bj_count_reset(&c);

    /* a fresh 6-deck shoe */
    CHECK(bj_remaining(&s) == BJ_SHOE_CARDS);
    CHECK(bj_decks_left(&s) == 6.0);
    CHECK(bj_true_count(&c, &s) == 0.0);

    /* running +6 over 3 decks is a true count of +2 */
    bj_session_t t = s;
    t.shoe.pos = t.shoe.count - 156;
    c.running = 6;
    CHECK(bj_remaining(&t) == 156);
    CHECK(bj_decks_left(&t) == 3.0);
    CHECK(bj_true_count(&c, &t) == 2.0);

    /* fractional decks are kept, not rounded to whole decks: 6 / 2.5 */
    t.shoe.pos = t.shoe.count - 130;
    CHECK(bj_decks_left(&t) == 2.5);
    double tc = bj_true_count(&c, &t);
    CHECK(tc > 2.399 && tc < 2.401);

    /* negatives divide the same way: -3 over 1.5 decks */
    t.shoe.pos = t.shoe.count - 78;
    c.running = -3;
    CHECK(bj_true_count(&c, &t) == -2.0);

    /* no division by zero when the shoe is exhausted */
    t.shoe.pos = t.shoe.count;
    CHECK(bj_remaining(&t) == 0);
    CHECK(bj_decks_left(&t) == 0.0);
    c.running = -4;
    CHECK(bj_true_count(&c, &t) == -4.0);
}

int main(void)
{
    test_tags();
    test_hole_card();
    test_session();
    test_shuffle_reset();
    test_true_count();

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
