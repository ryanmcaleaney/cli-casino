#include "coin.h"

#include <stdio.h>

#include "output.h"

int coin_run(const cli_t *cli, rng_t *rng)
{
    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];
        if (!bet_is(b, "heads") && !bet_is(b, "tails")) {
            fprintf(stderr, "coin: unknown bet '%s' "
                            "(valid: heads, tails)\n", b->raw);
            return 2;
        }
        if (bet_has_value(b)) {
            fprintf(stderr, "coin: bet '%s' takes no value\n", b->raw);
            return 2;
        }
    }

    long wins[CLI_MAX_BETS] = { 0 };

    for (long it = 0; it < cli->iterations; it++) {
        int flip = (int)rng_below(rng, 2); /* 0 = heads, 1 = tails */
        const char *side = flip == 0 ? "HEADS" : "TAILS";

        bet_line_t lines[CLI_MAX_BETS];
        for (int i = 0; i < cli->nbets; i++) {
            bool w = (flip == 0) == bet_is(&cli->bets[i], "heads");
            lines[i] = (bet_line_t){ cli->bets[i].raw, w, "1:1" };
            wins[i] += w;
        }

        if (cli->stats)
            continue;

        if (cli->json) {
            printf("{\"game\":\"coin\",\"flip\":\"%s\",\"bets\":",
                   flip == 0 ? "heads" : "tails");
            out_bet_json(stdout, lines, cli->nbets);
            printf("}\n");
        } else if (cli->quiet || cli->iterations > 1) {
            printf("%s", side);
            out_bet_quiet(stdout, lines, cli->nbets);
            printf("\n");
        } else {
            printf("Flip: %s\n", side);
            out_bet_table(stdout, lines, cli->nbets);
        }
    }

    if (cli->stats) {
        if (cli->json) {
            printf("{\"game\":\"coin\",\"iterations\":%ld,\"bets\":[",
                   cli->iterations);
            for (int i = 0; i < cli->nbets; i++) {
                if (i)
                    printf(",");
                printf("{\"bet\":");
                json_string(stdout, cli->bets[i].raw);
                printf(",\"wins\":%ld,\"hit_rate\":%.6f}", wins[i],
                       (double)wins[i] / (double)cli->iterations);
            }
            printf("]}\n");
        } else {
            printf("Iterations: %ld\n", cli->iterations);
            for (int i = 0; i < cli->nbets; i++)
                printf("%s: %ld wins (%.4f%%)\n", cli->bets[i].raw,
                       wins[i],
                       100.0 * (double)wins[i] / (double)cli->iterations);
        }
    }
    return 0;
}

void coin_list_bets(void)
{
    puts("coin bets:");
    puts("  heads   1:1");
    puts("  tails   1:1");
}
