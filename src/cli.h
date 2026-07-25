#ifndef CASINO_CLI_H
#define CASINO_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BET_MAX_VALUES 8
#define CLI_MAX_BETS   32

/*
 * Bet token grammar:
 *   NAME                  e.g. "red"
 *   NAME:V                e.g. "straight:17"
 *   NAME:V,V[,V...]       e.g. "split:17,20"
 * NAME is [A-Za-z0-9,]+ (games may interpret it freely, e.g. dice "2d6"
 * or blackjack action lists "h,s").
 */
typedef struct bet {
    char raw[64];               /* original token, for display */
    char name[32];              /* lowercased name part */
    int  values[BET_MAX_VALUES];
    int  nvalues;
} bet_t;

typedef struct cli {
    bet_t    bets[CLI_MAX_BETS];
    int      nbets;
    bool     help;
    bool     list_bets;
    bool     quiet;
    bool     json;
    bool     stats;
    bool     seeded;
    uint64_t seed;
    long     iterations;        /* >= 1, default 1 */
} cli_t;

/*
 * Parse argv AFTER the game name has been removed.
 * Returns 0 on success; -1 on error with a message in err.
 */
int cli_parse(int argc, char **argv, cli_t *out, char *err, size_t errlen);

/* Case-insensitive bet-name compare. */
bool bet_is(const bet_t *b, const char *name);

#endif
