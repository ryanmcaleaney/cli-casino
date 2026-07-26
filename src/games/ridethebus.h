#ifndef CASINO_RIDETHEBUS_H
#define CASINO_RIDETHEBUS_H

#include <stdbool.h>

#include "cards.h"
#include "cli.h"
#include "rng.h"

int  ridethebus_run(const cli_t *cli, rng_t *rng);
void ridethebus_list_bets(void);

/* ---- frontend session (the GUI plays through this) --------------------- */

/*
 * The engine stays authoritative: a frontend starts a game, applies one
 * choice per round and reads the state back.  Rank comparison, the colour
 * rule, inside/outside, suit matching and the payout ladder all live in
 * ridethebus.c and are shared with the CLI, scripted and simulated play,
 * so a frontend cannot drift from them.
 */

#define RTB_ROUNDS      4
#define RTB_BET_DEFAULT 100
#define RTB_BET_MAX     1000000

typedef enum {
    RTB_RED_BLACK, RTB_HIGH_LOW, RTB_INSIDE_OUTSIDE, RTB_SUIT, RTB_COMPLETE
} rtb_stage_t;

typedef enum { RTB_LOSS, RTB_CASHOUT, RTB_BUS } rtb_outcome_t;

/*
 * One game: its own shuffled 52-card deck and exactly one card per round
 * played.  Frontends read these fields (cards, ncards, bet, payout,
 * rounds_won, outcome) and change them only through the calls below.
 */
typedef struct {
    long          bet;
    long          payout;               /* multiple of the ORIGINAL wager */
    int           rounds_won;
    rtb_outcome_t outcome;
    bool          walked;               /* EOF before finishing */
    bool          over;                 /* no further choice is possible */
    card_t        cards[RTB_ROUNDS];    /* one card per round played */
    int           ncards;
    int           guess[RTB_ROUNDS];
    shoe_t        shoe;                 /* one shuffled deck per game */
} rtb_game_t;

/* Fresh game on a freshly shuffled deck; the wager is taken by the caller. */
void        rtb_front_start(rtb_game_t *g, long bet, rng_t *rng);
/* The round about to be played, or RTB_COMPLETE when all four are won. */
rtb_stage_t rtb_front_stage(const rtb_game_t *g);
bool        rtb_front_over(const rtb_game_t *g);
/* Cashing out is offered from round 2 on, i.e. once a round has been won. */
bool        rtb_front_can_cash(const rtb_game_t *g);
/* How many guesses this round offers: 2, or 4 for the suit round. */
int         rtb_front_nchoices(rtb_stage_t st);

/* Apply a guess for the current stage: deals one card and resolves the
 * round, updating payout/outcome.  Returns true when the round was won. */
bool        rtb_front_guess(rtb_game_t *g, int guess);
/* Stop and keep the payout won so far. */
void        rtb_front_cash_out(rtb_game_t *g);

/* Round 3 reference range, low card first. */
void        rtb_front_range(const rtb_game_t *g, card_t *lo, card_t *hi);
const char *rtb_front_stage_title(rtb_stage_t st);   /* "HIGHER OR LOWER" */
const char *rtb_front_guess_word(rtb_stage_t st, int guess);  /* "hearts" */

#endif
