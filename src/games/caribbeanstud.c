#include "caribbeanstud.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cardart.h"
#include "cards.h"
#include "cs_strategy.h"
#include "output.h"
#include "poker.h"

#ifdef CASINO_GUI
#include "gui/caribbeanstud_gui.h"
#endif

#define CS_NCATS (POKER_ROYAL_FLUSH + 1)

/*
 * The raise pay table, and the only copy of it: profit per 1 unit of the
 * raise wager, indexed by poker_cat_t (low to high).  A winning raise
 * gets its stake back on top.  The ante is not in here - it always pays
 * 1:1.  Frontends read this through cs_raise_multiplier().
 */
static const int RAISE_PAY[CS_NCATS] = {
    1,      /* high card       1:1 */
    1,      /* one pair        1:1 */
    2,      /* two pair        2:1 */
    3,      /* three of a kind 3:1 */
    4,      /* straight        4:1 */
    5,      /* flush           5:1 */
    7,      /* full house      7:1 */
    20,     /* four of a kind 20:1 */
    50,     /* straight flush 50:1 */
    100     /* royal flush   100:1 */
};

/* poker.c names the categories for display; the machine spellings are
 * this game's presentation, like every other game's. */
static const char *const CAT_TOKEN[CS_NCATS] = {
    "HIGH_CARD", "PAIR", "TWO_PAIR", "THREE_OF_A_KIND", "STRAIGHT",
    "FLUSH", "FULL_HOUSE", "FOUR_OF_A_KIND", "STRAIGHT_FLUSH",
    "ROYAL_FLUSH"
};
static const char *const CAT_JSON[CS_NCATS] = {
    "high_card", "pair", "two_pair", "three_of_a_kind", "straight",
    "flush", "full_house", "four_of_a_kind", "straight_flush",
    "royal_flush"
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
 * Rank a five-card hand.  The category is the shared evaluator's; only
 * the kicker order is decided here, because settling player against
 * dealer needs more than a category.  Cards are grouped by how many
 * share a rank (so quads, trips and pairs lead their kickers) and then
 * by rank with the ace worth 14 - except in 5-4-3-2-A, the lowest
 * straight, where the ace drops below the deuce.
 */
cs_eval_t cs_eval(const card_t hand[CS_CARDS])
{
    cs_eval_t ev;
    poker_eval_t pe = poker_eval5(hand);
    int v[CS_CARDS], cnt[15] = { 0 };

    ev.cat = pe.cat;
    for (int i = 0; i < CS_CARDS; i++) {
        v[i] = hand[i].rank == 1 ? 14 : (int)hand[i].rank;
        cnt[v[i]]++;
    }

    for (int i = 0; i < CS_CARDS; i++)
        for (int j = i + 1; j < CS_CARDS; j++)
            if (cnt[v[j]] > cnt[v[i]] ||
                (cnt[v[j]] == cnt[v[i]] && v[j] > v[i])) {
                int t = v[i];
                v[i] = v[j];
                v[j] = t;
            }

    /* A-5-4-3-2: the ace plays low, so the hand ranks under 6-5-4-3-2 */
    if ((ev.cat == POKER_STRAIGHT || ev.cat == POKER_STRAIGHT_FLUSH) &&
        v[0] == 14 && v[1] == 5) {
        for (int i = 0; i < CS_CARDS - 1; i++)
            v[i] = v[i + 1];
        v[CS_CARDS - 1] = 1;
    }

    ev.key = (long)ev.cat;
    for (int i = 0; i < CS_CARDS; i++) {
        ev.rank[i] = v[i];
        ev.key = ev.key * 15 + v[i];    /* base 15 holds ranks 1..14 */
    }
    return ev;
}

int cs_compare(const cs_eval_t *a, const cs_eval_t *b)
{
    return a->key < b->key ? -1 : a->key > b->key ? 1 : 0;
}

/*
 * Ace-king or better.  Any made hand qualifies on its category alone; a
 * high-card hand qualifies only when its two top cards are the ace and
 * the king, which is what "ace-king high" means.  Nothing about generic
 * poker ranking changes to say this.
 */
bool cs_dealer_qualifies(const card_t dealer[CS_CARDS])
{
    cs_eval_t ev = cs_eval(dealer);

    return ev.cat > POKER_HIGH_CARD || (ev.rank[0] == 14 && ev.rank[1] == 13);
}

int cs_raise_multiplier(poker_cat_t cat)
{
    return RAISE_PAY[cat];
}

long cs_raise_amount(long ante)
{
    return ante * CS_RAISE_MULT;
}

/* ---- one round ---------------------------------------------------------- */

static void round_start(cs_round_t *r, long ante)
{
    memset(r, 0, sizeof *r);
    r->ante = ante;
    r->action = CS_ACT_ASK;
}

void cs_round_deal(cs_round_t *r, rng_t *rng, long ante)
{
    shoe_t shoe;

    round_start(r, ante);
    shoe_init(&shoe, 1);
    shoe_shuffle(&shoe, rng);
    /* one card each in turn, player first; dealer card 1 is the up-card */
    for (int i = 0; i < CS_CARDS; i++) {
        r->player[i] = shoe_draw(&shoe);
        r->dealer[i] = shoe_draw(&shoe);
    }
    r->pev = cs_eval(r->player);
    r->dev = cs_eval(r->dealer);
}

void cs_round_deal_fixed(cs_round_t *r, const card_t player[CS_CARDS],
                         const card_t dealer[CS_CARDS], long ante)
{
    round_start(r, ante);
    for (int i = 0; i < CS_CARDS; i++) {
        r->player[i] = player[i];
        r->dealer[i] = dealer[i];
    }
    r->pev = cs_eval(r->player);
    r->dev = cs_eval(r->dealer);
}

/*
 * Settle the round.  A fold stakes nothing but the ante and never looks
 * at the dealer's hand; a raise stakes twice the ante on top and goes to
 * a showdown the dealer must first qualify for.
 */
void cs_round_settle(cs_round_t *r, cs_action_t action)
{
    r->action = action == CS_ACT_FOLD ? CS_ACT_FOLD : CS_ACT_RAISE;
    r->dealer_qualifies = cs_dealer_qualifies(r->dealer);
    r->raise = 0;
    r->ante_net = r->raise_net = 0;

    if (r->action == CS_ACT_FOLD) {
        r->outcome = CS_OUT_FOLD;
        r->ante_net = -r->ante;
    } else {
        r->raise = cs_raise_amount(r->ante);

        if (!r->dealer_qualifies) {
            r->outcome = CS_OUT_NO_QUALIFY;  /* ante pays 1:1, raise pushes */
            r->ante_net = r->ante;
        } else {
            int cmp = cs_compare(&r->pev, &r->dev);

            if (cmp > 0) {
                r->outcome = CS_OUT_PLAYER;
                r->ante_net = r->ante;       /* the ante always pays 1:1 */
                r->raise_net = (long)cs_raise_multiplier(r->pev.cat) *
                               r->raise;
            } else if (cmp < 0) {
                r->outcome = CS_OUT_DEALER;
                r->ante_net = -r->ante;
                r->raise_net = -r->raise;
            } else {
                r->outcome = CS_OUT_PUSH;    /* both wagers come back */
            }
        }
    }

    r->wagered = r->ante + r->raise;
    r->returned = r->wagered + r->ante_net + r->raise_net;
    r->dealer_revealed = true;
    r->settled = true;
}

const card_t *cs_dealer_visible(const cs_round_t *r, int i)
{
    if (i < 0 || i >= CS_CARDS)
        return NULL;
    return (r->dealer_revealed || i == 0) ? &r->dealer[i] : NULL;
}

/* ---- frontend interface (see caribbeanstud.h) --------------------------- */

const char *cs_front_cat_name(poker_cat_t c)
{
    return poker_cat_str(c);
}

const char *cs_front_cat_token(poker_cat_t c)
{
    return CAT_TOKEN[c];
}

const char *cs_front_cat_json(poker_cat_t c)
{
    return CAT_JSON[c];
}

const char *cs_front_outcome_word(cs_outcome_t o)
{
    return OUTCOME_WORD[o];
}

void cs_front_credits(char *buf, size_t len, long v, bool sign)
{
    snprintf(buf, len, "%s%ld", sign && v > 0 ? "+" : "", v);
}

/* ---- session ------------------------------------------------------------ */

void cs_session_start(cs_session_t *s)
{
    memset(s, 0, sizeof *s);
    s->bankroll = CS_BANKROLL_START;
    s->ante = CS_ANTE_DEFAULT;
    s->phase = CS_PHASE_BET;
}

void cs_set_ante(cs_session_t *s, long v)
{
    long cap;

    if (s->phase == CS_PHASE_DECISION)
        return;
    if (v < CS_ANTE_MIN)
        v = CS_ANTE_MIN;
    if (v > CS_ANTE_MAX)
        v = CS_ANTE_MAX;
    /* a round must fund the ante and the raise behind it, so the ceiling
     * is a third of the bankroll however much the player asks for */
    cap = s->bankroll / (1 + CS_RAISE_MULT);
    if (v > cap)
        v = cap;
    if (v < CS_ANTE_MIN)
        v = CS_ANTE_MIN;            /* cs_can_deal() then reports false */
    s->ante = v;
}

long cs_max_exposure(const cs_session_t *s)
{
    return s->ante + cs_raise_amount(s->ante);
}

bool cs_can_deal(const cs_session_t *s)
{
    return s->phase != CS_PHASE_DECISION && s->ante >= CS_ANTE_MIN &&
           s->bankroll >= cs_max_exposure(s);
}

void cs_deal(cs_session_t *s, rng_t *rng)
{
    if (!cs_can_deal(s))
        return;
    s->bankroll -= s->ante;         /* the raise is staked when it is made */
    cs_round_deal(&s->round, rng, s->ante);
    s->phase = CS_PHASE_DECISION;
}

void cs_deal_fixed(cs_session_t *s, const card_t player[CS_CARDS],
                   const card_t dealer[CS_CARDS])
{
    if (!cs_can_deal(s))
        return;
    s->bankroll -= s->ante;
    cs_round_deal_fixed(&s->round, player, dealer, s->ante);
    s->phase = CS_PHASE_DECISION;
}

bool cs_can_raise(const cs_session_t *s)
{
    return s->phase == CS_PHASE_DECISION &&
           s->bankroll >= cs_raise_amount(s->round.ante);
}

bool cs_can_fold(const cs_session_t *s)
{
    return s->phase == CS_PHASE_DECISION;
}

void cs_decide(cs_session_t *s, cs_action_t action)
{
    if (s->phase != CS_PHASE_DECISION)
        return;
    if (action == CS_ACT_RAISE) {
        if (!cs_can_raise(s))
            return;                 /* the raise cannot be funded */
        s->bankroll -= cs_raise_amount(s->round.ante);
    }
    cs_round_settle(&s->round, action);
    s->bankroll += s->round.returned;
    s->phase = CS_PHASE_SETTLED;
}

void cs_buy_in(cs_session_t *s, long credits)
{
    if (s->phase == CS_PHASE_DECISION)
        return;
    if (credits < 0)
        credits = 0;
    s->bankroll = credits;
    s->phase = CS_PHASE_BET;
    cs_set_ante(s, s->ante);        /* re-clamped against the new bankroll */
}

void cs_bankroll_reset(cs_session_t *s)
{
    cs_buy_in(s, CS_BANKROLL_START);
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

/* "ah,kd,9c,5s,3d" -> one hand.  The two hands of a comparison are read
 * separately, so a self-test may reuse a card across them. */
static int parse_hand(const char *s, card_t out[CS_CARDS])
{
    int n = 0;

    while (*s) {
        const char *end = strchr(s, ',');
        size_t len = end ? (size_t)(end - s) : strlen(s);

        if (n == CS_CARDS || parse_card(s, len, &out[n]) < 0)
            return -1;
        n++;
        if (!end)
            break;
        s = end + 1;
    }
    return n == CS_CARDS ? 0 : -1;
}

/* Ten cards: the player's five then the dealer's five.  This is the hand
 * order, not the alternating physical deal order, which only decides
 * which card the shuffle hands to whom. */
static int parse_deal(const char *s, card_t player[CS_CARDS],
                      card_t dealer[CS_CARDS])
{
    card_t all[2 * CS_CARDS];
    int n = 0;

    while (*s) {
        const char *end = strchr(s, ',');
        size_t len = end ? (size_t)(end - s) : strlen(s);

        if (n == 2 * CS_CARDS || parse_card(s, len, &all[n]) < 0)
            return -1;
        n++;
        if (!end)
            break;
        s = end + 1;
        if (*s == '\0')
            return -1;              /* trailing comma */
    }
    if (n != 2 * CS_CARDS)
        return -1;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (all[i].rank == all[j].rank && all[i].suit == all[j].suit)
                return -1;          /* duplicate card */
    for (int i = 0; i < CS_CARDS; i++) {
        player[i] = all[i];
        dealer[i] = all[CS_CARDS + i];
    }
    return 0;
}

/* ---- the raise / fold decision ------------------------------------------ */

typedef enum {
    CS_SRC_ASK,             /* prompt on stdin */
    CS_SRC_FIXED,           /* the same scripted action every round */
    CS_SRC_BASIC            /* cs_basic_strategy() */
} cs_source_t;

typedef struct {
    cs_source_t src;
    cs_action_t fixed;
    FILE       *disp;
    bool        display;
    long        raise;      /* what a raise costs, for the prompt */
} cs_agent_t;

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
    if (strcmp(w, "r") == 0 || strcmp(w, "raise") == 0)
        return CS_ACT_RAISE;
    if (strcmp(w, "f") == 0 || strcmp(w, "fold") == 0)
        return CS_ACT_FOLD;
    return -1;
}

/* Returns a cs_action_t, or -1 on EOF at an interactive prompt. */
static int decide(cs_agent_t *ag, const cs_round_t *r)
{
    if (ag->src == CS_SRC_BASIC)
        return cs_basic_strategy(r->player, r->dealer[0]) == CS_DECISION_RAISE
               ? CS_ACT_RAISE : CS_ACT_FOLD;

    if (ag->src == CS_SRC_FIXED) {
        if (ag->display)
            fprintf(ag->disp, "[r]aise %ld / [f]old\n> %s\n", ag->raise,
                    ag->fixed == CS_ACT_RAISE ? "raise" : "fold");
        return ag->fixed;
    }

    for (;;) {
        char line[64], norm[16];
        int  a;

        fprintf(ag->disp, "[r]aise %ld / [f]old\n> ", ag->raise);
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

static void hand_str(char *buf, size_t len, const card_t h[CS_CARDS],
                     char sep)
{
    char c[CS_CARDS][8];

    for (int i = 0; i < CS_CARDS; i++)
        card_name(h[i], c[i], sizeof c[i]);
    snprintf(buf, len, "%s%c%s%c%s%c%s%c%s", c[0], sep, c[1], sep, c[2], sep,
             c[3], sep, c[4]);
}

static void show_hand(FILE *f, const char *label, const card_t h[CS_CARDS],
                      const cs_eval_t *ev)
{
    char buf[64], lab[16];

    snprintf(lab, sizeof lab, "%s:", label);
    if (cardart_enabled(f)) {
        fprintf(f, "%s\n", lab);
        cardart_hand(f, h, NULL, CS_CARDS);
    } else {
        hand_str(buf, sizeof buf, h, ' ');
        fprintf(f, "%-9s %s\n", lab, buf);
    }
    if (ev)
        fprintf(f, "%-9s %s\n", "Hand:", poker_cat_str(ev->cat));
}

/*
 * The dealer before the decision.  What may be shown comes from the
 * round itself - cs_dealer_visible() is the only thing that decides
 * which cards are public - so no frontend can leak the hole cards.
 */
static void show_dealer_down(FILE *f, const cs_round_t *r)
{
    bool hidden[CS_CARDS];
    char up[8];

    for (int i = 0; i < CS_CARDS; i++)
        hidden[i] = cs_dealer_visible(r, i) == NULL;

    if (cardart_enabled(f)) {
        fprintf(f, "Dealer:\n");
        cardart_hand(f, r->dealer, hidden, CS_CARDS);
    } else {
        card_name(r->dealer[0], up, sizeof up);
        fprintf(f, "%-9s %s\n", "Up-card:", up);
    }
}

static void print_result(FILE *f, const cs_round_t *r, long bankroll)
{
    char a[24], p[24], w[24], ret[24], net[24], bank[24];

    cs_front_credits(a, sizeof a, r->ante_net, true);
    cs_front_credits(p, sizeof p, r->raise_net, true);
    cs_front_credits(w, sizeof w, r->wagered, false);
    cs_front_credits(ret, sizeof ret, r->returned, false);
    cs_front_credits(net, sizeof net, r->returned - r->wagered, true);
    cs_front_credits(bank, sizeof bank, bankroll, false);

    fprintf(f, "\nResult:      %s\n", OUTCOME_WORD[r->outcome]);
    if (r->outcome == CS_OUT_FOLD)
        fprintf(f, "Player folded, ante lost\n");
    else if (r->outcome == CS_OUT_NO_QUALIFY)
        fprintf(f, "Dealer does not qualify: ante wins 1:1, raise pushes\n");
    fprintf(f, "Ante:        %8s\n", a);
    if (r->action == CS_ACT_RAISE) {
        fprintf(f, "Raise:       %8s%s\n", p,
                r->outcome == CS_OUT_NO_QUALIFY ? "  (push)" : "");
        if (r->outcome == CS_OUT_PLAYER)
            fprintf(f, "Raise pays:  %8d:1  (%s)\n",
                    cs_raise_multiplier(r->pev.cat),
                    poker_cat_str(r->pev.cat));
    }
    fprintf(f, "Wagered:     %8s\n", w);
    fprintf(f, "Returned:    %8s\n", ret);
    fprintf(f, "Net:         %8s\n", net);
    fprintf(f, "Bankroll:    %8s\n", bank);
}

static void result_quiet(FILE *f, const cs_round_t *r, long bankroll)
{
    char ph[64], dh[64], up[8], a[24], p[24], w[24], ret[24], net[24];

    hand_str(ph, sizeof ph, r->player, ',');
    hand_str(dh, sizeof dh, r->dealer, ',');
    card_name(r->dealer[0], up, sizeof up);
    cs_front_credits(a, sizeof a, r->ante_net, true);
    cs_front_credits(p, sizeof p, r->raise_net, true);
    cs_front_credits(w, sizeof w, r->wagered, false);
    cs_front_credits(ret, sizeof ret, r->returned, false);
    cs_front_credits(net, sizeof net, r->returned - r->wagered, true);

    fprintf(f, "%s player=%s hand=%s action=%s upcard=%s dealer=%s dhand=%s "
               "qualifies=%s ante=%s raise=%s wagered=%s returned=%s net=%s "
               "bankroll=%ld\n",
            OUTCOME_TAG[r->outcome], ph, CAT_JSON[r->pev.cat],
            r->action == CS_ACT_RAISE ? "raise" : "fold",
            up, dh, CAT_JSON[r->dev.cat],
            r->dealer_qualifies ? "yes" : "no", a, p, w, ret, net, bankroll);
}

static void cards_json(FILE *f, const card_t h[CS_CARDS])
{
    char buf[8];

    fputc('[', f);
    for (int i = 0; i < CS_CARDS; i++) {
        if (i)
            fputc(',', f);
        card_name(h[i], buf, sizeof buf);
        json_string(f, buf);
    }
    fputc(']', f);
}

static void result_json(FILE *f, const cs_round_t *r, long bankroll)
{
    char up[8];

    card_name(r->dealer[0], up, sizeof up);
    fprintf(f, "{\"game\":\"caribbeanstud\",\"ante\":%ld,\"raise\":%ld,"
               "\"player\":{\"cards\":", r->ante, r->raise);
    cards_json(f, r->player);
    fprintf(f, ",\"category\":");
    json_string(f, CAT_JSON[r->pev.cat]);
    fprintf(f, "},\"action\":");
    json_string(f, r->action == CS_ACT_RAISE ? "raise" : "fold");
    fprintf(f, ",\"dealer\":{\"upcard\":");
    json_string(f, up);
    fprintf(f, ",\"cards\":");
    cards_json(f, r->dealer);
    fprintf(f, ",\"category\":");
    json_string(f, CAT_JSON[r->dev.cat]);
    fprintf(f, "},\"dealer_qualifies\":%s,\"outcome\":",
            r->dealer_qualifies ? "true" : "false");
    json_string(f, OUTCOME_JSON[r->outcome]);
    fprintf(f, ",\"ante_net\":%ld,\"raise_net\":%ld,\"wagered\":%ld,"
               "\"returned\":%ld,\"net\":%ld,\"bankroll\":%ld}\n",
            r->ante_net, r->raise_net, r->wagered, r->returned,
            r->returned - r->wagered, bankroll);
}

/* ---- statistics --------------------------------------------------------- */

/*
 * Round counts reconcile: player_wins + dealer_wins + pushes + folds ==
 * rounds.  A dealer who fails to qualify is a player win (the ante pays
 * 1:1, the raise pushes) and is also counted on its own in no_qualify.
 * Everything dealer-side is counted over showdowns - the rounds the
 * player raised - because a folded hand never reaches one.
 */
typedef struct {
    long long rounds, raises, folds, rebuys;
    long long dealer_qualifies, dealer_no_qualify;
    long long player_wins, dealer_wins, pushes, no_qualify;
    long long pcats[CS_NCATS], dcats[CS_NCATS], raise_wins[CS_NCATS];
    long long ante_wagered, raise_wagered, returned;
} cs_stats_t;

static void stats_add(cs_stats_t *st, const cs_round_t *r)
{
    st->rounds++;
    st->pcats[r->pev.cat]++;
    st->ante_wagered += r->ante;
    st->raise_wagered += r->raise;
    st->returned += r->returned;

    if (r->action == CS_ACT_RAISE) {
        st->raises++;
        st->dcats[r->dev.cat]++;
        if (r->dealer_qualifies)
            st->dealer_qualifies++;
        else
            st->dealer_no_qualify++;
    } else {
        st->folds++;
    }

    switch (r->outcome) {
    case CS_OUT_FOLD:                                     break;
    case CS_OUT_NO_QUALIFY: st->player_wins++;
                            st->no_qualify++;             break;
    case CS_OUT_PLAYER:     st->player_wins++;
                            st->raise_wins[r->pev.cat]++; break;
    case CS_OUT_DEALER:     st->dealer_wins++;            break;
    default:                st->pushes++;                 break;
    }
}

static void stats_print(const cli_t *cli, const cs_stats_t *st, long ante,
                        const char *strat)
{
    long long wagered = st->ante_wagered + st->raise_wagered;
    double ret = wagered ? (double)st->returned / (double)wagered : 0.0;
    double n = (double)(st->rounds ? st->rounds : 1);
    long long net = st->returned - wagered;

    if (cli->json) {
        printf("{\"game\":\"caribbeanstud\",\"iterations\":%ld,\"ante\":%ld,"
               "\"strategy\":", cli->iterations, ante);
        json_string(stdout, strat);
        printf(",\"rounds\":%lld,\"decisions\":{\"raise\":%lld,"
               "\"fold\":%lld},\"dealer\":{\"qualified\":%lld,"
               "\"not_qualified\":%lld},\"outcomes\":{\"player_win\":%lld,"
               "\"dealer_win\":%lld,\"push\":%lld,\"fold\":%lld,"
               "\"no_qualify\":%lld},\"player_hands\":{",
               st->rounds, st->raises, st->folds, st->dealer_qualifies,
               st->dealer_no_qualify, st->player_wins, st->dealer_wins,
               st->pushes, st->folds, st->no_qualify);
        for (int c = 0; c < CS_NCATS; c++) {
            if (c)
                fputc(',', stdout);
            json_string(stdout, CAT_JSON[c]);
            printf(":%lld", st->pcats[c]);
        }
        printf("},\"dealer_hands\":{");
        for (int c = 0; c < CS_NCATS; c++) {
            if (c)
                fputc(',', stdout);
            json_string(stdout, CAT_JSON[c]);
            printf(":%lld", st->dcats[c]);
        }
        printf("},\"raise_wins\":{");
        for (int c = 0; c < CS_NCATS; c++) {
            if (c)
                fputc(',', stdout);
            json_string(stdout, CAT_JSON[c]);
            printf(":%lld", st->raise_wins[c]);
        }
        printf("},\"money\":{\"ante_wagered\":%lld,\"raise_wagered\":%lld,"
               "\"total_wagered\":%lld,\"returned\":%lld,\"net\":%lld,"
               "\"return_per_unit\":%.6f},\"rebuys\":%lld}\n",
               st->ante_wagered, st->raise_wagered, wagered, st->returned,
               net, ret, st->rebuys);
    } else if (cli->quiet) {
        printf("game=caribbeanstud runs=%ld ante=%ld strategy=%s "
               "rounds=%lld raises=%lld folds=%lld dealer_qualifies=%lld "
               "dealer_not_qualified=%lld player_wins=%lld no_qualify=%lld "
               "dealer_wins=%lld pushes=%lld ante_wagered=%lld "
               "raise_wagered=%lld wagered=%lld returned=%lld net=%lld "
               "return_per_unit=%.4f rebuys=%lld\n",
               cli->iterations, ante, strat, st->rounds, st->raises,
               st->folds, st->dealer_qualifies, st->dealer_no_qualify,
               st->player_wins, st->no_qualify, st->dealer_wins, st->pushes,
               st->ante_wagered, st->raise_wagered, wagered, st->returned,
               net, ret, st->rebuys);
    } else {
        printf("Iterations: %lld   Ante: %ld   Strategy: %s\n", st->rounds,
               ante, strat);
        printf("%-22s %10s %9s\n", "RESULT", "COUNT", "RATE%");
        printf("%-22s %10lld %9.4f\n", "player wins", st->player_wins,
               100.0 * (double)st->player_wins / n);
        printf("%-22s %10lld %9.4f\n", "dealer wins", st->dealer_wins,
               100.0 * (double)st->dealer_wins / n);
        printf("%-22s %10lld %9.4f\n", "pushes", st->pushes,
               100.0 * (double)st->pushes / n);
        printf("%-22s %10lld %9.4f\n", "folds", st->folds,
               100.0 * (double)st->folds / n);
        printf("  (player wins include %lld dealer non-qualifiers)\n",
               st->no_qualify);
        printf("\n%-22s %10s %9s\n", "DECISION", "COUNT", "RATE%");
        printf("%-22s %10lld %9.4f\n", "raises", st->raises,
               100.0 * (double)st->raises / n);
        printf("%-22s %10lld %9.4f\n", "folds", st->folds,
               100.0 * (double)st->folds / n);
        {
            double sd = (double)(st->raises ? st->raises : 1);

            printf("\n%-22s %10s %9s\n", "DEALER (showdowns)", "COUNT",
                   "RATE%");
            printf("%-22s %10lld %9.4f\n", "dealer qualifies",
                   st->dealer_qualifies,
                   100.0 * (double)st->dealer_qualifies / sd);
            printf("%-22s %10lld %9.4f\n", "dealer not qualified",
                   st->dealer_no_qualify,
                   100.0 * (double)st->dealer_no_qualify / sd);
            printf("\n%-22s %10s %9s %9s\n", "HAND", "PLAYER", "RATE%",
                   "DEALER");
            for (int c = CS_NCATS - 1; c >= 0; c--)
                printf("%-22s %10lld %9.4f %9lld\n", CAT_JSON[c],
                       st->pcats[c], 100.0 * (double)st->pcats[c] / n,
                       st->dcats[c]);
            printf("\n%-22s %10s\n", "WINNING RAISE", "COUNT");
            for (int c = CS_NCATS - 1; c >= 0; c--)
                if (st->raise_wins[c])
                    printf("%-22s %10lld  (%d:1)\n", CAT_JSON[c],
                           st->raise_wins[c], RAISE_PAY[c]);
        }
        printf("\nAnte wagered:   %lld\n", st->ante_wagered);
        printf("Raise wagered:  %lld\n", st->raise_wagered);
        printf("Wagered:        %lld\n", wagered);
        printf("Returned:       %lld\n", st->returned);
        printf("Rebuys:         %lld\n", st->rebuys);
        printf("Net units:      %+lld (%.4f per unit wagered)\n", net,
               ret - 1.0);
    }
}

/* ---- rule self-test (pure evaluation and settlement, no RNG) ----------- */

static int check_case(int *pass, int *failed, const char *label,
                      const char *got, const char *want)
{
    bool ok = strcmp(got, want) == 0;

    printf("%-28s %-22s %s\n", label, got, ok ? "ok" : "FAIL");
    if (ok)
        (*pass)++;
    else
        (*failed)++;
    return ok;
}

static const char *cat_of(const char *spec)
{
    card_t h[CS_CARDS];

    if (parse_hand(spec, h) < 0)
        return "BADSPEC";
    return CAT_JSON[cs_eval(h).cat];
}

static const char *cmp_of(const char *a, const char *b)
{
    card_t    ha[CS_CARDS], hb[CS_CARDS];
    cs_eval_t ea, eb;
    int       c;

    if (parse_hand(a, ha) < 0 || parse_hand(b, hb) < 0)
        return "BADSPEC";
    ea = cs_eval(ha);
    eb = cs_eval(hb);
    c = cs_compare(&ea, &eb);
    return c > 0 ? "higher" : c < 0 ? "lower" : "equal";
}

static const char *qual_of(const char *spec)
{
    card_t h[CS_CARDS];

    if (parse_hand(spec, h) < 0)
        return "BADSPEC";
    return cs_dealer_qualifies(h) ? "qualifies" : "no";
}

/* Settle a fixed deal and describe both wagers in one line. */
static const char *settle_of(const char *spec, cs_action_t act, long ante)
{
    static char out[96];
    card_t p[CS_CARDS], d[CS_CARDS];
    cs_round_t r;

    if (parse_deal(spec, p, d) < 0)
        return "BADSPEC";
    cs_round_deal_fixed(&r, p, d, ante);
    cs_round_settle(&r, act);
    snprintf(out, sizeof out, "a%+ld r%+ld n%+ld w%ld", r.ante_net,
             r.raise_net, r.returned - r.wagered, r.wagered);
    return out;
}

/* The strategy adviser, from the CLI: a hand and one up-card. */
static const char *strat_of(const char *hand, const char *upcard)
{
    card_t h[CS_CARDS], up;

    if (parse_hand(hand, h) < 0 || parse_card(upcard, strlen(upcard), &up) < 0)
        return "BADSPEC";
    return cs_basic_strategy(h, up) == CS_DECISION_RAISE ? "raise" : "fold";
}

static int run_check(void)
{
    int pass = 0, failed = 0;

    puts("caribbeanstud rule self-test");

    /* dealer qualification: ace-king high or better */
    check_case(&pass, &failed, "qualify A-K high", qual_of("ah,kd,9c,5s,3d"),
               "qualifies");
    check_case(&pass, &failed, "qualify A-Q high", qual_of("ah,qd,jc,9s,4d"),
               "no");
    check_case(&pass, &failed, "qualify K-Q high", qual_of("kh,qd,jc,9s,4d"),
               "no");
    check_case(&pass, &failed, "qualify low pair", qual_of("2h,2d,7c,5s,3d"),
               "qualifies");
    check_case(&pass, &failed, "qualify K-K pair", qual_of("kh,kd,8c,5s,2d"),
               "qualifies");
    check_case(&pass, &failed, "qualify straight", qual_of("5h,6d,7c,8s,9d"),
               "qualifies");
    check_case(&pass, &failed, "qualify flush", qual_of("2h,7h,9h,jh,4h"),
               "qualifies");
    check_case(&pass, &failed, "qualify A-K-Q-J-9", qual_of("ah,kd,qc,js,9d"),
               "qualifies");
    check_case(&pass, &failed, "qualify A high only", qual_of("ah,qd,9c,5s,3d"),
               "no");

    /* categories, including the two aces of a straight */
    check_case(&pass, &failed, "cat wheel straight",
               cat_of("ah,2d,3c,4s,5d"), "straight");
    check_case(&pass, &failed, "cat broadway straight",
               cat_of("10h,jd,qc,ks,ad"), "straight");
    check_case(&pass, &failed, "cat royal flush",
               cat_of("10h,jh,qh,kh,ah"), "royal_flush");
    check_case(&pass, &failed, "cat steel wheel",
               cat_of("ah,2h,3h,4h,5h"), "straight_flush");
    check_case(&pass, &failed, "cat quads", cat_of("9h,9d,9c,9s,3d"),
               "four_of_a_kind");
    check_case(&pass, &failed, "cat full house", cat_of("9h,9d,9c,3s,3d"),
               "full_house");
    check_case(&pass, &failed, "cat two pair", cat_of("9h,9d,3c,3s,kd"),
               "two_pair");

    /* comparison, category first then kickers */
    check_case(&pass, &failed, "pair beats A-K high",
               cmp_of("2h,2d,7c,5s,3d", "ah,kd,9c,5s,3d"), "higher");
    check_case(&pass, &failed, "A-K high loses to pair",
               cmp_of("ah,kd,9c,5s,3d", "2h,2d,7c,5s,3d"), "lower");
    check_case(&pass, &failed, "higher pair wins",
               cmp_of("kh,kd,7c,5s,3d", "qh,qd,jc,9s,8d"), "higher");
    check_case(&pass, &failed, "pair kicker decides",
               cmp_of("9h,9d,kc,5s,3d", "9c,9s,qd,5h,3c"), "higher");
    check_case(&pass, &failed, "high card fifth decides",
               cmp_of("ah,kd,9c,5s,4d", "as,kc,9d,5h,3c"), "higher");
    check_case(&pass, &failed, "identical ranks push",
               cmp_of("ah,kd,9c,5s,3d", "as,kc,9d,5h,3c"), "equal");
    check_case(&pass, &failed, "wheel is the low straight",
               cmp_of("ah,2d,3c,4s,5d", "2h,3d,4c,5s,6d"), "lower");
    check_case(&pass, &failed, "trips over two pair",
               cmp_of("4h,4d,4c,ks,3d", "ah,ad,kc,ks,3d"), "higher");

    /* settlement: fold, non-qualifier, showdown, push */
    check_case(&pass, &failed, "fold loses ante only",
               settle_of("2h,7d,9c,5s,3d,ah,kd,qc,js,9h", CS_ACT_FOLD, 25),
               "a-25 r+0 n-25 w25");
    check_case(&pass, &failed, "fold ignores the dealer",
               settle_of("ah,ad,ac,as,kd,2h,7d,9c,5s,3c", CS_ACT_FOLD, 25),
               "a-25 r+0 n-25 w25");
    check_case(&pass, &failed, "no qualify pays ante",
               settle_of("ah,kd,9c,5s,3d,2h,7d,9s,5c,3h", CS_ACT_RAISE, 25),
               "a+25 r+0 n+25 w75");
    check_case(&pass, &failed, "no qualify beats better",
               settle_of("2h,7d,9c,5s,3d,ah,qd,jc,9s,4h", CS_ACT_RAISE, 25),
               "a+25 r+0 n+25 w75");
    check_case(&pass, &failed, "dealer wins takes both",
               settle_of("2h,7d,9c,5s,3d,ah,ad,kc,qs,jh", CS_ACT_RAISE, 25),
               "a-25 r-50 n-75 w75");
    check_case(&pass, &failed, "equal hands push both",
               settle_of("ah,kd,9c,5s,3d,as,kc,9d,5h,3c", CS_ACT_RAISE, 25),
               "a+0 r+0 n+0 w75");

    /* the raise pay table, every category, always against a qualifying
     * dealer the player beats */
    check_case(&pass, &failed, "raise high card 1:1",
               settle_of("ah,kd,9c,5s,4d,as,kc,9d,5h,3c", CS_ACT_RAISE, 25),
               "a+25 r+50 n+75 w75");
    check_case(&pass, &failed, "raise pair 1:1",
               settle_of("2h,2d,7c,5s,3d,ah,kd,9c,5c,3h", CS_ACT_RAISE, 25),
               "a+25 r+50 n+75 w75");
    check_case(&pass, &failed, "raise two pair 2:1",
               settle_of("9h,9d,3c,3s,kd,ah,ks,9c,5h,3h", CS_ACT_RAISE, 25),
               "a+25 r+100 n+125 w75");
    check_case(&pass, &failed, "raise trips 3:1",
               settle_of("9h,9d,9c,3s,kd,ah,ks,8c,5h,3h", CS_ACT_RAISE, 25),
               "a+25 r+150 n+175 w75");
    check_case(&pass, &failed, "raise straight 4:1",
               settle_of("5h,6d,7c,8s,9d,ah,ks,8c,5c,3h", CS_ACT_RAISE, 25),
               "a+25 r+200 n+225 w75");
    check_case(&pass, &failed, "raise flush 5:1",
               settle_of("2h,7h,9h,jh,4h,ah,ks,8c,5c,3d", CS_ACT_RAISE, 25),
               "a+25 r+250 n+275 w75");
    check_case(&pass, &failed, "raise full house 7:1",
               settle_of("9h,9d,9c,3s,3d,ah,ks,8c,5c,3h", CS_ACT_RAISE, 25),
               "a+25 r+350 n+375 w75");
    check_case(&pass, &failed, "raise quads 20:1",
               settle_of("9h,9d,9c,9s,3d,ah,ks,8c,5c,3h", CS_ACT_RAISE, 25),
               "a+25 r+1000 n+1025 w75");
    check_case(&pass, &failed, "raise straight flush 50:1",
               settle_of("5h,6h,7h,8h,9h,ah,ks,8c,5c,3d", CS_ACT_RAISE, 25),
               "a+25 r+2500 n+2525 w75");
    check_case(&pass, &failed, "raise royal flush 100:1",
               settle_of("10h,jh,qh,kh,ah,as,ks,8c,5c,3d", CS_ACT_RAISE, 25),
               "a+25 r+5000 n+5025 w75");

    /* basic strategy: pair or better, ace-king, and everything below */
    check_case(&pass, &failed, "strat pair raises",
               strat_of("2h,2d,7c,5s,3d", "ah"), "raise");
    check_case(&pass, &failed, "strat flush raises",
               strat_of("2h,7h,9h,jh,4h", "ah"), "raise");
    check_case(&pass, &failed, "strat A-Q folds",
               strat_of("ah,qd,9c,5s,3d", "2c"), "fold");
    check_case(&pass, &failed, "strat K-Q folds",
               strat_of("kh,qd,9c,5s,3d", "kc"), "fold");
    check_case(&pass, &failed, "strat A-K match raises",
               strat_of("ah,kd,9c,5s,3d", "9d"), "raise");
    check_case(&pass, &failed, "strat A-K no match folds",
               strat_of("ah,kd,9c,5s,3d", "8d"), "fold");
    check_case(&pass, &failed, "strat A-K-J vs ace raises",
               strat_of("ah,kd,jc,5s,3d", "ac"), "raise");
    check_case(&pass, &failed, "strat A-K-10 vs king folds",
               strat_of("ah,kd,10c,5s,3d", "kc"), "fold");
    check_case(&pass, &failed, "strat A-K-Q fourth card",
               strat_of("ah,kd,qc,8s,3d", "7c"), "raise");
    check_case(&pass, &failed, "strat A-K-Q fourth matches",
               strat_of("ah,kd,qc,8s,3d", "8c"), "raise");
    check_case(&pass, &failed, "strat A-K-Q high upcard",
               strat_of("ah,kd,qc,8s,3d", "9c"), "fold");

    printf("check: %d passed, %d failed\n", pass, failed);
    return failed == 0 ? 0 : 1;
}

/* ---- argument parsing --------------------------------------------------- */

static const char *const USAGE_ARGS =
    "raise, fold, ante:N, deal:..., check";

static int parse_args(const cli_t *cli, long *ante, cs_action_t *strategy,
                      bool *check, bool *fixed, card_t player[CS_CARDS],
                      card_t dealer[CS_CARDS])
{
    *ante = CS_ANTE_DEFAULT;
    *strategy = CS_ACT_ASK;
    *check = false;
    *fixed = false;

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];
        int a;

        if (bet_is(b, "check")) {
            if (bet_has_value(b) || cli->nbets != 1) {
                fprintf(stderr, "caribbeanstud: 'check' must be the only "
                                "argument\n");
                return 2;
            }
            *check = true;
            continue;
        }
        if (bet_is(b, "ante")) {
            if (b->nvalues != 1 || b->values[0] < CS_ANTE_MIN ||
                b->values[0] > CS_ANTE_MAX) {
                fprintf(stderr, "caribbeanstud: '%s': ante must be %d-%d\n",
                        b->raw, CS_ANTE_MIN, CS_ANTE_MAX);
                return 2;
            }
            *ante = b->values[0];
            continue;
        }
        if (bet_is(b, "deal")) {
            if (*fixed || parse_deal(b->vraw, player, dealer) < 0) {
                fprintf(stderr, "caribbeanstud: '%s': deal needs ten "
                                "distinct cards like "
                                "deal:ah,kd,9c,5s,3d,kh,qs,8d,6c,2h "
                                "(player's five first)\n", b->raw);
                return 2;
            }
            *fixed = true;
            continue;
        }
        if (bet_has_value(b)) {
            fprintf(stderr, "caribbeanstud: unknown argument '%s' "
                            "(valid: %s)\n", b->raw, USAGE_ARGS);
            return 2;
        }
        a = action_lookup(b->name);
        if (a < 0) {
            fprintf(stderr, "caribbeanstud: unknown argument '%s' "
                            "(valid: %s)\n", b->raw, USAGE_ARGS);
            return 2;
        }
        if (*strategy != CS_ACT_ASK) {
            fprintf(stderr, "caribbeanstud: choose at most one action "
                            "(raise or fold)\n");
            return 2;
        }
        *strategy = (cs_action_t)a;
    }
    return 0;
}

/* ---- driver ------------------------------------------------------------- */

/*
 * The buy-in for a CLI session.  A round has to fund the ante and the
 * raise behind it, so a large ante buys in for more than the standard
 * stake rather than having the engine clamp the wager the player asked
 * for.
 */
static long buy_in_for(long ante)
{
    long need = ante + cs_raise_amount(ante);

    return need > CS_BANKROLL_START ? need : CS_BANKROLL_START;
}

int caribbeanstud_run(const cli_t *cli, rng_t *rng)
{
    long         ante;
    cs_action_t  strategy;
    bool         check, fixed;
    card_t       fp[CS_CARDS], fd[CS_CARDS];
    cs_stats_t   st = { 0 };
    cs_session_t s;

    if (cli->gui) {
        if (cli->nbets != 0 || cli->quiet || cli->json || cli->stats ||
            cli->iterations != 1) {
            fprintf(stderr, "caribbeanstud: --gui takes no other arguments "
                            "(only --seed)\n");
            return 2;
        }
#ifdef CASINO_GUI
        return cs_gui_run(rng);
#else
        fprintf(stderr, "caribbeanstud: this build has no GUI support "
                        "(install raylib and run make again)\n");
        return 2;
#endif
    }

    if (parse_args(cli, &ante, &strategy, &check, &fixed, fp, fd))
        return 2;
    if (check)
        return run_check();

    if (fixed && (cli->stats || cli->iterations != 1)) {
        fprintf(stderr, "caribbeanstud: deal:... plays one fixed hand and "
                        "cannot be simulated\n");
        return 2;
    }

    bool machine = cli->quiet || cli->json || cli->stats;
    /* a simulation with no scripted action plays basic strategy; anything
     * else with no action asks */
    cs_agent_t ag = {
        .src = strategy != CS_ACT_ASK ? CS_SRC_FIXED
             : cli->stats             ? CS_SRC_BASIC : CS_SRC_ASK,
        .fixed = strategy,
        .disp = machine ? stderr : stdout,
        .display = false,
        .raise = cs_raise_amount(ante),
    };
    ag.display = ag.src == CS_SRC_ASK || (!machine && cli->iterations == 1);

    const char *strat = ag.src == CS_SRC_BASIC ? "basic"
                      : strategy == CS_ACT_RAISE ? "raise" : "fold";

    cs_session_start(&s);
    cs_buy_in(&s, buy_in_for(ante));
    cs_set_ante(&s, ante);

    for (long it = 0; it < cli->iterations; it++) {
        const cs_round_t *r = &s.round;
        long  bankroll_before;
        int   act;
        char  net[24];

        if (!cs_can_deal(&s)) {
            if (!cli->stats) {
                fprintf(ag.disp, "Out of credits.\n");
                break;
            }
            cs_buy_in(&s, buy_in_for(ante));    /* fresh buy-in */
            st.rebuys++;
        }

        bankroll_before = s.bankroll;
        if (fixed)
            cs_deal_fixed(&s, fp, fd);
        else
            cs_deal(&s, rng);

        if (ag.display) {
            fprintf(ag.disp, "========================================\n");
            fprintf(ag.disp, "         CARIBBEAN STUD POKER\n");
            fprintf(ag.disp, "========================================\n\n");
            fprintf(ag.disp, "Ante: %ld   Raise: %ld   Bankroll: %ld\n\n",
                    r->ante, cs_raise_amount(r->ante), bankroll_before);
            show_hand(ag.disp, "Player", r->player, &r->pev);
            fprintf(ag.disp, "\n");
            /* only the up-card is public until the decision is made */
            show_dealer_down(ag.disp, r);
            fprintf(ag.disp, "\n");
        }

        act = decide(&ag, r);
        if (act < 0)
            act = CS_ACT_FOLD;      /* EOF folds: no further wager */
        if (act == CS_ACT_RAISE && !cs_can_raise(&s))
            act = CS_ACT_FOLD;      /* cannot fund it: the hand is folded */
        cs_decide(&s, (cs_action_t)act);
        stats_add(&st, r);

        if (ag.display) {
            fprintf(ag.disp, "\n");
            show_hand(ag.disp, "Dealer", r->dealer, &r->dev);
            fprintf(ag.disp, "%-9s %s\n", "Qualify:",
                    r->dealer_qualifies ? "yes (ace-king or better)"
                                        : "no (below ace-king)");
        }

        if (cli->stats)
            continue;

        if (cli->json) {
            result_json(stdout, r, s.bankroll);
        } else if (cli->quiet) {
            result_quiet(stdout, r, s.bankroll);
        } else if (!ag.display) {
            cs_front_credits(net, sizeof net, r->returned - r->wagered, true);
            printf("%s %s net=%s\n", OUTCOME_TAG[r->outcome],
                   r->action == CS_ACT_RAISE ? "raise" : "fold", net);
        } else {
            print_result(stdout, r, s.bankroll);
        }
    }

    if (cli->stats)
        stats_print(cli, &st, ante, strat);
    return 0;
}

void caribbeanstud_list_bets(void)
{
    puts("caribbean stud poker: five cards each against the dealer");
    puts("play:");
    puts("  place an ante, see your five cards and the dealer's up-card,");
    puts("  then raise or fold - no draw, no discards");
    puts("  fold           the ante is lost, the hand is over");
    puts("  raise          adds a raise wager of exactly twice the ante");
    puts("the dealer qualifies with ace-king high or better:");
    puts("  A K 9 5 3      qualifies (ace-king high)");
    puts("  A Q J 9 4      does not qualify");
    puts("  any pair or better always qualifies");
    puts("settlement:");
    puts("  dealer does not qualify   ante pays 1:1, raise pushes");
    puts("  player hand higher        ante pays 1:1, raise pays the table");
    puts("  dealer hand higher        ante and raise both lose");
    puts("  equal hands               ante and raise push");
    puts("raise pay table (profit per unit of the raise wager):");
    for (int c = POKER_ROYAL_FLUSH; c >= POKER_HIGH_CARD; c--)
        printf("  %-16s %3d:1\n", poker_cat_str((poker_cat_t)c),
               cs_raise_multiplier((poker_cat_t)c));
    puts("  the ante always pays 1:1; the raise pay table is not used on it");
    puts("  the progressive jackpot side bet is not part of this game");
    puts("arguments:");
    puts("  raise | fold   scripted action (r | f); simulation without one");
    puts("                 plays Caribbean Stud basic strategy");
    puts("  ante:N         ante wager (default 25, 1-500)");
    puts("  deal:...       ten fixed cards, the player's five first, e.g.");
    puts("                 deal:ah,kd,9c,5s,3d,kh,qs,8d,6c,2h");
    puts("  check          run the rule self-test and exit");
    puts("basic strategy (used by --runs when no action is scripted):");
    puts("  raise on one pair or better");
    puts("  fold below ace-king high");
    puts("  holding exactly ace-king high, raise when the up-card is a 2");
    puts("  through queen matching one of your cards, or is an ace or king");
    puts("  and you hold a queen or jack, or you hold a queen and the");
    puts("  up-card ranks below your fourth-highest card");
    puts("usage:");
    puts("  caribbeanstud                     interactive hand");
    puts("  caribbeanstud ante:25 r           scripted: raise");
    puts("  caribbeanstud ante:25 f           scripted: fold");
    puts("  caribbeanstud --runs 100000       simulate with basic strategy");
    puts("  caribbeanstud --seed 42           deterministic deal");
    puts("  caribbeanstud --gui               graphical table");
    puts("results: WIN | LOSS | PUSH | NO QUALIFY | FOLD");
}
