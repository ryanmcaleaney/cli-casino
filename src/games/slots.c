#include "slots.h"

#include <stdio.h>

#include "output.h"

typedef enum {
    SYM_CHERRY, SYM_LEMON, SYM_ORANGE, SYM_BELL, SYM_BAR, SYM_SEVEN
} slot_sym_t;

static const char *const SYM_NAME[] = {
    "CHERRY", "LEMON", "ORANGE", "BELL", "BAR", "SEVEN"
};

typedef enum {
    SLOT_LOSS, SLOT_SMALL_WIN, SLOT_WIN, SLOT_BIG_WIN, SLOT_JACKPOT
} slot_result_t;

static const char *const RESULT_WORD[] = {
    "LOSS", "SMALL_WIN", "WIN", "BIG_WIN", "JACKPOT"
};
static const char *const RESULT_JSON[] = {
    "loss", "small_win", "win", "big_win", "jackpot"
};
/* Informational only; no money is tracked. */
static const char *const RESULT_PAYOUT[] = {
    "-", "2:1", "10:1", "20:1", "100:1"
};

/* Each reel is an explicit strip: symbol frequency on the strip is the
 * probability weight.  Strips differ per reel and are meant to be edited. */
static const slot_sym_t REEL_A[] = {
    SYM_CHERRY, SYM_LEMON,  SYM_ORANGE, SYM_BELL,   SYM_CHERRY,
    SYM_LEMON,  SYM_BAR,    SYM_ORANGE, SYM_CHERRY, SYM_SEVEN,
    SYM_LEMON,  SYM_ORANGE, SYM_BELL,   SYM_CHERRY, SYM_LEMON,
    SYM_BAR,    SYM_ORANGE, SYM_BELL,   SYM_SEVEN,  SYM_LEMON,
};
static const slot_sym_t REEL_B[] = {
    SYM_LEMON,  SYM_CHERRY, SYM_BELL,   SYM_ORANGE, SYM_LEMON,
    SYM_BAR,    SYM_ORANGE, SYM_BELL,   SYM_SEVEN,  SYM_LEMON,
    SYM_CHERRY, SYM_ORANGE, SYM_BAR,    SYM_BELL,   SYM_LEMON,
    SYM_ORANGE, SYM_CHERRY, SYM_BELL,   SYM_SEVEN,  SYM_BAR,
};
static const slot_sym_t REEL_C[] = {
    SYM_ORANGE, SYM_LEMON,  SYM_CHERRY, SYM_BELL,   SYM_BAR,
    SYM_LEMON,  SYM_ORANGE, SYM_SEVEN,  SYM_LEMON,  SYM_BELL,
    SYM_CHERRY, SYM_BAR,    SYM_ORANGE, SYM_LEMON,  SYM_BELL,
    SYM_SEVEN,  SYM_ORANGE, SYM_CHERRY, SYM_LEMON,  SYM_BAR,
};
static const slot_sym_t REEL_D[] = {
    SYM_BELL,   SYM_CHERRY, SYM_LEMON,  SYM_ORANGE, SYM_SEVEN,
    SYM_LEMON,  SYM_BAR,    SYM_CHERRY, SYM_ORANGE, SYM_LEMON,
    SYM_BELL,   SYM_ORANGE, SYM_CHERRY, SYM_BAR,    SYM_LEMON,
    SYM_BELL,   SYM_SEVEN,  SYM_ORANGE, SYM_LEMON,  SYM_CHERRY,
};
static const slot_sym_t REEL_E[] = {
    SYM_LEMON,  SYM_ORANGE, SYM_BAR,    SYM_CHERRY, SYM_BELL,
    SYM_SEVEN,  SYM_LEMON,  SYM_CHERRY, SYM_ORANGE, SYM_BELL,
    SYM_LEMON,  SYM_BAR,    SYM_ORANGE, SYM_CHERRY, SYM_LEMON,
    SYM_BELL,   SYM_ORANGE, SYM_SEVEN,  SYM_LEMON,  SYM_CHERRY,
};
static const slot_sym_t REEL_F[] = {
    SYM_CHERRY, SYM_BELL,   SYM_ORANGE, SYM_LEMON,  SYM_BAR,
    SYM_ORANGE, SYM_LEMON,  SYM_SEVEN,  SYM_BELL,   SYM_CHERRY,
    SYM_LEMON,  SYM_ORANGE, SYM_BAR,    SYM_LEMON,  SYM_BELL,
    SYM_CHERRY, SYM_ORANGE, SYM_LEMON,  SYM_SEVEN,  SYM_BAR,
};

typedef struct {
    const slot_sym_t *strip;
    int               len;
} slot_reel_t;

#define SLOT_NREELS 6

#define STRIP(s) { s, (int)(sizeof s / sizeof s[0]) }
static const slot_reel_t REELS[SLOT_NREELS] = {
    STRIP(REEL_A), STRIP(REEL_B), STRIP(REEL_C),
    STRIP(REEL_D), STRIP(REEL_E), STRIP(REEL_F)
};

/* Six reels: pay on symbol counts across the line.  All-six combos would
 * be ~1-in-10^8, so the tiers trigger on 4 of a kind instead. */
static slot_result_t classify(const slot_sym_t s[SLOT_NREELS])
{
    int cnt[6] = { 0 };
    for (int i = 0; i < SLOT_NREELS; i++)
        cnt[s[i]]++;

    if (cnt[SYM_SEVEN] >= 4)
        return SLOT_JACKPOT;
    if (cnt[SYM_BAR] >= 4)
        return SLOT_BIG_WIN;
    if (cnt[SYM_BELL] >= 4 || cnt[SYM_CHERRY] >= 4)
        return SLOT_WIN;
    if (cnt[SYM_CHERRY] == 3)
        return SLOT_SMALL_WIN;
    return SLOT_LOSS;               /* 4+ lemons/oranges pay nothing */
}

int slots_run(const cli_t *cli, rng_t *rng)
{
    if (cli->nbets != 0) {
        fprintf(stderr, "slots: takes no bets "
                        "(payouts are informational only)\n");
        return 2;
    }

    long counts[5] = { 0 };

    for (long it = 0; it < cli->iterations; it++) {
        slot_sym_t s[SLOT_NREELS];
        for (int i = 0; i < SLOT_NREELS; i++) {
            int stop = (int)rng_below(rng, (uint32_t)REELS[i].len);
            s[i] = REELS[i].strip[stop];
        }
        slot_result_t r = classify(s);
        counts[r]++;

        if (cli->stats)
            continue;

        if (cli->json) {
            printf("{\"game\":\"slots\",\"reels\":[");
            for (int i = 0; i < SLOT_NREELS; i++) {
                if (i)
                    printf(",");
                json_string(stdout, SYM_NAME[s[i]]);
            }
            printf("],\"result\":");
            json_string(stdout, RESULT_JSON[r]);
            printf(",\"payout\":");
            json_string(stdout, RESULT_PAYOUT[r]);
            printf("}\n");
        } else if (cli->quiet) {
            puts(RESULT_WORD[r]);
        } else if (cli->iterations > 1) {
            for (int i = 0; i < SLOT_NREELS; i++)
                printf("%s ", SYM_NAME[s[i]]);
            printf(" %s\n", RESULT_WORD[r]);
        } else {
            for (int i = 0; i < SLOT_NREELS; i++)
                printf("%s[ %s ]", i ? " " : "", SYM_NAME[s[i]]);
            printf("\n\n%s\n", RESULT_WORD[r]);
            if (r != SLOT_LOSS)
                printf("Payout: %s\n", RESULT_PAYOUT[r]);
        }
    }

    if (cli->stats) {
        if (cli->json) {
            printf("{\"game\":\"slots\",\"iterations\":%ld,\"results\":{",
                   cli->iterations);
            for (int i = 0; i < 5; i++) {
                if (i)
                    printf(",");
                json_string(stdout, RESULT_JSON[i]);
                printf(":%ld", counts[i]);
            }
            printf("}}\n");
        } else {
            printf("Iterations: %ld\n", cli->iterations);
            printf("%-10s %8s %9s\n", "RESULT", "COUNT", "RATE%");
            for (int i = 4; i >= 0; i--)
                printf("%-10s %8ld %9.4f\n", RESULT_WORD[i], counts[i],
                       100.0 * (double)counts[i] /
                           (double)cli->iterations);
        }
    }
    return 0;
}

void slots_list_bets(void)
{
    puts("slots: 6-reel slot machine, single payline, one spin per round");
    puts("takes no bets; payouts are informational only:");
    puts("  4+ SEVEN       100:1  JACKPOT");
    puts("  4+ BAR          20:1  BIG_WIN");
    puts("  4+ BELL         10:1  WIN");
    puts("  4+ CHERRY       10:1  WIN");
    puts("  3 CHERRY         2:1  SMALL_WIN");
    puts("  anything else      -  LOSS");
}
