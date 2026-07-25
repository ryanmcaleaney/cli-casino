#ifndef CASINO_GAME_H
#define CASINO_GAME_H

#include "cli.h"
#include "rng.h"

/*
 * Contract for every game:
 *   run()       : play cli->iterations rounds; return 0 on success,
 *                 2 on bet-validation error (losing a bet is NOT an error).
 *   list_bets() : print supported bet syntax to stdout.
 *   help        : one-line description for `casino --help`.
 * A game with run == NULL is registered but not yet implemented.
 */
typedef struct game {
    const char *name;
    const char *help;
    int  (*run)(const cli_t *cli, rng_t *rng);
    void (*list_bets)(void);
} game_t;

extern const game_t GAMES[];
extern const int    NGAMES;

const game_t *game_find(const char *name);

#endif
