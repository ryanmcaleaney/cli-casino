#include "blackjack.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

/* Reshuffle only between rounds, once the cut card is reached. */
static void maybe_shuffle(bj_session_t *s, rng_t *rng)
{
    s->shuffled = false;
    if (shoe_remaining(&s->shoe) <= BJ_RESHUFFLE_AT) {
        shoe_init(&s->shoe, BJ_DECKS);
        shoe_shuffle(&s->shoe, rng);
        s->shuffled = true;
    }
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

    maybe_shuffle(s, rng);

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
 * Drive a round to settlement.  Interactive and scripted play share this
 * path, so the rules are applied identically either way.
 */
static void play_round(bj_session_t *s, rng_t *rng, bj_script_t *sc,
                       bool interactive, FILE *disp, bool display,
                       bj_actlog_t *lg)
{
    lg->n = 0;
    bj_deal(s, rng);

    if (display) {
        if (s->shuffled)
            fprintf(disp, "-- shuffling a fresh %d-deck shoe --\n",
                    BJ_DECKS);
        print_table(disp, s);
    }

    if (bj_insurance_pending(s)) {
        bool take = false;
        if (interactive) {
            ask_insurance(disp, &take);
        } else if (sc->pos < sc->n &&
                   (sc->acts[sc->pos] == SC_INS ||
                    sc->acts[sc->pos] == SC_NOINS)) {
            take = sc->acts[sc->pos++] == SC_INS;
            if (display)
                fprintf(disp, "> %s\n", take ? "insurance" : "noinsurance");
        }
        log_action(lg, take ? "insurance" : "noinsurance");
        bj_insurance(s, take, rng);
        if (display && s->round.insurance > 0)
            fprintf(disp, "Insurance taken.\n");
    }

    while (s->round.phase == BJ_PHASE_PLAYER) {
        bj_action_t a = BJ_STAND;

        if (interactive) {
            if (!ask_action(s, disp, &a))
                a = BJ_STAND;
        } else {
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
            if (display)
                fprintf(disp, "> %s\n", bj_action_word(a));
        }
        log_action(lg, bj_action_word(a));
        bj_act(s, a, rng);
        if (display && s->round.phase == BJ_PHASE_PLAYER)
            print_table(disp, s);
    }

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

typedef struct {
    long rounds, hands;
    long wins, losses, pushes, naturals;
    long doubles, splits, surrenders;
    long ins_bets, ins_wins;
    long pbust, dbust;
    long shuffles, rebuys;
    long wagered, returned;
} bj_stats_t;

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

int blackjack_run(const cli_t *cli, rng_t *rng)
{
    bj_script_t sc;
    long bet;

    if (script_build(cli, &sc, &bet))
        return 2;

    if (cli->gui) {
        if (sc.n != 0 || cli->quiet || cli->json || cli->stats ||
            cli->iterations != 1) {
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

    bool interactive = sc.n == 0;
    if (interactive && cli->stats) {
        fprintf(stderr, "blackjack: simulation (--runs/--stats) needs "
                        "scripted actions, e.g. 's' or 'h,s'\n");
        return 2;
    }

    bool machine = cli->quiet || cli->json || cli->stats;
    FILE *disp = machine ? stderr : stdout;
    bool display = interactive || (!machine && cli->iterations == 1);

    bj_session_t s;
    bj_session_start(&s, rng);
    bj_set_bet(&s, bet);

    bj_stats_t st = { 0 };
    bj_actlog_t lg = { { 0 }, 0 };

    for (long it = 0; it < cli->iterations; it++) {
        if (s.bankroll < s.base_bet) {
            if (interactive || cli->iterations == 1) {
                fprintf(disp, "Out of credits.\n");
                break;
            }
            bj_bankroll_reset(&s);      /* fresh buy-in, shoe untouched */
            st.rebuys++;
        }

        sc.pos = 0;                     /* the script replays each round */
        play_round(&s, rng, &sc, interactive, disp, display, &lg);
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
        if (cli->json) {
            printf("{\"game\":\"blackjack\",\"iterations\":%ld,"
                   "\"rounds\":%ld,\"hands\":%ld,\"wins\":%ld,"
                   "\"losses\":%ld,\"pushes\":%ld,\"blackjacks\":%ld,"
                   "\"doubles\":%ld,\"splits\":%ld,\"surrenders\":%ld,"
                   "\"insurance_bets\":%ld,\"insurance_wins\":%ld,"
                   "\"player_busts\":%ld,\"dealer_busts\":%ld,"
                   "\"shuffles\":%ld,\"rebuys\":%ld,\"wagered\":%.1f,"
                   "\"returned\":%.1f,\"net\":%.1f,"
                   "\"return_per_unit\":%.6f}\n",
                   cli->iterations, st.rounds, st.hands, st.wins,
                   st.losses, st.pushes, st.naturals, st.doubles,
                   st.splits, st.surrenders, st.ins_bets, st.ins_wins,
                   st.pbust, st.dbust, st.shuffles, st.rebuys,
                   (double)st.wagered / BJ_HALF,
                   (double)st.returned / BJ_HALF,
                   (double)(st.returned - st.wagered) / BJ_HALF, ret);
        } else if (cli->quiet) {
            printf("rounds=%ld hands=%ld wins=%ld losses=%ld pushes=%ld "
                   "blackjacks=%ld doubles=%ld splits=%ld surrenders=%ld "
                   "insurance=%ld/%ld player_busts=%ld dealer_busts=%ld "
                   "wagered=%.1f returned=%.1f net=%.1f return=%.4f\n",
                   st.rounds, st.hands, st.wins, st.losses, st.pushes,
                   st.naturals, st.doubles, st.splits, st.surrenders,
                   st.ins_wins, st.ins_bets, st.pbust, st.dbust,
                   (double)st.wagered / BJ_HALF,
                   (double)st.returned / BJ_HALF,
                   (double)(st.returned - st.wagered) / BJ_HALF, ret);
        } else {
            char money[32];
            printf("Iterations: %ld   Rounds: %ld   Hands: %ld\n",
                   cli->iterations, st.rounds, st.hands);
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
    puts("usage:");
    puts("  blackjack                interactive game");
    puts("  blackjack h,s            scripted: hit then stand");
    puts("  blackjack bet:50 p,s,s   split, then stand both hands");
    puts("  blackjack s --runs 100000   simulate always-stand");
    puts("results: BLACKJACK | WIN | LOSS | PUSH | SURRENDER");
}
