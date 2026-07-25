#include "blackjack.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cards.h"
#include "output.h"

/* Single deck: 11 cards is the largest hand that can still be < 21,
 * so one more (the busting/21st draw) bounds any hand at 12. */
#define BJ_MAX_CARDS   12
#define BJ_MAX_ACTIONS 64

typedef enum { BJ_HIT, BJ_STAND, BJ_DOUBLE } bj_action_t;
typedef enum { BJ_WIN, BJ_LOSS, BJ_PUSH, BJ_BLACKJACK } bj_result_t;

static const char *const ACTION_WORD[] = { "hit", "stand", "double" };
static const char *const RESULT_WORD[] = { "WIN", "LOSS", "PUSH", "BLACKJACK" };
static const char *const RESULT_JSON[] = { "win", "loss", "push", "blackjack" };
static const char *const RESULT_LINE[] = {
    "PLAYER WINS", "DEALER WINS", "PUSH", "BLACKJACK"
};

typedef struct {
    card_t cards[BJ_MAX_CARDS];
    int    n;
} bj_hand_t;

typedef struct {
    bj_action_t acts[BJ_MAX_ACTIONS];
    int         n;              /* 0 => interactive mode */
    int         pos;
} bj_script_t;

static void hand_add(bj_hand_t *h, card_t c)
{
    if (h->n >= BJ_MAX_CARDS) {
        fprintf(stderr, "blackjack: hand overflow\n");
        exit(70);
    }
    h->cards[h->n++] = c;
}

static int hand_value(const bj_hand_t *h)
{
    int  sum = 0;
    bool ace = false;

    for (int i = 0; i < h->n; i++) {
        int r = h->cards[i].rank;
        if (r == 1)
            ace = true;
        sum += r > 10 ? 10 : r;
    }
    /* At most one ace can ever count as 11. */
    if (ace && sum + 10 <= 21)
        sum += 10;
    return sum;
}

static bool hand_natural(const bj_hand_t *h)
{
    return h->n == 2 && hand_value(h) == 21;
}

static void hand_print(FILE *f, const char *label, const bj_hand_t *h,
                       bool hide_hole)
{
    char buf[8];
    int  show = hide_hole ? 1 : h->n;

    fprintf(f, "%s:", label);
    for (int i = 0; i < show; i++) {
        card_name(h->cards[i], buf, sizeof buf);
        fprintf(f, " %s", buf);
    }
    if (hide_hole)
        fprintf(f, " ??\n");
    else
        fprintf(f, " = %d\n", hand_value(h));
}

static void hand_json(FILE *f, const bj_hand_t *h)
{
    char buf[8];

    fputc('[', f);
    for (int i = 0; i < h->n; i++) {
        if (i)
            fputc(',', f);
        card_name(h->cards[i], buf, sizeof buf);
        json_string(f, buf);
    }
    fputc(']', f);
}

static int action_parse(const char *w, bj_action_t *out)
{
    if (strcasecmp(w, "h") == 0 || strcasecmp(w, "hit") == 0)
        *out = BJ_HIT;
    else if (strcasecmp(w, "s") == 0 || strcasecmp(w, "stand") == 0)
        *out = BJ_STAND;
    else if (strcasecmp(w, "d") == 0 || strcasecmp(w, "double") == 0)
        *out = BJ_DOUBLE;
    else
        return -1;
    return 0;
}

/* Turn the bet tokens into an action script: each token is one or more
 * comma-separated actions ("h,s"), and tokens concatenate ("h s"). */
static int script_build(const cli_t *cli, bj_script_t *sc)
{
    sc->n = 0;
    sc->pos = 0;

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];
        if (b->nvalues != 0) {
            fprintf(stderr, "blackjack: action '%s' takes no value\n",
                    b->raw);
            return 2;
        }
        const char *p = b->name;
        for (;;) {
            const char *end = strchr(p, ',');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            char word[16];
            if (len == 0 || len >= sizeof word) {
                fprintf(stderr, "blackjack: bad action list '%s'\n",
                        b->raw);
                return 2;
            }
            memcpy(word, p, len);
            word[len] = '\0';
            bj_action_t a;
            if (action_parse(word, &a) < 0) {
                fprintf(stderr, "blackjack: unknown action '%s' "
                                "(valid: h/hit, s/stand, d/double)\n",
                        word);
                return 2;
            }
            if (sc->n >= BJ_MAX_ACTIONS) {
                fprintf(stderr, "blackjack: too many actions (max %d)\n",
                        BJ_MAX_ACTIONS);
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

/* Next player action.  Scripted: consume in order, implicit stand when
 * exhausted, illegal double is fatal (returns 2).  Interactive: prompt on
 * disp, read stdin, EOF means stand, bad input re-prompts. */
static int next_action(bj_script_t *sc, FILE *disp, bool echo,
                       bool allow_double, bj_action_t *out)
{
    if (sc->n > 0) {
        if (sc->pos >= sc->n) {
            *out = BJ_STAND;
            return 0;
        }
        bj_action_t a = sc->acts[sc->pos++];
        if (a == BJ_DOUBLE && !allow_double) {
            fprintf(stderr, "blackjack: double only allowed on the first "
                            "two cards\n");
            return 2;
        }
        if (echo)
            fprintf(disp, "> %s\n", ACTION_WORD[a]);
        *out = a;
        return 0;
    }

    for (;;) {
        fprintf(disp, "\n[h]it [s]tand%s\n> ",
                allow_double ? " [d]ouble" : "");
        fflush(disp);

        char line[64];
        if (!fgets(line, sizeof line, stdin)) {
            fprintf(disp, "\n");
            *out = BJ_STAND;
            return 0;
        }
        char *w = line + strspn(line, " \t");
        w[strcspn(w, " \t\r\n")] = '\0';

        if (action_parse(w, out) < 0) {
            fprintf(disp, "invalid action\n");
            continue;
        }
        if (*out == BJ_DOUBLE && !allow_double) {
            fprintf(disp, "double only allowed on the first two cards\n");
            continue;
        }
        return 0;
    }
}

/* Play one round from a freshly shuffled single deck.  Fills the final
 * hands, the actions taken and the result; prints the transcript to disp
 * when display is set.  Returns 0, or 2 on a scripted illegal action. */
static int play_round(rng_t *rng, bj_script_t *sc, FILE *disp, bool display,
                      bj_hand_t *player, bj_hand_t *dealer,
                      bj_action_t actions[], int *nactions,
                      bj_result_t *result)
{
    shoe_t shoe;

    shoe_init(&shoe, 1);
    shoe_shuffle(&shoe, rng);
    sc->pos = 0;
    player->n = dealer->n = 0;
    *nactions = 0;

    hand_add(player, shoe_draw(&shoe));
    hand_add(dealer, shoe_draw(&shoe));
    hand_add(player, shoe_draw(&shoe));
    hand_add(dealer, shoe_draw(&shoe));

    if (display) {
        hand_print(disp, "Player", player, false);
        hand_print(disp, "Dealer", dealer, true);
    }

    /* Dealer peeks: naturals settle before the player acts. */
    bool pn = hand_natural(player), dn = hand_natural(dealer);
    if (pn || dn) {
        if (display)
            hand_print(disp, "Dealer", dealer, false);
        *result = pn && dn ? BJ_PUSH : pn ? BJ_BLACKJACK : BJ_LOSS;
        return 0;
    }

    while (hand_value(player) < 21) {
        bool allow_double = (player->n == 2);
        bj_action_t a;
        if (next_action(sc, disp, display && sc->n > 0, allow_double, &a))
            return 2;
        actions[(*nactions)++] = a;
        if (a == BJ_STAND)
            break;
        hand_add(player, shoe_draw(&shoe));
        if (display)
            hand_print(disp, "Player", player, false);
        if (a == BJ_DOUBLE)
            break;
    }

    int pv = hand_value(player);
    if (pv > 21) {
        *result = BJ_LOSS;      /* bust; dealer does not play */
        return 0;
    }

    if (display)
        hand_print(disp, "Dealer", dealer, false);
    while (hand_value(dealer) < 17) {   /* stands on all 17s */
        card_t c = shoe_draw(&shoe);
        hand_add(dealer, c);
        if (display) {
            char buf[8];
            card_name(c, buf, sizeof buf);
            fprintf(disp, "Dealer draws: %s\n", buf);
        }
    }
    if (display && dealer->n > 2)
        hand_print(disp, "Dealer", dealer, false);

    int dv = hand_value(dealer);
    if (dv > 21 || pv > dv)
        *result = BJ_WIN;
    else if (pv < dv)
        *result = BJ_LOSS;
    else
        *result = BJ_PUSH;
    return 0;
}

int blackjack_run(const cli_t *cli, rng_t *rng)
{
    bj_script_t sc;
    if (script_build(cli, &sc))
        return 2;

    bool interactive = (sc.n == 0);
    bool machine = cli->quiet || cli->json || cli->stats;
    /* Interactive play needs the table visible even when stdout is
     * reserved for machine output, so the transcript moves to stderr. */
    FILE *disp = machine ? stderr : stdout;
    bool display = interactive || (!machine && cli->iterations == 1);

    long counts[4] = { 0 };

    for (long it = 0; it < cli->iterations; it++) {
        bj_hand_t   player, dealer;
        bj_action_t actions[BJ_MAX_CARDS];
        int         nact;
        bj_result_t r;

        if (display && cli->iterations > 1)
            fprintf(disp, "-- round %ld --\n", it + 1);

        if (play_round(rng, &sc, disp, display, &player, &dealer,
                       actions, &nact, &r))
            return 2;
        counts[r]++;

        if (cli->stats)
            continue;

        if (cli->json) {
            printf("{\"game\":\"blackjack\",\"player\":");
            hand_json(stdout, &player);
            printf(",\"player_total\":%d,\"dealer\":", hand_value(&player));
            hand_json(stdout, &dealer);
            printf(",\"dealer_total\":%d,\"actions\":[", hand_value(&dealer));
            for (int i = 0; i < nact; i++) {
                if (i)
                    printf(",");
                json_string(stdout, ACTION_WORD[actions[i]]);
            }
            printf("],\"result\":");
            json_string(stdout, RESULT_JSON[r]);
            printf("}\n");
        } else if (cli->quiet) {
            puts(RESULT_WORD[r]);
        } else if (!display) {
            printf("%s  player=%d dealer=%d\n", RESULT_WORD[r],
                   hand_value(&player), hand_value(&dealer));
        } else {
            fprintf(disp, "\n%s\n", RESULT_LINE[r]);
        }
    }

    if (cli->stats) {
        if (cli->json) {
            printf("{\"game\":\"blackjack\",\"iterations\":%ld,"
                   "\"results\":{\"win\":%ld,\"loss\":%ld,\"push\":%ld,"
                   "\"blackjack\":%ld},\"win_rate\":%.6f}\n",
                   cli->iterations, counts[BJ_WIN], counts[BJ_LOSS],
                   counts[BJ_PUSH], counts[BJ_BLACKJACK],
                   (double)(counts[BJ_WIN] + counts[BJ_BLACKJACK]) /
                       (double)cli->iterations);
        } else {
            printf("Iterations: %ld\n", cli->iterations);
            printf("%-10s %8s %9s\n", "RESULT", "COUNT", "RATE%");
            for (int i = 0; i < 4; i++)
                printf("%-10s %8ld %9.4f\n", RESULT_JSON[i], counts[i],
                       100.0 * (double)counts[i] /
                           (double)cli->iterations);
        }
    }
    return 0;
}

void blackjack_list_bets(void)
{
    puts("blackjack actions (single deck, dealer stands on all 17s, "
         "no splits):");
    puts("  h | hit      take another card");
    puts("  s | stand    end your turn");
    puts("  d | double   take exactly one card, then stand "
         "(first two cards only)");
    puts("usage:");
    puts("  blackjack                 interactive prompt");
    puts("  blackjack h,s             scripted: hit then stand");
    puts("  blackjack hit hit stand   same, space-separated");
    puts("results: WIN | LOSS | PUSH | BLACKJACK (natural 21)");
}
