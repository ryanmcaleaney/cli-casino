#include "threecard.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cardart.h"
#include "cards.h"
#include "output.h"

#ifdef CASINO_GUI
#include "gui/threecard_gui.h"
#endif

/*
 * The two pay tables, and the only copies of them.  Both are indexed by
 * tc_cat_t (low to high) and quoted per 1 unit of the wager; 0 means the
 * category pays nothing.  Frontends read them through tc_front_*.
 */
static const int ANTE_BONUS[TC_NCATS] = { 0, 0, 0, 1, 4, 5 };
static const int PAIR_PLUS[TC_NCATS]  = { 0, 1, 4, 6, 30, 40 };

static const char *const CAT_NAME[TC_NCATS] = {
    "High Card", "Pair", "Flush", "Straight", "Three of a Kind",
    "Straight Flush"
};
static const char *const CAT_TOKEN[TC_NCATS] = {
    "HIGH_CARD", "PAIR", "FLUSH", "STRAIGHT", "THREE_OF_A_KIND",
    "STRAIGHT_FLUSH"
};
static const char *const CAT_JSON[TC_NCATS] = {
    "high_card", "pair", "flush", "straight", "three_of_a_kind",
    "straight_flush"
};

static const char *const OUTCOME_WORD[] = {
    "FOLD", "NO QUALIFY", "WIN", "LOSS", "PUSH"
};
/* single-word forms for the one-line machine outputs */
static const char *const OUTCOME_TAG[] = {
    "FOLD", "NOQUALIFY", "WIN", "LOSS", "PUSH"
};
static const char *const OUTCOME_JSON[] = {
    "fold", "no_qualify", "player", "dealer", "push"
};

/* ---- evaluation --------------------------------------------------------- */

/*
 * Rank the three cards.  Ace is high (14) everywhere except in A-2-3,
 * the lowest straight, where it drops below the deuce.  Suits only ever
 * matter for the flush test, so equal categories compare on rank alone.
 */
tc_eval_t tc_eval(const card_t hand[TC_CARDS])
{
    tc_eval_t ev = { TC_HIGH_CARD, { 0, 0, 0 }, 0 };
    int  v[TC_CARDS];
    bool flush, straight = false;

    for (int i = 0; i < TC_CARDS; i++)
        v[i] = hand[i].rank == 1 ? 14 : hand[i].rank;

    for (int i = 0; i < TC_CARDS; i++)
        for (int j = i + 1; j < TC_CARDS; j++)
            if (v[j] > v[i]) {
                int t = v[i];
                v[i] = v[j];
                v[j] = t;
            }

    flush = hand[0].suit == hand[1].suit && hand[1].suit == hand[2].suit;

    if (v[0] == v[1] && v[1] == v[2]) {
        ev.cat = TC_THREE_OF_A_KIND;            /* never a flush */
    } else if (v[0] == v[1] || v[1] == v[2]) {
        /* the pair leads, the kicker trails, so kickers compare in place */
        int pair = v[0] == v[1] ? v[0] : v[1];
        int kick = v[0] == v[1] ? v[2] : v[0];
        v[0] = v[1] = pair;
        v[2] = kick;
        ev.cat = TC_PAIR;
    } else {
        if (v[0] - 1 == v[1] && v[1] - 1 == v[2]) {
            straight = true;                    /* includes Q-K-A */
        } else if (v[0] == 14 && v[1] == 3 && v[2] == 2) {
            straight = true;                    /* A-2-3: the ace plays low */
            v[0] = 3;
            v[1] = 2;
            v[2] = 1;
        }
        ev.cat = straight && flush ? TC_STRAIGHT_FLUSH
               : straight          ? TC_STRAIGHT
               : flush             ? TC_FLUSH
                                   : TC_HIGH_CARD;
    }

    for (int i = 0; i < TC_CARDS; i++)
        ev.rank[i] = v[i];
    /* base 15 holds every rank (2..14) without carrying */
    ev.key = (((long)ev.cat * 15 + v[0]) * 15 + v[1]) * 15 + v[2];
    return ev;
}

int tc_compare(const tc_eval_t *a, const tc_eval_t *b)
{
    return a->key < b->key ? -1 : a->key > b->key ? 1 : 0;
}

bool tc_qualifies(const tc_eval_t *dealer)
{
    return dealer->cat > TC_HIGH_CARD || dealer->rank[0] >= TC_QUALIFY_RANK;
}

/* ---- one round ---------------------------------------------------------- */

static void round_start(tc_round_t *r, long ante, long pairplus)
{
    memset(r, 0, sizeof *r);
    r->ante = ante;
    r->pairplus = pairplus;
    r->action = TC_ACT_ASK;
}

void tc_round_deal(tc_round_t *r, rng_t *rng, long ante, long pairplus)
{
    shoe_t shoe;

    round_start(r, ante, pairplus);
    shoe_init(&shoe, 1);
    shoe_shuffle(&shoe, rng);
    for (int i = 0; i < TC_CARDS; i++)
        r->player[i] = shoe_draw(&shoe);
    for (int i = 0; i < TC_CARDS; i++)
        r->dealer[i] = shoe_draw(&shoe);
    r->pev = tc_eval(r->player);
    r->dev = tc_eval(r->dealer);
}

void tc_round_deal_fixed(tc_round_t *r, const card_t player[TC_CARDS],
                         const card_t dealer[TC_CARDS], long ante,
                         long pairplus)
{
    round_start(r, ante, pairplus);
    for (int i = 0; i < TC_CARDS; i++) {
        r->player[i] = player[i];
        r->dealer[i] = dealer[i];
    }
    r->pev = tc_eval(r->player);
    r->dev = tc_eval(r->dealer);
}

/*
 * Settle every wager on the table.  Pair plus stands on the player's own
 * three cards and pays whether or not the ante hand was played; the ante
 * bonus rides on the ante and is paid whenever the hand was played, no
 * matter what the dealer holds.
 */
void tc_round_settle(tc_round_t *r, tc_action_t action)
{
    r->action = action == TC_ACT_FOLD ? TC_ACT_FOLD : TC_ACT_PLAY;
    r->dealer_qualifies = tc_qualifies(&r->dev);
    r->play = 0;
    r->ante_net = r->play_net = r->ante_bonus = r->pairplus_net = 0;

    if (r->pairplus > 0) {
        int pay = PAIR_PLUS[r->pev.cat];
        r->pairplus_net = pay > 0 ? r->pairplus * pay : -r->pairplus;
    }

    if (r->action == TC_ACT_FOLD) {
        r->outcome = TC_OUT_FOLD;
        r->ante_net = -r->ante;
    } else {
        r->play = r->ante;                  /* the play wager matches it */
        r->ante_bonus = (long)ANTE_BONUS[r->pev.cat] * r->ante;

        if (!r->dealer_qualifies) {
            r->outcome = TC_OUT_NO_QUALIFY; /* ante pays 1:1, play pushes */
            r->ante_net = r->ante;
        } else {
            int cmp = tc_compare(&r->pev, &r->dev);

            if (cmp > 0) {
                r->outcome = TC_OUT_PLAYER;
                r->ante_net = r->ante;
                r->play_net = r->play;
            } else if (cmp < 0) {
                r->outcome = TC_OUT_DEALER;
                r->ante_net = -r->ante;
                r->play_net = -r->play;
            } else {
                r->outcome = TC_OUT_PUSH;   /* both wagers come back */
            }
        }
    }

    r->wagered = r->ante + r->play + r->pairplus;
    r->returned = r->wagered + r->ante_net + r->play_net + r->ante_bonus +
                  r->pairplus_net;
    r->settled = true;
}

/* ---- frontend interface (see threecard.h) ------------------------------- */

const char *tc_front_cat_name(tc_cat_t c)
{
    return CAT_NAME[c];
}

const char *tc_front_cat_token(tc_cat_t c)
{
    return CAT_TOKEN[c];
}

int tc_front_ante_bonus(tc_cat_t c)
{
    return ANTE_BONUS[c];
}

int tc_front_pairplus(tc_cat_t c)
{
    return PAIR_PLUS[c];
}

void tc_front_credits(char *buf, size_t len, long v, bool sign)
{
    snprintf(buf, len, "%s%ld", sign && v > 0 ? "+" : "", v);
}

const char *tc_front_outcome_word(tc_outcome_t o)
{
    return OUTCOME_WORD[o];
}

/* ---- GUI session -------------------------------------------------------- */

void tc_session_start(tc_session_t *s)
{
    memset(s, 0, sizeof *s);
    s->bankroll = TC_BANKROLL_START;
    s->ante = TC_ANTE_DEFAULT;
    s->phase = TC_PHASE_BET;
}

void tc_set_ante(tc_session_t *s, long v)
{
    if (s->phase == TC_PHASE_DECISION)
        return;
    if (v < TC_ANTE_MIN)
        v = TC_ANTE_MIN;
    if (v > TC_ANTE_MAX)
        v = TC_ANTE_MAX;
    if (v + s->pairplus > s->bankroll)
        v = s->bankroll - s->pairplus;
    if (v < TC_ANTE_MIN)
        v = TC_ANTE_MIN;            /* tc_can_deal() then reports false */
    s->ante = v;
}

void tc_set_pairplus(tc_session_t *s, long v)
{
    if (s->phase == TC_PHASE_DECISION)
        return;
    if (v < 0)
        v = 0;
    if (v > TC_ANTE_MAX)
        v = TC_ANTE_MAX;
    if (s->ante + v > s->bankroll)
        v = s->bankroll - s->ante;
    if (v < 0)
        v = 0;
    s->pairplus = v;
}

bool tc_can_deal(const tc_session_t *s)
{
    return s->phase != TC_PHASE_DECISION && s->ante >= TC_ANTE_MIN &&
           s->bankroll >= s->ante + s->pairplus;
}

void tc_deal(tc_session_t *s, rng_t *rng)
{
    if (!tc_can_deal(s))
        return;
    s->bankroll -= s->ante + s->pairplus;
    tc_round_deal(&s->round, rng, s->ante, s->pairplus);
    s->phase = TC_PHASE_DECISION;
}

bool tc_can_play(const tc_session_t *s)
{
    return s->phase == TC_PHASE_DECISION && s->bankroll >= s->round.ante;
}

void tc_decide(tc_session_t *s, tc_action_t action)
{
    if (s->phase != TC_PHASE_DECISION)
        return;
    if (action == TC_ACT_PLAY) {
        if (!tc_can_play(s))
            return;                 /* the play wager cannot be funded */
        s->bankroll -= s->round.ante;
    }
    tc_round_settle(&s->round, action);
    s->bankroll += s->round.returned;
    s->phase = TC_PHASE_SETTLED;
}

void tc_bankroll_reset(tc_session_t *s)
{
    if (s->phase == TC_PHASE_DECISION)
        return;
    s->bankroll = TC_BANKROLL_START;
    s->phase = TC_PHASE_BET;
    tc_set_ante(s, s->ante);
    tc_set_pairplus(s, s->pairplus);
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

/* "qh,qd,7c,ks,9d,4c" -> the player's three cards then the dealer's. */
static int parse_deal(const char *s, card_t player[TC_CARDS],
                      card_t dealer[TC_CARDS])
{
    card_t all[2 * TC_CARDS];
    int n = 0;

    while (*s) {
        const char *end = strchr(s, ',');
        size_t len = end ? (size_t)(end - s) : strlen(s);

        if (n == 2 * TC_CARDS || parse_card(s, len, &all[n]) < 0)
            return -1;
        n++;
        if (!end)
            break;
        s = end + 1;
        if (*s == '\0')
            return -1;              /* trailing comma */
    }
    if (n != 2 * TC_CARDS)
        return -1;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (all[i].rank == all[j].rank && all[i].suit == all[j].suit)
                return -1;          /* duplicate card */
    for (int i = 0; i < TC_CARDS; i++) {
        player[i] = all[i];
        dealer[i] = all[TC_CARDS + i];
    }
    return 0;
}

/* ---- the play / fold decision ------------------------------------------- */

typedef struct {
    tc_action_t fixed;      /* TC_ACT_ASK means prompt on stdin */
    FILE       *disp;
    bool        display;
} tc_agent_t;

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

static int action_lookup(const char *w)
{
    if (strcmp(w, "p") == 0 || strcmp(w, "play") == 0)
        return TC_ACT_PLAY;
    if (strcmp(w, "f") == 0 || strcmp(w, "fold") == 0)
        return TC_ACT_FOLD;
    return -1;
}

/* Returns a tc_action_t, or -1 on EOF at an interactive prompt. */
static int decide(tc_agent_t *ag)
{
    if (ag->fixed != TC_ACT_ASK) {
        if (ag->display)
            fprintf(ag->disp, "[p]lay / [f]old\n> %s\n",
                    ag->fixed == TC_ACT_PLAY ? "play" : "fold");
        return ag->fixed;
    }

    for (;;) {
        char line[64], norm[16];
        int  a;

        fprintf(ag->disp, "[p]lay / [f]old\n> ");
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

static void hand_str(char *buf, size_t len, const card_t h[TC_CARDS],
                     char sep)
{
    char a[8], b[8], c[8];

    card_name(h[0], a, sizeof a);
    card_name(h[1], b, sizeof b);
    card_name(h[2], c, sizeof c);
    snprintf(buf, len, "%s%c%s%c%s", a, sep, b, sep, c);
}

static void show_hand(FILE *f, const char *label, const card_t h[TC_CARDS],
                      const tc_eval_t *ev)
{
    char buf[32], lab[16];

    snprintf(lab, sizeof lab, "%s:", label);
    if (cardart_enabled(f)) {
        fprintf(f, "%s\n", lab);
        cardart_hand(f, h, NULL, TC_CARDS);
    } else {
        hand_str(buf, sizeof buf, h, ' ');
        fprintf(f, "%-8s %s\n", lab, buf);
    }
    fprintf(f, "%-8s %s\n", "Hand:", CAT_NAME[ev->cat]);
}

static void print_result(FILE *f, const tc_round_t *r)
{
    char a[24], p[24], b[24], pp[24], w[24], ret[24], net[24];

    tc_front_credits(a, sizeof a, r->ante_net, true);
    tc_front_credits(p, sizeof p, r->play_net, true);
    tc_front_credits(b, sizeof b, r->ante_bonus, true);
    tc_front_credits(pp, sizeof pp, r->pairplus_net, true);
    tc_front_credits(w, sizeof w, r->wagered, false);
    tc_front_credits(ret, sizeof ret, r->returned, false);
    tc_front_credits(net, sizeof net, r->returned - r->wagered, true);

    fprintf(f, "\nResult:      %s\n", OUTCOME_WORD[r->outcome]);
    fprintf(f, "Ante:        %8s\n", a);
    if (r->action == TC_ACT_PLAY)
        fprintf(f, "Play:        %8s%s\n", p,
                r->outcome == TC_OUT_NO_QUALIFY ? "  (push)" : "");
    fprintf(f, "Ante bonus:  %8s\n", b);
    if (r->pairplus > 0)
        fprintf(f, "Pair Plus:   %8s\n", pp);
    fprintf(f, "Wagered:     %8s\n", w);
    fprintf(f, "Returned:    %8s\n", ret);
    fprintf(f, "Net:         %8s\n", net);
}

static void result_quiet(FILE *f, const tc_round_t *r)
{
    char ph[32], dh[32], a[24], p[24], b[24], pp[24], w[24], ret[24],
         net[24];

    hand_str(ph, sizeof ph, r->player, ',');
    hand_str(dh, sizeof dh, r->dealer, ',');
    tc_front_credits(a, sizeof a, r->ante_net, true);
    tc_front_credits(p, sizeof p, r->play_net, true);
    tc_front_credits(b, sizeof b, r->ante_bonus, true);
    tc_front_credits(pp, sizeof pp, r->pairplus_net, true);
    tc_front_credits(w, sizeof w, r->wagered, false);
    tc_front_credits(ret, sizeof ret, r->returned, false);
    tc_front_credits(net, sizeof net, r->returned - r->wagered, true);

    fprintf(f, "%s player=%s hand=%s action=%s dealer=%s dhand=%s "
               "qualifies=%s ante=%s play=%s bonus=%s",
            OUTCOME_TAG[r->outcome], ph, CAT_JSON[r->pev.cat],
            r->action == TC_ACT_PLAY ? "play" : "fold", dh,
            CAT_JSON[r->dev.cat], r->dealer_qualifies ? "yes" : "no",
            a, p, b);
    if (r->pairplus > 0)
        fprintf(f, " pairplus=%s", pp);
    fprintf(f, " wagered=%s returned=%s net=%s\n", w, ret, net);
}

static void cards_json(FILE *f, const card_t h[TC_CARDS])
{
    char buf[8];

    fputc('[', f);
    for (int i = 0; i < TC_CARDS; i++) {
        if (i)
            fputc(',', f);
        card_name(h[i], buf, sizeof buf);
        json_string(f, buf);
    }
    fputc(']', f);
}

static void result_json(FILE *f, const tc_round_t *r)
{
    fprintf(f, "{\"game\":\"threecard\",\"ante\":%ld,\"pairplus\":%ld,"
               "\"player\":{\"cards\":", r->ante, r->pairplus);
    cards_json(f, r->player);
    fprintf(f, ",\"category\":");
    json_string(f, CAT_JSON[r->pev.cat]);
    fprintf(f, "},\"action\":");
    json_string(f, r->action == TC_ACT_PLAY ? "play" : "fold");
    fprintf(f, ",\"dealer\":{\"cards\":");
    cards_json(f, r->dealer);
    fprintf(f, ",\"category\":");
    json_string(f, CAT_JSON[r->dev.cat]);
    fprintf(f, "},\"dealer_qualifies\":%s,\"outcome\":",
            r->dealer_qualifies ? "true" : "false");
    json_string(f, OUTCOME_JSON[r->outcome]);
    fprintf(f, ",\"ante_net\":%ld,\"play_net\":%ld,\"ante_bonus\":%ld,"
               "\"pairplus_net\":%ld,\"wagered\":%ld,\"returned\":%ld,"
               "\"net\":%ld}\n",
            r->ante_net, r->play_net, r->ante_bonus, r->pairplus_net,
            r->wagered, r->returned, r->returned - r->wagered);
}

/* ---- statistics --------------------------------------------------------- */

typedef struct {
    long long rounds, plays, folds;
    long long dealer_qualifies;
    long long player_wins, dealer_wins, pushes, no_qualify;
    long long cats[TC_NCATS];
    long long pairplus_wins, ante_bonus_hits;
    long long wagered, returned;
} tc_stats_t;

static void stats_add(tc_stats_t *st, const tc_round_t *r)
{
    st->rounds++;
    st->wagered += r->wagered;
    st->returned += r->returned;
    st->cats[r->pev.cat]++;
    if (r->dealer_qualifies)
        st->dealer_qualifies++;
    if (r->pairplus > 0 && r->pairplus_net > 0)
        st->pairplus_wins++;
    if (r->ante_bonus > 0)
        st->ante_bonus_hits++;

    switch (r->outcome) {
    case TC_OUT_FOLD:       st->folds++; break;
    case TC_OUT_NO_QUALIFY: st->plays++; st->no_qualify++; break;
    case TC_OUT_PLAYER:     st->plays++; st->player_wins++; break;
    case TC_OUT_DEALER:     st->plays++; st->dealer_wins++; break;
    default:                st->plays++; st->pushes++; break;
    }
}

static void stats_print(const cli_t *cli, const tc_stats_t *st, long ante,
                        long pairplus, tc_action_t strategy)
{
    const char *strat = strategy == TC_ACT_PLAY ? "play" : "fold";
    double ret = st->wagered ? (double)st->returned / (double)st->wagered
                             : 0.0;
    double n = (double)(st->rounds ? st->rounds : 1);
    long   net = st->returned - st->wagered;

    if (cli->json) {
        printf("{\"game\":\"threecard\",\"iterations\":%ld,\"ante\":%ld,"
               "\"pairplus\":%ld,\"strategy\":", cli->iterations, ante,
               pairplus);
        json_string(stdout, strat);
        printf(",\"rounds\":%lld,\"plays\":%lld,\"folds\":%lld,"
               "\"dealer_qualifies\":%lld,\"player_wins\":%lld,"
               "\"dealer_wins\":%lld,\"pushes\":%lld,\"no_qualify\":%lld,"
               "\"player_hands\":{",
               st->rounds, st->plays, st->folds, st->dealer_qualifies,
               st->player_wins, st->dealer_wins, st->pushes,
               st->no_qualify);
        for (int c = 0; c < TC_NCATS; c++) {
            if (c)
                fputc(',', stdout);
            json_string(stdout, CAT_JSON[c]);
            printf(":%lld", st->cats[c]);
        }
        printf("},\"pairplus_wins\":%lld,\"ante_bonus_hits\":%lld,"
               "\"wagered\":%lld,\"returned\":%lld,\"net\":%ld,"
               "\"return_per_unit\":%.6f}\n",
               st->pairplus_wins, st->ante_bonus_hits, st->wagered,
               st->returned, net, ret);
    } else if (cli->quiet) {
        printf("runs=%ld ante=%ld pairplus=%ld strategy=%s rounds=%lld "
               "plays=%lld folds=%lld qualifies=%lld player_wins=%lld "
               "dealer_wins=%lld pushes=%lld no_qualify=%lld "
               "pairplus_wins=%lld bonus_hits=%lld wagered=%lld "
               "returned=%lld net=%ld return=%.4f\n",
               cli->iterations, ante, pairplus, strat, st->rounds,
               st->plays, st->folds, st->dealer_qualifies, st->player_wins,
               st->dealer_wins, st->pushes, st->no_qualify,
               st->pairplus_wins, st->ante_bonus_hits, st->wagered,
               st->returned, net, ret);
    } else {
        printf("Iterations: %lld   Ante: %ld   Pair Plus: %ld   "
               "Strategy: %s\n", st->rounds, ante, pairplus, strat);
        printf("%-16s %10s %9s\n", "OUTCOME", "COUNT", "RATE%");
        printf("%-16s %10lld %9.4f\n", "plays", st->plays,
               100.0 * (double)st->plays / n);
        printf("%-16s %10lld %9.4f\n", "folds", st->folds,
               100.0 * (double)st->folds / n);
        printf("%-16s %10lld %9.4f\n", "dealer qualifies",
               st->dealer_qualifies,
               100.0 * (double)st->dealer_qualifies / n);
        printf("%-16s %10lld %9.4f\n", "player wins", st->player_wins,
               100.0 * (double)st->player_wins / n);
        printf("%-16s %10lld %9.4f\n", "dealer wins", st->dealer_wins,
               100.0 * (double)st->dealer_wins / n);
        printf("%-16s %10lld %9.4f\n", "pushes", st->pushes,
               100.0 * (double)st->pushes / n);
        printf("%-16s %10lld %9.4f\n", "no qualify", st->no_qualify,
               100.0 * (double)st->no_qualify / n);
        printf("%-16s %10s %9s\n", "PLAYER HAND", "COUNT", "RATE%");
        for (int c = TC_NCATS - 1; c >= 0; c--)
            printf("%-16s %10lld %9.4f\n", CAT_JSON[c], st->cats[c],
                   100.0 * (double)st->cats[c] / n);
        printf("Ante bonus hits: %lld\n", st->ante_bonus_hits);
        if (pairplus > 0)
            printf("Pair Plus wins:  %lld\n", st->pairplus_wins);
        printf("Wagered:    %lld\n", st->wagered);
        printf("Returned:   %lld\n", st->returned);
        printf("Net units:  %+ld (%.4f per unit wagered)\n", net,
               ret - 1.0);
    }
}

/* ---- rule self-test (pure evaluation and settlement, no RNG) ----------- */

static int check_case(int *pass, int *failed, const char *label,
                      const char *got, const char *want)
{
    bool ok = strcmp(got, want) == 0;

    printf("%-26s %-22s %s\n", label, got, ok ? "ok" : "FAIL");
    if (ok)
        (*pass)++;
    else
        (*failed)++;
    return ok;
}

/* "qh,qd,7c" -> one hand.  The two hands of a comparison are read
 * separately, so a self-test may reuse a card across them. */
static int parse_hand(const char *s, card_t out[TC_CARDS])
{
    int n = 0;

    while (*s) {
        const char *end = strchr(s, ',');
        size_t len = end ? (size_t)(end - s) : strlen(s);

        if (n == TC_CARDS || parse_card(s, len, &out[n]) < 0)
            return -1;
        n++;
        if (!end)
            break;
        s = end + 1;
    }
    return n == TC_CARDS ? 0 : -1;
}

static const char *cat_of(const char *spec)
{
    card_t h[TC_CARDS];

    if (parse_hand(spec, h) < 0)
        return "BADSPEC";
    return CAT_JSON[tc_eval(h).cat];
}

static const char *cmp_of(const char *a, const char *b)
{
    card_t    ha[TC_CARDS], hb[TC_CARDS];
    tc_eval_t ea, eb;
    int       c;

    if (parse_hand(a, ha) < 0 || parse_hand(b, hb) < 0)
        return "BADSPEC";
    ea = tc_eval(ha);
    eb = tc_eval(hb);
    c = tc_compare(&ea, &eb);
    return c > 0 ? "higher" : c < 0 ? "lower" : "equal";
}

static const char *qual_of(const char *spec)
{
    card_t    h[TC_CARDS];
    tc_eval_t ev;

    if (parse_hand(spec, h) < 0)
        return "BADSPEC";
    ev = tc_eval(h);
    return tc_qualifies(&ev) ? "qualifies" : "no";
}

/* Settle a fixed deal and describe every wager in one line. */
static const char *settle_of(const char *spec, tc_action_t act, long ante,
                             long pairplus)
{
    static char out[96];
    card_t p[TC_CARDS], d[TC_CARDS];
    tc_round_t r;

    if (parse_deal(spec, p, d) < 0)
        return "BADSPEC";
    tc_round_deal_fixed(&r, p, d, ante, pairplus);
    tc_round_settle(&r, act);
    snprintf(out, sizeof out, "a%+ld p%+ld b%+ld pp%+ld n%+ld", r.ante_net,
             r.play_net, r.ante_bonus, r.pairplus_net,
             r.returned - r.wagered);
    return out;
}

static int run_check(void)
{
    int pass = 0, failed = 0;

    puts("threecard rule self-test");

    /* dealer qualification: queen-high or better */
    check_case(&pass, &failed, "qualify Q-high", qual_of("qs,9d,4c"),
               "qualifies");
    check_case(&pass, &failed, "qualify J-high", qual_of("js,9d,4c"), "no");
    check_case(&pass, &failed, "qualify K-high", qual_of("ks,2d,3c"),
               "qualifies");
    check_case(&pass, &failed, "qualify A-high", qual_of("as,9d,4c"),
               "qualifies");
    /* any made hand qualifies, however low */
    check_case(&pass, &failed, "qualify low pair", qual_of("2s,2d,4c"),
               "qualifies");

    /* categories */
    check_case(&pass, &failed, "cat A-2-3 straight", cat_of("as,2d,3c"),
               "straight");
    check_case(&pass, &failed, "cat Q-K-A straight", cat_of("qs,kd,ac"),
               "straight");
    check_case(&pass, &failed, "cat 2-3-4 straight", cat_of("2s,3d,4c"),
               "straight");
    check_case(&pass, &failed, "cat A-2-3 suited", cat_of("ah,2h,3h"),
               "straight_flush");
    check_case(&pass, &failed, "cat Q-K-A suited", cat_of("qh,kh,ah"),
               "straight_flush");
    check_case(&pass, &failed, "cat A-K-Q offsuit", cat_of("ah,kd,qc"),
               "straight");
    check_case(&pass, &failed, "cat K-A-2 not straight", cat_of("kh,ad,2c"),
               "high_card");
    check_case(&pass, &failed, "cat trips", cat_of("7h,7d,7c"),
               "three_of_a_kind");
    check_case(&pass, &failed, "cat flush", cat_of("2h,7h,jh"), "flush");
    check_case(&pass, &failed, "cat pair", cat_of("9h,9d,2c"), "pair");
    check_case(&pass, &failed, "cat high card", cat_of("2h,7d,jc"),
               "high_card");

    /* the three-card order: straight beats flush, trips beats straight,
     * straight flush beats trips */
    check_case(&pass, &failed, "straight beats flush",
               cmp_of("2s,3d,4c", "2h,7h,jh"), "higher");
    check_case(&pass, &failed, "flush beats pair",
               cmp_of("2h,7h,jh", "ah,ad,kc"), "higher");
    check_case(&pass, &failed, "pair beats high card",
               cmp_of("2h,2d,3c", "ah,kd,jc"), "higher");
    check_case(&pass, &failed, "trips beats straight",
               cmp_of("2h,2d,2c", "qs,kd,ac"), "higher");
    check_case(&pass, &failed, "sf beats trips",
               cmp_of("2h,3h,4h", "ah,ad,ac"), "higher");

    /* equal categories compare on rank */
    check_case(&pass, &failed, "pair kicker higher",
               cmp_of("9h,9d,kc", "9s,9c,qd"), "higher");
    check_case(&pass, &failed, "pair kicker lower",
               cmp_of("9h,9d,2c", "9s,9c,qd"), "lower");
    check_case(&pass, &failed, "pair rank beats kicker",
               cmp_of("10h,10d,2c", "9s,9c,ad"), "higher");
    check_case(&pass, &failed, "high card second",
               cmp_of("ah,qd,2c", "as,jd,kc"), "lower");
    check_case(&pass, &failed, "high card third",
               cmp_of("ah,qd,9c", "as,qc,8d"), "higher");
    check_case(&pass, &failed, "flush ranks",
               cmp_of("kh,7h,2h", "qs,js,9s"), "higher");
    check_case(&pass, &failed, "A-2-3 is the low straight",
               cmp_of("ah,2d,3c", "2h,3d,4c"), "lower");
    check_case(&pass, &failed, "Q-K-A is the high straight",
               cmp_of("qh,kd,ac", "jh,qd,kc"), "higher");
    check_case(&pass, &failed, "equal hands push",
               cmp_of("ah,qd,9c", "as,qc,9d"), "equal");

    /* settlement: ante / play / ante bonus / pair plus */
    /* fold loses the ante and nothing else */
    check_case(&pass, &failed, "fold loses ante",
               settle_of("2h,7d,9c,as,kd,qh", TC_ACT_FOLD, 25, 0),
               "a-25 p+0 b+0 pp+0 n-25");
    /* pair plus still resolves after a fold */
    check_case(&pass, &failed, "fold pays pair plus",
               settle_of("9h,9d,2c,as,kd,qh", TC_ACT_FOLD, 25, 5),
               "a-25 p+0 b+0 pp+5 n-20");
    check_case(&pass, &failed, "fold loses pair plus",
               settle_of("2h,7d,9c,as,kd,qh", TC_ACT_FOLD, 25, 5),
               "a-25 p+0 b+0 pp-5 n-30");
    /* the ante bonus needs a played hand */
    check_case(&pass, &failed, "no bonus after fold",
               settle_of("2h,3d,4c,as,kd,qh", TC_ACT_FOLD, 25, 0),
               "a-25 p+0 b+0 pp+0 n-25");
    /* the bonus is paid on the player's own hand even when the hand loses */
    check_case(&pass, &failed, "bonus straight on a loss",
               settle_of("2h,3d,4c,5s,6d,7h", TC_ACT_PLAY, 25, 0),
               "a-25 p-25 b+25 pp+0 n-25");
    check_case(&pass, &failed, "bonus straight 1:1",
               settle_of("2h,3d,4c,as,kd,9h", TC_ACT_PLAY, 25, 0),
               "a+25 p+25 b+25 pp+0 n+75");
    check_case(&pass, &failed, "bonus trips 4:1",
               settle_of("2h,2d,2c,as,kd,9h", TC_ACT_PLAY, 25, 0),
               "a+25 p+25 b+100 pp+0 n+150");
    check_case(&pass, &failed, "bonus straight flush 5:1",
               settle_of("2h,3h,4h,as,kd,9c", TC_ACT_PLAY, 25, 0),
               "a+25 p+25 b+125 pp+0 n+175");
    /* the dealer failing to qualify pays the ante and pushes the play */
    check_case(&pass, &failed, "no qualify pays ante",
               settle_of("2h,7d,9c,js,8d,4h", TC_ACT_PLAY, 25, 0),
               "a+25 p+0 b+0 pp+0 n+25");
    /* ... and a losing hand still collects it */
    check_case(&pass, &failed, "no qualify beats better",
               settle_of("2h,3d,5c,js,9d,4h", TC_ACT_PLAY, 25, 0),
               "a+25 p+0 b+0 pp+0 n+25");
    /* a qualifying dealer is compared */
    check_case(&pass, &failed, "play beats dealer",
               settle_of("ah,ad,2c,qs,9d,4h", TC_ACT_PLAY, 25, 0),
               "a+25 p+25 b+0 pp+0 n+50");
    check_case(&pass, &failed, "play loses to dealer",
               settle_of("2h,7d,9c,as,kd,qh", TC_ACT_PLAY, 25, 0),
               "a-25 p-25 b+0 pp+0 n-50");
    check_case(&pass, &failed, "equal hands push both",
               settle_of("ah,qd,9c,as,qc,9d", TC_ACT_PLAY, 25, 0),
               "a+0 p+0 b+0 pp+0 n+0");
    /* the pair plus pay table, top to bottom */
    check_case(&pass, &failed, "pairplus pair 1:1",
               settle_of("9h,9d,2c,as,kd,qh", TC_ACT_PLAY, 25, 5),
               "a-25 p-25 b+0 pp+5 n-45");
    check_case(&pass, &failed, "pairplus flush 4:1",
               settle_of("2h,7h,jh,as,kd,qc", TC_ACT_PLAY, 25, 5),
               "a-25 p-25 b+0 pp+20 n-30");
    check_case(&pass, &failed, "pairplus straight 6:1",
               settle_of("2h,3d,4c,as,kd,qh", TC_ACT_PLAY, 25, 5),
               "a-25 p-25 b+25 pp+30 n+5");
    check_case(&pass, &failed, "pairplus trips 30:1",
               settle_of("2h,2d,2c,as,kd,qh", TC_ACT_PLAY, 25, 5),
               "a+25 p+25 b+100 pp+150 n+300");
    check_case(&pass, &failed, "pairplus sf 40:1",
               settle_of("2h,3h,4h,as,kd,qc", TC_ACT_PLAY, 25, 5),
               "a+25 p+25 b+125 pp+200 n+375");

    printf("check: %d passed, %d failed\n", pass, failed);
    return failed == 0 ? 0 : 1;
}

/* ---- argument parsing --------------------------------------------------- */

static int parse_args(const cli_t *cli, long *ante, long *pairplus,
                      tc_action_t *strategy, bool *check, bool *fixed,
                      card_t player[TC_CARDS], card_t dealer[TC_CARDS])
{
    *ante = TC_ANTE_DEFAULT;
    *pairplus = 0;
    *strategy = TC_ACT_ASK;
    *check = false;
    *fixed = false;

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];
        int a;

        if (bet_is(b, "check")) {
            if (bet_has_value(b) || cli->nbets != 1) {
                fprintf(stderr, "threecard: 'check' must be the only "
                                "argument\n");
                return 2;
            }
            *check = true;
            continue;
        }
        if (bet_is(b, "ante")) {
            if (b->nvalues != 1 || b->values[0] < TC_ANTE_MIN ||
                b->values[0] > TC_ANTE_MAX) {
                fprintf(stderr, "threecard: '%s': ante must be %d-%d\n",
                        b->raw, TC_ANTE_MIN, TC_ANTE_MAX);
                return 2;
            }
            *ante = b->values[0];
            continue;
        }
        if (bet_is(b, "pairplus")) {
            if (b->nvalues != 1 || b->values[0] < 0 ||
                b->values[0] > TC_ANTE_MAX) {
                fprintf(stderr, "threecard: '%s': pair plus must be 0-%d\n",
                        b->raw, TC_ANTE_MAX);
                return 2;
            }
            *pairplus = b->values[0];
            continue;
        }
        if (bet_is(b, "deal")) {
            if (*fixed || parse_deal(b->vraw, player, dealer) < 0) {
                fprintf(stderr, "threecard: '%s': deal needs six distinct "
                                "cards like deal:qh,qd,7c,ks,9d,4c "
                                "(player first)\n", b->raw);
                return 2;
            }
            *fixed = true;
            continue;
        }
        if (bet_has_value(b)) {
            fprintf(stderr, "threecard: unknown argument '%s' (valid: "
                            "play, fold, ante:N, pairplus:N, deal:..., "
                            "check)\n", b->raw);
            return 2;
        }
        a = action_lookup(b->name);
        if (a < 0) {
            fprintf(stderr, "threecard: unknown argument '%s' (valid: "
                            "play, fold, ante:N, pairplus:N, deal:..., "
                            "check)\n", b->raw);
            return 2;
        }
        if (*strategy != TC_ACT_ASK) {
            fprintf(stderr, "threecard: choose at most one action "
                            "(play or fold)\n");
            return 2;
        }
        *strategy = (tc_action_t)a;
    }
    return 0;
}

/* ---- driver ------------------------------------------------------------- */

int threecard_run(const cli_t *cli, rng_t *rng)
{
    long        ante, pairplus;
    tc_action_t strategy;
    bool        check, fixed;
    card_t      fp[TC_CARDS], fd[TC_CARDS];
    tc_stats_t  st = { 0 };

    if (cli->gui) {
        if (cli->nbets != 0 || cli->quiet || cli->json || cli->stats ||
            cli->iterations != 1) {
            fprintf(stderr, "threecard: --gui takes no other arguments "
                            "(only --seed)\n");
            return 2;
        }
#ifdef CASINO_GUI
        return tc_gui_run(rng);
#else
        fprintf(stderr, "threecard: this build has no GUI support "
                        "(install raylib and run make again)\n");
        return 2;
#endif
    }

    if (parse_args(cli, &ante, &pairplus, &strategy, &check, &fixed, fp, fd))
        return 2;
    if (check)
        return run_check();

    if (cli->stats && strategy == TC_ACT_ASK) {
        fprintf(stderr, "threecard: simulation needs a scripted action "
                        "(play or fold)\n");
        return 2;
    }
    if (fixed && (cli->stats || cli->iterations != 1)) {
        fprintf(stderr, "threecard: deal:... plays one fixed hand and "
                        "cannot be simulated\n");
        return 2;
    }

    bool machine = cli->quiet || cli->json || cli->stats;
    bool interactive = strategy == TC_ACT_ASK;
    tc_agent_t ag = {
        .fixed = strategy,
        .disp = machine ? stderr : stdout,
        .display = false,
    };
    ag.display = interactive || (!machine && cli->iterations == 1);

    for (long it = 0; it < cli->iterations; it++) {
        tc_round_t r;
        int        act;
        char       net[24];

        if (fixed)
            tc_round_deal_fixed(&r, fp, fd, ante, pairplus);
        else
            tc_round_deal(&r, rng, ante, pairplus);

        if (ag.display) {
            fprintf(ag.disp, "========================================\n");
            fprintf(ag.disp, "           THREE CARD POKER\n");
            fprintf(ag.disp, "========================================\n\n");
            fprintf(ag.disp, "Ante: %ld", ante);
            if (pairplus > 0)
                fprintf(ag.disp, "   Pair Plus: %ld", pairplus);
            fprintf(ag.disp, "\n\n");
            show_hand(ag.disp, "Player", r.player, &r.pev);
            fprintf(ag.disp, "\n");
        }

        /* the dealer's cards stay down until the decision is made */
        act = decide(&ag);
        if (act < 0)
            act = TC_ACT_FOLD;      /* EOF folds: no further wager */
        tc_round_settle(&r, (tc_action_t)act);
        stats_add(&st, &r);

        if (ag.display) {
            fprintf(ag.disp, "\n");
            show_hand(ag.disp, "Dealer", r.dealer, &r.dev);
            fprintf(ag.disp, "%-8s %s\n", "Qualify:",
                    r.dealer_qualifies ? "yes (queen-high or better)"
                                       : "no (below queen-high)");
        }

        if (cli->stats)
            continue;

        if (cli->json) {
            result_json(stdout, &r);
        } else if (cli->quiet) {
            result_quiet(stdout, &r);
        } else if (!ag.display) {
            tc_front_credits(net, sizeof net, r.returned - r.wagered, true);
            printf("%s %s net=%s\n", OUTCOME_TAG[r.outcome],
                   r.action == TC_ACT_PLAY ? "play" : "fold", net);
        } else {
            print_result(stdout, &r);
        }
    }

    if (cli->stats)
        stats_print(cli, &st, ante, pairplus, strategy);
    return 0;
}

void threecard_list_bets(void)
{
    puts("three card poker: three cards each against the dealer");
    puts("hand ranking (high to low) - a straight beats a flush with "
         "only three cards:");
    for (int c = TC_NCATS - 1; c >= 0; c--)
        printf("  %s\n", CAT_NAME[c]);
    puts("ace is high, except A-2-3 which is the lowest straight.");
    puts("play:");
    puts("  place an ante, see your three cards, then play or fold");
    puts("  fold           the ante is lost (pair plus still resolves)");
    puts("  play           adds a play wager equal to the ante");
    puts("the dealer qualifies with queen-high or better:");
    puts("  dealer does not qualify   ante pays 1:1, play pushes");
    puts("  player hand higher        ante and play both pay 1:1");
    puts("  dealer hand higher        ante and play both lose");
    puts("  equal hands               ante and play push");
    puts("ante bonus (paid on any played hand, dealer irrelevant):");
    for (int c = TC_NCATS - 1; c >= 0; c--)
        if (tc_front_ante_bonus((tc_cat_t)c))
            printf("  %-16s %2d:1\n", CAT_NAME[c],
                   tc_front_ante_bonus((tc_cat_t)c));
    puts("pair plus (optional side bet on your own three cards):");
    for (int c = TC_NCATS - 1; c >= 0; c--)
        if (tc_front_pairplus((tc_cat_t)c))
            printf("  %-16s %2d:1\n", CAT_NAME[c],
                   tc_front_pairplus((tc_cat_t)c));
    puts("  anything below a pair loses");
    puts("arguments:");
    puts("  play | fold    scripted action (required for simulation)");
    puts("  ante:N         ante wager (default 25)");
    puts("  pairplus:N     pair plus side bet (default 0, meaning none)");
    puts("  deal:...       six fixed cards, player first, e.g.");
    puts("                 deal:qh,qd,7c,ks,9d,4c");
    puts("  check          run the rule self-test and exit");
    puts("usage:");
    puts("  threecard                        interactive hand");
    puts("  threecard play                   scripted: always play");
    puts("  threecard ante:25 pairplus:5 play");
    puts("  threecard play --runs 100000     simulate");
    puts("  threecard --gui                  graphical table");
    puts("results: WIN | LOSS | PUSH | NO QUALIFY | FOLD");
}
