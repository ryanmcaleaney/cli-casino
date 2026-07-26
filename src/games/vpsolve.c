#include "vpsolve.h"

void vp_solve(const card_t hand[5], vp_payfn_t pay,
              vp_hold_ev_t evs[VP_NMASKS])
{
    /* the 47 cards not in the dealt hand */
    card_t rest[47];
    int nrest = 0;
    for (int r = 1; r <= 13; r++) {
        for (int s = 0; s < 4; s++) {
            bool used = false;
            for (int i = 0; i < 5; i++)
                if (hand[i].rank == r && hand[i].suit == s)
                    used = true;
            if (!used) {
                rest[nrest].rank = (uint8_t)r;
                rest[nrest].suit = (uint8_t)s;
                nrest++;
            }
        }
    }

    for (uint32_t m = 0; m < VP_NMASKS; m++) {
        card_t final5[5];
        int    slot[5];         /* positions to refill */
        int    nslot = 0;

        for (int i = 0; i < 5; i++) {
            if (m & (1u << i))
                final5[i] = hand[i];
            else
                slot[nslot++] = i;
        }

        long total = 0, draws = 0;
        if (nslot == 0) {
            total = pay(final5);
            draws = 1;
        } else {
            /* iterate combinations idx[0] < ... < idx[nslot-1] of rest[] */
            int idx[5];
            for (int i = 0; i < nslot; i++)
                idx[i] = i;
            for (;;) {
                for (int i = 0; i < nslot; i++)
                    final5[slot[i]] = rest[idx[i]];
                total += pay(final5);
                draws++;

                int i = nslot - 1;
                while (i >= 0 && idx[i] == nrest - nslot + i)
                    i--;
                if (i < 0)
                    break;
                idx[i]++;
                for (int j = i + 1; j < nslot; j++)
                    idx[j] = idx[j - 1] + 1;
            }
        }
        evs[m].total = total;
        evs[m].draws = draws;
    }
}

/* Exact rational comparison: sign of a - b. */
static int ev_cmp(const vp_hold_ev_t *a, const vp_hold_ev_t *b)
{
    long long lhs = (long long)a->total * b->draws;
    long long rhs = (long long)b->total * a->draws;
    return (lhs > rhs) - (lhs < rhs);
}

bool vp_ev_equal(const vp_hold_ev_t *a, const vp_hold_ev_t *b)
{
    return ev_cmp(a, b) == 0;
}

static int popcount5(uint32_t m)
{
    int n = 0;
    for (int i = 0; i < 5; i++)
        n += (m >> i) & 1u;
    return n;
}

int vp_solve_best(const vp_hold_ev_t evs[VP_NMASKS])
{
    int best = 0;
    for (int m = 1; m < VP_NMASKS; m++) {
        int c = ev_cmp(&evs[m], &evs[best]);
        if (c > 0 ||
            (c == 0 && popcount5((uint32_t)m) >
                           popcount5((uint32_t)best)))
            best = m;
    }
    return best;
}

double vp_ev(const vp_hold_ev_t *e)
{
    return (double)e->total / (double)e->draws;
}
