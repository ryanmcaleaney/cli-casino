#ifndef CASINO_THREECARD_H
#define CASINO_THREECARD_H

#include <stdbool.h>
#include <stddef.h>

#include "cards.h"
#include "cli.h"
#include "rng.h"

int  threecard_run(const cli_t *cli, rng_t *rng);
void threecard_list_bets(void);

/* ---- rule profile ------------------------------------------------------- */

#define TC_CARDS         3
#define TC_ANTE_MIN      1
#define TC_ANTE_MAX      500
#define TC_ANTE_DEFAULT  25
#define TC_BANKROLL_START 1000      /* GUI session only */

/*
 * Three-card hand categories, ordered low to high.  This is not the
 * five-card order: with only three cards a straight is rarer than a
 * flush, so STRAIGHT outranks FLUSH.  The five-card evaluator in
 * poker.c is left alone; this game has its own.
 */
typedef enum {
    TC_HIGH_CARD, TC_PAIR, TC_FLUSH, TC_STRAIGHT, TC_THREE_OF_A_KIND,
    TC_STRAIGHT_FLUSH
} tc_cat_t;
#define TC_NCATS 6

/*
 * An evaluated hand.  `rank` holds the comparison ranks high to low with
 * the ace worth 14 (a pair repeats its own rank, then the kicker), so two
 * hands of the same category compare on rank alone.  `key` folds the
 * category and all three ranks into one comparable value.
 */
typedef struct {
    tc_cat_t cat;
    int      rank[TC_CARDS];
    long     key;
} tc_eval_t;

/* The dealer plays with queen-high or better. */
#define TC_QUALIFY_RANK 12

typedef enum { TC_ACT_ASK, TC_ACT_PLAY, TC_ACT_FOLD } tc_action_t;

typedef enum {
    TC_OUT_FOLD,            /* folded: the ante is lost */
    TC_OUT_NO_QUALIFY,      /* dealer below queen-high: ante pays, play pushes */
    TC_OUT_PLAYER,          /* player beat a qualifying dealer */
    TC_OUT_DEALER,          /* dealer won */
    TC_OUT_PUSH             /* equal hands: both wagers push */
} tc_outcome_t;

/*
 * One round.  Money is whole credits; every payout in this game is an
 * integer multiple of a wager, so no fractional accounting is needed.
 * The per-wager fields are NET (a lost wager is negative); ante_bonus is
 * a pure bonus payout that rides on the ante without being wagered.
 */
typedef struct {
    long        ante, pairplus, play;   /* wagers placed this round */
    card_t      player[TC_CARDS], dealer[TC_CARDS];
    tc_eval_t   pev, dev;
    tc_action_t action;
    bool        dealer_qualifies;
    tc_outcome_t outcome;
    long        ante_net, play_net, ante_bonus, pairplus_net;
    long        wagered, returned;
    bool        settled;
} tc_round_t;

/* ---- engine: evaluation, comparison and settlement --------------------- */

tc_eval_t tc_eval(const card_t hand[TC_CARDS]);
/* <0, 0 or >0 as hand a ranks below, equal to or above hand b. */
int       tc_compare(const tc_eval_t *a, const tc_eval_t *b);
bool      tc_qualifies(const tc_eval_t *dealer);

/* Deal from a freshly shuffled single deck; the wagers are recorded but
 * the round is not resolved until tc_round_settle(). */
void tc_round_deal(tc_round_t *r, rng_t *rng, long ante, long pairplus);
/* The same round from fixed cards (the deal:... hook and self-tests). */
void tc_round_deal_fixed(tc_round_t *r, const card_t player[TC_CARDS],
                         const card_t dealer[TC_CARDS], long ante,
                         long pairplus);
/* Resolve ante, play, ante bonus and pair plus.  This is the only place
 * the game settles money; the CLI and the GUI both come through here. */
void tc_round_settle(tc_round_t *r, tc_action_t action);

/* ---- read-only rules and pay tables for frontends ---------------------- */

const char *tc_front_cat_name(tc_cat_t c);    /* "Three of a Kind" */
const char *tc_front_cat_token(tc_cat_t c);   /* "THREE_OF_A_KIND" */
int  tc_front_ante_bonus(tc_cat_t c);         /* per 1 unit of ante, 0 = none */
int  tc_front_pairplus(tc_cat_t c);           /* per 1 unit, 0 = loses */
/* Credits as text, e.g. "25", "+25", "-25" ("0" is never signed). */
void tc_front_credits(char *buf, size_t len, long v, bool sign);
const char *tc_front_outcome_word(tc_outcome_t o);

/* ---- GUI session (bankroll and wager plumbing live here, not in the GUI) */

typedef enum {
    TC_PHASE_BET,        /* between rounds: set wagers, deal */
    TC_PHASE_DECISION,   /* three cards dealt, waiting on play or fold */
    TC_PHASE_SETTLED     /* resolved and paid */
} tc_phase_t;

typedef struct {
    long       bankroll;
    long       ante, pairplus;      /* wagers for the next round */
    tc_round_t round;
    tc_phase_t phase;
} tc_session_t;

void tc_session_start(tc_session_t *s);
void tc_set_ante(tc_session_t *s, long v);      /* clamped */
void tc_set_pairplus(tc_session_t *s, long v);  /* clamped, 0 = no bet */
bool tc_can_deal(const tc_session_t *s);
void tc_deal(tc_session_t *s, rng_t *rng);
bool tc_can_play(const tc_session_t *s);        /* bankroll funds the play */
void tc_decide(tc_session_t *s, tc_action_t action);
void tc_bankroll_reset(tc_session_t *s);

#endif
