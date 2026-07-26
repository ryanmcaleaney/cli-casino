#ifndef CASINO_LETITRIDE_H
#define CASINO_LETITRIDE_H

#include <stdbool.h>
#include <stddef.h>

#include "cards.h"
#include "cli.h"
#include "rng.h"

int  letitride_run(const cli_t *cli, rng_t *rng);
void letitride_list_bets(void);

/* ---- rule profile ------------------------------------------------------- */

#define LIR_BETS         3          /* bet 1, bet 2, bet 3 */
#define LIR_PLAYER_CARDS 3
#define LIR_COMMUNITY    2
#define LIR_HAND         (LIR_PLAYER_CARDS + LIR_COMMUNITY)

#define LIR_BET_MIN       1
#define LIR_BET_MAX     500
#define LIR_BET_DEFAULT  25
#define LIR_BANKROLL_START 1000     /* GUI session only */

/*
 * Paying categories, low to high.  This is the five-card poker ladder with
 * one Let It Ride twist: the qualifying pair is TENS or better, not the
 * jacks-or-better used by video poker, so a pair splits into two
 * categories here.  The hand itself is ranked by the shared evaluator in
 * poker.c; only the pay classification lives in this game.
 */
typedef enum {
    LIR_NOTHING,            /* below a pair of tens: every riding bet loses */
    LIR_PAIR_TENS,          /* pair of tens, jacks, queens, kings or aces */
    LIR_TWO_PAIR,
    LIR_THREE_OF_A_KIND,
    LIR_STRAIGHT,
    LIR_FLUSH,
    LIR_FULL_HOUSE,
    LIR_FOUR_OF_A_KIND,
    LIR_STRAIGHT_FLUSH,
    LIR_ROYAL_FLUSH
} lir_cat_t;
#define LIR_NCATS 10

/* Where a round has got to.  A decision belongs to the bet of the same
 * number; bet 3 is never offered and never withdrawn. */
typedef enum {
    LIR_STAGE_DECISION1,    /* three player cards are up */
    LIR_STAGE_DECISION2,    /* the first community card is up */
    LIR_STAGE_DONE          /* both community cards up, hand settled */
} lir_stage_t;

/*
 * One round.  Money is whole credits: every payout is an integer multiple
 * of a wager, so no fractional accounting is needed.
 *
 * The three wagers are staked together at the deal.  A pulled wager is
 * handed straight back and never gambles, so `wagered` counts only what
 * was still riding at the showdown and `returned` only what those riding
 * wagers brought back; `pulled_back` is reported separately.  Net is
 * always returned - wagered.
 */
typedef struct {
    long        bet;                        /* each of the three wagers */
    card_t      player[LIR_PLAYER_CARDS];
    card_t      community[LIR_COMMUNITY];
    bool        revealed[LIR_COMMUNITY];    /* face up yet? */
    bool        riding[LIR_BETS];           /* still at risk */
    lir_stage_t stage;
    lir_cat_t   cat;                        /* valid once settled */
    int         nriding;
    long        committed;                  /* LIR_BETS * bet, at the deal */
    long        pulled_back;                /* stakes handed back on a pull */
    long        wagered;                    /* nriding * bet: what gambled */
    long        returned;                   /* stake + profit on winners */
    bool        settled;
} lir_round_t;

/* ---- engine ------------------------------------------------------------- */

/* Classify a five-card hand for the pay table (tens or better). */
lir_cat_t lir_classify(const card_t hand[LIR_HAND]);

void lir_round_deal(lir_round_t *r, rng_t *rng, long bet);
/* The same round from fixed cards: player's three then the two community
 * cards (the deal:... hook and the self-tests). */
void lir_round_deal_fixed(lir_round_t *r, const card_t player[LIR_PLAYER_CARDS],
                          const card_t community[LIR_COMMUNITY], long bet);
/*
 * Resolve the current decision: pull withdraws that stage's wager, else it
 * rides.  Reveals the matching community card and advances; the second
 * decision also evaluates and settles the hand.  A no-op once done.
 */
void lir_round_decide(lir_round_t *r, bool pull);

/* The community card at i, or NULL until its reveal stage: frontends can
 * only draw what the round has actually turned over. */
const card_t *lir_community_visible(const lir_round_t *r, int i);

/* The five-card hand, valid once both community cards are up. */
void lir_round_hand(const lir_round_t *r, card_t out[LIR_HAND]);

/* ---- read-only pay table for frontends ---------------------------------- */

const char *lir_front_cat_name(lir_cat_t c);    /* "Full House" */
const char *lir_front_cat_token(lir_cat_t c);   /* "FULL_HOUSE" */
int  lir_front_payout(lir_cat_t c);             /* profit per unit staked */
/* Credits as text, e.g. "25", "+25", "-25" ("0" is never signed). */
void lir_front_credits(char *buf, size_t len, long v, bool sign);

/* ---- GUI session (bankroll and wager plumbing, not in the GUI) ---------- */

typedef enum {
    LIR_PHASE_BET,
    LIR_PHASE_DECISION,     /* a decision is pending; see round.stage */
    LIR_PHASE_SETTLED
} lir_phase_t;

typedef struct {
    long        bankroll;
    long        bet;                /* each wager for the next round */
    lir_round_t round;
    lir_phase_t phase;
} lir_session_t;

void lir_session_start(lir_session_t *s);
void lir_set_bet(lir_session_t *s, long v);     /* clamped */
bool lir_can_deal(const lir_session_t *s);
void lir_deal(lir_session_t *s, rng_t *rng);
void lir_decide(lir_session_t *s, bool pull);
void lir_bankroll_reset(lir_session_t *s);

#endif
