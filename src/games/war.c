#include "war.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cardart.h"
#include "cards.h"
#include "output.h"

static const char *const RESULT_WORD[] = {
    "WIN", "LOSS", "PUSH", "SURRENDER"
};
static const char *const RESULT_JSON[] = {
    "win", "loss", "push", "surrender"
};

/* Ace is high and suits are irrelevant: rank is the whole comparison. */
static int war_value(card_t c)
{
    return c.rank == 1 ? 14 : c.rank;
}

/* Format halves as units, e.g. "100", "-12.5", "+50" when plus is set. */
static void war_money(char *buf, size_t len, long halves, bool plus)
{
    long a = halves < 0 ? -halves : halves;
    const char *sign = halves < 0 ? "-" : plus ? "+" : "";

    if (a % WAR_HALF == 0)
        snprintf(buf, len, "%s%ld", sign, a / WAR_HALF);
    else
        snprintf(buf, len, "%s%ld.5", sign, a / WAR_HALF);
}

/* JSON money: units with one decimal, so halves survive the round trip. */
static double war_units(long halves)
{
    return (double)halves / WAR_HALF;
}

/* ---- the tie decision --------------------------------------------------- */

typedef struct {
    war_tie_t fixed;        /* WAR_TIE_ASK means prompt on stdin */
    FILE     *disp;         /* where prompts and the transcript go */
    bool      display;      /* full transcript rather than a result line */
} war_agent_t;

/* Lowercase and trim; returns -1 if the word does not fit. */
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

static int decision_lookup(const char *w)
{
    if (strcmp(w, "w") == 0 || strcmp(w, "war") == 0)
        return WAR_TIE_WAR;
    if (strcmp(w, "s") == 0 || strcmp(w, "surrender") == 0)
        return WAR_TIE_SURRENDER;
    return -1;
}

/*
 * Resolve a tie into a decision.  Returns a war_tie_t, or -1 on EOF at an
 * interactive prompt (the caller concedes the tie, which needs no further
 * wager).
 */
static int decide(war_agent_t *ag)
{
    if (ag->fixed != WAR_TIE_ASK) {
        if (ag->display)
            fprintf(ag->disp, "[w]ar / [s]urrender\n> %s\n",
                    ag->fixed == WAR_TIE_WAR ? "war" : "surrender");
        return ag->fixed;
    }

    for (;;) {
        char line[64], norm[16];
        int  d;

        fprintf(ag->disp, "[w]ar / [s]urrender\n> ");
        fflush(ag->disp);
        if (!fgets(line, sizeof line, stdin)) {
            fprintf(ag->disp, "\n");
            return -1;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (normalise(line, norm, sizeof norm) == 0 &&
            (d = decision_lookup(norm)) >= 0)
            return d;
        fprintf(ag->disp, "invalid choice, try again\n");
    }
}

/* ---- display (reuses cardart; no card art is defined here) -------------- */

static void show_cards(FILE *f, card_t player, card_t dealer)
{
    char p[8], d[8];

    card_name(player, p, sizeof p);
    card_name(dealer, d, sizeof d);
    if (cardart_enabled(f)) {
        fprintf(f, "Player:\n");
        cardart_hand(f, &player, NULL, 1);
        fprintf(f, "Dealer:\n");
        cardart_hand(f, &dealer, NULL, 1);
        return;
    }
    fprintf(f, "Player: %s\n", p);
    fprintf(f, "Dealer: %s\n", d);
}

static void print_result(FILE *f, const war_round_t *r)
{
    char w[32], ret[32], net[32];

    war_money(w, sizeof w, r->wagered, false);
    war_money(ret, sizeof ret, r->returned, false);
    war_money(net, sizeof net, r->returned - r->wagered, true);

    fprintf(f, "\nResult:   %s\n", RESULT_WORD[r->result]);
    fprintf(f, "Wagered:  %8s\n", w);
    fprintf(f, "Returned: %8s\n", ret);
    fprintf(f, "Net:      %8s\n", net);
}

/* ---- one round ---------------------------------------------------------- */

/*
 * Play one complete round.  Interactive, scripted and simulated play all
 * run through here, so there is exactly one implementation of the rules.
 */
static void play_round(war_agent_t *ag, rng_t *rng, long bet, war_round_t *r)
{
    FILE   *f = ag->disp;
    bool    show = ag->display;
    shoe_t  shoe;
    int     dec, p, d;
    char    money[32];

    memset(r, 0, sizeof *r);
    r->bet = bet;
    r->decision = WAR_TIE_ASK;

    shoe_init(&shoe, WAR_DECKS);
    shoe_shuffle(&shoe, rng);

    r->player = shoe_draw(&shoe);
    r->dealer = shoe_draw(&shoe);
    r->wagered = bet;

    if (show)
        show_cards(f, r->player, r->dealer);

    p = war_value(r->player);
    d = war_value(r->dealer);

    if (p > d) {
        r->result = WAR_WIN;
        r->returned = 2 * bet;      /* wager back plus 1:1 */
        return;
    }
    if (p < d) {
        r->result = WAR_LOSS;
        r->returned = 0;
        return;
    }

    r->tie = true;
    if (show)
        fprintf(f, "\nTIE!\n\n");

    dec = decide(ag);
    if (dec < 0)
        dec = WAR_TIE_SURRENDER;    /* EOF concedes: no further wager */
    r->decision = (war_tie_t)dec;

    if (r->decision == WAR_TIE_SURRENDER) {
        r->result = WAR_SURRENDER;
        r->returned = bet / 2;      /* exact: a wager is an even number
                                     * of halves */
        if (show) {
            war_money(money, sizeof money, bet / 2, false);
            fprintf(f, "\nSurrendered: half the wager (%s) comes back.\n",
                    money);
        }
        return;
    }

    /* Go to war: match the wager, burn three, one more card each. */
    r->wagered += bet;
    for (int i = 0; i < WAR_BURN; i++)
        r->burn[i] = shoe_draw(&shoe);
    r->war_player = shoe_draw(&shoe);
    r->war_dealer = shoe_draw(&shoe);

    if (show) {
        war_money(money, sizeof money, bet, false);
        fprintf(f, "\nWAR!\n\n");
        fprintf(f, "War wager: %s\n", money);
        fprintf(f, "Burned:    %d cards\n\n", WAR_BURN);
        show_cards(f, r->war_player, r->war_dealer);
    }

    p = war_value(r->war_player);
    d = war_value(r->war_dealer);

    if (p > d) {
        /* original wager pushes, the war wager is paid 1:1 */
        r->result = WAR_WIN;
        r->returned = 3 * bet;
    } else if (p < d) {
        r->result = WAR_LOSS;
        r->returned = 0;
    } else {
        r->second_tie = true;
        r->result = WAR_PUSH;       /* the war wager pushes too */
        r->returned = 2 * bet;
        if (show)
            fprintf(f, "\nTIE AGAIN - both wagers come back.\n");
    }
}

/* ---- result reporting --------------------------------------------------- */

static const char *decision_word(const war_round_t *r)
{
    return r->decision == WAR_TIE_WAR ? "war" : "surrender";
}

static void result_quiet(FILE *f, const war_round_t *r)
{
    char pc[8], dc[8], wpc[8], wdc[8], w[32], ret[32], net[32];

    card_name(r->player, pc, sizeof pc);
    card_name(r->dealer, dc, sizeof dc);
    war_money(w, sizeof w, r->wagered, false);
    war_money(ret, sizeof ret, r->returned, false);
    war_money(net, sizeof net, r->returned - r->wagered, true);

    fprintf(f, "%s player=%s dealer=%s", RESULT_WORD[r->result], pc, dc);
    if (r->tie) {
        fprintf(f, " tie=%s", decision_word(r));
        if (r->decision == WAR_TIE_WAR) {
            card_name(r->war_player, wpc, sizeof wpc);
            card_name(r->war_dealer, wdc, sizeof wdc);
            fprintf(f, " war_player=%s war_dealer=%s second_tie=%s", wpc, wdc,
                    r->second_tie ? "yes" : "no");
        }
    }
    fprintf(f, " wagered=%s returned=%s net=%s\n", w, ret, net);
}

static void result_json(FILE *f, const war_round_t *r)
{
    char buf[8];

    fprintf(f, "{\"game\":\"war\",\"bet\":%.1f,\"player\":", war_units(r->bet));
    card_name(r->player, buf, sizeof buf);
    json_string(f, buf);
    fprintf(f, ",\"dealer\":");
    card_name(r->dealer, buf, sizeof buf);
    json_string(f, buf);
    fprintf(f, ",\"tie\":%s,\"decision\":", r->tie ? "true" : "false");
    if (r->tie)
        json_string(f, decision_word(r));
    else
        fputs("null", f);

    if (r->tie && r->decision == WAR_TIE_WAR) {
        fprintf(f, ",\"burn\":[");
        for (int i = 0; i < WAR_BURN; i++) {
            if (i)
                fputc(',', f);
            card_name(r->burn[i], buf, sizeof buf);
            json_string(f, buf);
        }
        fprintf(f, "],\"war_player\":");
        card_name(r->war_player, buf, sizeof buf);
        json_string(f, buf);
        fprintf(f, ",\"war_dealer\":");
        card_name(r->war_dealer, buf, sizeof buf);
        json_string(f, buf);
        fprintf(f, ",\"second_tie\":%s", r->second_tie ? "true" : "false");
    }

    fprintf(f, ",\"result\":");
    json_string(f, RESULT_JSON[r->result]);
    fprintf(f, ",\"wagered\":%.1f,\"returned\":%.1f,\"net\":%.1f}\n",
            war_units(r->wagered), war_units(r->returned),
            war_units(r->returned - r->wagered));
}

/* ---- statistics --------------------------------------------------------- */

typedef struct {
    long long rounds;
    long long player_wins, dealer_wins, ties;   /* first comparison */
    long long wars, surrenders;
    long long war_wins, war_losses, second_ties;
    long long wagered, returned;                /* halves */
} war_stats_t;

static void stats_add(war_stats_t *st, const war_round_t *r)
{
    st->rounds++;
    st->wagered += r->wagered;
    st->returned += r->returned;

    if (!r->tie) {
        if (r->result == WAR_WIN)
            st->player_wins++;
        else
            st->dealer_wins++;
        return;
    }

    st->ties++;
    if (r->decision == WAR_TIE_SURRENDER) {
        st->surrenders++;
        return;
    }
    st->wars++;
    if (r->second_tie)
        st->second_ties++;
    else if (r->result == WAR_WIN)
        st->war_wins++;
    else
        st->war_losses++;
}

static void stats_print(const cli_t *cli, const war_stats_t *st, long bet,
                        war_tie_t strategy)
{
    const char *strat = strategy == WAR_TIE_WAR ? "war" : "surrender";
    double ret = st->wagered ? (double)st->returned / (double)st->wagered
                             : 0.0;
    char b[32], w[32], back[32], net[32];

    war_money(b, sizeof b, bet, false);
    war_money(w, sizeof w, st->wagered, false);
    war_money(back, sizeof back, st->returned, false);
    war_money(net, sizeof net, st->returned - st->wagered, true);

    if (cli->json) {
        printf("{\"game\":\"war\",\"iterations\":%ld,\"bet\":%.1f,"
               "\"strategy\":", cli->iterations, war_units(bet));
        json_string(stdout, strat);
        printf(",\"rounds\":%lld,\"player_wins\":%lld,\"dealer_wins\":%lld,"
               "\"ties\":%lld,\"wars\":%lld,\"surrenders\":%lld,"
               "\"war_wins\":%lld,\"war_losses\":%lld,\"second_ties\":%lld,"
               "\"wagered\":%.1f,\"returned\":%.1f,\"net\":%.1f,"
               "\"return_per_unit\":%.6f}\n",
               st->rounds, st->player_wins, st->dealer_wins, st->ties,
               st->wars, st->surrenders, st->war_wins, st->war_losses,
               st->second_ties, war_units(st->wagered),
               war_units(st->returned),
               war_units(st->returned - st->wagered), ret);
    } else if (cli->quiet) {
        printf("runs=%ld bet=%s strategy=%s rounds=%lld player_wins=%lld "
               "dealer_wins=%lld ties=%lld wars=%lld surrenders=%lld "
               "war_wins=%lld war_losses=%lld second_ties=%lld "
               "wagered=%s returned=%s net=%s return=%.4f\n",
               cli->iterations, b, strat, st->rounds, st->player_wins,
               st->dealer_wins, st->ties, st->wars, st->surrenders,
               st->war_wins, st->war_losses, st->second_ties, w, back, net,
               ret);
    } else {
        double n = (double)(st->rounds ? st->rounds : 1);

        printf("Iterations: %lld   Bet: %s   Tie strategy: %s\n",
               st->rounds, b, strat);
        printf("%-14s %10s %9s\n", "OUTCOME", "COUNT", "RATE%");
        printf("%-14s %10lld %9.4f\n", "player wins", st->player_wins,
               100.0 * (double)st->player_wins / n);
        printf("%-14s %10lld %9.4f\n", "dealer wins", st->dealer_wins,
               100.0 * (double)st->dealer_wins / n);
        printf("%-14s %10lld %9.4f\n", "ties", st->ties,
               100.0 * (double)st->ties / n);
        printf("%-14s %10lld %9.4f\n", "wars", st->wars,
               100.0 * (double)st->wars / n);
        printf("%-14s %10lld %9.4f\n", "surrenders", st->surrenders,
               100.0 * (double)st->surrenders / n);
        printf("%-14s %10lld %9.4f\n", "war wins", st->war_wins,
               100.0 * (double)st->war_wins / n);
        printf("%-14s %10lld %9.4f\n", "war losses", st->war_losses,
               100.0 * (double)st->war_losses / n);
        printf("%-14s %10lld %9.4f\n", "second ties", st->second_ties,
               100.0 * (double)st->second_ties / n);
        printf("Wagered:    %s\n", w);
        printf("Returned:   %s\n", back);
        printf("Net units:  %s (%.4f per unit wagered)\n", net, ret - 1.0);
    }
}

/* ---- argument parsing --------------------------------------------------- */

static int parse_args(const cli_t *cli, long *bet, war_tie_t *strategy)
{
    *bet = WAR_BET_DEFAULT * WAR_HALF;
    *strategy = WAR_TIE_ASK;

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];
        int d;

        if (bet_is(b, "bet")) {
            if (b->nvalues != 1 || b->values[0] < WAR_BET_MIN ||
                b->values[0] > WAR_BET_MAX) {
                fprintf(stderr, "war: bet '%s': wager must be %d-%d\n",
                        b->raw, WAR_BET_MIN, WAR_BET_MAX);
                return 2;
            }
            *bet = (long)b->values[0] * WAR_HALF;
            continue;
        }
        if (bet_has_value(b)) {
            fprintf(stderr, "war: unknown argument '%s' "
                            "(valid: war, surrender, bet:N)\n", b->raw);
            return 2;
        }
        d = decision_lookup(b->name);
        if (d < 0) {
            fprintf(stderr, "war: unknown argument '%s' "
                            "(valid: war, surrender, bet:N)\n", b->raw);
            return 2;
        }
        if (*strategy != WAR_TIE_ASK) {
            fprintf(stderr, "war: choose at most one tie strategy "
                            "(war or surrender)\n");
            return 2;
        }
        *strategy = (war_tie_t)d;
    }
    return 0;
}

/* ---- driver ------------------------------------------------------------- */

int war_run(const cli_t *cli, rng_t *rng)
{
    war_tie_t   strategy;
    long        bet;
    war_stats_t st = { 0 };

    if (parse_args(cli, &bet, &strategy))
        return 2;

    if (cli->stats && strategy == WAR_TIE_ASK) {
        fprintf(stderr, "war: simulation needs a scripted tie strategy "
                        "(war or surrender)\n");
        return 2;
    }

    bool machine = cli->quiet || cli->json || cli->stats;
    bool interactive = strategy == WAR_TIE_ASK;
    war_agent_t ag = {
        .fixed = strategy,
        .disp = machine ? stderr : stdout,
        .display = false,
    };
    ag.display = interactive || (!machine && cli->iterations == 1);

    for (long it = 0; it < cli->iterations; it++) {
        war_round_t r;
        char net[32];

        if (ag.display) {
            char b[32];
            war_money(b, sizeof b, bet, false);
            fprintf(ag.disp, "========================================\n");
            fprintf(ag.disp, "               CASINO WAR\n");
            fprintf(ag.disp, "========================================\n\n");
            fprintf(ag.disp, "Bet: %s\n\n", b);
        }

        play_round(&ag, rng, bet, &r);
        stats_add(&st, &r);

        if (cli->stats)
            continue;

        if (cli->json) {
            result_json(stdout, &r);
        } else if (cli->quiet) {
            result_quiet(stdout, &r);
        } else if (!ag.display) {
            war_money(net, sizeof net, r.returned - r.wagered, true);
            printf("%s%s net=%s\n", RESULT_WORD[r.result],
                   r.tie ? (r.decision == WAR_TIE_WAR ? " (war)"
                                                      : " (surrender)")
                         : "", net);
        } else {
            print_result(stdout, &r);
        }
    }

    if (cli->stats)
        stats_print(cli, &st, bet, strategy);
    return 0;
}

void war_list_bets(void)
{
    puts("casino war: one card each against the dealer, high card wins");
    puts("rules:");
    puts("  the player and the dealer each get one card from a "
         "6-deck shoe");
    puts("  (reshuffled every round).  ace is high and suits are "
         "irrelevant.");
    puts("  higher player rank  wins even money (1:1)");
    puts("  lower player rank   loses the wager");
    puts("  equal ranks         a tie: go to war, or surrender");
    puts("surrender:");
    puts("  concede the tie and lose half the original wager");
    puts("go to war:");
    puts("  match the original wager, burn 3 cards, then one more card "
         "each");
    puts("  player card higher  original wager pushes, war wager pays 1:1");
    puts("  dealer card higher  both wagers lose");
    puts("  equal again         the war wager pushes, both wagers "
         "come back");
    puts("arguments:");
    puts("  war            always go to war on a tie");
    puts("  surrender      always surrender on a tie");
    puts("  bet:N          wager for the round (default 100)");
    puts("with no tie strategy the round is interactive and a tie prompts");
    puts("  [w]ar / [s]urrender");
    puts("usage:");
    puts("  war                    interactive round");
    puts("  war war                scripted: always go to war");
    puts("  war surrender          scripted: always surrender");
    puts("  war war --runs 100000  simulate (a tie strategy is required)");
    puts("results: WIN | LOSS | PUSH | SURRENDER");
}
