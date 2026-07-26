#ifndef CASINO_BLACKJACK_H
#define CASINO_BLACKJACK_H

#include <stdbool.h>
#include <stddef.h>

#include "cards.h"
#include "cli.h"
#include "rng.h"

int  blackjack_run(const cli_t *cli, rng_t *rng);
void blackjack_list_bets(void);

/* ---- rule profile ------------------------------------------------------- */

#define BJ_DECKS        6
#define BJ_SHOE_CARDS   (52 * BJ_DECKS)     /* 312 */
#define BJ_RESHUFFLE_AT 78                  /* ~75% penetration */
#define BJ_MAX_HANDS    4
#define BJ_MAX_CARDS    12

/*
 * Credits are held in halves so that 3:2 blackjacks and half-wager
 * surrenders stay exact in integer arithmetic.  Wagers are always an
 * even number of halves, so wager*5/2 (blackjack) and wager/2
 * (surrender, insurance stake) never truncate.
 */
#define BJ_HALF           2
#define BJ_BANKROLL_START (1000 * BJ_HALF)
#define BJ_BET_MIN        (5 * BJ_HALF)
#define BJ_BET_MAX        (500 * BJ_HALF)
#define BJ_BET_DEFAULT    (25 * BJ_HALF)

typedef enum {
    BJ_HIT, BJ_STAND, BJ_DOUBLE, BJ_SPLIT, BJ_SURRENDER
} bj_action_t;
#define BJ_NACTIONS 5

typedef enum {
    BJ_PENDING, BJ_WIN, BJ_LOSS, BJ_PUSH, BJ_BLACKJACK, BJ_SURRENDERED
} bj_result_t;

typedef enum {
    BJ_PHASE_BET,        /* between rounds */
    BJ_PHASE_INSURANCE,  /* dealer shows an ace, waiting for the decision */
    BJ_PHASE_PLAYER,     /* waiting for an action on the active hand */
    BJ_PHASE_SETTLED     /* round finished and paid */
} bj_phase_t;

typedef struct {
    card_t      cards[BJ_MAX_CARDS];
    int         n;
    long        wager;          /* half-credits, always even */
    bool        doubled;
    bool        from_split;
    bool        split_aces;     /* one card only, no double, no re-split */
    bool        surrendered;
    bool        done;
    bj_result_t result;
    long        payout;         /* returned to the bankroll, half-credits */
} bj_hand_t;

/* One round at the table. */
typedef struct {
    bj_hand_t  hands[BJ_MAX_HANDS];
    int        nhands;
    int        active;
    card_t     dealer[BJ_MAX_CARDS];
    int        ndealer;
    bool       hole_hidden;
    bool       dealer_bj;
    long       insurance;       /* stake, 0 when not taken */
    bool       insurance_won;
    bool       insurance_offered;
    bj_phase_t phase;
    long       wagered;         /* money committed this round */
    long       returned;        /* money paid back this round */
} bj_round_t;

/* The table: persistent shoe, bankroll and base wager across rounds. */
typedef struct {
    shoe_t     shoe;
    long       bankroll;
    long       base_bet;
    bool       shuffled;        /* the shoe was shuffled before this round */
    bj_round_t round;
} bj_session_t;

/* ---- frontend API (CLI and GUI both drive the engine through this) ----- */

/*
 * Between rounds means BJ_PHASE_BET or BJ_PHASE_SETTLED: a table showing
 * the result of the last hand is idle, so re-betting, re-buying and
 * dealing the next hand are all allowed from it.  The calls below are
 * ignored mid-round (BJ_PHASE_INSURANCE, BJ_PHASE_PLAYER).
 */
void bj_session_start(bj_session_t *s, rng_t *rng);
/* Fresh starting bankroll.  The shoe is left alone: a re-buy does not
 * shuffle, so the running count a player has been keeping stays valid. */
void bj_bankroll_reset(bj_session_t *s);

/* Clamp and set the base wager (half-credits). */
void bj_set_bet(bj_session_t *s, long units);
bool bj_can_deal(const bj_session_t *s);
/* Clears the previous round, so it starts the next hand from a settled
 * one as well as from a fresh table. */
void bj_deal(bj_session_t *s, rng_t *rng);

bool bj_legal(const bj_session_t *s, bj_action_t a);
void bj_act(bj_session_t *s, bj_action_t a, rng_t *rng);

bool bj_insurance_pending(const bj_session_t *s);
void bj_insurance(bj_session_t *s, bool take, rng_t *rng);

/* Best total of a hand, and whether an ace is still counted as 11. */
int  bj_total(const card_t *cards, int n);
bool bj_soft(const card_t *cards, int n);
bool bj_natural(const bj_hand_t *h);

int  bj_remaining(const bj_session_t *s);

/* ---- Hi-Lo card counting (GUI training mode) --------------------------- */

/* Hi-Lo tag: 2-6 = +1, 7-9 = 0, 10/J/Q/K/A = -1. */
int    bj_hilo(card_t c);
/* Cards dealt out of the current shoe since it was last shuffled, and the
 * i-th of them in deal order (a zero card outside that range). */
int    bj_dealt_count(const bj_session_t *s);
card_t bj_dealt_card(const bj_session_t *s, int i);

/*
 * Running count over the cards a player has actually seen.  The frontend
 * decides what is visible - it knows about deal animations and the face
 * down hole card - and this folds each dealt card in exactly once.  A
 * reshuffled shoe restarts the count on its own.
 */
typedef struct {
    int running;
    int counted;        /* dealt-card prefix already folded in */
    int hole_done;      /* dealt index of the hole card counted, -1 = none */
} bj_count_t;

void bj_count_reset(bj_count_t *c);
/*
 * `visible` is how many dealt cards are on the felt, in deal order.
 * `hole` is the dealt index of the dealer's face-down card (-1 when there
 * is none); it is skipped until `hole_shown`, then counted exactly once.
 */
void   bj_count_update(bj_count_t *c, const bj_session_t *s, int visible,
                       int hole, bool hole_shown);
double bj_decks_left(const bj_session_t *s);
double bj_true_count(const bj_count_t *c, const bj_session_t *s);

const char *bj_result_word(bj_result_t r);
const char *bj_action_word(bj_action_t a);
/* Format half-credits as credits, e.g. "1000" or "1007.5". */
void bj_credits(char *buf, size_t len, long units);

#endif
