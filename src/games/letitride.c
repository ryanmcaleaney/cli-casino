#include "letitride.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cardart.h"
#include "cards.h"
#include "output.h"
#include "poker.h"

#ifdef CASINO_GUI
#include "gui/letitride_gui.h"
#endif

/*
 * The pay table, and the only copy of it: profit per 1 unit staked,
 * indexed by lir_cat_t (low to high).  A winning wager gets its stake
 * back on top.  Frontends read it through lir_front_payout().
 */
static const int PAYOUT[LIR_NCATS] = {
    0, 1, 2, 3, 5, 8, 11, 50, 200, 1000
};

static const char *const CAT_NAME[LIR_NCATS] = {
    "Nothing", "Pair of Tens or Better", "Two Pair", "Three of a Kind",
    "Straight", "Flush", "Full House", "Four of a Kind", "Straight Flush",
    "Royal Flush"
};
static const char *const CAT_TOKEN[LIR_NCATS] = {
    "NOTHING", "PAIR_TENS_OR_BETTER", "TWO_PAIR", "THREE_OF_A_KIND",
    "STRAIGHT", "FLUSH", "FULL_HOUSE", "FOUR_OF_A_KIND", "STRAIGHT_FLUSH",
    "ROYAL_FLUSH"
};
static const char *const CAT_JSON[LIR_NCATS] = {
    "nothing", "pair_tens_or_better", "two_pair", "three_of_a_kind",
    "straight", "flush", "full_house", "four_of_a_kind", "straight_flush",
    "royal_flush"
};
/* Short names for the compact GUI pay table. */
static const char *const CAT_SHORT[LIR_NCATS] = {
    "NOTHING", "TENS OR BETTER", "TWO PAIR", "THREE OF A KIND", "STRAIGHT",
    "FLUSH", "FULL HOUSE", "FOUR OF A KIND", "STRAIGHT FLUSH",
    "ROYAL FLUSH"
};

/* ---- hand classification ------------------------------------------------ */

/*
 * The shared five-card evaluator does the ranking; the only thing decided
 * here is where a bare pair falls.  Let It Ride qualifies at TENS or
 * better - poker.c numbers ranks 1 = ace, 10 = ten - which is a rank lower
 * than video poker's jacks or better.  A royal flush is its own category
 * and must not fall through to the straight-flush payout.
 */
lir_cat_t lir_classify(const card_t hand[LIR_HAND])
{
    poker_eval_t ev = poker_eval5(hand);

    switch (ev.cat) {
    case POKER_ROYAL_FLUSH:     return LIR_ROYAL_FLUSH;
    case POKER_STRAIGHT_FLUSH:  return LIR_STRAIGHT_FLUSH;
    case POKER_FOUR_OF_A_KIND:  return LIR_FOUR_OF_A_KIND;
    case POKER_FULL_HOUSE:      return LIR_FULL_HOUSE;
    case POKER_FLUSH:           return LIR_FLUSH;
    case POKER_STRAIGHT:        return LIR_STRAIGHT;
    case POKER_THREE_OF_A_KIND: return LIR_THREE_OF_A_KIND;
    case POKER_TWO_PAIR:        return LIR_TWO_PAIR;
    case POKER_PAIR:
        return (ev.pair_rank == 1 || ev.pair_rank >= 10) ? LIR_PAIR_TENS
                                                         : LIR_NOTHING;
    default:                    return LIR_NOTHING;
    }
}

/* ---- one round ---------------------------------------------------------- */

static void round_start(lir_round_t *r, long bet)
{
    memset(r, 0, sizeof *r);
    r->bet = bet;
    r->stage = LIR_STAGE_DECISION1;
    for (int i = 0; i < LIR_BETS; i++)
        r->riding[i] = true;
    r->nriding = LIR_BETS;
    r->committed = (long)LIR_BETS * bet;
}

void lir_round_deal(lir_round_t *r, rng_t *rng, long bet)
{
    shoe_t shoe;

    round_start(r, bet);
    shoe_init(&shoe, 1);
    shoe_shuffle(&shoe, rng);
    for (int i = 0; i < LIR_PLAYER_CARDS; i++)
        r->player[i] = shoe_draw(&shoe);
    for (int i = 0; i < LIR_COMMUNITY; i++)
        r->community[i] = shoe_draw(&shoe);
}

void lir_round_deal_fixed(lir_round_t *r, const card_t player[LIR_PLAYER_CARDS],
                          const card_t community[LIR_COMMUNITY], long bet)
{
    round_start(r, bet);
    for (int i = 0; i < LIR_PLAYER_CARDS; i++)
        r->player[i] = player[i];
    for (int i = 0; i < LIR_COMMUNITY; i++)
        r->community[i] = community[i];
}

const card_t *lir_community_visible(const lir_round_t *r, int i)
{
    if (i < 0 || i >= LIR_COMMUNITY || !r->revealed[i])
        return NULL;
    return &r->community[i];
}

void lir_round_hand(const lir_round_t *r, card_t out[LIR_HAND])
{
    for (int i = 0; i < LIR_PLAYER_CARDS; i++)
        out[i] = r->player[i];
    for (int i = 0; i < LIR_COMMUNITY; i++)
        out[LIR_PLAYER_CARDS + i] = r->community[i];
}

/*
 * Settle the showdown.  Every wager still riding is paid independently
 * from the same pay table, so three riding wagers on a full house pay
 * three times over; a losing hand costs only what was still riding.
 */
static void settle(lir_round_t *r)
{
    card_t hand[LIR_HAND];
    int    pay;

    lir_round_hand(r, hand);
    r->cat = lir_classify(hand);
    pay = PAYOUT[r->cat];

    r->wagered = (long)r->nriding * r->bet;
    r->returned = 0;
    if (pay > 0)
        for (int i = 0; i < LIR_BETS; i++)
            if (r->riding[i])
                r->returned += r->bet + (long)pay * r->bet;
    r->settled = true;
    r->stage = LIR_STAGE_DONE;
}

void lir_round_decide(lir_round_t *r, bool pull)
{
    int bet_index;

    if (r->stage == LIR_STAGE_DONE)
        return;
    /* decision 1 governs bet 1, decision 2 governs bet 2; bet 3 rides */
    bet_index = r->stage == LIR_STAGE_DECISION1 ? 0 : 1;

    if (pull) {
        r->riding[bet_index] = false;
        r->nriding--;
        r->pulled_back += r->bet;
    }

    if (r->stage == LIR_STAGE_DECISION1) {
        r->revealed[0] = true;
        r->stage = LIR_STAGE_DECISION2;
    } else {
        r->revealed[1] = true;
        settle(r);
    }
}

/* ---- frontend interface (see letitride.h) ------------------------------- */

const char *lir_front_cat_name(lir_cat_t c)
{
    return CAT_NAME[c];
}

const char *lir_front_cat_token(lir_cat_t c)
{
    return CAT_TOKEN[c];
}

int lir_front_payout(lir_cat_t c)
{
    return PAYOUT[c];
}

void lir_front_credits(char *buf, size_t len, long v, bool sign)
{
    snprintf(buf, len, "%s%ld", sign && v > 0 ? "+" : "", v);
}

/* ---- GUI session -------------------------------------------------------- */

void lir_session_start(lir_session_t *s)
{
    memset(s, 0, sizeof *s);
    s->bankroll = LIR_BANKROLL_START;
    s->bet = LIR_BET_DEFAULT;
    s->phase = LIR_PHASE_BET;
}

void lir_set_bet(lir_session_t *s, long v)
{
    if (s->phase == LIR_PHASE_DECISION)
        return;
    if (v < LIR_BET_MIN)
        v = LIR_BET_MIN;
    if (v > LIR_BET_MAX)
        v = LIR_BET_MAX;
    /* all three wagers go up at the deal, so all three must be affordable */
    if (v * LIR_BETS > s->bankroll)
        v = s->bankroll / LIR_BETS;
    if (v < LIR_BET_MIN)
        v = LIR_BET_MIN;            /* lir_can_deal() then reports false */
    s->bet = v;
}

bool lir_can_deal(const lir_session_t *s)
{
    return s->phase != LIR_PHASE_DECISION && s->bet >= LIR_BET_MIN &&
           s->bankroll >= s->bet * LIR_BETS;
}

void lir_deal(lir_session_t *s, rng_t *rng)
{
    if (!lir_can_deal(s))
        return;
    lir_round_deal(&s->round, rng, s->bet);
    s->bankroll -= s->round.committed;
    s->phase = LIR_PHASE_DECISION;
}

/* A pulled wager comes straight back to the bankroll; the showdown pays
 * whatever the riding wagers earned. */
void lir_decide(lir_session_t *s, bool pull)
{
    long before;

    if (s->phase != LIR_PHASE_DECISION)
        return;
    before = s->round.pulled_back;
    lir_round_decide(&s->round, pull);
    s->bankroll += s->round.pulled_back - before;
    if (s->round.settled) {
        s->bankroll += s->round.returned;
        s->phase = LIR_PHASE_SETTLED;
    }
}

void lir_bankroll_reset(lir_session_t *s)
{
    if (s->phase == LIR_PHASE_DECISION)
        return;
    s->bankroll = LIR_BANKROLL_START;
    s->phase = LIR_PHASE_BET;
    lir_set_bet(s, s->bet);
}

/* ---- fixed hands: the deal:... hook and the self-test ------------------- */

static int parse_card(const char *w, size_t len, card_t *out)
{
    int rank, suit;

    if (len == 3 && w[0] == '1' && w[1] == '0') {
        rank = 10;
        w += 2;
    } else if (len == 2) {
        switch (w[0]) {
        case 'a': rank = 1;  break;
        case 'j': rank = 11; break;
        case 'q': rank = 12; break;
        case 'k': rank = 13; break;
        default:
            if (w[0] >= '2' && w[0] <= '9')
                rank = w[0] - '0';
            else
                return -1;
        }
        w++;
    } else {
        return -1;
    }
    switch (w[0]) {
    case 'c': suit = 0; break;
    case 'd': suit = 1; break;
    case 'h': suit = 2; break;
    case 's': suit = 3; break;
    default: return -1;
    }
    out->rank = (uint8_t)rank;
    out->suit = (uint8_t)suit;
    return 0;
}

/* "ah,kh,qh,jh,10h" -> the player's three cards then the two community. */
static int parse_deal(const char *s, card_t player[LIR_PLAYER_CARDS],
                      card_t community[LIR_COMMUNITY])
{
    card_t all[LIR_HAND];
    int    n = 0;

    while (*s) {
        const char *end = strchr(s, ',');
        size_t len = end ? (size_t)(end - s) : strlen(s);

        if (n == LIR_HAND || parse_card(s, len, &all[n]) < 0)
            return -1;
        n++;
        if (!end)
            break;
        s = end + 1;
        if (*s == '\0')
            return -1;              /* trailing comma */
    }
    if (n != LIR_HAND)
        return -1;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (all[i].rank == all[j].rank && all[i].suit == all[j].suit)
                return -1;          /* duplicate card */
    for (int i = 0; i < LIR_PLAYER_CARDS; i++)
        player[i] = all[i];
    for (int i = 0; i < LIR_COMMUNITY; i++)
        community[i] = all[LIR_PLAYER_CARDS + i];
    return 0;
}

/* ---- scripted and interactive decisions --------------------------------- */

#define LIR_DECISIONS 2

typedef struct {
    bool   scripted;
    bool   pull[LIR_DECISIONS];     /* scripted answers */
    int    n;                       /* how many were supplied */
    FILE  *disp;
    bool   display;
} lir_agent_t;

static int normalise(const char *in, char *out, size_t len)
{
    size_t n = 0;

    while (*in && isspace((unsigned char)*in))
        in++;
    while (*in && !isspace((unsigned char)*in)) {
        if (n + 1 >= len)
            return -1;
        out[n++] = (char)tolower((unsigned char)*in++);
    }
    out[n] = '\0';
    return 0;
}

/* 1 = pull, 0 = ride, -1 = not an action word. */
static int action_lookup(const char *w)
{
    if (strcmp(w, "r") == 0 || strcmp(w, "ride") == 0 ||
        strcmp(w, "letitride") == 0)
        return 0;
    if (strcmp(w, "p") == 0 || strcmp(w, "pull") == 0)
        return 1;
    return -1;
}

/* Returns 1 to pull, 0 to ride, or -1 on EOF at an interactive prompt. */
static int decide_at(lir_agent_t *ag, int stage)
{
    if (ag->scripted) {
        bool pull = ag->pull[stage];

        if (ag->display)
            fprintf(ag->disp, "[r]ide / [p]ull\n> %s\n",
                    pull ? "pull" : "ride");
        return pull;
    }

    for (;;) {
        char line[64], norm[16];
        int  a;

        fprintf(ag->disp, "[r]ide / [p]ull\n> ");
        fflush(ag->disp);
        if (!fgets(line, sizeof line, stdin)) {
            fprintf(ag->disp, "\n");
            return -1;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (normalise(line, norm, sizeof norm) == 0 &&
            (a = action_lookup(norm)) >= 0)
            return a;
        fprintf(ag->disp, "invalid choice, try again\n");
    }
}

/* ---- display ------------------------------------------------------------ */

/* Only the cards the round has turned over are ever printed. */
static void show_cards(FILE *f, const lir_round_t *r, const char *label)
{
    card_t vis[LIR_HAND];
    char   buf[8];
    int    n = 0;

    for (int i = 0; i < LIR_PLAYER_CARDS; i++)
        vis[n++] = r->player[i];
    for (int i = 0; i < LIR_COMMUNITY; i++) {
        const card_t *c = lir_community_visible(r, i);
        if (c)
            vis[n++] = *c;
    }

    if (cardart_enabled(f)) {
        fprintf(f, "%s\n", label);
        cardart_hand(f, vis, NULL, n);
        return;
    }
    fprintf(f, "%-11s", label);
    for (int i = 0; i < n; i++) {
        card_name(vis[i], buf, sizeof buf);
        fprintf(f, " %s", buf);
    }
    fputc('\n', f);
}

static void bets_str(char *buf, size_t len, const lir_round_t *r)
{
    snprintf(buf, len, "%s,%s,%s",
             r->riding[0] ? "ride" : "pulled",
             r->riding[1] ? "ride" : "pulled",
             r->riding[2] ? "ride" : "pulled");
}

static void print_result(FILE *f, const lir_round_t *r)
{
    char pull[24], w[24], back[24], net[24];
    int  pay = lir_front_payout(r->cat);

    lir_front_credits(pull, sizeof pull, r->pulled_back, false);
    lir_front_credits(w, sizeof w, r->wagered, false);
    lir_front_credits(back, sizeof back, r->returned, false);
    lir_front_credits(net, sizeof net, r->returned - r->wagered, true);

    fprintf(f, "\nHand:       %s", CAT_NAME[r->cat]);
    if (pay > 0)
        fprintf(f, " (pays %d:1)", pay);
    fputc('\n', f);
    fprintf(f, "Bet 1:      %s\n", r->riding[0] ? "riding" : "PULLED");
    fprintf(f, "Bet 2:      %s\n", r->riding[1] ? "riding" : "PULLED");
    fprintf(f, "Bet 3:      riding\n");
    fprintf(f, "Riding:     %d of %d\n", r->nriding, LIR_BETS);
    fprintf(f, "Pulled back:%8s\n", pull);
    fprintf(f, "Wagered:    %8s\n", w);
    fprintf(f, "Returned:   %8s\n", back);
    fprintf(f, "Net:        %8s\n", net);
}

static void result_quiet(FILE *f, const lir_round_t *r)
{
    char cards[64], bets[32], pull[24], w[24], back[24], net[24], buf[8];
    card_t hand[LIR_HAND];
    size_t at = 0;

    lir_round_hand(r, hand);
    cards[0] = '\0';
    for (int i = 0; i < LIR_HAND; i++) {
        card_name(hand[i], buf, sizeof buf);
        at += (size_t)snprintf(cards + at, sizeof cards - at, "%s%s",
                               i ? "," : "", buf);
    }
    bets_str(bets, sizeof bets, r);
    lir_front_credits(pull, sizeof pull, r->pulled_back, false);
    lir_front_credits(w, sizeof w, r->wagered, false);
    lir_front_credits(back, sizeof back, r->returned, false);
    lir_front_credits(net, sizeof net, r->returned - r->wagered, true);

    fprintf(f, "%s hand=%s cards=%s bets=%s riding=%d pulled_back=%s "
               "wagered=%s returned=%s net=%s\n",
            r->returned > r->wagered ? "WIN" : "LOSS", CAT_JSON[r->cat],
            cards, bets, r->nriding, pull, w, back, net);
}

static void result_json(FILE *f, const lir_round_t *r)
{
    char buf[8];

    fprintf(f, "{\"game\":\"letitride\",\"bet\":%ld,\"committed\":%ld,"
               "\"player\":[", r->bet, r->committed);
    for (int i = 0; i < LIR_PLAYER_CARDS; i++) {
        if (i)
            fputc(',', f);
        card_name(r->player[i], buf, sizeof buf);
        json_string(f, buf);
    }
    fprintf(f, "],\"community\":[");
    for (int i = 0; i < LIR_COMMUNITY; i++) {
        if (i)
            fputc(',', f);
        card_name(r->community[i], buf, sizeof buf);
        json_string(f, buf);
    }
    fprintf(f, "],\"bets\":[");
    for (int i = 0; i < LIR_BETS; i++)
        fprintf(f, "%s%s", i ? "," : "", r->riding[i] ? "\"riding\""
                                                      : "\"pulled\"");
    fprintf(f, "],\"riding\":%d,\"hand\":", r->nriding);
    json_string(f, CAT_JSON[r->cat]);
    fprintf(f, ",\"payout_per_unit\":%d,\"pulled_back\":%ld,"
               "\"wagered\":%ld,\"returned\":%ld,\"net\":%ld}\n",
            lir_front_payout(r->cat), r->pulled_back, r->wagered,
            r->returned, r->returned - r->wagered);
}

/* ---- statistics --------------------------------------------------------- */

typedef struct {
    long long rounds;
    long long pulls[LIR_DECISIONS];
    long long riding[LIR_BETS + 1];     /* rounds ending with N riding */
    long long cats[LIR_NCATS];
    long long wins, losses;
    long long committed, pulled_back;
    long long wagered, returned;
} lir_stats_t;

static void stats_add(lir_stats_t *st, const lir_round_t *r)
{
    st->rounds++;
    if (!r->riding[0])
        st->pulls[0]++;
    if (!r->riding[1])
        st->pulls[1]++;
    st->riding[r->nriding]++;
    st->cats[r->cat]++;
    if (r->returned > r->wagered)
        st->wins++;
    else
        st->losses++;
    st->committed += r->committed;
    st->pulled_back += r->pulled_back;
    st->wagered += r->wagered;
    st->returned += r->returned;
}

static void stats_print(const cli_t *cli, const lir_stats_t *st, long bet,
                        const lir_agent_t *ag)
{
    const char *s1 = ag->pull[0] ? "pull" : "ride";
    const char *s2 = ag->pull[1] ? "pull" : "ride";
    double ret = st->wagered ? (double)st->returned / (double)st->wagered
                             : 0.0;
    double n = (double)(st->rounds ? st->rounds : 1);
    long long net = st->returned - st->wagered;

    if (cli->json) {
        printf("{\"game\":\"letitride\",\"iterations\":%ld,\"bet\":%ld,"
               "\"strategy\":\"%s,%s\",\"rounds\":%lld,\"bet1_pulls\":%lld,"
               "\"bet2_pulls\":%lld,\"riding\":{\"1\":%lld,\"2\":%lld,"
               "\"3\":%lld},\"hands\":{",
               cli->iterations, bet, s1, s2, st->rounds, st->pulls[0],
               st->pulls[1], st->riding[1], st->riding[2], st->riding[3]);
        for (int c = 0; c < LIR_NCATS; c++) {
            if (c)
                fputc(',', stdout);
            json_string(stdout, CAT_JSON[c]);
            printf(":%lld", st->cats[c]);
        }
        printf("},\"wins\":%lld,\"losses\":%lld,\"committed\":%lld,"
               "\"pulled_back\":%lld,\"wagered\":%lld,\"returned\":%lld,"
               "\"net\":%lld,\"return_per_unit\":%.6f}\n",
               st->wins, st->losses, st->committed, st->pulled_back,
               st->wagered, st->returned, net, ret);
    } else if (cli->quiet) {
        printf("runs=%ld bet=%ld strategy=%s,%s rounds=%lld "
               "bet1_pulls=%lld bet2_pulls=%lld riding1=%lld riding2=%lld "
               "riding3=%lld wins=%lld losses=%lld committed=%lld "
               "pulled_back=%lld wagered=%lld returned=%lld net=%lld "
               "return=%.4f\n",
               cli->iterations, bet, s1, s2, st->rounds, st->pulls[0],
               st->pulls[1], st->riding[1], st->riding[2], st->riding[3],
               st->wins, st->losses, st->committed, st->pulled_back,
               st->wagered, st->returned, net, ret);
    } else {
        printf("Iterations: %lld   Bet: %ld each   Strategy: %s,%s\n",
               st->rounds, bet, s1, s2);
        printf("%-22s %10s %9s\n", "OUTCOME", "COUNT", "RATE%");
        printf("%-22s %10lld %9.4f\n", "bet 1 pulled", st->pulls[0],
               100.0 * (double)st->pulls[0] / n);
        printf("%-22s %10lld %9.4f\n", "bet 2 pulled", st->pulls[1],
               100.0 * (double)st->pulls[1] / n);
        for (int k = 1; k <= LIR_BETS; k++) {
            char lbl[32];
            snprintf(lbl, sizeof lbl, "%d wager%s riding", k,
                     k == 1 ? "" : "s");
            printf("%-22s %10lld %9.4f\n", lbl, st->riding[k],
                   100.0 * (double)st->riding[k] / n);
        }
        printf("%-22s %10lld %9.4f\n", "winning rounds", st->wins,
               100.0 * (double)st->wins / n);
        printf("%-22s %10lld %9.4f\n", "losing rounds", st->losses,
               100.0 * (double)st->losses / n);
        printf("%-22s %10s %9s\n", "FINAL HAND", "COUNT", "RATE%");
        for (int c = LIR_NCATS - 1; c >= 0; c--)
            printf("%-22s %10lld %9.4f\n", CAT_JSON[c], st->cats[c],
                   100.0 * (double)st->cats[c] / n);
        printf("Committed:   %lld\n", st->committed);
        printf("Pulled back: %lld\n", st->pulled_back);
        printf("Wagered:     %lld  (only what stayed at risk)\n",
               st->wagered);
        printf("Returned:    %lld\n", st->returned);
        printf("Net units:   %+lld (%.4f per unit wagered)\n", net,
               ret - 1.0);
    }
}

/* ---- rule self-test (pure classification and settlement, no RNG) ------- */

static int check_case(int *pass, int *failed, const char *label,
                      const char *got, const char *want)
{
    bool ok = strcmp(got, want) == 0;

    printf("%-24s %-26s %s\n", label, got, ok ? "ok" : "FAIL");
    if (ok)
        (*pass)++;
    else
        (*failed)++;
    return ok;
}

/* "ah,kh,qh,jh,10h" -> the pay category, plus its payout. */
static const char *cat_of(const char *spec)
{
    static char out[48];
    card_t p[LIR_PLAYER_CARDS], c[LIR_COMMUNITY], hand[LIR_HAND];
    lir_cat_t cat;

    if (parse_deal(spec, p, c) < 0)
        return "BADSPEC";
    for (int i = 0; i < LIR_PLAYER_CARDS; i++)
        hand[i] = p[i];
    for (int i = 0; i < LIR_COMMUNITY; i++)
        hand[LIR_PLAYER_CARDS + i] = c[i];
    cat = lir_classify(hand);
    snprintf(out, sizeof out, "%s %d:1", CAT_JSON[cat],
             lir_front_payout(cat));
    return out;
}

/* Play a fixed deal with the two scripted decisions and describe the
 * money: what came back from pulls, what gambled, what it paid. */
static const char *settle_of(const char *spec, bool pull1, bool pull2,
                             long bet)
{
    static char out[96];
    card_t p[LIR_PLAYER_CARDS], c[LIR_COMMUNITY];
    lir_round_t r;

    if (parse_deal(spec, p, c) < 0)
        return "BADSPEC";
    lir_round_deal_fixed(&r, p, c, bet);
    lir_round_decide(&r, pull1);
    lir_round_decide(&r, pull2);
    snprintf(out, sizeof out, "ride%d back%ld w%ld r%ld n%+ld", r.nriding,
             r.pulled_back, r.wagered, r.returned,
             r.returned - r.wagered);
    return out;
}

/* What a frontend may draw at each stage, as a card count. */
static const char *visible_of(const char *spec, int decisions)
{
    static char out[32];
    card_t p[LIR_PLAYER_CARDS], c[LIR_COMMUNITY];
    lir_round_t r;
    int seen = 0;

    if (parse_deal(spec, p, c) < 0)
        return "BADSPEC";
    lir_round_deal_fixed(&r, p, c, 25);
    for (int i = 0; i < decisions; i++)
        lir_round_decide(&r, false);
    for (int i = 0; i < LIR_COMMUNITY; i++)
        if (lir_community_visible(&r, i))
            seen++;
    snprintf(out, sizeof out, "%d community", seen);
    return out;
}

static int run_check(void)
{
    int pass = 0, failed = 0;

    puts("letitride rule self-test");

    /* the qualifying pair is TENS or better, not jacks or better */
    check_case(&pass, &failed, "pair of tens", cat_of("10h,10d,2c,5s,7d"),
               "pair_tens_or_better 1:1");
    check_case(&pass, &failed, "pair of nines", cat_of("9h,9d,2c,5s,7d"),
               "nothing 0:1");
    check_case(&pass, &failed, "pair of jacks", cat_of("jh,jd,2c,5s,7d"),
               "pair_tens_or_better 1:1");
    check_case(&pass, &failed, "pair of queens", cat_of("qh,qd,2c,5s,7d"),
               "pair_tens_or_better 1:1");
    check_case(&pass, &failed, "pair of kings", cat_of("kh,kd,2c,5s,7d"),
               "pair_tens_or_better 1:1");
    check_case(&pass, &failed, "pair of aces", cat_of("ah,ad,2c,5s,7d"),
               "pair_tens_or_better 1:1");
    check_case(&pass, &failed, "pair of twos", cat_of("2h,2d,5c,8s,jd"),
               "nothing 0:1");
    check_case(&pass, &failed, "ace high nothing", cat_of("ah,kd,9c,5s,3d"),
               "nothing 0:1");

    /* the rest of the ladder, each at its own price */
    check_case(&pass, &failed, "two pair", cat_of("3h,3d,5c,5s,9d"),
               "two_pair 2:1");
    check_case(&pass, &failed, "three of a kind", cat_of("4h,4d,4c,9s,2d"),
               "three_of_a_kind 3:1");
    check_case(&pass, &failed, "straight", cat_of("5h,6d,7c,8s,9d"),
               "straight 5:1");
    check_case(&pass, &failed, "ace low straight", cat_of("ah,2d,3c,4s,5d"),
               "straight 5:1");
    check_case(&pass, &failed, "flush", cat_of("2h,5h,9h,jh,kh"),
               "flush 8:1");
    check_case(&pass, &failed, "full house", cat_of("6h,6d,6c,9s,9d"),
               "full_house 11:1");
    check_case(&pass, &failed, "four of a kind", cat_of("7h,7d,7c,7s,2d"),
               "four_of_a_kind 50:1");
    check_case(&pass, &failed, "straight flush", cat_of("5h,6h,7h,8h,9h"),
               "straight_flush 200:1");
    /* a royal must not fall through to the straight-flush payout */
    check_case(&pass, &failed, "royal flush", cat_of("10h,jh,qh,kh,ah"),
               "royal_flush 1000:1");
    check_case(&pass, &failed, "king high sf", cat_of("9s,10s,js,qs,ks"),
               "straight_flush 200:1");

    /* decisions: what rides, what comes back, what it costs */
    /* ride,ride keeps all three wagers at risk */
    check_case(&pass, &failed, "ride,ride on a loss",
               settle_of("9h,9d,2c,5s,7d", false, false, 25),
               "ride3 back0 w75 r0 n-75");
    /* pulling bet 1 hands 25 back at once and risks only two */
    check_case(&pass, &failed, "pull,ride on a loss",
               settle_of("9h,9d,2c,5s,7d", true, false, 25),
               "ride2 back25 w50 r0 n-50");
    /* pulling bet 2 as well leaves only bet 3, which can never be pulled */
    check_case(&pass, &failed, "pull,pull on a loss",
               settle_of("9h,9d,2c,5s,7d", true, true, 25),
               "ride1 back50 w25 r0 n-25");
    check_case(&pass, &failed, "ride,pull on a loss",
               settle_of("9h,9d,2c,5s,7d", false, true, 25),
               "ride2 back25 w50 r0 n-50");
    /* every riding wager is paid independently: stake + profit each */
    check_case(&pass, &failed, "ride,ride full house",
               settle_of("6h,6d,6c,9s,9d", false, false, 25),
               "ride3 back0 w75 r900 n+825");
    check_case(&pass, &failed, "pull,ride full house",
               settle_of("6h,6d,6c,9s,9d", true, false, 25),
               "ride2 back25 w50 r600 n+550");
    check_case(&pass, &failed, "pull,pull full house",
               settle_of("6h,6d,6c,9s,9d", true, true, 25),
               "ride1 back50 w25 r300 n+275");
    /* a qualifying pair returns stake plus 1:1 on each riding wager */
    check_case(&pass, &failed, "ride,ride pair of tens",
               settle_of("10h,10d,2c,5s,7d", false, false, 25),
               "ride3 back0 w75 r150 n+75");
    /* the royal pays 1000:1 on every riding wager */
    check_case(&pass, &failed, "ride,ride royal",
               settle_of("10h,jh,qh,kh,ah", false, false, 25),
               "ride3 back0 w75 r75075 n+75000");
    /* the bet size scales the whole round */
    check_case(&pass, &failed, "bet size scales",
               settle_of("6h,6d,6c,9s,9d", false, false, 10),
               "ride3 back0 w30 r360 n+330");

    /* community cards stay face down until their own stage */
    check_case(&pass, &failed, "before decision 1",
               visible_of("10h,jh,qh,kh,ah", 0), "0 community");
    check_case(&pass, &failed, "after decision 1",
               visible_of("10h,jh,qh,kh,ah", 1), "1 community");
    check_case(&pass, &failed, "after decision 2",
               visible_of("10h,jh,qh,kh,ah", 2), "2 community");

    printf("check: %d passed, %d failed\n", pass, failed);
    return failed == 0 ? 0 : 1;
}

/* ---- argument parsing --------------------------------------------------- */

static int script_add(lir_agent_t *ag, const char *word, const char *raw)
{
    int a = action_lookup(word);

    if (a < 0) {
        fprintf(stderr, "letitride: '%s' is not an action "
                        "(valid: ride, pull)\n", raw);
        return 2;
    }
    if (ag->n >= LIR_DECISIONS) {
        fprintf(stderr, "letitride: '%s': there are only %d decisions\n",
                raw, LIR_DECISIONS);
        return 2;
    }
    ag->pull[ag->n++] = a == 1;
    ag->scripted = true;
    return 0;
}

static int parse_args(const cli_t *cli, long *bet, lir_agent_t *ag,
                      bool *check, bool *fixed,
                      card_t player[LIR_PLAYER_CARDS],
                      card_t community[LIR_COMMUNITY])
{
    *bet = LIR_BET_DEFAULT;
    *check = false;
    *fixed = false;

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];

        if (bet_is(b, "check")) {
            if (bet_has_value(b) || cli->nbets != 1) {
                fprintf(stderr, "letitride: 'check' must be the only "
                                "argument\n");
                return 2;
            }
            *check = true;
            continue;
        }
        if (bet_is(b, "bet")) {
            if (b->nvalues != 1 || b->values[0] < LIR_BET_MIN ||
                b->values[0] > LIR_BET_MAX) {
                fprintf(stderr, "letitride: '%s': bet must be %d-%d "
                                "(each of the three wagers)\n", b->raw,
                        LIR_BET_MIN, LIR_BET_MAX);
                return 2;
            }
            *bet = b->values[0];
            continue;
        }
        if (bet_is(b, "deal")) {
            if (*fixed || parse_deal(b->vraw, player, community) < 0) {
                fprintf(stderr, "letitride: '%s': deal needs five distinct "
                                "cards like deal:ah,kh,qh,jh,10h "
                                "(player's three first)\n", b->raw);
                return 2;
            }
            *fixed = true;
            continue;
        }
        if (bet_has_value(b)) {
            fprintf(stderr, "letitride: unknown argument '%s' (valid: "
                            "ride, pull, bet:N, deal:..., check)\n", b->raw);
            return 2;
        }

        /* a bare token is one or more comma-separated actions */
        {
            const char *p = b->name;

            for (;;) {
                const char *end = strchr(p, ',');
                size_t len = end ? (size_t)(end - p) : strlen(p);
                char word[16];

                if (len == 0 || len >= sizeof word) {
                    fprintf(stderr, "letitride: bad action list '%s'\n",
                            b->raw);
                    return 2;
                }
                memcpy(word, p, len);
                word[len] = '\0';
                if (script_add(ag, word, b->raw))
                    return 2;
                if (!end)
                    break;
                p = end + 1;
            }
        }
    }

    if (ag->scripted && ag->n != LIR_DECISIONS) {
        fprintf(stderr, "letitride: %d decisions are needed, got %d "
                        "(e.g. ride,ride or pull,ride)\n", LIR_DECISIONS,
                ag->n);
        return 2;
    }
    return 0;
}

/* ---- driver ------------------------------------------------------------- */

int letitride_run(const cli_t *cli, rng_t *rng)
{
    long        bet;
    lir_agent_t ag = { 0 };
    bool        check, fixed;
    card_t      fp[LIR_PLAYER_CARDS], fc[LIR_COMMUNITY];
    lir_stats_t st = { 0 };

    if (cli->gui) {
        if (cli->nbets != 0 || cli->quiet || cli->json || cli->stats ||
            cli->iterations != 1) {
            fprintf(stderr, "letitride: --gui takes no other arguments "
                            "(only --seed)\n");
            return 2;
        }
#ifdef CASINO_GUI
        return lir_gui_run(rng);
#else
        fprintf(stderr, "letitride: this build has no GUI support "
                        "(install raylib and run make again)\n");
        return 2;
#endif
    }

    if (parse_args(cli, &bet, &ag, &check, &fixed, fp, fc))
        return 2;
    if (check)
        return run_check();

    if (cli->stats && !ag.scripted) {
        fprintf(stderr, "letitride: simulation needs scripted decisions "
                        "(e.g. ride,ride)\n");
        return 2;
    }
    if (fixed && (cli->stats || cli->iterations != 1)) {
        fprintf(stderr, "letitride: deal:... plays one fixed hand and "
                        "cannot be simulated\n");
        return 2;
    }

    bool machine = cli->quiet || cli->json || cli->stats;
    ag.disp = machine ? stderr : stdout;
    ag.display = !ag.scripted || (!machine && cli->iterations == 1);

    for (long it = 0; it < cli->iterations; it++) {
        lir_round_t r;
        char net[24];

        if (fixed)
            lir_round_deal_fixed(&r, fp, fc, bet);
        else
            lir_round_deal(&r, rng, bet);

        if (ag.display) {
            fprintf(ag.disp, "========================================\n");
            fprintf(ag.disp, "             LET IT RIDE\n");
            fprintf(ag.disp, "========================================\n\n");
            fprintf(ag.disp, "Bet: %ld each, %ld committed\n\n", bet,
                    r.committed);
        }

        /* two decisions, each on strictly less than the whole board */
        for (int d = 0; d < LIR_DECISIONS; d++) {
            int a;

            if (ag.display) {
                show_cards(ag.disp, &r,
                           d == 0 ? "Your cards:" : "Board:");
                fputc('\n', ag.disp);
            }
            a = decide_at(&ag, d);
            if (a < 0)
                a = 0;              /* EOF lets the wager ride */
            lir_round_decide(&r, a == 1);
            if (ag.display)
                fputc('\n', ag.disp);
        }

        stats_add(&st, &r);

        if (ag.display)
            show_cards(ag.disp, &r, "Final hand:");

        if (cli->stats)
            continue;

        if (cli->json) {
            result_json(stdout, &r);
        } else if (cli->quiet) {
            result_quiet(stdout, &r);
        } else if (!ag.display) {
            lir_front_credits(net, sizeof net, r.returned - r.wagered, true);
            printf("%s %s riding=%d net=%s\n",
                   r.returned > r.wagered ? "WIN" : "LOSS",
                   CAT_JSON[r.cat], r.nriding, net);
        } else {
            print_result(stdout, &r);
        }
    }

    if (cli->stats)
        stats_print(cli, &st, bet, &ag);
    return 0;
}

void letitride_list_bets(void)
{
    puts("let it ride: three cards plus two community cards, one deck");
    puts("you start with three equal wagers and may take two of them back:");
    puts("  after seeing your three cards      pull bet 1, or let it ride");
    puts("  after the first community card     pull bet 2, or let it ride");
    puts("  bet 3 always rides");
    puts("both community cards join your three for the final five-card");
    puts("hand.  every wager still riding is paid separately from:");
    for (int c = LIR_NCATS - 1; c >= 1; c--)
        printf("  %-22s %4d:1\n", CAT_SHORT[c], lir_front_payout((lir_cat_t)c));
    puts("  anything below a pair of tens loses");
    puts("a winning wager returns its stake on top of the payout.");
    puts("arguments:");
    puts("  ride | pull   one action per decision, e.g. ride,ride or");
    puts("                pull,ride (required for simulation)");
    puts("  bet:N         EACH of the three wagers (default 25, so");
    puts("                bet:25 commits 75)");
    puts("  deal:...      five fixed cards, your three first, e.g.");
    puts("                deal:ah,kh,qh,jh,10h");
    puts("  check         run the rule self-test and exit");
    puts("usage:");
    puts("  letitride                      interactive hand");
    puts("  letitride ride,ride            scripted: never pull");
    puts("  letitride bet:25 pull,ride");
    puts("  letitride ride,ride --runs 100000    simulate");
    puts("  letitride --gui                graphical table");
    puts("results: WIN | LOSS (a pulled wager is returned, never lost)");
}
