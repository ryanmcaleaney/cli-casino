#include "vpseed.h"

#include <string.h>

#include "rng.h"
#include "videopoker.h"

#define VP_HOLD_ALL 0x1fu

bool vp_seed_find(int target, bool after_draw, uint64_t start, uint64_t end,
                  const vp_seed_report_t *report, vp_seed_hit_t *out)
{
    uint64_t checked = 0;

    if (start > end)
        return false;                   /* empty range, nothing to try */

    /* The loop body allocates nothing and formats nothing: only a match
     * is ever turned into text, by the caller. */
    for (uint64_t seed = start;; seed++) {
        rng_t    rng;
        shoe_t   shoe;
        card_t   initial[5], hand[5];
        uint32_t hold = VP_HOLD_ALL;

        rng_init(&rng, true, seed);
        vp_front_deal(&rng, &shoe, initial);
        memcpy(hand, initial, sizeof hand);

        if (after_draw) {
            vp_strategy_t st;

            /* the same solver and the same tie-break the game itself
             * plays, so the search cannot invent a hold of its own */
            vp_front_solve(initial, &st);
            hold = vp_front_best_mask(&st);
            vp_front_draw(&shoe, hold, hand);
        }

        if (vp_front_category(hand) == target) {
            if (out) {
                out->seed = seed;
                out->hold = hold;
                out->cat = target;
                memcpy(out->initial, initial, sizeof out->initial);
                memcpy(out->final5, hand, sizeof out->final5);
            }
            return true;
        }

        checked++;
        if (report && report->fn && report->every &&
            checked % report->every == 0)
            report->fn(checked, report->ctx);

        if (seed == end)
            break;                      /* inclusive end, and no overflow */
    }
    return false;
}
