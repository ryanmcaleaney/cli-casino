#include "gdice.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dice.h"
#include "output.h"

#define GD_MAX_DICE 16

/* "NdM" or "dM" spec, e.g. 2d6, d20. */
static bool parse_spec(const char *s, int *count, int *sides)
{
    char *end;
    long c = 1, m;
    if (*s != 'd' && *s != 'D') {
        c = strtol(s, &end, 10);
        if (end == s || (*end != 'd' && *end != 'D'))
            return false;
        s = end;
    }
    s++; /* skip 'd' */
    m = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return false;
    if (c < 1 || c > GD_MAX_DICE || m < 2 || m > 1000)
        return false;
    *count = (int)c;
    *sides = (int)m;
    return true;
}

int gdice_run(const cli_t *cli, rng_t *rng)
{
    int count = 1, sides = 6;
    const bet_t *bets[CLI_MAX_BETS];
    int nbets = 0;
    bool spec_seen = false;

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];
        int c, m;
        if (b->nvalues == 0 && parse_spec(b->name, &c, &m)) {
            if (spec_seen) {
                fprintf(stderr, "dice: multiple dice specs given\n");
                return 2;
            }
            count = c;
            sides = m;
            spec_seen = true;
        } else if ((bet_is(b, "total") || bet_is(b, "over") ||
                    bet_is(b, "under")) && b->nvalues == 1) {
            bets[nbets++] = b;
        } else {
            fprintf(stderr, "dice: invalid argument '%s' "
                            "(expected NdM, total:N, over:N or under:N)\n",
                    b->raw);
            return 2;
        }
    }

    for (int i = 0; i < nbets; i++) {
        int v = bets[i]->values[0];
        if (v < count || v > count * sides) {
            fprintf(stderr, "dice: bet '%s': value out of range %d-%d "
                            "for %dd%d\n",
                    bets[i]->raw, count, count * sides, count, sides);
            return 2;
        }
    }

    long wins[CLI_MAX_BETS] = { 0 };

    for (long it = 0; it < cli->iterations; it++) {
        int rolls[GD_MAX_DICE];
        int sum = dice_roll_many(rng, count, sides, rolls);

        bet_line_t lines[CLI_MAX_BETS];
        for (int i = 0; i < nbets; i++) {
            int v = bets[i]->values[0];
            bool w = bet_is(bets[i], "total") ? sum == v
                   : bet_is(bets[i], "over")  ? sum > v
                   :                            sum < v;
            lines[i] = (bet_line_t){ bets[i]->raw, w, NULL };
            wins[i] += w;
        }

        if (cli->stats)
            continue;

        if (cli->json) {
            printf("{\"game\":\"dice\",\"spec\":\"%dd%d\",\"rolls\":[",
                   count, sides);
            for (int i = 0; i < count; i++)
                printf("%s%d", i ? "," : "", rolls[i]);
            printf("],\"total\":%d,\"bets\":", sum);
            out_bet_json(stdout, lines, nbets);
            printf("}\n");
        } else if (cli->quiet || cli->iterations > 1) {
            printf("%d", sum);
            out_bet_quiet(stdout, lines, nbets);
            printf("\n");
        } else {
            printf("Roll (%dd%d):", count, sides);
            for (int i = 0; i < count; i++)
                printf(" %d", rolls[i]);
            printf("  total %d\n", sum);
            out_bet_table(stdout, lines, nbets);
        }
    }

    if (cli->stats) {
        printf("Iterations: %ld (%dd%d)\n", cli->iterations, count, sides);
        for (int i = 0; i < nbets; i++)
            printf("%s: %ld wins (%.4f%%)\n", bets[i]->raw, wins[i],
                   100.0 * (double)wins[i] / (double)cli->iterations);
    }
    return 0;
}

void gdice_list_bets(void)
{
    puts("dice arguments:");
    puts("  NdM       dice spec (default 1d6), e.g. 2d6, d20, 3d8");
    puts("  total:N   win if the sum equals N");
    puts("  over:N    win if the sum is greater than N");
    puts("  under:N   win if the sum is less than N");
    puts("  (informational game: no payout odds)");
}
