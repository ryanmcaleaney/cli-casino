/*
 * Caribbean Stud engine and session regression tests.
 *
 * These drive the cs_session_t API both frontends play through: wager
 * plumbing, the funding rule that keeps the 2x raise affordable, bankroll
 * accounting across a round, what a frontend is allowed to see before the
 * decision, and the settlement of every branch of the rules.  The rule
 * table itself is also checked from the CLI by `casino caribbeanstud
 * check`; the cases here are the ones a frontend depends on.
 */
#include <stdio.h>
#include <string.h>

#include "games/caribbeanstud.h"
#include "rng.h"

static int fails;

#define CHECK(c) do {                                                   \
        if (!(c)) {                                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c);                 \
            fails++;                                                    \
        }                                                               \
    } while (0)

static void start(cs_session_t *s, rng_t *rng, uint64_t seed)
{
    rng_init(rng, true, seed);
    cs_session_start(s);
}

/* "ah,kd,9c,5s,3d" -> five cards (the tests' own spelling, so the engine
 * is never asked to parse its way to an expectation). */
static void hand(const char *spec, card_t out[CS_CARDS])
{
    int n = 0;

    for (const char *p = spec; *p && n < CS_CARDS; ) {
        int rank = 0;

        if (p[0] == '1' && p[1] == '0') {
            rank = 10;
            p += 2;
        } else {
            switch (*p) {
            case 'a': rank = 1;  break;
            case 'j': rank = 11; break;
            case 'q': rank = 12; break;
            case 'k': rank = 13; break;
            default:  rank = *p - '0';
            }
            p++;
        }
        out[n].rank = (uint8_t)rank;
        switch (*p++) {
        case 'c': out[n].suit = 0; break;
        case 'd': out[n].suit = 1; break;
        case 'h': out[n].suit = 2; break;
        default:  out[n].suit = 3; break;
        }
        n++;
        if (*p == ',')
            p++;
    }
}

static bool same_hand(const card_t a[CS_CARDS], const card_t b[CS_CARDS])
{
    for (int i = 0; i < CS_CARDS; i++)
        if (a[i].rank != b[i].rank || a[i].suit != b[i].suit)
            return false;
    return true;
}

/* How many dealer cards a frontend may draw right now. */
static int visible_dealer_cards(const cs_round_t *r)
{
    int n = 0;

    for (int i = 0; i < CS_CARDS; i++)
        if (cs_dealer_visible(r, i))
            n++;
    return n;
}

/* Settle one fixed round outside any session. */
static void settle(cs_round_t *r, const char *p, const char *d,
                   cs_action_t act, long ante)
{
    card_t ph[CS_CARDS], dh[CS_CARDS];

    hand(p, ph);
    hand(d, dh);
    cs_round_deal_fixed(r, ph, dh, ante);
    cs_round_settle(r, act);
}

static bool qualifies(const char *spec)
{
    card_t h[CS_CARDS];

    hand(spec, h);
    return cs_dealer_qualifies(h);
}

static int compare(const char *a, const char *b)
{
    card_t ha[CS_CARDS], hb[CS_CARDS];
    cs_eval_t ea, eb;

    hand(a, ha);
    hand(b, hb);
    ea = cs_eval(ha);
    eb = cs_eval(hb);
    return cs_compare(&ea, &eb);
}

/* ---- opening state and wagers ------------------------------------------ */

static void test_opening(void)
{
    cs_session_t s;
    rng_t rng;

    start(&s, &rng, 1);
    CHECK(s.bankroll == CS_BANKROLL_START);
    CHECK(s.ante == CS_ANTE_DEFAULT);
    CHECK(s.phase == CS_PHASE_BET);
    CHECK(cs_can_deal(&s));
    CHECK(!s.round.settled);
    /* no round yet: nothing to raise or fold */
    CHECK(!cs_can_raise(&s));
    CHECK(!cs_can_fold(&s));

    /* the raise is always exactly twice the ante, and the frontends ask
     * the engine for it rather than doubling anything themselves */
    CHECK(cs_raise_amount(25) == 50);
    CHECK(cs_raise_amount(1) == 2);
    CHECK(cs_raise_amount(500) == 1000);
    CHECK(cs_max_exposure(&s) == s.ante + cs_raise_amount(s.ante));
    CHECK(cs_max_exposure(&s) == 3 * s.ante);

    /* the ante clamps to the table limits ... */
    cs_set_ante(&s, 0);
    CHECK(s.ante == CS_ANTE_MIN);
    /* ... and to what the bankroll can cover for a whole round, not just
     * the ante: the raise behind it has to be affordable too, so the
     * standard stake tops out well below the table maximum */
    cs_set_ante(&s, CS_ANTE_MAX + 1000);
    CHECK(s.ante == CS_BANKROLL_START / 3);
    CHECK(cs_max_exposure(&s) <= s.bankroll);
    cs_buy_in(&s, 100000);
    cs_set_ante(&s, CS_ANTE_MAX + 1000);
    CHECK(s.ante == CS_ANTE_MAX);
    cs_buy_in(&s, CS_BANKROLL_START);

    cs_buy_in(&s, 90);
    cs_set_ante(&s, 50);
    CHECK(s.ante == 30);            /* 90 / 3, not 50 */
    CHECK(cs_can_deal(&s));
    cs_buy_in(&s, 2);
    CHECK(s.ante == CS_ANTE_MIN);
    CHECK(!cs_can_deal(&s));        /* 2 credits cannot fund 1 + 2 */
    cs_buy_in(&s, 3);
    CHECK(cs_can_deal(&s));
}

/* ---- the deal ----------------------------------------------------------- */

static void test_deal(void)
{
    cs_session_t s;
    rng_t rng;

    start(&s, &rng, 7);
    cs_set_ante(&s, 25);
    cs_deal(&s, &rng);

    CHECK(s.phase == CS_PHASE_DECISION);
    CHECK(s.round.ante == 25);
    CHECK(s.round.raise == 0);      /* not staked until the raise is made */
    CHECK(!s.round.settled);
    /* only the ante leaves the bankroll at the deal */
    CHECK(s.bankroll == CS_BANKROLL_START - 25);
    /* a legal deal always leaves the raise affordable */
    CHECK(cs_can_raise(&s));
    CHECK(cs_can_fold(&s));

    /* five cards each, all ten distinct */
    {
        card_t all[2 * CS_CARDS];

        for (int i = 0; i < CS_CARDS; i++) {
            all[i] = s.round.player[i];
            all[CS_CARDS + i] = s.round.dealer[i];
            CHECK(s.round.player[i].rank >= 1 && s.round.player[i].rank <= 13);
            CHECK(s.round.dealer[i].suit < 4);
        }
        for (int i = 0; i < 2 * CS_CARDS; i++)
            for (int j = i + 1; j < 2 * CS_CARDS; j++)
                CHECK(all[i].rank != all[j].rank || all[i].suit != all[j].suit);
    }

    /* exactly one dealer card is exposed, and it is the up-card */
    CHECK(visible_dealer_cards(&s.round) == 1);
    CHECK(cs_dealer_visible(&s.round, 0) == &s.round.dealer[0]);
    CHECK(cs_dealer_visible(&s.round, 1) == NULL);
    CHECK(cs_dealer_visible(&s.round, CS_CARDS) == NULL);
    CHECK(!s.round.dealer_revealed);

    /* the ante is locked and no second deal is possible mid-hand */
    cs_set_ante(&s, 100);
    CHECK(s.ante == 25);
    CHECK(!cs_can_deal(&s));
    cs_deal(&s, &rng);
    CHECK(s.round.ante == 25);
    CHECK(!s.round.settled);

    /* the whole hand turns over once it is settled */
    cs_decide(&s, CS_ACT_RAISE);
    CHECK(s.round.dealer_revealed);
    CHECK(visible_dealer_cards(&s.round) == CS_CARDS);
    for (int i = 0; i < CS_CARDS; i++)
        CHECK(cs_dealer_visible(&s.round, i) == &s.round.dealer[i]);
}

/* ---- money -------------------------------------------------------------- */

static void test_money(void)
{
    cs_session_t s;
    rng_t rng;

    /* a raised round stakes ante + 2x ante and pays what it settled */
    start(&s, &rng, 11);
    cs_set_ante(&s, 25);
    cs_deal(&s, &rng);
    cs_decide(&s, CS_ACT_RAISE);
    CHECK(s.phase == CS_PHASE_SETTLED);
    CHECK(s.round.settled);
    CHECK(s.round.raise == 50);
    CHECK(s.round.wagered == 75);
    CHECK(s.round.returned == s.round.wagered + s.round.ante_net +
                              s.round.raise_net);
    CHECK(s.bankroll == CS_BANKROLL_START - s.round.wagered +
                        s.round.returned);

    /* folding stakes the ante and nothing else */
    start(&s, &rng, 11);
    cs_set_ante(&s, 25);
    cs_deal(&s, &rng);
    cs_decide(&s, CS_ACT_FOLD);
    CHECK(s.round.raise == 0);
    CHECK(s.round.wagered == 25);
    CHECK(s.round.returned == 0);
    CHECK(s.round.ante_net == -25);
    CHECK(s.round.raise_net == 0);
    CHECK(s.round.outcome == CS_OUT_FOLD);
    CHECK(s.bankroll == CS_BANKROLL_START - 25);

    /* the same deal both ways: folding never changes the cards */
    {
        cs_session_t a, b;
        rng_t ra, rb;

        start(&a, &ra, 19);
        cs_deal(&a, &ra);
        cs_decide(&a, CS_ACT_RAISE);
        start(&b, &rb, 19);
        cs_deal(&b, &rb);
        cs_decide(&b, CS_ACT_FOLD);
        CHECK(same_hand(a.round.player, b.round.player));
        CHECK(same_hand(a.round.dealer, b.round.dealer));
        CHECK(a.round.dealer_qualifies == b.round.dealer_qualifies);
    }
}

/* ---- illegal actions ---------------------------------------------------- */

static void test_illegal(void)
{
    cs_session_t s;
    rng_t rng;

    /* nothing settles before a deal */
    start(&s, &rng, 5);
    cs_decide(&s, CS_ACT_RAISE);
    CHECK(s.phase == CS_PHASE_BET);
    CHECK(!s.round.settled);
    cs_decide(&s, CS_ACT_FOLD);
    CHECK(s.phase == CS_PHASE_BET);

    /* and nothing settles twice: a second decision is ignored, money and
     * outcome stay exactly as the first one left them */
    cs_deal(&s, &rng);
    cs_decide(&s, CS_ACT_FOLD);
    {
        long bank = s.bankroll;
        cs_outcome_t out = s.round.outcome;

        cs_decide(&s, CS_ACT_RAISE);
        cs_decide(&s, CS_ACT_FOLD);
        CHECK(s.bankroll == bank);
        CHECK(s.round.outcome == out);
        CHECK(s.round.raise == 0);
        CHECK(s.phase == CS_PHASE_SETTLED);
    }
    CHECK(!cs_can_raise(&s));
    CHECK(!cs_can_fold(&s));

    /* a raise that cannot be funded is refused, and folding still works.
     * The funding rule means this only happens if the bankroll is cut
     * from under a live round, never through the normal deal path. */
    start(&s, &rng, 5);
    cs_set_ante(&s, 25);
    cs_deal(&s, &rng);
    s.bankroll = 10;                /* not enough for the 50 raise */
    CHECK(!cs_can_raise(&s));
    cs_decide(&s, CS_ACT_RAISE);
    CHECK(s.phase == CS_PHASE_DECISION);
    CHECK(!s.round.settled);
    CHECK(cs_can_fold(&s));
    cs_decide(&s, CS_ACT_FOLD);
    CHECK(s.phase == CS_PHASE_SETTLED);
    CHECK(s.round.outcome == CS_OUT_FOLD);
}

/* ---- a clean round after a reset ---------------------------------------- */

static void test_reset(void)
{
    cs_session_t s;
    rng_t rng;

    start(&s, &rng, 13);
    cs_set_ante(&s, 25);
    cs_deal(&s, &rng);
    cs_decide(&s, CS_ACT_RAISE);
    CHECK(s.round.settled);

    /* the next deal starts a fresh round: no stale settlement, no stale
     * reveal, no stale raise */
    cs_deal(&s, &rng);
    CHECK(s.phase == CS_PHASE_DECISION);
    CHECK(!s.round.settled);
    CHECK(!s.round.dealer_revealed);
    CHECK(s.round.raise == 0);
    CHECK(s.round.ante_net == 0 && s.round.raise_net == 0);
    CHECK(s.round.returned == 0);
    CHECK(visible_dealer_cards(&s.round) == 1);

    /* a broken bankroll stops the deal; the rebuy restores it */
    cs_decide(&s, CS_ACT_FOLD);
    cs_buy_in(&s, 0);
    CHECK(!cs_can_deal(&s));
    cs_bankroll_reset(&s);
    CHECK(s.bankroll == CS_BANKROLL_START);
    CHECK(s.phase == CS_PHASE_BET);
    CHECK(cs_can_deal(&s));
    /* ... and neither may happen while a decision is pending */
    cs_deal(&s, &rng);
    cs_buy_in(&s, 5000);
    CHECK(s.bankroll == CS_BANKROLL_START - s.ante);
}

/* ---- determinism -------------------------------------------------------- */

static void test_determinism(void)
{
    cs_session_t a, b;
    cs_round_t   r;
    rng_t ra, rb;

    start(&a, &ra, 21);
    cs_set_ante(&a, 25);
    cs_deal(&a, &ra);
    start(&b, &rb, 21);
    cs_set_ante(&b, 25);
    cs_deal(&b, &rb);
    CHECK(same_hand(a.round.player, b.round.player));
    CHECK(same_hand(a.round.dealer, b.round.dealer));

    /* the same seed deals the same cards whatever the wager is, so the
     * CLI and the GUI see one hand for one seed */
    start(&b, &rb, 21);
    cs_set_ante(&b, 100);
    cs_deal(&b, &rb);
    CHECK(same_hand(a.round.player, b.round.player));
    CHECK(same_hand(a.round.dealer, b.round.dealer));

    /* ... and the session deals exactly what the bare round deals */
    rng_init(&rb, true, 21);
    cs_round_deal(&r, &rb, 25);
    CHECK(same_hand(a.round.player, r.player));
    CHECK(same_hand(a.round.dealer, r.dealer));

    start(&b, &rb, 22);
    cs_deal(&b, &rb);
    CHECK(!same_hand(a.round.player, b.round.player));
}

/* ---- dealer qualification ----------------------------------------------- */

static void test_qualification(void)
{
    /* ace-king high is the floor */
    CHECK(qualifies("ah,kd,9c,5s,3d"));
    CHECK(qualifies("ah,kd,4c,3s,2d"));    /* the lowest qualifying hand */
    CHECK(!qualifies("ah,qd,jc,9s,4d"));
    CHECK(!qualifies("ah,qd,jc,10s,4d"));  /* ace-queen, however pretty */
    CHECK(!qualifies("kh,qd,jc,9s,4d"));
    CHECK(!qualifies("qh,jd,9c,5s,3d"));

    /* every pair qualifies, right down to deuces */
    CHECK(qualifies("2h,2d,7c,5s,3d"));
    CHECK(qualifies("3h,3d,7c,5s,2d"));
    CHECK(qualifies("kh,kd,8c,5s,2d"));

    /* and so does everything above a pair */
    CHECK(qualifies("9h,9d,3c,3s,kd"));         /* two pair */
    CHECK(qualifies("9h,9d,9c,3s,kd"));         /* trips */
    CHECK(qualifies("5h,6d,7c,8s,9d"));         /* straight */
    CHECK(qualifies("ah,2d,3c,4s,5d"));         /* the wheel */
    CHECK(qualifies("2h,7h,9h,jh,4h"));         /* flush */
    CHECK(qualifies("9h,9d,9c,3s,3d"));         /* full house */
    CHECK(qualifies("9h,9d,9c,9s,3d"));         /* quads */
    CHECK(qualifies("5h,6h,7h,8h,9h"));         /* straight flush */
    CHECK(qualifies("10h,jh,qh,kh,ah"));        /* royal flush */
}

/* ---- hand comparison ---------------------------------------------------- */

static void test_comparison(void)
{
    /* a pair beats ace-king high in both directions */
    CHECK(compare("2h,2d,7c,5s,3d", "ah,kd,9c,5s,3c") > 0);
    CHECK(compare("ah,kd,9c,5s,3c", "2h,2d,7c,5s,3d") < 0);
    /* equal categories fall through to the kickers, in order */
    CHECK(compare("kh,kd,7c,5s,3d", "qh,qd,jc,9s,8d") > 0);
    CHECK(compare("9h,9d,kc,5s,3d", "9c,9s,qd,5h,3c") > 0);
    CHECK(compare("9h,9d,kc,5s,3d", "9c,9s,kd,5h,4c") < 0);
    CHECK(compare("ah,kd,9c,5s,4d", "as,kc,9d,5h,3c") > 0);
    /* the same five ranks in different suits is a push */
    CHECK(compare("ah,kd,9c,5s,3d", "as,kc,9d,5h,3c") == 0);
    CHECK(compare("2h,2d,7c,5s,3d", "2s,2c,7d,5h,3h") == 0);
    /* group order: quads over full house over flush over straight */
    CHECK(compare("9h,9d,9c,9s,3d", "kh,kd,kc,ks,2d") < 0);
    CHECK(compare("9h,9d,9c,3s,3d", "2h,7h,9h,jh,4h") > 0);
    CHECK(compare("2h,7h,9h,jh,4h", "5h,6d,7c,8s,9d") > 0);
    /* the wheel is the lowest straight, broadway the highest */
    CHECK(compare("ah,2d,3c,4s,5d", "2h,3d,4c,5s,6d") < 0);
    CHECK(compare("10h,jd,qc,ks,ad", "9h,10d,jc,qs,kd") > 0);
    CHECK(compare("ah,2h,3h,4h,5h", "6d,7d,8d,9d,10d") < 0);
}

/* ---- settlement, branch by branch --------------------------------------- */

static void test_settlement(void)
{
    cs_round_t r;

    /* the dealer failing to qualify pays the ante and pushes the raise */
    settle(&r, "ah,kd,9c,5s,3d", "2h,7d,9s,5c,3h", CS_ACT_RAISE, 25);
    CHECK(r.outcome == CS_OUT_NO_QUALIFY);
    CHECK(!r.dealer_qualifies);
    CHECK(r.ante_net == 25);
    CHECK(r.raise_net == 0);            /* pushed, stake comes back */
    CHECK(r.wagered == 75 && r.returned == 100);

    /* ... even when the dealer's hand is the better one */
    settle(&r, "2h,7d,9c,5s,3d", "ah,qd,jc,9s,4h", CS_ACT_RAISE, 25);
    CHECK(r.outcome == CS_OUT_NO_QUALIFY);
    CHECK(r.returned - r.wagered == 25);

    /* a qualifying dealer with the better hand takes both wagers */
    settle(&r, "2h,7d,9c,5s,3d", "ah,ad,kc,qs,jh", CS_ACT_RAISE, 25);
    CHECK(r.outcome == CS_OUT_DEALER);
    CHECK(r.ante_net == -25 && r.raise_net == -50);
    CHECK(r.returned == 0);

    /* identical hands push both wagers, and pay no profit */
    settle(&r, "ah,kd,9c,5s,3d", "as,kc,9d,5h,3c", CS_ACT_RAISE, 25);
    CHECK(r.outcome == CS_OUT_PUSH);
    CHECK(r.ante_net == 0 && r.raise_net == 0);
    CHECK(r.returned == r.wagered);

    /* a fold loses the ante whatever the dealer had */
    settle(&r, "ah,ad,ac,as,kd", "2h,7d,9c,5s,3c", CS_ACT_FOLD, 25);
    CHECK(r.outcome == CS_OUT_FOLD);
    CHECK(r.raise == 0 && r.wagered == 25 && r.returned == 0);

    /* the ante pays 1:1 and the raise pays the table, on the player's own
     * hand: every category, checked against cs_raise_multiplier() */
    {
        static const struct { const char *p, *d; poker_cat_t cat; } W[] = {
            { "ah,kd,9c,5s,4d", "as,kc,9d,5h,3c", POKER_HIGH_CARD },
            { "2h,2d,7c,5s,3d", "ah,kd,9c,5c,3h", POKER_PAIR },
            { "9h,9d,3c,3s,kd", "ah,ks,9c,5h,3h", POKER_TWO_PAIR },
            { "9h,9d,9c,3s,kd", "ah,ks,8c,5h,3h", POKER_THREE_OF_A_KIND },
            { "5h,6d,7c,8s,9d", "ah,ks,8c,5c,3h", POKER_STRAIGHT },
            { "2h,7h,9h,jh,4h", "ah,ks,8c,5c,3d", POKER_FLUSH },
            { "9h,9d,9c,3s,3d", "ah,ks,8c,5c,3h", POKER_FULL_HOUSE },
            { "9h,9d,9c,9s,3d", "ah,ks,8c,5c,3h", POKER_FOUR_OF_A_KIND },
            { "5h,6h,7h,8h,9h", "ah,ks,8c,5c,3d", POKER_STRAIGHT_FLUSH },
            { "10h,jh,qh,kh,ah", "as,ks,8c,5c,3d", POKER_ROYAL_FLUSH },
        };
        static const int WANT[] = { 1, 1, 2, 3, 4, 5, 7, 20, 50, 100 };

        for (int i = 0; i < (int)(sizeof W / sizeof W[0]); i++) {
            settle(&r, W[i].p, W[i].d, CS_ACT_RAISE, 25);
            CHECK(r.pev.cat == W[i].cat);
            CHECK(r.dealer_qualifies);
            CHECK(r.outcome == CS_OUT_PLAYER);
            CHECK(cs_raise_multiplier(W[i].cat) == WANT[i]);
            CHECK(r.ante_net == 25);
            CHECK(r.raise_net == (long)WANT[i] * r.raise);
            CHECK(r.returned == r.wagered + r.ante_net + r.raise_net);
        }
    }

    /* the pay table scales with the wager, not with anything else */
    settle(&r, "9h,9d,9c,9s,3d", "ah,ks,8c,5c,3h", CS_ACT_RAISE, 10);
    CHECK(r.raise == 20 && r.raise_net == 400);
    CHECK(r.returned == 30 + 10 + 400);
}

/* ---- what the GUI shows on the felt ------------------------------------- */

static void test_gui_logic(void)
{
    cs_session_t s;
    rng_t rng;

    /* the buttons follow the phase, and nothing else */
    start(&s, &rng, 31);
    cs_set_ante(&s, 25);
    CHECK(cs_can_deal(&s) && !cs_can_raise(&s) && !cs_can_fold(&s));
    cs_deal(&s, &rng);
    CHECK(!cs_can_deal(&s) && cs_can_raise(&s) && cs_can_fold(&s));
    cs_decide(&s, CS_ACT_FOLD);
    CHECK(cs_can_deal(&s) && !cs_can_raise(&s) && !cs_can_fold(&s));

    /* the raise the felt offers is the wager the engine will take */
    CHECK(cs_raise_amount(s.ante) == 2 * s.ante);
    cs_set_ante(&s, 40);
    CHECK(cs_raise_amount(s.ante) == 80);
    CHECK(cs_max_exposure(&s) == 120);

    /* an ante the engine would refuse can never be displayed: the GUI's
     * "+" step is exactly the funding rule */
    cs_buy_in(&s, 100);
    cs_set_ante(&s, CS_ANTE_MAX);
    CHECK(s.ante == 33);
    CHECK(cs_max_exposure(&s) <= s.bankroll);
    CHECK(cs_can_deal(&s));
    cs_set_ante(&s, 34);
    CHECK(s.ante == 33);

    /* DEAL is dead once the bankroll cannot fund a whole round */
    cs_buy_in(&s, 2);
    CHECK(!cs_can_deal(&s));
    {
        long bank = s.bankroll;

        cs_deal(&s, &rng);              /* the click does nothing at all */
        CHECK(s.phase == CS_PHASE_BET);
        CHECK(s.bankroll == bank);
    }

    /* the hole cards stay down for the whole decision, however many
     * times the felt asks */
    cs_bankroll_reset(&s);
    cs_deal(&s, &rng);
    for (int i = 0; i < 3; i++)
        CHECK(visible_dealer_cards(&s.round) == 1);
    cs_decide(&s, CS_ACT_RAISE);
    CHECK(visible_dealer_cards(&s.round) == CS_CARDS);
    /* a fold reveals the hand too, so the felt can show what was folded
     * into - the outcome is still a fold */
    cs_deal(&s, &rng);
    CHECK(visible_dealer_cards(&s.round) == 1);
    cs_decide(&s, CS_ACT_FOLD);
    CHECK(s.round.dealer_revealed);
    CHECK(visible_dealer_cards(&s.round) == CS_CARDS);
    CHECK(s.round.outcome == CS_OUT_FOLD);
}

int main(void)
{
    test_opening();
    test_deal();
    test_money();
    test_illegal();
    test_reset();
    test_determinism();
    test_qualification();
    test_comparison();
    test_settlement();
    test_gui_logic();

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
