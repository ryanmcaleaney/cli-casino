#ifndef CASINO_BACCARAT_H
#define CASINO_BACCARAT_H

#include "cards.h"
#include "cli.h"
#include "rng.h"

int  baccarat_run(const cli_t *cli, rng_t *rng);
void baccarat_list_bets(void);

/* Punto Banco: no player decisions, the draw rules are fully mechanical. */
#define BAC_MAX_CARDS 3

typedef enum { BAC_PLAYER, BAC_BANKER, BAC_TIE } bac_side_t;
typedef enum { BAC_WIN, BAC_LOSS, BAC_PUSH } bac_result_t;

/*
 * Read-only frontend interface (GUI and other presentation layers).
 * The drawing rules, totals and outcome all stay in baccarat.c; these
 * only hand back a round the engine has already resolved.
 */
typedef struct {
    card_t     player[BAC_MAX_CARDS];
    int        nplayer;
    card_t     banker[BAC_MAX_CARDS];
    int        nbanker;
    bac_side_t outcome;
} bac_round_t;

/* Deal and fully resolve one round from a freshly shuffled single deck.
 * Deal order is player[0], banker[0], player[1], banker[1], then any
 * third cards, the player's first. */
void bac_front_round(rng_t *rng, bac_round_t *out);

/* Baccarat total of the first n cards of a hand (card values summed
 * mod 10), so a frontend can show a running total while dealing. */
int bac_front_total(const card_t *cards, int n);

bac_result_t bac_front_result(bac_side_t outcome, bac_side_t bet);

const char *bac_front_side_word(bac_side_t side);       /* "PLAYER" ... */
const char *bac_front_result_word(bac_result_t result); /* "WIN" ... */

#endif
