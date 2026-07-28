#include "blackjack.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bj_strategy.h"
#include "cardart.h"
#include "cards.h"
#include "output.h"

#ifdef CASINO_GUI
#include "gui/blackjack_gui.h"
#endif

/* ======================================================================
 * Engine
 * ==================================================================== */

int bj_total(const card_t *cards, int n)
{
    int  sum = 0;
    bool ace = false;

    for (int i = 0; i < n; i++) {
        int r = cards[i].rank;
        if (r == 1)
            ace = true;
        sum += r > 10 ? 10 : r;
    }
    /* at most one ace can ever count as eleven */
    if (ace && sum + 10 <= 21)
        sum += 10;
    return sum;
}

bool bj_soft(const card_t *cards, int n)
{
    int  sum = 0;
    bool ace = false;

    for (int i = 0; i < n; i++) {
        int r = cards[i].rank;
        if (r == 1)
            ace = true;
        sum += r > 10 ? 10 : r;
    }
    return ace && sum + 10 <= 21;
}

/* A natural is 21 on the first two cards of a hand that was not split. */
bool bj_natural(const bj_hand_t *h)
{
    return h->n == 2 && !h->from_split && bj_total(h->cards, 2) == 21;
}

static int card_ten_value(card_t c)
{
    return c.rank > 10 ? 10 : c.rank;
}

int bj_remaining(const bj_session_t *s)
{
    return shoe_remaining(&s->shoe);
}

/* ---- Hi-Lo card counting (see blackjack.h) ----------------------------- */

int bj_hilo(card_t c)
{
    if (c.rank >= 2 && c.rank <= 6)
        return 1;
    if (c.rank >= 7 && c.rank <= 9)
        return 0;
    if (c.rank == 1 || (c.rank >= 10 && c.rank <= 13))
        return -1;                  /* ten, court card or ace */
    return 0;                       /* not a card */
}

int bj_dealt_count(const bj_session_t *s)
{
    return s->shoe.pos;
}

card_t bj_dealt_card(const bj_session_t *s, int i)
{
    if (i < 0 || i >= s->shoe.pos)
        return (card_t){ 0, 0 };
    return s->shoe.cards[i];
}

int bj_round_cards(const bj_session_t *s)
{
    const bj_round_t *r = &s->round;
    int n = r->ndealer;

    /* a split moves a card between hands, it never draws one, so the
     * hands and the dealer hold every card this round has taken */
    for (int i = 0; i < r->nhands; i++)
        n += r->hands[i].n;
    return n;
}

int bj_hole_index(const bj_session_t *s)
{
    if (s->round.ndealer < 2)
        return -1;                      /* no round on the felt */
    return bj_dealt_count(s) - bj_round_cards(s) + 3;
}

void bj_count_reset(bj_count_t *c)
{
    c->running = 0;
    c->counted = 0;
    c->hole_done = -1;
}

void bj_count_update(bj_count_t *c, const bj_session_t *s, int visible,
                     int hole, bool hole_shown)
{
    int dealt = bj_dealt_count(s);

    /* a reshuffle puts the shoe back to the start: those cards are gone */
    if (c->counted > dealt)
        bj_count_reset(c);
    if (visible > dealt)
        visible = dealt;

    /* the prefix only ever moves forward, so no card is counted twice */
    while (c->counted < visible) {
        int i = c->counted++;
        if (i == hole)
            continue;               /* face down: counted when it turns */
        c->running += bj_hilo(bj_dealt_card(s, i));
    }

    if (hole_shown && hole >= 0 && hole < dealt && c->hole_done != hole) {
        c->running += bj_hilo(bj_dealt_card(s, hole));
        c->hole_done = hole;
    }
}

double bj_decks_left(const bj_session_t *s)
{
    return bj_remaining(s) / 52.0;
}

double bj_true_count(const bj_count_t *c, const bj_session_t *s)
{
    double decks = bj_decks_left(s);

    /* the engine reshuffles at the cut card, so this never divides by a
     * sliver of a deck in practice; an exhausted shoe still cannot fault */
    return decks > 0.0 ? c->running / decks : (double)c->running;
}

/* ---- Hi-Lo true-count bet ramp (see blackjack.h) ----------------------- */

/* The ramp: step -> multiple of the base unit. */
static const long RAMP_MULT[BJ_RAMP_STEPS] = { 1, 2, 4, 6, 8 };

/*
 * Which step a true count sits on.  The count is used as it comes out of
 * the division, not rounded to the one decimal the GUI displays, so +0.99
 * is still the bottom step and +1.0 is the second.
 */
static int ramp_step(double true_count)
{
    if (true_count < 1.0)
        return 0;
    if (true_count < 2.0)
        return 1;
    if (true_count < 3.0)
        return 2;
    if (true_count < 4.0)
        return 3;
    return 4;
}

long bj_count_bet_units(double true_count)
{
    return RAMP_MULT[ramp_step(true_count)];
}

void bj_count_bet(const bj_session_t *s, const bj_count_t *c, long unit,
                  bj_bet_plan_t *out)
{
    long want;

    /* the unit is a legal wager in its own right, which also keeps the
     * widest spread inside long arithmetic */
    if (unit < BJ_BET_MIN)
        unit = BJ_BET_MIN;
    if (unit > BJ_BET_MAX)
        unit = BJ_BET_MAX;

    out->true_count = bj_true_count(c, s);
    out->step = ramp_step(out->true_count);
    out->units = RAMP_MULT[out->step];
    out->capped_table = false;
    out->capped_bankroll = false;

    want = unit * out->units;
    if (want > BJ_BET_MAX) {
        want = BJ_BET_MAX;              /* the table tops out first */
        out->capped_table = true;
    }
    if (want > s->bankroll) {
        /* the most the money on the table can cover, in whole credits */
        want = s->bankroll - s->bankroll % BJ_HALF;
        out->capped_bankroll = true;
    }
    if (want < BJ_BET_MIN)
        want = BJ_BET_MIN;              /* a re-buy is due before this bets */
    out->wager = want;
}

const char *bj_result_word(bj_result_t r)
{
    static const char *const W[] = {
        "PENDING", "WIN", "LOSS", "PUSH", "BLACKJACK", "SURRENDER"
    };
    return W[r];
}

const char *bj_action_word(bj_action_t a)
{
    static const char *const W[] = {
        "hit", "stand", "double", "split", "surrender"
    };
    return W[a];
}

void bj_credits(char *buf, size_t len, long units)
{
    long a = units < 0 ? -units : units;
    const char *sign = units < 0 ? "-" : "";

    if (a % BJ_HALF == 0)
        snprintf(buf, len, "%s%ld", sign, a / BJ_HALF);
    else
        snprintf(buf, len, "%s%ld.5", sign, a / BJ_HALF);
}

void bj_session_start(bj_session_t *s, rng_t *rng)
{
    memset(s, 0, sizeof *s);
    shoe_init(&s->shoe, BJ_DECKS);
    shoe_shuffle(&s->shoe, rng);
    s->bankroll = BJ_BANKROLL_START;
    s->base_bet = BJ_BET_DEFAULT;
    s->round.phase = BJ_PHASE_BET;
}

/* The table sits between rounds in two phases: before the first deal, and
 * while the settled result of the last round is still on display.  Betting,
 * re-buying and dealing are all allowed in either; bj_deal() clears the
 * round, so the next hand starts just as cleanly from a settled one. */
static bool between_rounds(const bj_session_t *s)
{
    return s->round.phase == BJ_PHASE_BET ||
           s->round.phase == BJ_PHASE_SETTLED;
}

/* A re-buy deliberately leaves the shoe untouched so a player keeping a
 * count is not reset by it; only penetration reshuffles the shoe. */
void bj_bankroll_reset(bj_session_t *s)
{
    if (between_rounds(s))
        s->bankroll = BJ_BANKROLL_START;
}

void bj_set_bet(bj_session_t *s, long units)
{
    if (!between_rounds(s))
        return;
    if (units < BJ_BET_MIN)
        units = BJ_BET_MIN;
    if (units > BJ_BET_MAX)
        units = BJ_BET_MAX;
    s->base_bet = units;
}

bool bj_can_deal(const bj_session_t *s)
{
    return between_rounds(s) && s->bankroll >= s->base_bet;
}

static card_t bj_draw(bj_session_t *s)
{
    return shoe_draw(&s->shoe);
}

static void hand_add(bj_hand_t *h, card_t c)
{
    if (h->n >= BJ_MAX_CARDS) {
        fprintf(stderr, "blackjack: hand overflow\n");
        exit(70);
    }
    h->cards[h->n++] = c;
}

static void dealer_add(bj_round_t *r, card_t c)
{
    if (r->ndealer >= BJ_MAX_CARDS) {
        fprintf(stderr, "blackjack: dealer hand overflow\n");
        exit(70);
    }
    r->dealer[r->ndealer++] = c;
}

static void settle(bj_session_t *s);
static void advance(bj_session_t *s, rng_t *rng);

/* Reshuffle only between rounds, once the cut card is reached.  Doing it
 * as its own step lets a frontend settle its count and size its wager
 * against the shoe the round will actually be dealt from; the `prepared`
 * flag makes the extra call idempotent for everyone else. */
bool bj_prepare_round(bj_session_t *s, rng_t *rng)
{
    if (!between_rounds(s))
        return false;
    if (s->prepared)
        return s->shuffled;

    s->shuffled = false;
    if (shoe_remaining(&s->shoe) <= BJ_RESHUFFLE_AT) {
        shoe_init(&s->shoe, BJ_DECKS);
        shoe_shuffle(&s->shoe, rng);
        s->shuffled = true;
    }
    s->prepared = true;
    return s->shuffled;
}

/* Dealer peeks on an ace or ten-value upcard. */
static void peek_and_open(bj_session_t *s, rng_t *rng)
{
    bj_round_t *r = &s->round;
    card_t up = r->dealer[0];

    if (up.rank == 1 || card_ten_value(up) == 10) {
        if (bj_total(r->dealer, 2) == 21) {
            r->dealer_bj = true;
            r->hole_hidden = false;
            settle(s);
            return;
        }
    }
    r->phase = BJ_PHASE_PLAYER;
    if (bj_natural(&r->hands[0]))
        r->hands[0].done = true;
    advance(s, rng);
}

void bj_deal(bj_session_t *s, rng_t *rng)
{
    bj_round_t *r = &s->round;

    if (!bj_can_deal(s))
        return;

    bj_prepare_round(s, rng);
    s->prepared = false;            /* this round consumes the preparation */

    memset(r, 0, sizeof *r);
    r->nhands = 1;
    r->hands[0].wager = s->base_bet;
    s->bankroll -= s->base_bet;
    r->wagered = s->base_bet;
    r->hole_hidden = true;

    /* real dealing order: player, dealer, player, dealer hole */
    hand_add(&r->hands[0], bj_draw(s));
    dealer_add(r, bj_draw(s));
    hand_add(&r->hands[0], bj_draw(s));
    dealer_add(r, bj_draw(s));

    if (r->dealer[0].rank == 1 && s->bankroll >= s->base_bet / 2) {
        r->insurance_offered = true;
        r->phase = BJ_PHASE_INSURANCE;
        return;
    }
    peek_and_open(s, rng);
}

bool bj_insurance_pending(const bj_session_t *s)
{
    return s->round.phase == BJ_PHASE_INSURANCE;
}

void bj_insurance(bj_session_t *s, bool take, rng_t *rng)
{
    bj_round_t *r = &s->round;

    if (r->phase != BJ_PHASE_INSURANCE)
        return;
    if (take) {
        long stake = s->base_bet / 2;    /* half the main wager */
        if (s->bankroll >= stake) {
            r->insurance = stake;
            s->bankroll -= stake;
            r->wagered += stake;
        }
    }
    peek_and_open(s, rng);
}

/* The dealer draws only if some player hand can still be beaten. */
static void dealer_play(bj_session_t *s)
{
    bj_round_t *r = &s->round;
    bool live = false;

    r->hole_hidden = false;
    for (int i = 0; i < r->nhands; i++) {
        const bj_hand_t *h = &r->hands[i];
        if (!h->surrendered && bj_total(h->cards, h->n) <= 21)
            live = true;
    }
    if (!live)
        return;
    while (bj_total(r->dealer, r->ndealer) < 17)   /* S17 */
        dealer_add(r, bj_draw(s));
}

static void settle(bj_session_t *s)
{
    bj_round_t *r = &s->round;
    int dt = bj_total(r->dealer, r->ndealer);

    if (r->insurance > 0) {
        if (r->dealer_bj) {
            r->insurance_won = true;
            r->returned += r->insurance * 3;   /* stake + 2:1 */
            s->bankroll += r->insurance * 3;
        }
    }

    for (int i = 0; i < r->nhands; i++) {
        bj_hand_t *h = &r->hands[i];
        int pt = bj_total(h->cards, h->n);

        if (h->surrendered) {
            h->result = BJ_SURRENDERED;
            h->payout = h->wager / 2;
        } else if (r->dealer_bj) {
            /* nothing was played yet: naturals push, everything else loses */
            if (bj_natural(h)) {
                h->result = BJ_PUSH;
                h->payout = h->wager;
            } else {
                h->result = BJ_LOSS;
                h->payout = 0;
            }
        } else if (pt > 21) {
            h->result = BJ_LOSS;
            h->payout = 0;
        } else if (bj_natural(h)) {
            h->result = BJ_BLACKJACK;
            h->payout = h->wager * 5 / 2;      /* wager + 3:2, exact */
        } else if (dt > 21 || pt > dt) {
            h->result = BJ_WIN;
            h->payout = h->wager * 2;
        } else if (pt < dt) {
            h->result = BJ_LOSS;
            h->payout = 0;
        } else {
            h->result = BJ_PUSH;
            h->payout = h->wager;
        }
        r->returned += h->payout;
        s->bankroll += h->payout;
    }
    r->phase = BJ_PHASE_SETTLED;
}

/* Move to the next hand needing a decision; play out the dealer and
 * settle once every hand is finished. */
static void advance(bj_session_t *s, rng_t *rng)
{
    bj_round_t *r = &s->round;

    for (;;) {
        while (r->active < r->nhands && r->hands[r->active].done)
            r->active++;
        if (r->active >= r->nhands)
            break;

        bj_hand_t *h = &r->hands[r->active];
        if (h->n == 1) {                 /* fresh half of a split */
            hand_add(h, bj_draw(s));
            if (h->split_aces || bj_total(h->cards, h->n) == 21) {
                h->done = true;          /* split aces get exactly one card */
                continue;
            }
        }
        return;                          /* waiting for a player action */
    }

    (void)rng;
    dealer_play(s);
    settle(s);
}

bool bj_legal(const bj_session_t *s, bj_action_t a)
{
    const bj_round_t *r = &s->round;

    if (r->phase != BJ_PHASE_PLAYER || r->active >= r->nhands)
        return false;

    const bj_hand_t *h = &r->hands[r->active];
    if (h->done)
        return false;

    switch (a) {
    case BJ_HIT:
    case BJ_STAND:
        return true;
    case BJ_DOUBLE:
        /* any initial two cards, including after a split (DAS), but
         * never on split aces */
        return h->n == 2 && !h->split_aces && s->bankroll >= h->wager;
    case BJ_SPLIT:
        return h->n == 2 && !h->split_aces &&
               r->nhands < BJ_MAX_HANDS &&
               h->cards[0].rank == h->cards[1].rank &&
               s->bankroll >= h->wager;
    case BJ_SURRENDER:
        /* late surrender, first decision on the unsplit starting hand */
        return r->nhands == 1 && h->n == 2 && !h->from_split && !h->doubled;
    }
    return false;
}

void bj_act(bj_session_t *s, bj_action_t a, rng_t *rng)
{
    bj_round_t *r = &s->round;

    if (!bj_legal(s, a))
        return;

    bj_hand_t *h = &r->hands[r->active];

    switch (a) {
    case BJ_HIT:
        hand_add(h, bj_draw(s));
        if (bj_total(h->cards, h->n) >= 21)
            h->done = true;
        break;
    case BJ_STAND:
        h->done = true;
        break;
    case BJ_DOUBLE:
        s->bankroll -= h->wager;
        r->wagered += h->wager;
        h->wager *= 2;
        h->doubled = true;
        hand_add(h, bj_draw(s));
        h->done = true;
        break;
    case BJ_SPLIT: {
        bool aces = h->cards[0].rank == 1;
        card_t moved = h->cards[1];

        s->bankroll -= h->wager;
        r->wagered += h->wager;

        for (int i = r->nhands; i > r->active + 1; i--)
            r->hands[i] = r->hands[i - 1];
        r->nhands++;

        bj_hand_t *nh = &r->hands[r->active + 1];
        memset(nh, 0, sizeof *nh);
        nh->cards[0] = moved;
        nh->n = 1;
        nh->wager = h->wager;
        nh->from_split = true;
        nh->split_aces = aces;

        h->n = 1;
        h->from_split = true;
        h->split_aces = aces;
        break;
    }
    case BJ_SURRENDER:
        h->surrendered = true;
        h->done = true;
        break;
    }
    advance(s, rng);
}

/* ======================================================================
 * CLI
 * ==================================================================== */

#define SC_MAX 64

typedef enum {
    SC_HIT, SC_STAND, SC_DOUBLE, SC_SPLIT, SC_SURRENDER, SC_INS, SC_NOINS
} sc_act_t;

typedef struct {
    sc_act_t acts[SC_MAX];
    int      n;
    int      pos;
} bj_script_t;

static int action_parse(const char *w, sc_act_t *out)
{
    static const struct { const char *w; sc_act_t a; } WORDS[] = {
        { "h", SC_HIT }, { "hit", SC_HIT },
        { "s", SC_STAND }, { "stand", SC_STAND },
        { "d", SC_DOUBLE }, { "double", SC_DOUBLE },
        { "p", SC_SPLIT }, { "sp", SC_SPLIT }, { "split", SC_SPLIT },
        { "r", SC_SURRENDER }, { "sur", SC_SURRENDER },
        { "surrender", SC_SURRENDER },
        { "i", SC_INS }, { "ins", SC_INS }, { "insurance", SC_INS },
        { "noins", SC_NOINS }, { "noinsurance", SC_NOINS },
    };
    for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++) {
        if (strcasecmp(WORDS[i].w, w) == 0) {
            *out = WORDS[i].a;
            return 0;
        }
    }
    return -1;
}

static int script_build(const cli_t *cli, bj_script_t *sc, long *bet)
{
    sc->n = sc->pos = 0;
    *bet = BJ_BET_DEFAULT;

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];

        if (bet_is(b, "bet")) {
            if (b->nvalues != 1 || b->values[0] < BJ_BET_MIN / BJ_HALF ||
                b->values[0] > BJ_BET_MAX / BJ_HALF) {
                fprintf(stderr, "blackjack: bet '%s': wager must be %d-%d "
                                "credits\n", b->raw, BJ_BET_MIN / BJ_HALF,
                        BJ_BET_MAX / BJ_HALF);
                return 2;
            }
            *bet = (long)b->values[0] * BJ_HALF;
            continue;
        }
        if (bet_has_value(b)) {
            fprintf(stderr, "blackjack: action '%s' takes no value\n",
                    b->raw);
            return 2;
        }

        const char *p = b->name;
        for (;;) {
            const char *end = strchr(p, ',');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            char word[16];
            sc_act_t a;

            if (len == 0 || len >= sizeof word) {
                fprintf(stderr, "blackjack: bad action list '%s'\n", b->raw);
                return 2;
            }
            memcpy(word, p, len);
            word[len] = '\0';
            if (action_parse(word, &a) < 0) {
                fprintf(stderr, "blackjack: unknown action '%s' (valid: "
                                "hit, stand, double, split, surrender, "
                                "insurance, noinsurance)\n", word);
                return 2;
            }
            if (sc->n >= SC_MAX) {
                fprintf(stderr, "blackjack: too many actions (max %d)\n",
                        SC_MAX);
                return 2;
            }
            sc->acts[sc->n++] = a;
            if (!end)
                break;
            p = end + 1;
        }
    }
    return 0;
}

/* ---- display ------------------------------------------------------------ */

static void show_cards(FILE *f, const card_t *cards, int n, bool hide_hole)
{
    char buf[8];

    if (cardart_enabled(f)) {
        bool hidden[BJ_MAX_CARDS] = { false };
        if (hide_hole && n > 1)
            hidden[1] = true;
        cardart_hand(f, cards, hidden, n);
        return;
    }
    for (int i = 0; i < n; i++) {
        if (hide_hole && i == 1) {
            fprintf(f, " ??");
            continue;
        }
        card_name(cards[i], buf, sizeof buf);
        fprintf(f, " %s", buf);
    }
}

static void print_dealer(FILE *f, const bj_round_t *r)
{
    if (cardart_enabled(f)) {
        if (r->hole_hidden)
            fprintf(f, "Dealer:\n");
        else
            fprintf(f, "Dealer (%d):\n", bj_total(r->dealer, r->ndealer));
        show_cards(f, r->dealer, r->ndealer, r->hole_hidden);
        return;
    }
    fprintf(f, "Dealer:");
    show_cards(f, r->dealer, r->ndealer, r->hole_hidden);
    if (r->hole_hidden)
        fprintf(f, "\n");
    else
        fprintf(f, " = %d\n", bj_total(r->dealer, r->ndealer));
}

static void print_hand(FILE *f, const bj_round_t *r, int i)
{
    const bj_hand_t *h = &r->hands[i];
    char label[32];

    if (r->nhands > 1)
        snprintf(label, sizeof label, "Hand %d%s", i + 1,
                 i == r->active && r->phase == BJ_PHASE_PLAYER ? " *" : "");
    else
        snprintf(label, sizeof label, "Player");

    if (cardart_enabled(f)) {
        fprintf(f, "%s (%d):\n", label, bj_total(h->cards, h->n));
        show_cards(f, h->cards, h->n, false);
        return;
    }
    fprintf(f, "%s:", label);
    show_cards(f, h->cards, h->n, false);
    fprintf(f, " = %d\n", bj_total(h->cards, h->n));
}

static void print_table(FILE *f, const bj_session_t *s)
{
    const bj_round_t *r = &s->round;

    print_dealer(f, r);
    for (int i = 0; i < r->nhands; i++)
        print_hand(f, r, i);
}

/* ---- interactive -------------------------------------------------------- */

static bool ask_action(const bj_session_t *s, FILE *disp, bj_action_t *out)
{
    static const char *const PROMPT[BJ_NACTIONS] = {
        "[h]it", "[s]tand", "[d]ouble", "s[p]lit", "su[r]render"
    };

    for (;;) {
        fprintf(disp, "\n");
        for (int a = 0; a < BJ_NACTIONS; a++)
            if (bj_legal(s, (bj_action_t)a))
                fprintf(disp, "%s ", PROMPT[a]);
        fprintf(disp, "\n> ");
        fflush(disp);

        char line[64];
        if (!fgets(line, sizeof line, stdin)) {
            fprintf(disp, "\n");
            *out = BJ_STAND;
            return false;
        }
        line[strcspn(line, " \t\r\n")] = '\0';
        char *w = line + strspn(line, " \t");

        sc_act_t sa;
        if (action_parse(w, &sa) == 0 && sa <= SC_SURRENDER &&
            bj_legal(s, (bj_action_t)sa)) {
            *out = (bj_action_t)sa;
            return true;
        }
        fprintf(disp, "not a legal action here\n");
    }
}

static bool ask_insurance(FILE *disp, bool *take)
{
    for (;;) {
        fprintf(disp, "\nDealer shows an ace. [i]nsurance / [n]o\n> ");
        fflush(disp);

        char line[64];
        if (!fgets(line, sizeof line, stdin)) {
            fprintf(disp, "\n");
            *take = false;
            return false;
        }
        line[strcspn(line, " \t\r\n")] = '\0';
        char *w = line + strspn(line, " \t");

        sc_act_t sa;
        if (*w == '\0' || strcasecmp(w, "n") == 0 ||
            strcasecmp(w, "no") == 0) {
            *take = false;
            return true;
        }
        if (action_parse(w, &sa) == 0 &&
            (sa == SC_INS || sa == SC_NOINS)) {
            *take = (sa == SC_INS);
            return true;
        }
        fprintf(disp, "answer insurance or no\n");
    }
}

/* ---- one round ---------------------------------------------------------- */

/* Log of what was actually applied this round (an illegal scripted
 * action falls back to standing, so this differs from the script). */
typedef struct {
    const char *words[SC_MAX];
    int         n;
} bj_actlog_t;

static void log_action(bj_actlog_t *lg, const char *w)
{
    if (lg->n < SC_MAX)
        lg->words[lg->n++] = w;
}

/*
 * Where the player's decisions come from.  All three sources end up in the
 * same round loop and pass their action through bj_act(), so the rules are
 * applied identically however the hand is being played.
 */
typedef enum {
    BJ_PLAY_INTERACTIVE,        /* prompt on stdin */
    BJ_PLAY_SCRIPTED,           /* actions listed on the command line */
    BJ_PLAY_BASIC               /* --basic: the strategy adviser decides */
} bj_play_mode_t;

/*
 * Fold every card now face up into the running count.  There is no deal
 * animation here, so everything the shoe has given out is on the felt
 * except the hole card while the engine still hides it.  A NULL count is
 * a session that is not counting.
 */
static void count_sync(bj_count_t *c, const bj_session_t *s)
{
    if (c)
        bj_count_update(c, s, bj_dealt_count(s), bj_hole_index(s),
                        !s->round.hole_hidden);
}

/*
 * Drive a round to settlement.
 */
static void play_round(bj_session_t *s, rng_t *rng, bj_script_t *sc,
                       bj_play_mode_t mode, FILE *disp, bool display,
                       bj_actlog_t *lg, bj_count_t *count)
{
    lg->n = 0;
    bj_deal(s, rng);
    count_sync(count, s);

    if (display) {
        if (s->shuffled)
            fprintf(disp, "-- shuffling a fresh %d-deck shoe --\n",
                    BJ_DECKS);
        print_table(disp, s);
    }

    if (bj_insurance_pending(s)) {
        bool take = false;
        if (mode == BJ_PLAY_INTERACTIVE) {
            ask_insurance(disp, &take);
        } else if (mode == BJ_PLAY_BASIC) {
            /* basic strategy always declines: insurance is a side bet on
             * the hole card, and the count indices that would ever make it
             * worth taking are outside this mode */
            if (display)
                fprintf(disp, "> noinsurance\n");
        } else if (sc->pos < sc->n &&
                   (sc->acts[sc->pos] == SC_INS ||
                    sc->acts[sc->pos] == SC_NOINS)) {
            take = sc->acts[sc->pos++] == SC_INS;
            if (display)
                fprintf(disp, "> %s\n", take ? "insurance" : "noinsurance");
        }
        log_action(lg, take ? "insurance" : "noinsurance");
        bj_insurance(s, take, rng);
        count_sync(count, s);       /* the peek may have turned the hole */
        if (display && s->round.insurance > 0)
            fprintf(disp, "Insurance taken.\n");
    }

    while (s->round.phase == BJ_PHASE_PLAYER) {
        bj_action_t a = BJ_STAND;

        switch (mode) {
        case BJ_PLAY_INTERACTIVE:
            if (!ask_action(s, disp, &a))
                a = BJ_STAND;
            break;

        case BJ_PLAY_BASIC: {
            /* One decision at a time, read back from the live state: the
             * next card of a hit, each half of a split and the bankroll
             * fallbacks for an unaffordable double or split all come out
             * of the next pass round this loop. */
            bj_action_t want;

            if (bj_strategy_action(bj_basic_strategy(s), &want) &&
                bj_legal(s, want))
                a = want;
            break;
        }

        case BJ_PLAY_SCRIPTED:
        default:
            /* skip insurance words that belong to another round */
            while (sc->pos < sc->n && (sc->acts[sc->pos] == SC_INS ||
                                       sc->acts[sc->pos] == SC_NOINS))
                sc->pos++;
            if (sc->pos < sc->n) {
                sc_act_t sa = sc->acts[sc->pos++];
                a = (bj_action_t)sa;
                if (!bj_legal(s, a))
                    a = BJ_STAND;     /* illegal here: stand instead */
            } else {
                a = BJ_STAND;         /* script exhausted */
            }
            break;
        }
        if (mode != BJ_PLAY_INTERACTIVE && display)
            fprintf(disp, "> %s\n", bj_action_word(a));
        log_action(lg, bj_action_word(a));
        bj_act(s, a, rng);
        /* hit, double and split cards, and the dealer's own draws once
         * the last hand is finished, all arrive face up */
        count_sync(count, s);
        if (display && s->round.phase == BJ_PHASE_PLAYER)
            print_table(disp, s);
    }

    count_sync(count, s);           /* settled: nothing is face down now */

    if (display) {
        fprintf(disp, "\n");
        print_table(disp, s);
    }
}

/* ---- output ------------------------------------------------------------- */

static void round_results(const bj_round_t *r, char *buf, size_t len)
{
    size_t off = 0;

    buf[0] = '\0';
    for (int i = 0; i < r->nhands; i++)
        off += (size_t)snprintf(buf + off, len - off, "%s%s", off ? "," : "",
                                bj_result_word(r->hands[i].result));
}

static void print_result(FILE *f, const bj_session_t *s)
{
    const bj_round_t *r = &s->round;
    char money[32];

    if (r->insurance > 0)
        fprintf(f, "Insurance: %s\n", r->insurance_won ? "WIN" : "LOSS");
    for (int i = 0; i < r->nhands; i++) {
        const bj_hand_t *h = &r->hands[i];
        bj_credits(money, sizeof money, h->payout - h->wager);
        if (r->nhands > 1)
            fprintf(f, "Hand %d: %s (%s)\n", i + 1,
                    bj_result_word(h->result), money);
        else
            fprintf(f, "%s (%s)\n", bj_result_word(h->result), money);
    }
    bj_credits(money, sizeof money, r->returned - r->wagered);
    fprintf(f, "Net: %s   ", money);
    bj_credits(money, sizeof money, s->bankroll);
    fprintf(f, "Bankroll: %s   Shoe: %d/%d\n", money, bj_remaining(s),
            BJ_SHOE_CARDS);
}

static void round_json(FILE *f, const bj_session_t *s,
                       const bj_actlog_t *lg)
{
    const bj_round_t *r = &s->round;
    char buf[8];

    fprintf(f, "{\"game\":\"blackjack\",\"bet\":%.1f,\"dealer\":[",
            (double)s->base_bet / BJ_HALF);
    for (int i = 0; i < r->ndealer; i++) {
        if (i)
            fputc(',', f);
        card_name(r->dealer[i], buf, sizeof buf);
        json_string(f, buf);
    }
    fprintf(f, "],\"dealer_total\":%d,\"hands\":[",
            bj_total(r->dealer, r->ndealer));
    for (int i = 0; i < r->nhands; i++) {
        const bj_hand_t *h = &r->hands[i];
        if (i)
            fputc(',', f);
        fprintf(f, "{\"cards\":[");
        for (int c = 0; c < h->n; c++) {
            if (c)
                fputc(',', f);
            card_name(h->cards[c], buf, sizeof buf);
            json_string(f, buf);
        }
        fprintf(f, "],\"total\":%d,\"wager\":%.1f,\"doubled\":%s,"
                   "\"split\":%s,\"result\":",
                bj_total(h->cards, h->n), (double)h->wager / BJ_HALF,
                h->doubled ? "true" : "false",
                h->from_split ? "true" : "false");
        json_string(f, bj_result_word(h->result));
        fprintf(f, ",\"payout\":%.1f}", (double)h->payout / BJ_HALF);
    }
    fprintf(f, "],\"insurance\":{\"taken\":%s,\"won\":%s},\"actions\":[",
            r->insurance > 0 ? "true" : "false",
            r->insurance_won ? "true" : "false");
    for (int i = 0; i < lg->n; i++) {
        if (i)
            fputc(',', f);
        json_string(f, lg->words[i]);
    }
    fprintf(f, "],\"result\":");
    {
        bj_result_t first = r->hands[0].result;
        bool same = true;
        for (int i = 1; i < r->nhands; i++)
            if (r->hands[i].result != first)
                same = false;
        json_string(f, same ? bj_result_word(first) : "MIXED");
    }
    fprintf(f, ",\"net\":%.1f,\"bankroll\":%.1f,\"shoe_remaining\":%d,"
               "\"shoe_size\":%d,\"shuffled\":%s}\n",
            (double)(r->returned - r->wagered) / BJ_HALF,
            (double)s->bankroll / BJ_HALF, bj_remaining(s), BJ_SHOE_CARDS,
            s->shuffled ? "true" : "false");
}

/* ---- driver -------------------------------------------------------------- */

/* True-count bands the opening wager was chosen from: below zero, then
 * the four ramp steps from zero up. */
#define BJ_TC_BANDS (BJ_RAMP_STEPS + 1)

typedef struct {
    long rounds, hands;
    long wins, losses, pushes, naturals;
    long doubles, splits, surrenders;
    long ins_bets, ins_wins;
    long pbust, dbust;
    long shuffles, rebuys;
    long wagered, returned;

    /* --count-bet: how the opening wager was arrived at */
    long   ramp[BJ_RAMP_STEPS];     /* rounds bet at 1, 2, 4, 6, 8 units */
    long   band[BJ_TC_BANDS];       /* rounds bet at each true-count band */
    long   cap_table, cap_bank;
    long   opening;                 /* sum of the opening wagers */
    long   open_min, open_max;
    double tc_sum;
} bj_stats_t;

/* Which true-count band a wager was placed from: negative first, then the
 * ramp's own steps from zero up. */
static int tc_band(double true_count)
{
    if (true_count < 0.0)
        return 0;
    if (true_count < 1.0)
        return 1;
    if (true_count < 2.0)
        return 2;
    if (true_count < 3.0)
        return 3;
    if (true_count < 4.0)
        return 4;
    return 5;
}

static void tally_bet(bj_stats_t *st, const bj_bet_plan_t *p)
{
    st->ramp[p->step]++;
    st->band[tc_band(p->true_count)]++;
    st->tc_sum += p->true_count;
    st->opening += p->wager;
    if (st->open_min == 0 || p->wager < st->open_min)
        st->open_min = p->wager;
    if (p->wager > st->open_max)
        st->open_max = p->wager;
    if (p->capped_table)
        st->cap_table++;
    if (p->capped_bankroll)
        st->cap_bank++;
}

static void tally(bj_stats_t *st, const bj_session_t *s)
{
    const bj_round_t *r = &s->round;

    st->rounds++;
    st->hands += r->nhands;
    st->wagered += r->wagered;
    st->returned += r->returned;
    if (s->shuffled)
        st->shuffles++;
    if (r->insurance > 0) {
        st->ins_bets++;
        if (r->insurance_won)
            st->ins_wins++;
    }
    if (bj_total(r->dealer, r->ndealer) > 21)
        st->dbust++;
    if (r->nhands > 1)
        st->splits += r->nhands - 1;

    for (int i = 0; i < r->nhands; i++) {
        const bj_hand_t *h = &r->hands[i];
        if (h->doubled)
            st->doubles++;
        if (h->surrendered)
            st->surrenders++;
        if (bj_total(h->cards, h->n) > 21)
            st->pbust++;
        switch (h->result) {
        case BJ_WIN:        st->wins++; break;
        case BJ_BLACKJACK:  st->wins++; st->naturals++; break;
        case BJ_LOSS:       st->losses++; break;
        case BJ_PUSH:       st->pushes++; break;
        case BJ_SURRENDERED: st->losses++; break;
        default: break;
        }
    }
}

/* What the count-based wager was, before the cards come out. */
static void print_bet(FILE *f, const bj_count_t *c, const bj_bet_plan_t *p)
{
    char money[32];

    bj_credits(money, sizeof money, p->wager);
    fprintf(f, "Running count: %+d\nTrue count: %+.1f\n"
               "Bet ramp: %ld unit%s\nWager: %s\n",
            c->running, p->true_count, p->units,
            p->units == 1 ? "" : "s", money);
}

int blackjack_run(const cli_t *cli, rng_t *rng)
{
    bj_script_t sc;
    long bet;

    if (script_build(cli, &sc, &bet))
        return 2;

    if (cli->gui) {
        if (sc.n != 0 || cli->basic || cli->count_bet || cli->quiet ||
            cli->json || cli->stats || cli->iterations != 1) {
            fprintf(stderr, "blackjack: --gui takes no other arguments "
                            "(only --counting and --seed)\n");
            return 2;
        }
#ifdef CASINO_GUI
        return bj_gui_run(rng, cli->counting);
#else
        fprintf(stderr, "blackjack: this build has no GUI support "
                        "(install raylib and run make again)\n");
        return 2;
#endif
    }

    if (cli->counting) {
        fprintf(stderr, "blackjack: --counting is a GUI training mode; "
                        "use --gui --counting\n");
        return 2;
    }

    if (cli->basic && sc.n != 0) {
        fprintf(stderr, "blackjack: --basic plays the hand itself; it "
                        "cannot be combined with scripted actions\n");
        return 2;
    }

    /* the ramp sizes the wager for a hand somebody else plays: the
     * decisions have to come from the strategy engine */
    if (cli->count_bet && !cli->basic) {
        fprintf(stderr, "blackjack: --count-bet requires --basic\n");
        return 2;
    }

    bj_play_mode_t mode = cli->basic ? BJ_PLAY_BASIC
                        : sc.n != 0  ? BJ_PLAY_SCRIPTED
                                     : BJ_PLAY_INTERACTIVE;

    /* An unattended simulation needs a decision source; always-stand is
     * only ever played when the user asked for it with a script. */
    if (mode == BJ_PLAY_INTERACTIVE && cli->stats) {
        fprintf(stderr, "blackjack: simulation requires scripted actions "
                        "or --basic\n");
        return 2;
    }

    bool machine = cli->quiet || cli->json || cli->stats;
    FILE *disp = machine ? stderr : stdout;
    bool display = mode == BJ_PLAY_INTERACTIVE ||
                   (!machine && cli->iterations == 1);

    bj_session_t s;
    bj_session_start(&s, rng);
    bj_set_bet(&s, bet);

    bj_stats_t st = { 0 };
    bj_actlog_t lg = { { 0 }, 0 };

    /* one running count for the whole session, kept card by card by
     * play_round() and restarted only by a fresh shoe */
    bj_count_t count;
    bj_bet_plan_t plan;
    bj_count_reset(&count);

    for (long it = 0; it < cli->iterations; it++) {
        /* a flat game needs the whole wager; the ramp only needs enough
         * for a minimum bet, and drops to what the bankroll covers */
        long need = cli->count_bet ? BJ_BET_MIN : s.base_bet;

        if (s.bankroll < need) {
            if (mode == BJ_PLAY_INTERACTIVE || cli->iterations == 1) {
                fprintf(disp, "Out of credits.\n");
                break;
            }
            bj_bankroll_reset(&s);      /* fresh buy-in, shoe untouched */
            st.rebuys++;
        }

        if (cli->count_bet) {
            /* settle which shoe this round comes from before betting on
             * it: a fresh one starts at a zero count, so at one unit */
            if (bj_prepare_round(&s, rng))
                bj_count_reset(&count);
            bj_count_bet(&s, &count, bet, &plan);
            bj_set_bet(&s, plan.wager);
            tally_bet(&st, &plan);
            if (display)
                print_bet(disp, &count, &plan);
        }

        sc.pos = 0;                     /* the script replays each round */
        play_round(&s, rng, &sc, mode, disp, display, &lg,
                   cli->count_bet ? &count : NULL);
        tally(&st, &s);
        s.round.phase = BJ_PHASE_BET;

        if (cli->stats)
            continue;

        if (cli->json) {
            round_json(stdout, &s, &lg);
        } else if (cli->quiet) {
            char results[96], net[32], bank[32];
            round_results(&s.round, results, sizeof results);
            bj_credits(net, sizeof net, s.round.returned - s.round.wagered);
            bj_credits(bank, sizeof bank, s.bankroll);
            printf("%s net=%s bankroll=%s\n", results, net, bank);
        } else if (!display) {
            char results[96], net[32];
            round_results(&s.round, results, sizeof results);
            bj_credits(net, sizeof net, s.round.returned - s.round.wagered);
            printf("%s net=%s\n", results, net);
        } else {
            print_result(stdout, &s);
        }
    }

    if (cli->stats) {
        double ret = st.wagered ? (double)st.returned / (double)st.wagered
                                : 0.0;
        double rounds = st.rounds ? (double)st.rounds : 1.0;
        double open_avg = (double)st.opening / rounds / BJ_HALF;
        double tc_avg = st.tc_sum / rounds;
        const char *strategy = mode == BJ_PLAY_BASIC ? "basic" : "scripted";

        if (cli->json) {
            printf("{\"game\":\"blackjack\",\"iterations\":%ld,"
                   "\"rounds\":%ld,\"hands\":%ld,\"wins\":%ld,"
                   "\"losses\":%ld,\"pushes\":%ld,\"blackjacks\":%ld,"
                   "\"doubles\":%ld,\"splits\":%ld,\"surrenders\":%ld,"
                   "\"insurance_bets\":%ld,\"insurance_wins\":%ld,"
                   "\"player_busts\":%ld,\"dealer_busts\":%ld,"
                   "\"shuffles\":%ld,\"rebuys\":%ld,\"wagered\":%.1f,"
                   "\"returned\":%.1f,\"net\":%.1f,"
                   "\"return_per_unit\":%.6f,\"strategy\":\"%s\"",
                   cli->iterations, st.rounds, st.hands, st.wins,
                   st.losses, st.pushes, st.naturals, st.doubles,
                   st.splits, st.surrenders, st.ins_bets, st.ins_wins,
                   st.pbust, st.dbust, st.shuffles, st.rebuys,
                   (double)st.wagered / BJ_HALF,
                   (double)st.returned / BJ_HALF,
                   (double)(st.returned - st.wagered) / BJ_HALF, ret,
                   strategy);
            if (cli->count_bet)
                printf(",\"betting\":{\"mode\":\"hilo_true_count\","
                       "\"spread\":\"1-8\",\"base_unit\":%.1f,"
                       "\"average_initial_bet\":%.1f,"
                       "\"minimum_initial_bet\":%.1f,"
                       "\"maximum_initial_bet\":%.1f,"
                       "\"average_true_count\":%.4f,"
                       "\"capped_table\":%ld,\"capped_bankroll\":%ld,"
                       "\"multipliers\":{\"1\":%ld,\"2\":%ld,\"4\":%ld,"
                       "\"6\":%ld,\"8\":%ld},"
                       "\"true_count\":{\"negative\":%ld,\"0\":%ld,"
                       "\"1\":%ld,\"2\":%ld,\"3\":%ld,\"4\":%ld}}",
                       (double)bet / BJ_HALF, open_avg,
                       (double)st.open_min / BJ_HALF,
                       (double)st.open_max / BJ_HALF, tc_avg,
                       st.cap_table, st.cap_bank,
                       st.ramp[0], st.ramp[1], st.ramp[2], st.ramp[3],
                       st.ramp[4], st.band[0], st.band[1], st.band[2],
                       st.band[3], st.band[4], st.band[5]);
            printf("}\n");
        } else if (cli->quiet) {
            printf("rounds=%ld hands=%ld wins=%ld losses=%ld pushes=%ld "
                   "blackjacks=%ld doubles=%ld splits=%ld surrenders=%ld "
                   "insurance=%ld/%ld player_busts=%ld dealer_busts=%ld "
                   "wagered=%.1f returned=%.1f net=%.1f return=%.4f "
                   "strategy=%s",
                   st.rounds, st.hands, st.wins, st.losses, st.pushes,
                   st.naturals, st.doubles, st.splits, st.surrenders,
                   st.ins_wins, st.ins_bets, st.pbust, st.dbust,
                   (double)st.wagered / BJ_HALF,
                   (double)st.returned / BJ_HALF,
                   (double)(st.returned - st.wagered) / BJ_HALF, ret,
                   strategy);
            if (cli->count_bet)
                printf(" betting=count base_unit=%.1f avg_initial_bet=%.2f "
                       "min_initial_bet=%.1f max_initial_bet=%.1f "
                       "avg_true_count=%.4f bet_1u=%ld bet_2u=%ld "
                       "bet_4u=%ld bet_6u=%ld bet_8u=%ld cap_table=%ld "
                       "cap_bankroll=%ld tc_neg=%ld tc_0=%ld tc_1=%ld "
                       "tc_2=%ld tc_3=%ld tc_4=%ld",
                       (double)bet / BJ_HALF, open_avg,
                       (double)st.open_min / BJ_HALF,
                       (double)st.open_max / BJ_HALF, tc_avg,
                       st.ramp[0], st.ramp[1], st.ramp[2], st.ramp[3],
                       st.ramp[4], st.cap_table, st.cap_bank,
                       st.band[0], st.band[1], st.band[2], st.band[3],
                       st.band[4], st.band[5]);
            printf("\n");
        } else {
            char money[32];
            printf("Iterations: %ld   Rounds: %ld   Hands: %ld\n",
                   cli->iterations, st.rounds, st.hands);
            if (mode == BJ_PLAY_BASIC)
                printf("Strategy: basic\n");
            if (cli->count_bet)
                printf("Betting: Hi-Lo true-count 1-8 spread\n");
            printf("%-14s %10s %9s\n", "RESULT", "COUNT", "RATE%");
            printf("%-14s %10ld %9.4f\n", "win", st.wins,
                   100.0 * (double)st.wins / (double)st.hands);
            printf("%-14s %10ld %9.4f\n", "loss", st.losses,
                   100.0 * (double)st.losses / (double)st.hands);
            printf("%-14s %10ld %9.4f\n", "push", st.pushes,
                   100.0 * (double)st.pushes / (double)st.hands);
            printf("%-14s %10ld %9.4f\n", "blackjack", st.naturals,
                   100.0 * (double)st.naturals / (double)st.hands);
            printf("%-14s %10ld\n", "doubles", st.doubles);
            printf("%-14s %10ld\n", "splits", st.splits);
            printf("%-14s %10ld\n", "surrenders", st.surrenders);
            printf("%-14s %10ld / %ld\n", "insurance", st.ins_wins,
                   st.ins_bets);
            printf("%-14s %10ld\n", "player busts", st.pbust);
            printf("%-14s %10ld\n", "dealer busts", st.dbust);
            printf("%-14s %10ld\n", "shuffles", st.shuffles);
            printf("%-14s %10ld\n", "rebuys", st.rebuys);
            bj_credits(money, sizeof money, st.wagered);
            printf("Wagered: %s   ", money);
            bj_credits(money, sizeof money, st.returned);
            printf("Returned: %s   ", money);
            bj_credits(money, sizeof money, st.returned - st.wagered);
            printf("Net: %s\n", money);
            printf("Return per unit wagered: %.4f\n", ret);

            if (cli->count_bet) {
                static const char *const BAND[BJ_TC_BANDS] = {
                    "below 0", "0 to +1", "+1 to +2", "+2 to +3",
                    "+3 to +4", "+4 or more"
                };
                char label[16];

                printf("%-14s %10s %9s\n", "OPENING BET", "ROUNDS", "RATE%");
                for (int i = 0; i < BJ_RAMP_STEPS; i++) {
                    /* step i is the band starting at true count i, so the
                     * ramp itself still names its own multiples */
                    snprintf(label, sizeof label, "%ld unit%s",
                             bj_count_bet_units((double)i), i ? "s" : "");
                    printf("%-14s %10ld %9.4f\n", label, st.ramp[i],
                           100.0 * (double)st.ramp[i] / rounds);
                }
                printf("%-14s %10ld\n", "table cap", st.cap_table);
                printf("%-14s %10ld\n", "bankroll cap", st.cap_bank);
                printf("%-14s %10s %9s\n", "TRUE COUNT", "ROUNDS", "RATE%");
                for (int i = 0; i < BJ_TC_BANDS; i++)
                    printf("%-14s %10ld %9.4f\n", BAND[i], st.band[i],
                           100.0 * (double)st.band[i] / rounds);
                bj_credits(money, sizeof money, bet);
                printf("Base unit: %s   ", money);
                printf("Opening bet avg: %.2f   ", open_avg);
                bj_credits(money, sizeof money, st.open_min);
                printf("min: %s   ", money);
                bj_credits(money, sizeof money, st.open_max);
                printf("max: %s\n", money);
                printf("Average true count at bet: %+.4f\n", tc_avg);
            }
        }
    }
    return 0;
}

void blackjack_list_bets(void)
{
    puts("blackjack (6-deck shoe, dealer stands on all 17s):");
    puts("rules:");
    puts("  6-deck shoe, reshuffled between hands at ~75% penetration");
    puts("  blackjack pays 3:2, ordinary win 1:1, push returns the wager");
    puts("  double on any first two cards, double after split allowed");
    puts("  split equal ranks up to 4 hands; split aces get one card each");
    puts("  and cannot be doubled or re-split; 21 after a split is not a");
    puts("  natural");
    puts("  late surrender on the first two unsplit cards returns half");
    puts("  insurance offered only against a dealer ace, pays 2:1");
    puts("actions:");
    puts("  h | hit          take a card");
    puts("  s | stand        end the hand");
    puts("  d | double       double the wager, take exactly one card");
    puts("  p | split        split a pair into two hands");
    puts("  r | surrender    forfeit half the wager");
    puts("  insurance | noinsurance   answer the insurance offer");
    puts("arguments:");
    puts("  bet:N            wager per hand in credits (5-500, default 25)");
    puts("  --basic          play every decision by basic strategy instead");
    puts("                   of prompting or following a script; matches");
    puts("                   this table exactly (6 deck, S17, DAS, late");
    puts("                   surrender, dealer peek), always declines");
    puts("                   insurance and is count-independent");
    puts("  --count-bet      size each wager from the Hi-Lo true count");
    puts("                   (requires --basic).  bet:N sets the unit;");
    puts("                   without it the unit is the 25-credit default:");
    puts("                     TC < +1   1 unit");
    puts("                     TC +1     2 units");
    puts("                     TC +2     4 units");
    puts("                     TC +3     6 units");
    puts("                     TC +4+    8 units");
    puts("                   the wager is chosen before the round's cards");
    puts("                   are dealt, and the table maximum (500) or the");
    puts("                   bankroll can cap the top of the spread.  It is");
    puts("                   bet sizing only: insurance is still declined");
    puts("                   and no count playing deviations are used");
    puts("usage:");
    puts("  blackjack                interactive game");
    puts("  blackjack h,s            scripted: hit then stand");
    puts("  blackjack bet:50 p,s,s   split, then stand both hands");
    puts("  blackjack s --runs 100000   simulate always-stand");
    puts("  blackjack --basic           one hand played by basic strategy");
    puts("  blackjack --basic --runs 100000        simulate basic strategy");
    puts("  blackjack bet:50 --basic --runs 100000 same, 50-credit wager");
    puts("  blackjack --basic --count-bet --runs 100000   count bet ramp");
    puts("  blackjack bet:10 --basic --count-bet --runs 100000  10-unit ramp");
    puts("results: BLACKJACK | WIN | LOSS | PUSH | SURRENDER");
}
