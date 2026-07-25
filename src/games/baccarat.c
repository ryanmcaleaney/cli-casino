#include "baccarat.h"

#include <stdio.h>
#include <stdlib.h>

#include "cards.h"
#include "output.h"

/* Punto Banco: no player decisions, the draw rules are fully mechanical. */
#define BAC_MAX_CARDS 3

typedef enum { BAC_PLAYER, BAC_BANKER, BAC_TIE } bac_side_t;
typedef enum { BAC_WIN, BAC_LOSS, BAC_PUSH } bac_result_t;

static const char *const SIDE_WORD[]   = { "PLAYER", "BANKER", "TIE" };
static const char *const SIDE_JSON[]   = { "player", "banker", "tie" };
static const char *const RESULT_WORD[] = { "WIN", "LOSS", "PUSH" };
static const char *const RESULT_JSON[] = { "win", "loss", "push" };

typedef struct {
    card_t cards[BAC_MAX_CARDS];
    int    n;
} bac_hand_t;

static void hand_add(bac_hand_t *h, card_t c)
{
    if (h->n >= BAC_MAX_CARDS) {
        fprintf(stderr, "baccarat: hand overflow\n");
        exit(70);
    }
    h->cards[h->n++] = c;
}

/* A=1, 2-9=face, 10/J/Q/K=0. */
static int card_value(card_t c)
{
    return c.rank >= 10 ? 0 : c.rank;
}

static int hand_total(const bac_hand_t *h)
{
    int sum = 0;
    for (int i = 0; i < h->n; i++)
        sum += card_value(h->cards[i]);
    return sum % 10;
}

static void hand_print(FILE *f, const char *label, const bac_hand_t *h)
{
    char buf[8];

    fprintf(f, "%s:", label);
    for (int i = 0; i < h->n; i++) {
        card_name(h->cards[i], buf, sizeof buf);
        fprintf(f, " %s", buf);
    }
    fprintf(f, " = %d\n", hand_total(h));
}

static void hand_json(FILE *f, const bac_hand_t *h)
{
    char buf[8];

    fputs("{\"cards\":[", f);
    for (int i = 0; i < h->n; i++) {
        if (i)
            fputc(',', f);
        card_name(h->cards[i], buf, sizeof buf);
        json_string(f, buf);
    }
    fprintf(f, "],\"total\":%d}", hand_total(h));
}

/* Standard Punto Banco banker third-card table, given the player's third
 * card value (p3 < 0 means the player stood on 6/7 and drew no card). */
static bool banker_draws(int banker_total, int p3)
{
    if (p3 < 0)
        return banker_total <= 5;
    switch (banker_total) {
    case 0: case 1: case 2: return true;
    case 3:                 return p3 != 8;
    case 4:                 return p3 >= 2 && p3 <= 7;
    case 5:                 return p3 >= 4 && p3 <= 7;
    case 6:                 return p3 == 6 || p3 == 7;
    default:                return false; /* 7 stands; 8/9 are naturals */
    }
}

/* Deal and resolve one round from a freshly shuffled single deck. */
static void play_round(rng_t *rng, bac_hand_t *player, bac_hand_t *banker,
                       bac_side_t *outcome)
{
    shoe_t shoe;

    shoe_init(&shoe, 1);
    shoe_shuffle(&shoe, rng);
    player->n = banker->n = 0;

    hand_add(player, shoe_draw(&shoe));
    hand_add(banker, shoe_draw(&shoe));
    hand_add(player, shoe_draw(&shoe));
    hand_add(banker, shoe_draw(&shoe));

    int pt = hand_total(player);
    int bt = hand_total(banker);

    if (pt < 8 && bt < 8) {
        int p3 = -1;
        if (pt <= 5) {
            card_t c = shoe_draw(&shoe);
            hand_add(player, c);
            p3 = card_value(c);
            pt = hand_total(player);
        }
        if (banker_draws(bt, p3)) {
            hand_add(banker, shoe_draw(&shoe));
            bt = hand_total(banker);
        }
    }

    if (pt > bt)
        *outcome = BAC_PLAYER;
    else if (bt > pt)
        *outcome = BAC_BANKER;
    else
        *outcome = BAC_TIE;
}

int baccarat_run(const cli_t *cli, rng_t *rng)
{
    if (cli->nbets != 1) {
        fprintf(stderr, "baccarat: choose exactly one bet: "
                        "player, banker, or tie\n");
        return 2;
    }
    const bet_t *b = &cli->bets[0];
    bac_side_t bet;
    if (bet_is(b, "player"))
        bet = BAC_PLAYER;
    else if (bet_is(b, "banker"))
        bet = BAC_BANKER;
    else if (bet_is(b, "tie"))
        bet = BAC_TIE;
    else {
        fprintf(stderr, "baccarat: unknown bet '%s' "
                        "(valid: player, banker, tie)\n", b->raw);
        return 2;
    }
    if (b->nvalues != 0) {
        fprintf(stderr, "baccarat: bet '%s' takes no value\n", b->raw);
        return 2;
    }

    bool machine = cli->quiet || cli->json || cli->stats;
    bool display = !machine && cli->iterations == 1;
    long counts[3] = { 0 };

    for (long it = 0; it < cli->iterations; it++) {
        bac_hand_t player, banker;
        bac_side_t outcome;

        play_round(rng, &player, &banker, &outcome);

        bac_result_t r = outcome == bet    ? BAC_WIN
                        : outcome == BAC_TIE ? BAC_PUSH
                                              : BAC_LOSS;
        counts[r]++;

        if (cli->stats)
            continue;

        if (cli->json) {
            printf("{\"game\":\"baccarat\",\"bet\":");
            json_string(stdout, b->name);
            printf(",\"player\":");
            hand_json(stdout, &player);
            printf(",\"banker\":");
            hand_json(stdout, &banker);
            printf(",\"outcome\":");
            json_string(stdout, SIDE_JSON[outcome]);
            printf(",\"result\":");
            json_string(stdout, RESULT_JSON[r]);
            printf("}\n");
        } else if (cli->quiet) {
            puts(RESULT_WORD[r]);
        } else if (!display) {
            printf("%s  player=%d banker=%d\n", RESULT_WORD[r],
                   hand_total(&player), hand_total(&banker));
        } else {
            hand_print(stdout, "Player", &player);
            hand_print(stdout, "Banker", &banker);
            printf("\nResult: %s\n", SIDE_WORD[outcome]);
            printf("Bet: %s\n", SIDE_WORD[bet]);
            puts(RESULT_WORD[r]);
        }
    }

    if (cli->stats) {
        if (cli->json) {
            printf("{\"game\":\"baccarat\",\"iterations\":%ld,\"bet\":",
                   cli->iterations);
            json_string(stdout, b->name);
            printf(",\"results\":{\"win\":%ld,\"loss\":%ld,\"push\":%ld},"
                   "\"win_rate\":%.6f}\n",
                   counts[BAC_WIN], counts[BAC_LOSS], counts[BAC_PUSH],
                   (double)counts[BAC_WIN] / (double)cli->iterations);
        } else {
            printf("Iterations: %ld\n", cli->iterations);
            printf("%-10s %8s %9s\n", "RESULT", "COUNT", "RATE%");
            for (int i = 0; i < 3; i++)
                printf("%-10s %8ld %9.4f\n", RESULT_JSON[i], counts[i],
                       100.0 * (double)counts[i] / (double)cli->iterations);
        }
    }
    return 0;
}

void baccarat_list_bets(void)
{
    puts("baccarat bets (Punto Banco, single deck reshuffled each round):");
    puts("  player   wins if the player hand is higher (push on tie)");
    puts("  banker   wins if the banker hand is higher (push on tie)");
    puts("  tie      wins only if both hands are equal");
    puts("usage:");
    puts("  baccarat player");
    puts("  baccarat banker");
    puts("  baccarat tie");
    puts("rules: hand value = sum of card values mod 10 "
         "(A=1, 2-9=face, 10/J/Q/K=0).");
    puts("  a two-card total of 8 or 9 (\"natural\") ends the round "
         "immediately.");
    puts("  otherwise the standard Punto Banco player/banker third-card "
         "table applies.");
    puts("results: WIN | LOSS | PUSH (player/banker bets push on a tie)");
}
