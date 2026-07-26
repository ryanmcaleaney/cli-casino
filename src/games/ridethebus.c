#include "ridethebus.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cardart.h"
#include "cards.h"
#include "output.h"

#ifdef CASINO_GUI
#include "gui/ridethebus_gui.h"
#endif

/* ---- payout table (single source of truth) ----------------------------- */

/* Multiplier applied to the ORIGINAL wager after winning round N. */
static const long RTB_MULT[RTB_ROUNDS] = { 2, 3, 4, 20 };

static const char *const ROUND_TITLE[RTB_ROUNDS] = {
    "RED OR BLACK", "HIGHER OR LOWER", "INSIDE OR OUTSIDE", "SUIT"
};
static const char *const ROUND_SHORT[RTB_ROUNDS] = {
    "red/black", "higher/lower", "inside/outside", "suit"
};

/* ---- pure rules (exercised directly by the `check` self-test) ---------- */

/* cards.h suits: 0 = clubs, 1 = diamonds, 2 = hearts, 3 = spades. */
static bool rtb_is_red(card_t c)
{
    return c.suit == 1 || c.suit == 2;
}

/* Poker rank order with ace high: 2..10, J, Q, K, A. */
static int rtb_rank_value(card_t c)
{
    return c.rank == 1 ? 14 : c.rank;
}

/* <0 when a ranks below b, 0 when equal, >0 when a ranks above b. */
static int rtb_compare_rank(card_t a, card_t b)
{
    int va = rtb_rank_value(a), vb = rtb_rank_value(b);
    return (va > vb) - (va < vb);
}

/* Round 2: an equal rank counts as HIGHER, so "higher" takes the tie and
 * "lower" loses it.  Every draw resolves the round; nothing is re-drawn. */
static bool rtb_higher_wins(card_t first, card_t next)
{
    return rtb_compare_rank(first, next) <= 0;
}

/* Round 3: the two boundary ranks count as INSIDE, so "inside" takes them
 * and "outside" loses them.  Inside and outside are exact complements. */
static bool rtb_is_inside(card_t a, card_t b, card_t c)
{
    int va = rtb_rank_value(a), vb = rtb_rank_value(b);
    int lo = va < vb ? va : vb, hi = va < vb ? vb : va;
    int v = rtb_rank_value(c);
    return v >= lo && v <= hi;
}

static bool rtb_is_outside(card_t a, card_t b, card_t c)
{
    return !rtb_is_inside(a, b, c);
}

static bool rtb_suit_matches(card_t c, int suit)
{
    return c.suit == suit;
}

/* ---- choice vocabulary (shared by interactive and scripted input) ------ */

typedef struct { const char *word; int value; } rtb_opt_t;

static const rtb_opt_t OPT_COLOR[] = {
    { "r", 0 }, { "red", 0 }, { "b", 1 }, { "black", 1 }
};
static const rtb_opt_t OPT_HILO[] = {
    { "h", 0 }, { "high", 0 }, { "higher", 0 },
    { "l", 1 }, { "low", 1 },  { "lower", 1 }
};
static const rtb_opt_t OPT_INOUT[] = {
    { "i", 0 }, { "in", 0 },  { "inside", 0 },
    { "o", 1 }, { "out", 1 }, { "outside", 1 }
};
static const rtb_opt_t OPT_SUIT[] = {
    { "c", 0 }, { "club", 0 },    { "clubs", 0 },
    { "d", 1 }, { "diamond", 1 }, { "diamonds", 1 },
    { "h", 2 }, { "heart", 2 },   { "hearts", 2 },
    { "s", 3 }, { "spade", 3 },   { "spades", 3 }
};
/* Cash out shares the prompt with the next round's options from round 2
 * onwards.  'x' avoids colliding with [C] Clubs in round 4. */
#define RTB_CASH (-1)
static const rtb_opt_t OPT_CASH[] = {
    { "x", RTB_CASH }, { "cash", RTB_CASH }, { "cashout", RTB_CASH }
};

#define NOPT(a) ((int)(sizeof (a) / sizeof (a)[0]))

static const rtb_opt_t *stage_opts(rtb_stage_t st, int *n)
{
    switch (st) {
    case RTB_RED_BLACK:      *n = NOPT(OPT_COLOR); return OPT_COLOR;
    case RTB_HIGH_LOW:       *n = NOPT(OPT_HILO);  return OPT_HILO;
    case RTB_INSIDE_OUTSIDE: *n = NOPT(OPT_INOUT); return OPT_INOUT;
    default:                 *n = NOPT(OPT_SUIT);  return OPT_SUIT;
    }
}

static int opt_lookup(const rtb_opt_t *opts, int n, const char *w, int *out)
{
    for (int i = 0; i < n; i++) {
        if (strcasecmp(opts[i].word, w) == 0) {
            *out = opts[i].value;
            return 0;
        }
    }
    return -1;
}

static const char *guess_word(rtb_stage_t st, int guess)
{
    static const char *const SUITS[] = {
        "clubs", "diamonds", "hearts", "spades"
    };
    switch (st) {
    case RTB_RED_BLACK:      return guess ? "black" : "red";
    case RTB_HIGH_LOW:       return guess ? "lower" : "higher";
    case RTB_INSIDE_OUTSIDE: return guess ? "outside" : "inside";
    default: return (guess >= 0 && guess < 4) ? SUITS[guess] : "?";
    }
}

/* ---- action script (bare tokens, resolved at the point of use) --------- */

#define RTB_MAX_SCRIPT 32

typedef struct {
    char words[RTB_MAX_SCRIPT][16];
    int  n;
    int  pos;
} rtb_script_t;

/* Lowercase, strip spaces: "Cash Out" -> "cashout". */
static int normalise(const char *in, char *out, size_t len)
{
    size_t o = 0;
    for (size_t i = 0; in[i]; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isspace(c))
            continue;
        if (o + 1 >= len)
            return -1;
        out[o++] = (char)tolower(c);
    }
    out[o] = '\0';
    return o == 0 ? -1 : 0;
}

static int script_add(rtb_script_t *sc, const char *word, const char *raw)
{
    char norm[16];
    if (sc->n >= RTB_MAX_SCRIPT) {
        fprintf(stderr, "ridethebus: too many actions (max %d)\n",
                RTB_MAX_SCRIPT);
        return 2;
    }
    if (normalise(word, norm, sizeof norm) < 0) {
        fprintf(stderr, "ridethebus: bad action in '%s'\n", raw);
        return 2;
    }
    snprintf(sc->words[sc->n++], sizeof sc->words[0], "%s", norm);
    return 0;
}

/* ---- agent: one decision source for interactive, scripted and random --- */

typedef enum { AG_INTERACTIVE, AG_SCRIPT, AG_RANDOM } rtb_mode_t;

typedef struct {
    rtb_mode_t    mode;
    rtb_script_t *sc;
    rng_t        *rng;
    FILE         *disp;
    bool          display;
    int           cashout_after;   /* random mode: ride until this round */
} rtb_agent_t;

/* Look the word up among this round's guesses, then among the cash-out
 * words when cashing out is offered. */
static int choice_lookup(const rtb_opt_t *opts, int nopts, bool allow_cash,
                         const char *w, int *out)
{
    if (opt_lookup(opts, nopts, w, out) == 0)
        return 0;
    if (allow_cash && opt_lookup(OPT_CASH, NOPT(OPT_CASH), w, out) == 0)
        return 0;
    return -1;
}

/*
 * Ask for one choice: either a guess for `round`, or RTB_CASH when the
 * player is allowed to (and wants to) stop.  Picking a guess is itself
 * the decision to keep playing.  Returns 0, -1 when the player walks
 * away (EOF), or 2 on a hard error.
 */
static int agent_choose(rtb_agent_t *ag, const rtb_opt_t *opts, int nopts,
                        bool allow_cash, int round, int *out)
{
    if (ag->mode == AG_RANDOM) {
        /* `round` rounds have been won by the time this is asked */
        if (allow_cash && round >= ag->cashout_after)
            *out = RTB_CASH;
        else if (opts == OPT_SUIT)
            *out = (int)rng_below(ag->rng, 4);
        else
            *out = (int)rng_below(ag->rng, 2);
        return 0;
    }

    if (ag->mode == AG_SCRIPT) {
        if (ag->sc->pos >= ag->sc->n) {
            /* Out of actions: take the money if that is legal here. */
            if (allow_cash) {
                *out = RTB_CASH;
                if (ag->display)
                    fprintf(ag->disp, "> cash out (script ended)\n");
                return 0;
            }
            fprintf(stderr, "ridethebus: script ended before round %d\n",
                    round + 1);
            return 2;
        }
        const char *w = ag->sc->words[ag->sc->pos++];
        if (choice_lookup(opts, nopts, allow_cash, w, out) < 0) {
            fprintf(stderr, "ridethebus: '%s' is not valid in round %d\n",
                    w, round + 1);
            return 2;
        }
        if (ag->display)
            fprintf(ag->disp, "> %s\n", w);
        return 0;
    }

    /* interactive */
    for (;;) {
        char line[64], norm[16];
        fprintf(ag->disp, "> ");
        fflush(ag->disp);
        if (!fgets(line, sizeof line, stdin)) {
            fprintf(ag->disp, "\n");
            return -1;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (normalise(line, norm, sizeof norm) == 0 &&
            choice_lookup(opts, nopts, allow_cash, norm, out) == 0)
            return 0;
        fprintf(ag->disp, "invalid choice, try again\n");
    }
}

/* ---- game state (rtb_game_t and the API live in ridethebus.h) ---------- */

void rtb_front_start(rtb_game_t *g, long bet, rng_t *rng)
{
    memset(g, 0, sizeof *g);
    g->bet = bet;
    g->outcome = RTB_LOSS;
    shoe_init(&g->shoe, 1);
    shoe_shuffle(&g->shoe, rng);
}

rtb_stage_t rtb_front_stage(const rtb_game_t *g)
{
    return g->rounds_won >= RTB_ROUNDS ? RTB_COMPLETE
                                       : (rtb_stage_t)g->rounds_won;
}

bool rtb_front_over(const rtb_game_t *g)
{
    return g->over;
}

/* Nothing to cash before round 1 is won. */
bool rtb_front_can_cash(const rtb_game_t *g)
{
    return !g->over && g->rounds_won > 0;
}

int rtb_front_nchoices(rtb_stage_t st)
{
    return st == RTB_SUIT ? 4 : 2;
}

/*
 * One round: deal its card and resolve it.  Ties go to HIGHER and
 * boundaries go to INSIDE, so every card resolves its own round and
 * nothing is ever re-drawn.  This is the only place a round is judged.
 */
bool rtb_front_guess(rtb_game_t *g, int guess)
{
    rtb_stage_t st = rtb_front_stage(g);
    card_t c;
    bool correct;

    if (g->over || st == RTB_COMPLETE)
        return false;

    c = shoe_draw(&g->shoe);
    g->guess[g->ncards] = guess;
    g->cards[g->ncards++] = c;

    switch (st) {
    case RTB_HIGH_LOW:
        correct = rtb_higher_wins(g->cards[0], c) == (guess == 0);
        break;
    case RTB_INSIDE_OUTSIDE:
        correct = guess == 0
                      ? rtb_is_inside(g->cards[0], g->cards[1], c)
                      : rtb_is_outside(g->cards[0], g->cards[1], c);
        break;
    case RTB_SUIT:
        correct = rtb_suit_matches(c, guess);
        break;
    default:
        correct = (guess == 0) == rtb_is_red(c);
        break;
    }

    if (!correct) {
        g->payout = 0;
        g->outcome = RTB_LOSS;
        g->over = true;
        return false;
    }

    g->rounds_won = (int)st + 1;
    g->payout = g->bet * RTB_MULT[st];
    if (g->rounds_won == RTB_ROUNDS) {
        g->outcome = RTB_BUS;
        g->over = true;
    }
    return true;
}

void rtb_front_cash_out(rtb_game_t *g)
{
    g->outcome = RTB_CASHOUT;
    g->over = true;
}

void rtb_front_range(const rtb_game_t *g, card_t *lo, card_t *hi)
{
    bool first_low = rtb_compare_rank(g->cards[0], g->cards[1]) < 0;

    *lo = first_low ? g->cards[0] : g->cards[1];
    *hi = first_low ? g->cards[1] : g->cards[0];
}

const char *rtb_front_stage_title(rtb_stage_t st)
{
    return st < RTB_COMPLETE ? ROUND_TITLE[st] : "COMPLETE";
}

const char *rtb_front_guess_word(rtb_stage_t st, int guess)
{
    return guess_word(st, guess);
}

/* ---- display (reuses cardart; no card art is defined here) ------------- */

static void show_cards(FILE *f, const card_t *cards, int n)
{
    char buf[8];

    if (cardart_enabled(f)) {
        cardart_hand(f, cards, NULL, n);
        return;
    }
    for (int i = 0; i < n; i++) {
        card_name(cards[i], buf, sizeof buf);
        fprintf(f, "%s%s", i ? "     " : "", buf);
    }
    fputc('\n', f);
}

static void print_prompt(FILE *f, rtb_stage_t st, const rtb_game_t *g)
{
    char a[8], b[8];

    fprintf(f, "\nROUND %d - %s\n\n", (int)st + 1, ROUND_TITLE[st]);
    switch (st) {
    case RTB_HIGH_LOW:
        card_name(g->cards[0], a, sizeof a);
        fprintf(f, "Current card: %s\n\n", a);
        break;
    case RTB_INSIDE_OUTSIDE: {
        card_t lo, hi;
        rtb_front_range(g, &lo, &hi);
        card_name(lo, a, sizeof a);
        card_name(hi, b, sizeof b);
        fprintf(f, "Range: %s - %s\n\n", a, b);
        break;
    }
    default:
        break;
    }

    /* From round 2 on, cashing out shares the prompt: choosing any of the
     * round's own options is what keeps the game going. */
    if (st != RTB_RED_BLACK)
        fprintf(f, "[X] Cash out\n");

    switch (st) {
    case RTB_RED_BLACK:
        fprintf(f, "[R] Red\n[B] Black\n\n");
        break;
    case RTB_HIGH_LOW:
        fprintf(f, "[H] Higher\n[L] Lower\n\n");
        break;
    case RTB_INSIDE_OUTSIDE:
        fprintf(f, "[I] Inside\n[O] Outside\n\n");
        break;
    default:
        fprintf(f, "[H] Hearts\n[D] Diamonds\n[C] Clubs\n[S] Spades\n\n");
        break;
    }
}

/* ---- one game ---------------------------------------------------------- */

/*
 * Play one complete game.  Interactive, scripted and simulated play all
 * run through here, so there is exactly one implementation of the rules.
 * Returns 0, or 2 on a script error.
 */
static int play_game(rtb_agent_t *ag, rng_t *rng, long bet, rtb_game_t *g)
{
    FILE  *f = ag->disp;
    bool   show = ag->display;

    rtb_front_start(g, bet, rng);

    for (int round = 0; round < RTB_ROUNDS; round++) {
        rtb_stage_t st = (rtb_stage_t)round;
        int nopts, guess, rc;
        const rtb_opt_t *opts = stage_opts(st, &nopts);
        bool allow_cash = round > 0;    /* nothing to cash before round 1 */

        if (show)
            print_prompt(f, st, g);
        rc = agent_choose(ag, opts, nopts, allow_cash, round, &guess);
        if (rc == 2)
            return 2;
        if (rc < 0) {
            /* EOF: take the money if any has been won, else no bet. */
            g->walked = true;
            if (allow_cash)
                rtb_front_cash_out(g);
            return 0;
        }
        if (guess == RTB_CASH) {
            rtb_front_cash_out(g);
            return 0;
        }

        /* One card, one resolution, judged by the engine's own rules. */
        bool correct = rtb_front_guess(g, guess);

        if (show) {
            fprintf(f, "\n");
            show_cards(f, g->cards, g->ncards);
        }

        if (!correct) {
            if (show)
                fprintf(f, "\nWrong! You lose your bet of %ld.\n", g->bet);
            return 0;
        }

        if (show)
            fprintf(f, "\nCorrect!\n\nCurrent payout: %ld\n", g->payout);
    }

    return 0;                   /* the fourth win already set RTB_BUS */
}

/* ---- result reporting --------------------------------------------------- */

static const char *outcome_word(const rtb_game_t *g)
{
    return g->outcome == RTB_BUS ? "BUS"
         : g->outcome == RTB_CASHOUT ? "CASHOUT" : "LOSS";
}

static const char *outcome_json(const rtb_game_t *g)
{
    return g->outcome == RTB_BUS ? "bus"
         : g->outcome == RTB_CASHOUT ? "cashout" : "loss";
}

static void print_result(FILE *f, const rtb_game_t *g)
{
    if (g->outcome == RTB_BUS)
        fprintf(f, "\nYOU RODE THE BUS!\n\n");
    else if (g->outcome == RTB_CASHOUT)
        fprintf(f, "\nCASHED OUT after round %d\n\n", g->rounds_won);
    else
        fprintf(f, "\n");

    fprintf(f, "Bet:     %8ld\n", g->bet);
    fprintf(f, "Payout:  %8ld\n", g->payout);
    fprintf(f, "Profit:  %+8ld\n", g->payout - g->bet);
}

static void result_json(FILE *f, const rtb_game_t *g)
{
    char buf[8];

    /* one card and one guess per round played */
    fprintf(f, "{\"game\":\"ridethebus\",\"bet\":%ld,\"rounds_won\":%d,"
               "\"cards\":[", g->bet, g->rounds_won);
    for (int i = 0; i < g->ncards; i++) {
        if (i)
            fputc(',', f);
        card_name(g->cards[i], buf, sizeof buf);
        json_string(f, buf);
    }
    fprintf(f, "],\"guesses\":[");
    for (int i = 0; i < g->ncards; i++) {
        if (i)
            fputc(',', f);
        json_string(f, guess_word((rtb_stage_t)i, g->guess[i]));
    }
    fprintf(f, "],\"result\":");
    json_string(f, outcome_json(g));
    fprintf(f, ",\"payout\":%ld,\"net\":%ld}\n", g->payout,
            g->payout - g->bet);
}

/* ---- rule self-test (pure predicates, no RNG) -------------------------- */

static card_t mk(int rank, int suit)
{
    return (card_t){ (uint8_t)rank, (uint8_t)suit };
}

#define CLUB 0
#define DIAM 1
#define HEART 2
#define SPADE 3

static int check_case(int *pass, int *failed, const char *label,
                      const char *got, const char *want)
{
    bool ok = strcmp(got, want) == 0;
    printf("%-24s %-10s %s\n", label, got, ok ? "ok" : "FAIL");
    if (ok)
        (*pass)++;
    else
        (*failed)++;
    return ok;
}

static const char *cmp_word(card_t a, card_t b)
{
    int c = rtb_compare_rank(a, b);
    return c < 0 ? "higher" : c > 0 ? "lower" : "equal";
}

/* Round 2 as the player sees it: guess 0 = higher, 1 = lower. */
static const char *hilo_word(card_t first, card_t next, int guess)
{
    return rtb_higher_wins(first, next) == (guess == 0) ? "win" : "loss";
}

/* Round 3 as the player sees it: guess 0 = inside, 1 = outside. */
static const char *inout_word(card_t a, card_t b, card_t c, int guess)
{
    bool won = guess == 0 ? rtb_is_inside(a, b, c) : rtb_is_outside(a, b, c);
    return won ? "win" : "loss";
}

static int run_check(void)
{
    int pass = 0, failed = 0;
    card_t c7 = mk(7, HEART), cJ = mk(11, CLUB);

    puts("ridethebus rule self-test");

    check_case(&pass, &failed, "red heart",
               rtb_is_red(mk(5, HEART)) ? "red" : "black", "red");
    check_case(&pass, &failed, "red diamond",
               rtb_is_red(mk(5, DIAM)) ? "red" : "black", "red");
    check_case(&pass, &failed, "red club",
               rtb_is_red(mk(5, CLUB)) ? "red" : "black", "black");
    check_case(&pass, &failed, "red spade",
               rtb_is_red(mk(5, SPADE)) ? "red" : "black", "black");

    /* rank order: second card relative to the first, ace above king */
    check_case(&pass, &failed, "cmp 7vJ", cmp_word(c7, cJ), "higher");
    check_case(&pass, &failed, "cmp Jv7", cmp_word(cJ, c7), "lower");
    check_case(&pass, &failed, "cmp 7v7",
               cmp_word(c7, mk(7, SPADE)), "equal");
    check_case(&pass, &failed, "cmp KvA",
               cmp_word(mk(13, CLUB), mk(1, SPADE)), "higher");
    check_case(&pass, &failed, "cmp AvK",
               cmp_word(mk(1, SPADE), mk(13, CLUB)), "lower");
    check_case(&pass, &failed, "cmp AvQ",
               cmp_word(mk(1, SPADE), mk(12, CLUB)), "lower");

    /* round 2: an equal rank counts as HIGHER */
    check_case(&pass, &failed, "hilo 7 higher 7",
               hilo_word(c7, mk(7, SPADE), 0), "win");
    check_case(&pass, &failed, "hilo 7 lower 7",
               hilo_word(c7, mk(7, SPADE), 1), "loss");
    check_case(&pass, &failed, "hilo 7 higher J",
               hilo_word(c7, cJ, 0), "win");
    check_case(&pass, &failed, "hilo 7 lower J",
               hilo_word(c7, cJ, 1), "loss");
    check_case(&pass, &failed, "hilo 7 higher 4",
               hilo_word(c7, mk(4, SPADE), 0), "loss");
    check_case(&pass, &failed, "hilo 7 lower 4",
               hilo_word(c7, mk(4, SPADE), 1), "win");
    check_case(&pass, &failed, "hilo K higher A",
               hilo_word(mk(13, CLUB), mk(1, SPADE), 0), "win");

    /* round 3: the boundary ranks count as INSIDE */
    check_case(&pass, &failed, "inout 7J inside 9",
               inout_word(c7, cJ, mk(9, SPADE), 0), "win");
    check_case(&pass, &failed, "inout 7J inside 7",
               inout_word(c7, cJ, mk(7, SPADE), 0), "win");
    check_case(&pass, &failed, "inout 7J inside J",
               inout_word(c7, cJ, mk(11, SPADE), 0), "win");
    check_case(&pass, &failed, "inout 7J outside 7",
               inout_word(c7, cJ, mk(7, SPADE), 1), "loss");
    check_case(&pass, &failed, "inout 7J outside J",
               inout_word(c7, cJ, mk(11, SPADE), 1), "loss");
    check_case(&pass, &failed, "inout 7J outside K",
               inout_word(c7, cJ, mk(13, SPADE), 1), "win");
    check_case(&pass, &failed, "inout 7J inside K",
               inout_word(c7, cJ, mk(13, SPADE), 0), "loss");
    check_case(&pass, &failed, "inout 7J outside 4",
               inout_word(c7, cJ, mk(4, SPADE), 1), "win");
    /* ace is high, so it lies outside a 7-J range */
    check_case(&pass, &failed, "inout 7J outside A",
               inout_word(c7, cJ, mk(1, SPADE), 1), "win");

    check_case(&pass, &failed, "suit s+s",
               rtb_suit_matches(mk(3, SPADE), SPADE) ? "match" : "nomatch",
               "match");
    check_case(&pass, &failed, "suit s+h",
               rtb_suit_matches(mk(3, SPADE), HEART) ? "match" : "nomatch",
               "nomatch");

    printf("check: %d passed, %d failed\n", pass, failed);
    return failed == 0 ? 0 : 1;
}

/* ---- argument parsing --------------------------------------------------- */

static int parse_args(const cli_t *cli, rtb_script_t *sc, long *bet,
                      int *cashout_after, bool *check)
{
    *bet = RTB_BET_DEFAULT;
    *cashout_after = RTB_ROUNDS;
    *check = false;
    sc->n = sc->pos = 0;

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];

        if (bet_is(b, "check")) {
            if (bet_has_value(b) || cli->nbets != 1) {
                fprintf(stderr, "ridethebus: 'check' must be the only "
                                "argument\n");
                return 2;
            }
            *check = true;
            continue;
        }
        if (bet_is(b, "bet")) {
            if (b->nvalues != 1 || b->values[0] < 1 ||
                b->values[0] > RTB_BET_MAX) {
                fprintf(stderr, "ridethebus: bet '%s': wager must be "
                                "1-%d\n", b->raw, RTB_BET_MAX);
                return 2;
            }
            *bet = b->values[0];
            continue;
        }
        /* only with a value: a bare "cashout" is a cash-out action */
        if (bet_is(b, "cashout") && bet_has_value(b)) {
            if (b->nvalues != 1 || b->values[0] < 1 ||
                b->values[0] > RTB_ROUNDS) {
                fprintf(stderr, "ridethebus: '%s': cashout round must be "
                                "1-%d\n", b->raw, RTB_ROUNDS);
                return 2;
            }
            *cashout_after = b->values[0];
            continue;
        }
        if (bet_has_value(b)) {
            fprintf(stderr, "ridethebus: unknown argument '%s'\n", b->raw);
            return 2;
        }

        /* bare token: one or more comma-separated actions */
        const char *p = b->name;
        for (;;) {
            const char *end = strchr(p, ',');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            char word[16];
            if (len == 0 || len >= sizeof word) {
                fprintf(stderr, "ridethebus: bad action list '%s'\n",
                        b->raw);
                return 2;
            }
            memcpy(word, p, len);
            word[len] = '\0';
            if (script_add(sc, word, b->raw))
                return 2;
            if (!end)
                break;
            p = end + 1;
        }
    }
    return 0;
}

/* ---- driver ------------------------------------------------------------- */

int ridethebus_run(const cli_t *cli, rng_t *rng)
{
    rtb_script_t sc;
    long bet;
    int  cashout_after;
    bool check;

    if (cli->gui) {
        if (cli->nbets != 0 || cli->quiet || cli->json || cli->stats ||
            cli->trainer || cli->iterations != 1) {
            fprintf(stderr, "ridethebus: --gui takes no other arguments "
                            "(only --seed)\n");
            return 2;
        }
#ifdef CASINO_GUI
        return rtb_gui_run(rng);
#else
        fprintf(stderr, "ridethebus: this build has no GUI support "
                        "(install raylib and run make again)\n");
        return 2;
#endif
    }

    if (parse_args(cli, &sc, &bet, &cashout_after, &check))
        return 2;
    if (check)
        return run_check();

    bool machine = cli->quiet || cli->json || cli->stats;
    bool scripted = sc.n > 0;
    FILE *disp = machine ? stderr : stdout;

    rtb_agent_t ag = {
        .mode = cli->stats ? (scripted ? AG_SCRIPT : AG_RANDOM)
                           : (scripted ? AG_SCRIPT : AG_INTERACTIVE),
        .sc = &sc,
        .rng = rng,
        .disp = disp,
        .display = false,
        .cashout_after = cashout_after,
    };
    bool interactive = ag.mode == AG_INTERACTIVE;
    ag.display = interactive || (!machine && cli->iterations == 1);

    long long reached[RTB_ROUNDS] = { 0 }, won[RTB_ROUNDS] = { 0 };
    long long cashouts = 0, busses = 0, losses = 0;
    long long staked = 0, paid = 0;

    for (long it = 0; it < cli->iterations; it++) {
        rtb_game_t g;

        sc.pos = 0;                 /* the script replays each game */
        if (ag.display) {
            printf("========================================\n");
            printf("              RIDE THE BUS\n");
            printf("========================================\n\n");
            printf("Bet: %ld\n", bet);
        }

        if (play_game(&ag, rng, bet, &g) == 2)
            return 2;
        if (g.walked && g.rounds_won == 0 && g.ncards == 0) {
            if (ag.display)
                fprintf(disp, "\n(no bet placed)\n");
            return 0;               /* EOF at the very first prompt */
        }

        staked += g.bet;
        paid += g.payout;
        /* rounds actually played: every won round, plus the round that
         * ended the game when it was lost */
        for (int r = 0; r < RTB_ROUNDS; r++) {
            if (r < g.rounds_won) {
                reached[r]++;
                won[r]++;
            } else if (r == g.rounds_won && g.outcome == RTB_LOSS) {
                reached[r]++;
            }
        }
        if (g.outcome == RTB_BUS)
            busses++;
        else if (g.outcome == RTB_CASHOUT)
            cashouts++;
        else
            losses++;

        if (cli->stats)
            continue;

        if (cli->json) {
            result_json(stdout, &g);
        } else if (cli->quiet) {
            printf("%s rounds=%d bet=%ld payout=%ld net=%+ld\n",
                   outcome_word(&g), g.rounds_won, g.bet, g.payout,
                   g.payout - g.bet);
        } else if (!ag.display) {
            printf("%s rounds=%d payout=%ld net=%+ld\n", outcome_word(&g),
                   g.rounds_won, g.payout, g.payout - g.bet);
        } else {
            print_result(stdout, &g);
        }
    }

    if (cli->stats) {
        double ret = staked ? (double)paid / (double)staked : 0.0;
        if (cli->json) {
            printf("{\"game\":\"ridethebus\",\"iterations\":%ld,"
                   "\"bet\":%ld,\"rounds\":[", cli->iterations, bet);
            for (int r = 0; r < RTB_ROUNDS; r++)
                printf("%s{\"round\":%d,\"reached\":%lld,\"won\":%lld}",
                       r ? "," : "", r + 1, reached[r], won[r]);
            printf("],\"completed\":%lld,\"cashouts\":%lld,"
                   "\"losses\":%lld,\"staked\":%lld,\"payout\":%lld,"
                   "\"net\":%lld,\"return_per_unit\":%.6f}\n",
                   busses, cashouts, losses, staked, paid, paid - staked,
                   ret);
        } else if (cli->quiet) {
            printf("runs=%ld bet=%ld completed=%lld cashouts=%lld "
                   "losses=%lld", cli->iterations, bet, busses, cashouts,
                   losses);
            for (int r = 0; r < RTB_ROUNDS; r++)
                printf(" r%d=%lld/%lld", r + 1, won[r], reached[r]);
            printf(" staked=%lld payout=%lld net=%lld return=%.4f\n",
                   staked, paid, paid - staked, ret);
        } else {
            printf("Iterations: %ld   Bet: %ld   Strategy: %s\n",
                   cli->iterations, bet,
                   scripted ? "scripted"
                            : cashout_after >= RTB_ROUNDS
                                  ? "random guesses, ride to round 4"
                                  : "random guesses, cash out early");
            printf("%-4s %-16s %10s %10s %9s\n", "RND", "STAGE", "REACHED",
                   "WON", "RATE%");
            for (int r = 0; r < RTB_ROUNDS; r++)
                printf("%-4d %-16s %10lld %10lld %9.4f\n", r + 1,
                       ROUND_SHORT[r], reached[r], won[r],
                       reached[r] ? 100.0 * (double)won[r] /
                                        (double)reached[r] : 0.0);
            printf("Completed:  %lld (%.4f%%)\n", busses,
                   100.0 * (double)busses / (double)cli->iterations);
            printf("Cashed out: %lld\n", cashouts);
            printf("Lost:       %lld\n", losses);
            printf("Staked:     %lld\n", staked);
            printf("Payout:     %lld\n", paid);
            printf("Net units:  %+lld (%.4f per unit staked)\n",
                   paid - staked, ret - 1.0);
        }
    }
    return 0;
}

void ridethebus_list_bets(void)
{
    puts("ride the bus: four-stage card game, one wager per game");
    puts("rounds:");
    puts("  1 RED OR BLACK      [r]ed / [b]lack          pays 2x");
    puts("  2 HIGHER OR LOWER   [h]igher / [l]ower       pays 3x");
    puts("  3 INSIDE OR OUTSIDE [i]nside / [o]utside     pays 4x");
    puts("  4 SUIT              [h]earts [d]iamonds [c]lubs [s]pades "
         "pays 20x");
    puts("ties (ace is high, every card resolves its round - no re-draws):");
    puts("  round 2: an equal rank counts as HIGHER");
    puts("  round 3: either boundary rank counts as INSIDE");
    puts("from round 2 on, [x] cash out shares the prompt with the round's");
    puts("own options: picking a guess is what keeps you playing, and");
    puts("losing forfeits the whole wager.  payouts are multiples of the");
    puts("ORIGINAL wager, not of the running total.");
    puts("arguments:");
    puts("  bet:N          wager for the game (default 100)");
    puts("  r,h,o,s        scripted guesses, one per round");
    puts("  x | cash       cash out instead of playing the next round");
    puts("  cashout:N      simulation only: cash out after round N");
    puts("  check          run the rule self-test and exit");
    puts("usage:");
    puts("  ridethebus                 interactive game");
    puts("  ridethebus r,h,o,s         scripted: try all four rounds");
    puts("  ridethebus r,h,x           scripted: cash out after round 2");
    puts("  ridethebus --runs 100000   simulate with random guesses");
    puts("  ridethebus --gui           graphical table (raylib builds)");
    puts("results: BUS (all four rounds) | CASHOUT | LOSS");
}
