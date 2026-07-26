#include "slots.h"

#include <stdio.h>
#include <string.h>

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
typedef struct {
    const slot_sym_t *strip;
    int               len;
} slot_reel_t;

#define SLOT_NREELS 3

#define STRIP(s) { s, (int)(sizeof s / sizeof s[0]) }
static const slot_reel_t REELS[SLOT_NREELS] = {
    STRIP(REEL_A), STRIP(REEL_B), STRIP(REEL_C)
};

/* Payline evaluation: the three middle symbols only.  The visible top
 * and bottom rows are strip neighbours and never pay. */
static slot_result_t classify(const slot_sym_t s[SLOT_NREELS])
{
    if (s[0] == s[1] && s[1] == s[2]) {
        switch (s[0]) {
        case SYM_SEVEN:  return SLOT_JACKPOT;
        case SYM_BAR:    return SLOT_BIG_WIN;
        case SYM_BELL:
        case SYM_CHERRY: return SLOT_WIN;
        default: break;             /* three lemons/oranges pay nothing */
        }
    }
    int cherries = 0;
    for (int i = 0; i < SLOT_NREELS; i++)
        cherries += (s[i] == SYM_CHERRY);
    return cherries >= 2 ? SLOT_SMALL_WIN : SLOT_LOSS;
}

/* 3x3 reel window; row 1 is the payline. */
static void window_print(FILE *f, slot_sym_t w[3][SLOT_NREELS])
{
    fputs("┌──────────┬──────────┬──────────┐\n", f);
    for (int row = 0; row < 3; row++) {
        if (row)
            fputs("├──────────┼──────────┼──────────┤\n", f);
        for (int i = 0; i < SLOT_NREELS; i++) {
            const char *name = SYM_NAME[w[row][i]];
            int len = (int)strlen(name);
            int left = (10 - len) / 2;
            fprintf(f, "│%*s%s%*s", left, "", name, 10 - left - len, "");
        }
        fputs(row == 1 ? "│ <- PAYLINE\n" : "│\n", f);
    }
    fputs("└──────────┴──────────┴──────────┘\n", f);
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
        slot_sym_t win3[3][SLOT_NREELS];
        for (int i = 0; i < SLOT_NREELS; i++) {
            int len = REELS[i].len;
            /* one stop per reel; neighbours come from the circular strip,
             * consuming no extra randomness */
            int stop = (int)rng_below(rng, (uint32_t)len);
            win3[0][i] = REELS[i].strip[(stop - 1 + len) % len];
            win3[1][i] = REELS[i].strip[stop];
            win3[2][i] = REELS[i].strip[(stop + 1) % len];
        }
        const slot_sym_t *s = win3[1];      /* the payline */
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
            printf("],\"window\":[");
            for (int row = 0; row < 3; row++) {
                printf("%s[", row ? "," : "");
                for (int i = 0; i < SLOT_NREELS; i++) {
                    if (i)
                        printf(",");
                    json_string(stdout, SYM_NAME[win3[row][i]]);
                }
                printf("]");
            }
            printf("],\"payline\":[");
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
            window_print(stdout, win3);
            printf("\n%s\n", RESULT_WORD[r]);
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
        } else if (cli->quiet) {
            printf("runs=%ld", cli->iterations);
            for (int i = 4; i >= 0; i--)
                printf(" %s=%ld", RESULT_JSON[i], counts[i]);
            printf("\n");
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
    puts("slots: 3 reels, 3x3 visible window, single middle payline");
    puts("takes no bets; payouts are informational only:");
    puts("  SEVEN  SEVEN  SEVEN    100:1  JACKPOT");
    puts("  BAR    BAR    BAR       20:1  BIG_WIN");
    puts("  BELL   BELL   BELL      10:1  WIN");
    puts("  CHERRY CHERRY CHERRY    10:1  WIN");
    puts("  any two CHERRY           2:1  SMALL_WIN");
    puts("  anything else              -  LOSS");
    puts("only the middle row pays; top/bottom rows are the adjacent");
    puts("strip positions shown for context");
}
