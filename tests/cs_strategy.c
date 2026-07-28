/*
 * Caribbean Stud basic strategy regression tests.
 *
 * The rule under test (see cs_strategy.c):
 *   raise on one pair or better; fold below ace-king high; holding
 *   exactly ace-king high raise when the up-card is a 2 through queen
 *   matching one of the player's cards, or is an ace or king against a
 *   queen or jack of the player's, or the player holds a queen and the
 *   up-card ranks below the player's fourth-highest card.
 *
 * Every boundary of that rule is checked here, together with the two
 * properties the simulation relies on: the adviser reads suits only
 * through the hand category, and it writes nothing.
 */
#include <stdio.h>
#include <string.h>

#include "games/caribbeanstud.h"
#include "games/cs_strategy.h"

static int fails;

#define CHECK(c) do {                                                   \
        if (!(c)) {                                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c);                 \
            fails++;                                                    \
        }                                                               \
    } while (0)

static card_t card(const char *s)
{
    card_t c = { 0, 0 };
    int rank;

    if (s[0] == '1' && s[1] == '0') {
        rank = 10;
        s += 2;
    } else {
        switch (s[0]) {
        case 'a': rank = 1;  break;
        case 'j': rank = 11; break;
        case 'q': rank = 12; break;
        case 'k': rank = 13; break;
        default:  rank = s[0] - '0';
        }
        s++;
    }
    c.rank = (uint8_t)rank;
    switch (s[0]) {
    case 'c': c.suit = 0; break;
    case 'd': c.suit = 1; break;
    case 'h': c.suit = 2; break;
    default:  c.suit = 3; break;
    }
    return c;
}

static void hand(const char *spec, card_t out[CS_CARDS])
{
    char buf[64];
    int n = 0;

    snprintf(buf, sizeof buf, "%s", spec);
    for (char *tok = strtok(buf, ","); tok && n < CS_CARDS;
         tok = strtok(NULL, ","))
        out[n++] = card(tok);
}

/* true = raise */
static bool advise(const char *player, const char *upcard)
{
    card_t h[CS_CARDS];

    hand(player, h);
    return cs_basic_strategy(h, card(upcard)) == CS_DECISION_RAISE;
}

/* ---- 1. one pair or better always raises -------------------------------- */

static void test_made_hands(void)
{
    static const char *const UP[] = { "ac", "kc", "qc", "9c", "2c" };
    static const char *const MADE[] = {
        "2h,2d,7c,5s,3d",           /* the lowest pair there is */
        "kh,kd,8c,5s,2d",
        "9h,9d,3c,3s,kd",           /* two pair */
        "9h,9d,9c,3s,kd",           /* trips */
        "5h,6d,7c,8s,9d",           /* straight */
        "ah,2d,3c,4s,5d",           /* the wheel */
        "2h,7h,9h,jh,4h",           /* flush */
        "9h,9d,9c,3s,3d",           /* full house */
        "9h,9d,9c,9s,3d",           /* quads */
        "5h,6h,7h,8h,9h",           /* straight flush */
        "10h,jh,qh,kh,ah"           /* royal flush */
    };

    for (int i = 0; i < (int)(sizeof MADE / sizeof MADE[0]); i++)
        for (int u = 0; u < (int)(sizeof UP / sizeof UP[0]); u++)
            CHECK(advise(MADE[i], UP[u]));
}

/* ---- 2. below ace-king high, always fold -------------------------------- */

static void test_weak_hands(void)
{
    static const char *const UP[] = { "ac", "kc", "qc", "9c", "2c" };
    static const char *const WEAK[] = {
        "ah,qd,jc,9s,4d",           /* ace-queen, however good the rest */
        "ah,qd,jc,10s,4d",
        "kh,qd,jc,9s,4d",           /* king-queen */
        "kh,qd,jc,10s,8d",
        "qh,jd,9c,5s,3d",
        "9h,7d,5c,3s,2d",
        "ah,jd,9c,5s,3d"            /* ace high, but no king: not ace-king */
    };

    for (int i = 0; i < (int)(sizeof WEAK / sizeof WEAK[0]); i++)
        for (int u = 0; u < (int)(sizeof UP / sizeof UP[0]); u++)
            CHECK(!advise(WEAK[i], UP[u]));
}

/* ---- 3a. a 2-through-queen up-card the player also holds ---------------- */

static void test_matching_upcard(void)
{
    /* A-K-9-5-3: the up-card matches a card of ours */
    CHECK(advise("ah,kd,9c,5s,3d", "9d"));
    CHECK(advise("ah,kd,9c,5s,3d", "5d"));
    CHECK(advise("ah,kd,9c,5s,3d", "3c"));
    /* the same hand against ranks we do not hold */
    CHECK(!advise("ah,kd,9c,5s,3d", "8d"));
    CHECK(!advise("ah,kd,9c,5s,3d", "7d"));
    CHECK(!advise("ah,kd,9c,5s,3d", "4d"));
    CHECK(!advise("ah,kd,9c,5s,3d", "2d"));
    /* the suit of the matching card is irrelevant */
    CHECK(advise("ah,kd,9c,5s,3d", "9h"));
    CHECK(advise("ah,kd,9c,5s,3d", "9s"));
    /* a queen up-card that we match still counts (the top of the range) */
    CHECK(advise("ah,kd,qc,5s,3d", "qh"));
    /* ... and a jack we do not hold does not */
    CHECK(!advise("ah,kd,9c,5s,3d", "jh"));
}

/* ---- 3b. an ace or king up-card, against our queen or jack -------------- */

static void test_ace_king_upcard(void)
{
    CHECK(advise("ah,kd,qc,5s,3d", "ac"));
    CHECK(advise("ah,kd,qc,5s,3d", "kc"));
    CHECK(advise("ah,kd,jc,5s,3d", "ac"));
    CHECK(advise("ah,kd,jc,5s,3d", "kc"));
    /* no queen and no jack of our own: fold, however close the rest is */
    CHECK(!advise("ah,kd,10c,9s,8d", "ac"));
    CHECK(!advise("ah,kd,10c,9s,8d", "kc"));
    CHECK(!advise("ah,kd,10c,5s,3d", "ac"));
    /* an ace or king up-card is not a "match" - rule 3a stops at the
     * queen, or every ace-king hand would raise against one */
    CHECK(!advise("ah,kd,9c,5s,3d", "ac"));
    CHECK(!advise("ah,kd,9c,5s,3d", "kc"));
}

/* ---- 3c. our queen, against an up-card below our fourth card ------------ */

static void test_fourth_card(void)
{
    /* A-K-Q-8-3: the fourth-highest card is the 8 */
    CHECK(advise("ah,kd,qc,8s,3d", "7c"));      /* 7 < 8: raise */
    CHECK(advise("ah,kd,qc,8s,3d", "4c"));
    CHECK(advise("ah,kd,qc,8s,3d", "2c"));
    CHECK(!advise("ah,kd,qc,8s,3d", "9c"));     /* 9 > 8, and no match */
    CHECK(!advise("ah,kd,qc,8s,3d", "10c"));
    CHECK(!advise("ah,kd,qc,8s,3d", "jc"));
    /* an up-card equal to the fourth card is a match, so it raises under
     * rule 3a - the two rules cannot disagree */
    CHECK(advise("ah,kd,qc,8s,3d", "8c"));
    /* A-K-Q-10-9: a low up-card still raises */
    CHECK(advise("ah,kd,qc,10s,9d", "3c"));
    CHECK(!advise("ah,kd,qc,10s,9d", "jc"));
    /* without the queen the rule does not apply: A-K-J-8-3 folds to the
     * same low up-card that A-K-Q-8-3 raises against */
    CHECK(!advise("ah,kd,jc,8s,3d", "7c"));
    CHECK(advise("ah,kd,qc,8s,3d", "7c"));
    /* and the fourth card is the fourth card, not the lowest */
    CHECK(!advise("ah,kd,qc,9s,2d", "10c"));
    CHECK(advise("ah,kd,qc,9s,2d", "8c"));
}

/* ---- properties --------------------------------------------------------- */

static void test_suits_and_purity(void)
{
    /* the same ranks in different suits decide the same way, as long as
     * the category does not change */
    CHECK(advise("ah,kd,9c,5s,3d", "9d") == advise("as,kh,9d,5c,3s", "9h"));
    CHECK(advise("ah,kd,qc,8s,3d", "7c") == advise("ac,ks,qd,8h,3c", "7s"));
    CHECK(advise("ah,qd,jc,9s,4d", "2c") == advise("as,qh,jd,9c,4s", "2h"));
    /* ... and when it does change, the category is what decides: five
     * hearts is a flush, so it raises where the offsuit hand folds */
    CHECK(!advise("kh,qd,9c,5s,3d", "2c"));
    CHECK(advise("kh,qh,9h,5h,3h", "2c"));

    /* the adviser is pure: it writes to neither the hand nor the up-card */
    {
        card_t h[CS_CARDS], copy[CS_CARDS], up, upcopy;

        hand("ah,kd,qc,8s,3d", h);
        memcpy(copy, h, sizeof copy);
        up = upcopy = card("7c");
        for (int i = 0; i < 3; i++)
            CHECK(cs_basic_strategy(h, up) == CS_DECISION_RAISE);
        CHECK(memcmp(h, copy, sizeof copy) == 0);
        CHECK(up.rank == upcopy.rank && up.suit == upcopy.suit);
    }
}

int main(void)
{
    test_made_hands();
    test_weak_hands();
    test_matching_upcard();
    test_ace_king_upcard();
    test_fourth_card();
    test_suits_and_purity();

    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails != 0;
}
