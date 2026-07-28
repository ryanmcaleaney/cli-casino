#ifndef CASINO_CARIBBEANSTUD_H
#define CASINO_CARIBBEANSTUD_H

#include <stdbool.h>
#include <stddef.h>

#include "cards.h"
#include "cli.h"
#include "poker.h"
#include "rng.h"

int  caribbeanstud_run(const cli_t *cli, rng_t *rng);
void caribbeanstud_list_bets(void);

/* ---- rule profile ------------------------------------------------------- */

#define CS_CARDS          5
#define CS_ANTE_MIN       1
#define CS_ANTE_MAX       500
#define CS_ANTE_DEFAULT   25
#define CS_RAISE_MULT     2         /* the raise is always twice the ante */
#define CS_BANKROLL_START 1000

/*
 * The dealer qualifies with ace-king high or better: any made hand, or a
 * high-card hand holding both an ace and a king.  A-K-9-5-3 qualifies,
 * A-Q-J-9-4 does not.  This is the only rule in the game that generic
 * poker ranking cannot express, so it gets its own predicate; poker.c is
 * left alone.
 */
bool cs_dealer_qualifies(const card_t dealer[CS_CARDS]);

/* Profit per 1 unit of the raise wager (the stake comes back on top).
 * The ante always pays 1:1; only the raise uses this table. */
int  cs_raise_multiplier(poker_cat_t cat);

/* The raise wager that goes with an ante.  Frontends ask, they never
 * multiply. */
long cs_raise_amount(long ante);

/*
 * A ranked five-card hand.  The category comes from the shared evaluator
 * in poker.c; the only thing added here is the kicker order needed to
 * settle player against dealer.  `rank` holds the comparison ranks high
 * to low with the ace worth 14 (grouped first by how many cards share a
 * rank, so quads, trips and pairs lead their kickers), except in the
 * 5-4-3-2-A straight where the ace plays low.  `key` folds the category
 * and all five ranks into one comparable value.
 */
typedef struct {
    poker_cat_t cat;
    int         rank[CS_CARDS];
    long        key;
} cs_eval_t;

cs_eval_t cs_eval(const card_t hand[CS_CARDS]);
/* <0, 0 or >0 as hand a ranks below, equal to or above hand b. */
int       cs_compare(const cs_eval_t *a, const cs_eval_t *b);

/* ---- one round ---------------------------------------------------------- */

typedef enum { CS_ACT_ASK, CS_ACT_RAISE, CS_ACT_FOLD } cs_action_t;

typedef enum {
    CS_OUT_FOLD,            /* folded: the ante is lost */
    CS_OUT_NO_QUALIFY,      /* dealer below ace-king: ante pays, raise pushes */
    CS_OUT_PLAYER,          /* player beat a qualifying dealer */
    CS_OUT_DEALER,          /* dealer won */
    CS_OUT_PUSH             /* equal hands: both wagers push */
} cs_outcome_t;

/*
 * One round.  Money is whole credits: every payout is an integer multiple
 * of a wager, so no fractional accounting is needed.  The per-wager
 * fields are NET (a lost wager is negative).
 *
 * `raise` is 0 until the player actually raises, so a folded round never
 * stakes more than the ante.  `dealer_revealed` is the single source of
 * truth for what a frontend may show: before the decision only the
 * up-card (dealer[0]) is public.
 */
typedef struct {
    long         ante, raise;
    card_t       player[CS_CARDS], dealer[CS_CARDS];
    cs_eval_t    pev, dev;
    cs_action_t  action;
    bool         dealer_revealed;
    bool         dealer_qualifies;
    cs_outcome_t outcome;
    long         ante_net, raise_net;
    long         wagered, returned;
    bool         settled;
} cs_round_t;

/*
 * Deal from a freshly shuffled single deck.  The cards alternate, player
 * first: player 1, dealer 1, player 2, dealer 2, ... player 5, dealer 5.
 * The dealer's first card is the up-card.  The wager is recorded but the
 * round is not resolved until cs_round_settle().
 */
void cs_round_deal(cs_round_t *r, rng_t *rng, long ante);
/* The same round from fixed cards: the player's five then the dealer's
 * five (the deal:... hook and the self-tests). */
void cs_round_deal_fixed(cs_round_t *r, const card_t player[CS_CARDS],
                         const card_t dealer[CS_CARDS], long ante);
/* Resolve the round.  This is the only place the game settles money; the
 * CLI and the GUI both come through here. */
void cs_round_settle(cs_round_t *r, cs_action_t action);

/* The dealer card at i, or NULL while it is still face down: frontends
 * can only draw what the round has actually turned over. */
const card_t *cs_dealer_visible(const cs_round_t *r, int i);

/* ---- read-only rules and names for frontends ---------------------------- */

const char *cs_front_cat_name(poker_cat_t c);    /* "Full House" */
const char *cs_front_cat_token(poker_cat_t c);   /* "FULL_HOUSE" */
const char *cs_front_cat_json(poker_cat_t c);    /* "full_house" */
const char *cs_front_outcome_word(cs_outcome_t o);
/* Credits as text, e.g. "25", "+25", "-25" ("0" is never signed). */
void        cs_front_credits(char *buf, size_t len, long v, bool sign);

/* ---- session (bankroll and wager plumbing live here, not in a frontend) - */

typedef enum {
    CS_PHASE_BET,        /* between rounds: set the ante, deal */
    CS_PHASE_DECISION,   /* five cards dealt, waiting on raise or fold */
    CS_PHASE_SETTLED     /* resolved and paid */
} cs_phase_t;

typedef struct {
    long       bankroll;
    long       ante;                /* wager for the next round */
    cs_round_t round;
    cs_phase_t phase;
} cs_session_t;

void cs_session_start(cs_session_t *s);
/*
 * Clamped to the table limits and to what the bankroll can actually
 * cover: a round always has to fund the ante AND the 2x raise behind it,
 * so the ceiling is a third of the bankroll.  The ante never moves while
 * a decision is pending.
 */
void cs_set_ante(cs_session_t *s, long v);
/* The most a round can cost: ante + raise, all of it staked up front. */
long cs_max_exposure(const cs_session_t *s);
bool cs_can_deal(const cs_session_t *s);
void cs_deal(cs_session_t *s, rng_t *rng);
/* The same deal from fixed cards (the deal:... hook). */
void cs_deal_fixed(cs_session_t *s, const card_t player[CS_CARDS],
                   const card_t dealer[CS_CARDS]);
bool cs_can_raise(const cs_session_t *s);
bool cs_can_fold(const cs_session_t *s);
void cs_decide(cs_session_t *s, cs_action_t action);
/* Fresh money between rounds: the bankroll becomes `credits` and the ante
 * is re-clamped against it.  A CLI session buys in for whatever one round
 * at its ante costs; the GUI uses the standard stake below. */
void cs_buy_in(cs_session_t *s, long credits);
void cs_bankroll_reset(cs_session_t *s);    /* cs_buy_in(CS_BANKROLL_START) */

#endif
