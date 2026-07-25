#include "poker.h"

#include <stdbool.h>

static const char *const CAT_STR[] = {
    "High Card", "Pair", "Two Pair", "Three of a Kind", "Straight",
    "Flush", "Full House", "Four of a Kind", "Straight Flush",
    "Royal Flush"
};

const char *poker_cat_str(poker_cat_t cat)
{
    return CAT_STR[cat];
}

poker_eval_t poker_eval5(const card_t c[5])
{
    poker_eval_t ev = { POKER_HIGH_CARD, 0 };
    int  rank_cnt[14] = { 0 };          /* 1 = ace .. 13 = king */
    bool flush = true;

    for (int i = 0; i < 5; i++) {
        rank_cnt[c[i].rank]++;
        if (c[i].suit != c[0].suit)
            flush = false;
    }

    int pairs = 0, trips = 0, quads = 0, pair_rank = 0;
    for (int r = 1; r <= 13; r++) {
        if (rank_cnt[r] == 2) {
            pairs++;
            pair_rank = r;
        } else if (rank_cnt[r] == 3) {
            trips++;
        } else if (rank_cnt[r] == 4) {
            quads++;
        }
    }

    /* Straight: five distinct ranks spanning exactly 4 (ace low covers
     * A-2-3-4-5), or the ace-high 10-J-Q-K-A.  No wraparound. */
    bool straight = false, ace_high = false;
    if (pairs == 0 && trips == 0 && quads == 0) {
        int lo = 14, hi = 0;
        for (int r = 1; r <= 13; r++) {
            if (!rank_cnt[r])
                continue;
            if (r < lo)
                lo = r;
            if (r > hi)
                hi = r;
        }
        if (hi - lo == 4) {
            straight = true;
        } else if (rank_cnt[1] && rank_cnt[10] && rank_cnt[11] &&
                   rank_cnt[12] && rank_cnt[13]) {
            straight = true;
            ace_high = true;
        }
    }

    if (straight && flush)
        ev.cat = ace_high ? POKER_ROYAL_FLUSH : POKER_STRAIGHT_FLUSH;
    else if (quads)
        ev.cat = POKER_FOUR_OF_A_KIND;
    else if (trips && pairs)
        ev.cat = POKER_FULL_HOUSE;
    else if (flush)
        ev.cat = POKER_FLUSH;
    else if (straight)
        ev.cat = POKER_STRAIGHT;
    else if (trips)
        ev.cat = POKER_THREE_OF_A_KIND;
    else if (pairs == 2)
        ev.cat = POKER_TWO_PAIR;
    else if (pairs == 1) {
        ev.cat = POKER_PAIR;
        ev.pair_rank = pair_rank;
    }
    return ev;
}
