#include "videopoker.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cardart.h"
#include "cards.h"
#include "output.h"
#include "poker.h"

/* Jacks or Better pay ladder, ordered low to high. */
typedef enum {
    VP_HIGH_CARD, VP_LOW_PAIR, VP_JACKS_OR_BETTER, VP_TWO_PAIR,
    VP_THREE_OF_A_KIND, VP_STRAIGHT, VP_FLUSH, VP_FULL_HOUSE,
    VP_FOUR_OF_A_KIND, VP_STRAIGHT_FLUSH, VP_ROYAL_FLUSH
} vp_cat_t;

#define VP_NCATS 11

static const char *const VP_TOKEN[VP_NCATS] = {
    "HIGH_CARD", "LOW_PAIR", "JACKS_OR_BETTER", "TWO_PAIR",
    "THREE_OF_A_KIND", "STRAIGHT", "FLUSH", "FULL_HOUSE",
    "FOUR_OF_A_KIND", "STRAIGHT_FLUSH", "ROYAL_FLUSH"
};
static const char *const VP_JSON[VP_NCATS] = {
    "high_card", "low_pair", "jacks_or_better", "two_pair",
    "three_of_a_kind", "straight", "flush", "full_house",
    "four_of_a_kind", "straight_flush", "royal_flush"
};
/* Informational only; 1-unit bet, no money is tracked. */
static const int VP_PAYOUT[VP_NCATS] = { 0, 0, 1, 2, 3, 4, 6, 9, 25, 50, 250 };

static const char *const RANK_PLURAL[14] = {
    "", "Aces", "Twos", "Threes", "Fours", "Fives", "Sixes", "Sevens",
    "Eights", "Nines", "Tens", "Jacks", "Queens", "Kings"
};

static vp_cat_t vp_classify(const card_t hand[5], poker_eval_t *evout)
{
    poker_eval_t ev = poker_eval5(hand);
    if (evout)
        *evout = ev;
    switch (ev.cat) {
    case POKER_ROYAL_FLUSH:     return VP_ROYAL_FLUSH;
    case POKER_STRAIGHT_FLUSH:  return VP_STRAIGHT_FLUSH;
    case POKER_FOUR_OF_A_KIND:  return VP_FOUR_OF_A_KIND;
    case POKER_FULL_HOUSE:      return VP_FULL_HOUSE;
    case POKER_FLUSH:           return VP_FLUSH;
    case POKER_STRAIGHT:        return VP_STRAIGHT;
    case POKER_THREE_OF_A_KIND: return VP_THREE_OF_A_KIND;
    case POKER_TWO_PAIR:        return VP_TWO_PAIR;
    case POKER_PAIR:
        return (ev.pair_rank == 1 || ev.pair_rank >= 11)
                   ? VP_JACKS_OR_BETTER : VP_LOW_PAIR;
    case POKER_HIGH_CARD:
    default:                    return VP_HIGH_CARD;
    }
}

/* "Pair of Aces" for pairs, otherwise the generic category name. */
static const char *hand_desc(const poker_eval_t *ev, char *buf, size_t len)
{
    if (ev->cat == POKER_PAIR) {
        snprintf(buf, len, "Pair of %s", RANK_PLURAL[ev->pair_rank]);
        return buf;
    }
    return poker_cat_str(ev->cat);
}

/* --- hold parsing (kept separate from evaluation) --------------------- */

static int holds_from_values(const int *vals, int n, bool held[5])
{
    for (int i = 0; i < 5; i++)
        held[i] = false;
    for (int i = 0; i < n; i++) {
        if (vals[i] < 1 || vals[i] > 5)
            return -1;
        if (held[vals[i] - 1])
            return -1;              /* duplicate */
        held[vals[i] - 1] = true;
    }
    return 0;
}

/* Interactive input: "1,3", "1 3", "none", "all", "" (= none). */
static int parse_hold_line(const char *s, bool held[5])
{
    int vals[8];
    int n = 0;

    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0' || strncasecmp(s, "none", 4) == 0)
        return holds_from_values(NULL, 0, held);
    if (strncasecmp(s, "all", 3) == 0) {
        for (int i = 0; i < 5; i++)
            held[i] = true;
        return 0;
    }
    while (*s) {
        if (isdigit((unsigned char)*s)) {
            if (n >= 8)
                return -1;
            vals[n++] = *s - '0';
            s++;
            if (isdigit((unsigned char)*s))
                return -1;          /* multi-digit position */
        } else if (*s == ',' || isspace((unsigned char)*s)) {
            s++;
        } else {
            return -1;
        }
    }
    if (n == 0)
        return -1;
    return holds_from_values(vals, n, held);
}

/* --- deal:... testing/analysis hook ----------------------------------- */

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

/* vraw like "ah,kh,qh,jh,10h" -> 5 distinct cards. */
static int parse_deal(const char *s, card_t cards[5])
{
    int n = 0;
    while (*s) {
        const char *end = strchr(s, ',');
        size_t len = end ? (size_t)(end - s) : strlen(s);
        if (n == 5 || parse_card(s, len, &cards[n]) < 0)
            return -1;
        n++;
        if (!end)
            break;
        s = end + 1;
        if (*s == '\0')
            return -1;              /* trailing comma */
    }
    if (n != 5)
        return -1;
    for (int i = 0; i < 5; i++)
        for (int j = i + 1; j < 5; j++)
            if (cards[i].rank == cards[j].rank &&
                cards[i].suit == cards[j].suit)
                return -1;          /* duplicate card */
    return 0;
}

/* --- game flow --------------------------------------------------------- */

static void print_hand(FILE *f, const card_t hand[5])
{
    char buf[8];

    if (cardart_enabled(f)) {
        cardart_hand(f, hand, NULL, 5);
        /* position labels centred under each card, for hold input */
        for (int i = 0; i < 5; i++)
            fprintf(f, "%s%*s%d%*s", i ? " " : "",
                    CARDART_WIDTH / 2, "", i + 1, CARDART_WIDTH / 2, "");
        fputc('\n', f);
        return;
    }
    for (int i = 0; i < 5; i++) {
        card_name(hand[i], buf, sizeof buf);
        fprintf(f, "%d: %s\n", i + 1, buf);
    }
}

static void hand_json(FILE *f, const card_t hand[5])
{
    char buf[8];
    fputc('[', f);
    for (int i = 0; i < 5; i++) {
        if (i)
            fputc(',', f);
        card_name(hand[i], buf, sizeof buf);
        json_string(f, buf);
    }
    fputc(']', f);
}

/* Ask for holds on disp/stdin.  EOF keeps the dealt hand (hold all). */
static void interactive_holds(FILE *disp, bool held[5])
{
    for (;;) {
        fprintf(disp, "\nHold cards (e.g. 1,3 | none | all):\n> ");
        fflush(disp);
        char line[64];
        if (!fgets(line, sizeof line, stdin)) {
            fprintf(disp, "\n");
            for (int i = 0; i < 5; i++)
                held[i] = true;
            return;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (parse_hold_line(line, held) == 0)
            return;
        fprintf(disp, "invalid holds (positions 1-5, no duplicates)\n");
    }
}

int videopoker_run(const cli_t *cli, rng_t *rng)
{
    bool scripted = false, fixed_deal = false;
    bool held[5] = { false };
    card_t fixed[5];

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];
        if (bet_is(b, "hold")) {
            if (scripted) {
                fprintf(stderr, "videopoker: multiple hold arguments\n");
                return 2;
            }
            if (b->nvalues > 0) {
                if (holds_from_values(b->values, b->nvalues, held) < 0) {
                    fprintf(stderr, "videopoker: '%s': hold positions "
                                    "must be 1-5 with no duplicates\n",
                            b->raw);
                    return 2;
                }
            } else if (strcmp(b->vraw, "none") == 0) {
                ;                   /* nothing held */
            } else if (strcmp(b->vraw, "all") == 0) {
                for (int j = 0; j < 5; j++)
                    held[j] = true;
            } else {
                fprintf(stderr, "videopoker: '%s': expected hold:1-5 "
                                "positions, hold:none or hold:all\n",
                        b->raw);
                return 2;
            }
            scripted = true;
        } else if (bet_is(b, "deal")) {
            if (fixed_deal || parse_deal(b->vraw, fixed) < 0) {
                fprintf(stderr, "videopoker: '%s': expected 5 distinct "
                                "cards like deal:ah,10c,7d,kh,2s\n",
                        b->raw);
                return 2;
            }
            fixed_deal = true;
        } else {
            fprintf(stderr, "videopoker: unknown argument '%s' "
                            "(valid: hold:..., deal:...)\n", b->raw);
            return 2;
        }
    }
    if (fixed_deal && scripted) {
        fprintf(stderr, "videopoker: deal:... evaluates a fixed hand; "
                        "hold cannot be combined with it\n");
        return 2;
    }

    bool interactive = !scripted && !fixed_deal;
    if (interactive && cli->stats) {
        fprintf(stderr, "videopoker: simulation (--runs/--stats) needs a "
                        "hold strategy, e.g. hold:none or hold:1,3\n");
        return 2;
    }
    bool machine = cli->quiet || cli->json || cli->stats;
    FILE *disp = machine ? stderr : stdout;
    bool display = !machine && cli->iterations == 1;

    long counts[VP_NCATS] = { 0 };

    for (long it = 0; it < cli->iterations; it++) {
        card_t initial[5], final5[5];
        shoe_t shoe;

        if (fixed_deal) {
            memcpy(initial, fixed, sizeof initial);
            memcpy(final5, fixed, sizeof final5);
            for (int i = 0; i < 5; i++)
                held[i] = true;
        } else {
            shoe_init(&shoe, 1);
            shoe_shuffle(&shoe, rng);
            for (int i = 0; i < 5; i++)
                initial[i] = shoe_draw(&shoe);
            memcpy(final5, initial, sizeof final5);
        }

        if (display || interactive)
            print_hand(display ? stdout : disp, initial);

        if (interactive)
            interactive_holds(disp, held);

        if (!fixed_deal) {
            bool drew = false;
            for (int i = 0; i < 5; i++) {
                if (!held[i]) {
                    final5[i] = shoe_draw(&shoe);
                    drew = true;
                }
            }
            if (display || interactive) {
                FILE *f = display ? stdout : disp;
                fprintf(f, "\nHeld:");
                bool any = false;
                for (int i = 0; i < 5; i++)
                    if (held[i]) {
                        fprintf(f, "%s %d", any ? "," : "", i + 1);
                        any = true;
                    }
                fprintf(f, "%s\n", any ? "" : " none");
                if (drew) {
                    fprintf(f, "\nDraw:\n");
                    print_hand(f, final5);
                }
            }
        }

        poker_eval_t ev;
        vp_cat_t cat = vp_classify(final5, &ev);
        counts[cat]++;

        if (cli->stats)
            continue;

        if (cli->json) {
            printf("{\"game\":\"videopoker\","
                   "\"variant\":\"jacks_or_better\",\"initial_hand\":");
            hand_json(stdout, initial);
            printf(",\"held\":[");
            bool any = false;
            for (int i = 0; i < 5; i++)
                if (held[i]) {
                    printf("%s%d", any ? "," : "", i + 1);
                    any = true;
                }
            printf("],\"final_hand\":");
            hand_json(stdout, final5);
            printf(",\"category\":");
            json_string(stdout, VP_TOKEN[cat]);
            printf(",\"payout\":%d}\n", VP_PAYOUT[cat]);
        } else if (cli->quiet) {
            puts(VP_TOKEN[cat]);
        } else if (cli->iterations > 1) {
            char buf[8];
            for (int i = 0; i < 5; i++) {
                card_name(final5[i], buf, sizeof buf);
                printf("%s ", buf);
            }
            printf(" %s\n", VP_TOKEN[cat]);
        } else {
            char desc[32];
            printf("\nFinal hand: %s\nResult: %s\nPayout: %d\n",
                   hand_desc(&ev, desc, sizeof desc), VP_TOKEN[cat],
                   VP_PAYOUT[cat]);
        }
    }

    if (cli->stats) {
        long won = 0;
        for (int i = 0; i < VP_NCATS; i++)
            won += (long)VP_PAYOUT[i] * counts[i];
        if (cli->json) {
            printf("{\"game\":\"videopoker\",\"iterations\":%ld,"
                   "\"categories\":{", cli->iterations);
            for (int i = VP_NCATS - 1; i >= 0; i--) {
                json_string(stdout, VP_JSON[i]);
                printf(":%ld%s", counts[i], i ? "," : "");
            }
            printf("},\"return\":%.6f}\n",
                   (double)won / (double)cli->iterations);
        } else if (cli->quiet) {
            printf("runs=%ld", cli->iterations);
            for (int i = VP_NCATS - 1; i >= 0; i--)
                printf(" %s=%ld", VP_JSON[i], counts[i]);
            printf(" return=%.4f\n",
                   (double)won / (double)cli->iterations);
        } else {
            printf("Iterations: %ld\n", cli->iterations);
            printf("%-16s %8s %9s\n", "CATEGORY", "COUNT", "RATE%");
            for (int i = VP_NCATS - 1; i >= 0; i--)
                printf("%-16s %8ld %9.4f\n", VP_TOKEN[i], counts[i],
                       100.0 * (double)counts[i] /
                           (double)cli->iterations);
            printf("Return: %.4f units/hand\n",
                   (double)won / (double)cli->iterations);
        }
    }
    return 0;
}

void videopoker_list_bets(void)
{
    puts("videopoker (Jacks or Better, single hand):");
    puts("  videopoker                 interactive: choose holds at the "
         "prompt");
    puts("  videopoker hold:1,3        scripted: hold positions 1-5, draw "
         "the rest");
    puts("  videopoker hold:none       redraw all five cards");
    puts("  videopoker hold:all        keep the dealt hand");
    puts("  videopoker deal:ah,kh,qh,jh,10h   evaluate a fixed hand "
         "(no draw)");
    puts("payouts (informational, 1-unit bet):");
    puts("  ROYAL_FLUSH 250   STRAIGHT_FLUSH 50   FOUR_OF_A_KIND 25");
    puts("  FULL_HOUSE 9      FLUSH 6             STRAIGHT 4");
    puts("  THREE_OF_A_KIND 3 TWO_PAIR 2          JACKS_OR_BETTER 1");
    puts("  LOW_PAIR 0        HIGH_CARD 0");
    puts("notes: a pair of J/Q/K/A qualifies; ace plays high or low in "
         "straights");
}
