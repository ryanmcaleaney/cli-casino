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
 *   NAME:V,V[,V...]       e.g. "straight:17", "split:17,20"
 *   NAME:WORDS            e.g. "hold:none", "deal:ah,kh,qh,jh,10h"
 * NAME is [A-Za-z0-9,-]+ (games may interpret it freely, e.g. dice "2d6",
 * blackjack action lists "h,s", craps "dont-pass").  A leading '-' still
 * means an option; names must start with an alphanumeric.
 * The value part is parsed into values[] when it is an integer list;
 * otherwise nvalues stays 0 and the game interprets vraw.  Either way the
 * lowercased value text is kept in vraw.
 */
typedef struct bet {
    char raw[64];               /* original token, for display */
    char name[32];              /* lowercased name part */
    char vraw[32];              /* lowercased value part, "" if none */
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
    bool     trainer;           /* interactive strategy trainer (videopoker) */
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

/* True if the token carried any value part (integers or words). */
bool bet_has_value(const bet_t *b);

#endif
