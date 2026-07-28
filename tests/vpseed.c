/*
 * Video poker seed search regression tests.
 *
 * Covers the category parser, the ascending first-match contract in both
 * search modes, the inclusive range, and - most importantly - that a
 * reported seed reproduces through the ordinary deal/solve/draw path: the
 * search must not have its own shuffle, its own hold or its own
 * evaluator.  Every expectation is derived here from the game's public
 * API rather than from a table of remembered seeds.
 */
#include <stdio.h>
#include <string.h>

#include "cards.h"
#include "games/videopoker.h"
#include "games/vpseed.h"
#include "rng.h"

static int fails;

#define CHECK(c) do {                                                   \
        if (!(c)) {                                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c);                 \
            fails++;                                                    \
        }                                                               \
    } while (0)

#define HOLD_ALL 0x1fu

static bool same_hand(const card_t a[5], const card_t b[5])
{
    for (int i = 0; i < 5; i++)
        if (a[i].rank != b[i].rank || a[i].suit != b[i].suit)
            return false;
    return true;
}

/*
 * Play one seed exactly as the game would, without the search: a fresh
 * RNG, the game's deal, and (for after_draw) the game's own solver and
 * draw.  This is the independent expectation the search is judged by.
 */
static int play_seed(uint64_t seed, bool after_draw, card_t initial[5],
                     card_t final5[5], uint32_t *hold, shoe_t *shoe)
{
    rng_t  rng;
    shoe_t local;

    if (!shoe)
        shoe = &local;
    rng_init(&rng, true, seed);
    vp_front_deal(&rng, shoe, initial);
    memcpy(final5, initial, sizeof(card_t) * 5);
    *hold = HOLD_ALL;

    if (after_draw) {
        vp_strategy_t st;
        vp_front_solve(initial, &st);
        *hold = vp_front_best_mask(&st);
        vp_front_draw(shoe, *hold, final5);
    }
    return vp_front_category(final5);
}

/* --- the category parser ------------------------------------------------ */

static void test_parse(void)
{
    static const char *const names[VP_FRONT_NCATS] = {
        "high_card", "low_pair", "jacks_or_better", "two_pair",
        "three_of_a_kind", "straight", "flush", "full_house",
        "four_of_a_kind", "straight_flush", "royal_flush"
    };

    for (int i = 0; i < VP_FRONT_NCATS; i++) {
        /* every accepted name maps to its own category ... */
        CHECK(vp_front_parse_category(names[i]) == i);
        /* ... and is the name the output prints back */
        CHECK(strcmp(vp_front_name(i), names[i]) == 0);
        /* the shouted spelling used in JSON and quiet output parses too */
        CHECK(vp_front_parse_category(vp_front_token(i)) == i);
    }

    /* aliases, each naming exactly one category */
    CHECK(vp_front_parse_category("royal") ==
          vp_front_parse_category("royal_flush"));
    CHECK(vp_front_parse_category("quads") ==
          vp_front_parse_category("four_of_a_kind"));
    CHECK(vp_front_parse_category("trips") ==
          vp_front_parse_category("three_of_a_kind"));
    CHECK(vp_front_parse_category("Royal") ==
          vp_front_parse_category("royal_flush"));

    /* rejected: ambiguous, misspelled, spaced, empty */
    CHECK(vp_front_parse_category("pair") < 0);
    CHECK(vp_front_parse_category("royalflush") < 0);
    CHECK(vp_front_parse_category("full house") < 0);
    CHECK(vp_front_parse_category("royal_flush ") < 0);
    CHECK(vp_front_parse_category("") < 0);
    CHECK(vp_front_parse_category("42") < 0);
}

/* --- initial-deal search ------------------------------------------------ */

static void test_initial_first_match(void)
{
    enum { N = 400 };                   /* seeds 0 .. N-1 */
    int cat[N];
    card_t initial[5], final5[5];
    uint32_t hold;

    for (int s = 0; s < N; s++)
        cat[s] = play_seed((uint64_t)s, false, initial, final5, &hold, NULL);

    for (int target = 0; target < VP_FRONT_NCATS; target++) {
        vp_seed_hit_t hit;
        bool found = vp_seed_find(target, false, 0, N - 1, NULL, &hit);
        int want = -1;

        for (int s = 0; s < N && want < 0; s++)
            if (cat[s] == target)
                want = s;

        /* found exactly when the range holds one, and it is the lowest */
        CHECK(found == (want >= 0));
        if (!found || want < 0)
            continue;
        CHECK(hit.seed == (uint64_t)want);
        CHECK(hit.cat == target);

        /* the reported hand is the hand that seed really deals */
        play_seed(hit.seed, false, initial, final5, &hold, NULL);
        CHECK(same_hand(hit.initial, initial));
        /* nothing is drawn in this mode */
        CHECK(hit.hold == HOLD_ALL);
        CHECK(same_hand(hit.final5, hit.initial));
        CHECK(vp_front_category(hit.initial) == target);
    }
}

static void test_range(void)
{
    vp_seed_hit_t a, b;
    int two_pair = vp_front_parse_category("two_pair");

    /* the range includes both ends: a search of exactly the hit seed
     * finds it, one seed short of it does not */
    CHECK(vp_seed_find(two_pair, false, 0, 400, NULL, &a));
    CHECK(vp_seed_find(two_pair, false, a.seed, a.seed, NULL, &b));
    CHECK(b.seed == a.seed);
    if (a.seed > 0)
        CHECK(!vp_seed_find(two_pair, false, 0, a.seed - 1, NULL, &b));

    /* starting above the first hit reports the next one, not the first */
    CHECK(vp_seed_find(two_pair, false, a.seed + 1, a.seed + 400, NULL, &b));
    CHECK(b.seed > a.seed);

    /* an inverted range is empty, not a wraparound search */
    CHECK(!vp_seed_find(two_pair, false, 500, 499, NULL, &b));
    CHECK(!vp_seed_find(two_pair, false, 1000, 100, NULL, &b));

    /* a range with no match at all */
    CHECK(!vp_seed_find(vp_front_parse_category("royal_flush"),
                        false, 0, 200, NULL, &b));
}

static void test_determinism(void)
{
    vp_seed_hit_t a, b;
    int t = vp_front_parse_category("three_of_a_kind");

    CHECK(vp_seed_find(t, false, 0, 5000, NULL, &a));
    CHECK(vp_seed_find(t, false, 0, 5000, NULL, &b));
    CHECK(a.seed == b.seed);
    CHECK(same_hand(a.initial, b.initial));
    CHECK(a.cat == b.cat && a.hold == b.hold);
}

/* --- progress reporting ------------------------------------------------- */

static long ticks;
static uint64_t last_tick;

static void on_tick(uint64_t checked, void *ctx)
{
    ticks++;
    last_tick = checked;
    (*(long *)ctx)++;
}

static void test_progress(void)
{
    vp_seed_report_t rep;
    vp_seed_hit_t hit;
    long ctx = 0;

    ticks = 0;
    last_tick = 0;
    rep.every = 100;
    rep.fn = on_tick;
    rep.ctx = &ctx;

    /* 200 seeds with no royal flush in them: one tick per 100 tried */
    CHECK(!vp_seed_find(vp_front_parse_category("royal_flush"), false,
                        0, 199, &rep, &hit));
    CHECK(ticks == 2);
    CHECK(ctx == 2);
    CHECK(last_tick == 200);
}

/* --- optimal-draw search ------------------------------------------------ */

static void test_after_draw(void)
{
    enum { N = 16 };                    /* a full 32-mask solve per seed:
                                           keep it small, this is the slow
                                           mode */
    int  dealt[N], drawn_cat[N];
    uint32_t masks[N];
    card_t initial[5], final5[5];
    uint32_t hold;
    bool any_changed = false, any_partial = false;

    for (int s = 0; s < N; s++) {
        drawn_cat[s] = play_seed((uint64_t)s, true, initial, final5,
                                 &hold, NULL);
        dealt[s] = vp_front_category(initial);
        masks[s] = hold;
        if (drawn_cat[s] != dealt[s])
            any_changed = true;
        if (hold != HOLD_ALL)
            any_partial = true;
    }
    /* the two modes really do judge different hands */
    CHECK(any_changed);
    CHECK(any_partial);

    for (int target = 0; target < VP_FRONT_NCATS; target++) {
        vp_seed_hit_t hit;
        int want = -1;

        for (int s = 0; s < N && want < 0; s++)
            if (drawn_cat[s] == target)
                want = s;
        if (want < 0)
            continue;                   /* absent targets: see below */

        CHECK(vp_seed_find(target, true, 0, N - 1, NULL, &hit));
        CHECK(hit.seed == (uint64_t)want);
        CHECK(hit.hold == masks[want]);

        /* the search's hold is the solver's own optimal mask */
        shoe_t shoe;
        play_seed(hit.seed, true, initial, final5, &hold, &shoe);
        CHECK(hit.hold == hold);
        CHECK(same_hand(hit.initial, initial));
        CHECK(same_hand(hit.final5, final5));

        /* the final hand really is the requested category */
        CHECK(vp_front_category(hit.final5) == target);
        CHECK(hit.cat == target);

        /* held cards are untouched, and only unheld ones changed */
        int drawn = 0;
        for (int i = 0; i < 5; i++) {
            if (hit.hold & (1u << i)) {
                CHECK(hit.final5[i].rank == hit.initial[i].rank &&
                      hit.final5[i].suit == hit.initial[i].suit);
            } else {
                drawn++;
            }
        }

        /* replacements come off the same shoe, in position order, from
         * the cards behind the five that were dealt */
        int taken = 0;
        for (int i = 0; i < 5; i++) {
            if (hit.hold & (1u << i))
                continue;
            card_t next = shoe.cards[5 + taken];
            CHECK(hit.final5[i].rank == next.rank &&
                  hit.final5[i].suit == next.suit);
            taken++;
            /* and a replacement is never a card already in the deal */
            for (int j = 0; j < 5; j++)
                CHECK(!(hit.final5[i].rank == hit.initial[j].rank &&
                        hit.final5[i].suit == hit.initial[j].suit));
        }
        CHECK(taken == drawn);
        CHECK(shoe.pos == 5 + drawn);
    }

    /* a category none of these seeds reaches is reported as no match,
     * not as the nearest thing */
    vp_seed_hit_t miss;
    int royal = vp_front_parse_category("royal_flush");
    bool royal_here = false;
    for (int s = 0; s < N; s++)
        royal_here |= drawn_cat[s] == royal;
    CHECK(!royal_here);
    CHECK(!vp_seed_find(royal, true, 0, N - 1, NULL, &miss));

    /* the same target searched in the two modes is a different search:
     * pick one the scan shows differs and check both answers */
    for (int target = 0; target < VP_FRONT_NCATS; target++) {
        int fd = -1, fi = -1;
        vp_seed_hit_t a, b;

        for (int s = 0; s < N; s++) {
            if (fd < 0 && drawn_cat[s] == target)
                fd = s;
            if (fi < 0 && dealt[s] == target)
                fi = s;
        }
        if (fd < 0 || fi < 0 || fd == fi)
            continue;
        CHECK(vp_seed_find(target, true, 0, N - 1, NULL, &a));
        CHECK(vp_seed_find(target, false, 0, N - 1, NULL, &b));
        CHECK(a.seed == (uint64_t)fd);
        CHECK(b.seed == (uint64_t)fi);
        CHECK(a.seed != b.seed);
        break;
    }
}

int main(void)
{
    test_parse();
    test_initial_first_match();
    test_range();
    test_determinism();
    test_progress();
    test_after_draw();

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
