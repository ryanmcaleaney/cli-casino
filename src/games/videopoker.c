#define _DEFAULT_SOURCE

#include "videopoker.h"

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cardart.h"
#include "cards.h"
#include "output.h"
#include "poker.h"
#include "vpseed.h"
#include "vpsolve.h"

#ifdef CASINO_GUI
#include "gui/videopoker_gui.h"
#endif

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

/* --- frontend interface (see videopoker.h) ----------------------------- */

int vp_front_category(const card_t hand[5])
{
    return (int)vp_classify(hand, NULL);
}

int vp_front_payout(int cat)
{
    return (cat >= 0 && cat < VP_NCATS) ? VP_PAYOUT[cat] : 0;
}

const char *vp_front_token(int cat)
{
    return (cat >= 0 && cat < VP_NCATS) ? VP_TOKEN[cat] : "?";
}

const char *vp_front_name(int cat)
{
    return (cat >= 0 && cat < VP_NCATS) ? VP_JSON[cat] : "?";
}

/* Short spellings, only where they name exactly one category: "pair" is
 * deliberately absent, it could be either half of the pay ladder. */
static const struct { const char *word; vp_cat_t cat; } VP_ALIAS[] = {
    { "royal", VP_ROYAL_FLUSH },
    { "quads", VP_FOUR_OF_A_KIND },
    { "trips", VP_THREE_OF_A_KIND },
};
#define VP_NALIAS ((int)(sizeof VP_ALIAS / sizeof VP_ALIAS[0]))

int vp_front_parse_category(const char *s)
{
    /* VP_TOKEN is VP_JSON in capitals, so one case-insensitive pass takes
     * both spellings */
    for (int i = 0; i < VP_NCATS; i++)
        if (strcasecmp(s, VP_JSON[i]) == 0)
            return i;
    for (int i = 0; i < VP_NALIAS; i++)
        if (strcasecmp(s, VP_ALIAS[i].word) == 0)
            return (int)VP_ALIAS[i].cat;
    return -1;
}

void vp_front_deal(rng_t *rng, shoe_t *shoe, card_t hand[5])
{
    shoe_init(shoe, 1);
    shoe_shuffle(shoe, rng);
    for (int i = 0; i < 5; i++)
        hand[i] = shoe_draw(shoe);
}

bool vp_front_draw(shoe_t *shoe, uint32_t hold, card_t hand[5])
{
    bool drew = false;

    for (int i = 0; i < 5; i++) {
        if (!(hold & (1u << i))) {
            hand[i] = shoe_draw(shoe);
            drew = true;
        }
    }
    return drew;
}

void vp_front_describe(const card_t hand[5], char *buf, size_t len)
{
    poker_eval_t ev;
    char tmp[32];
    vp_classify(hand, &ev);
    snprintf(buf, len, "%s", hand_desc(&ev, tmp, sizeof tmp));
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

/* Ask for holds on disp/stdin.  Returns false on EOF. */
static bool interactive_holds(FILE *disp, bool held[5])
{
    for (;;) {
        fprintf(disp, "\nHold cards (e.g. 1,3 | none | all):\n> ");
        fflush(disp);
        char line[64];
        if (!fgets(line, sizeof line, stdin)) {
            fprintf(disp, "\n");
            return false;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (parse_hold_line(line, held) == 0)
            return true;
        fprintf(disp, "invalid holds (positions 1-5, no duplicates)\n");
    }
}

/* ---- strategy solver glue --------------------------------------------- */

/* Pay table hook handed to the generic solver: this is the game's
 * currently selected variant (Jacks or Better). */
static int vp_pay(const card_t hand[5])
{
    return VP_PAYOUT[vp_classify(hand, NULL)];
}

/* --- strategy interface for frontends (see videopoker.h) --------------- */

void vp_front_solve(const card_t hand[5], vp_strategy_t *out)
{
    vp_solve(hand, vp_pay, out->evs);
    out->best = vp_solve_best(out->evs);
}

bool vp_front_hold_optimal(const vp_strategy_t *s, uint32_t mask)
{
    return mask < VP_NMASKS &&
           vp_ev_equal(&s->evs[mask], &s->evs[s->best]);
}

double vp_front_hold_ev(const vp_strategy_t *s, uint32_t mask)
{
    return mask < VP_NMASKS ? vp_ev(&s->evs[mask]) : 0.0;
}

double vp_front_best_ev(const vp_strategy_t *s)
{
    return vp_ev(&s->evs[s->best]);
}

uint32_t vp_front_best_mask(const vp_strategy_t *s)
{
    return (uint32_t)s->best;
}

static const char *mask_positions(uint32_t mask, char *buf, size_t len)
{
    size_t off = 0;
    buf[0] = '\0';
    for (int i = 0; i < 5; i++)
        if (mask & (1u << i))
            off += (size_t)snprintf(buf + off, len - off, "%s%d",
                                    off ? "," : "", i + 1);
    return off ? buf : "none";
}

static const char *mask_cards(const card_t hand[5], uint32_t mask,
                              char *buf, size_t len)
{
    char cb[8];
    size_t off = 0;
    buf[0] = '\0';
    for (int i = 0; i < 5; i++) {
        if (mask & (1u << i)) {
            card_name(hand[i], cb, sizeof cb);
            off += (size_t)snprintf(buf + off, len - off, "%s%s",
                                    off ? " " : "", cb);
        }
    }
    return off ? buf : "none";
}

/* solve:... hook: print the EV of all 32 hold masks plus the optimum. */
static int solve_print(const card_t hand[5])
{
    vp_hold_ev_t evs[VP_NMASKS];
    char pos[16];

    vp_solve(hand, vp_pay, evs);
    int best = vp_solve_best(evs);

    for (int m = 0; m < VP_NMASKS; m++)
        printf("hold=%s draws=%ld ev=%.4f%s\n",
               mask_positions((uint32_t)m, pos, sizeof pos),
               evs[m].draws, vp_ev(&evs[m]),
               vp_ev_equal(&evs[m], &evs[best]) ? " *" : "");
    printf("optimal: hold=%s ev=%.4f\n",
           mask_positions((uint32_t)best, pos, sizeof pos),
           vp_ev(&evs[best]));
    return 0;
}

/* ---- seed search (--find-seed) ----------------------------------------- */

/* Inclusive range default: seeds 0 through 10,000,000. */
#define VP_SEED_END_DEFAULT 10000000ull
#define VP_SEED_PROGRESS    1000000ull

static void seed_progress(uint64_t checked, void *ctx)
{
    (void)ctx;
    fprintf(stderr, "checked %llu seeds...\n", (unsigned long long)checked);
}

/* Five bits, card 1 leftmost: "11110" keeps positions 1-4. */
static const char *mask_bits(uint32_t mask, char buf[6])
{
    for (int i = 0; i < 5; i++)
        buf[i] = (mask & (1u << i)) ? '1' : '0';
    buf[5] = '\0';
    return buf;
}

static void seed_cards(FILE *f, const card_t hand[5], uint32_t mask)
{
    char cb[8];
    bool any = false;

    for (int i = 0; i < 5; i++) {
        if (mask & (1u << i)) {
            card_name(hand[i], cb, sizeof cb);
            fprintf(f, "%s%s", any ? " " : "", cb);
            any = true;
        }
    }
    fprintf(f, "%s\n", any ? "" : "none");
}

static int seed_bad_category(const char *text)
{
    fprintf(stderr, "videopoker: '%s': unknown hand category\n", text);
    fprintf(stderr, "categories:");
    for (int i = VP_NCATS - 1; i >= 0; i--)
        fprintf(stderr, " %s", VP_JSON[i]);
    fprintf(stderr, "\naliases: royal quads trips\n");
    return 2;
}

static int seed_search(const cli_t *cli)
{
    vp_seed_hit_t hit;
    vp_seed_report_t rep = { VP_SEED_PROGRESS, seed_progress, NULL };
    char bits[6], pos[16];

    if (!cli->find_seed) {
        const char *opt = cli->after_draw ? "--after-draw"
                        : cli->seed_start_set ? "--seed-start" : "--seed-end";
        fprintf(stderr, "videopoker: %s only applies to a seed search; "
                        "add --find-seed CATEGORY\n", opt);
        return 2;
    }
    if (cli->gui || cli->trainer || cli->optimal) {
        fprintf(stderr, "videopoker: --find-seed is a non-interactive "
                        "search (no --gui, --trainer or --optimal)\n");
        return 2;
    }
    if (cli->stats || cli->iterations != 1) {
        fprintf(stderr, "videopoker: --find-seed searches seeds, it does "
                        "not play rounds (no --runs, --iterations or "
                        "--stats)\n");
        return 2;
    }
    if (cli->nbets != 0) {
        fprintf(stderr, "videopoker: --find-seed takes no other arguments "
                        "(got '%s')\n", cli->bets[0].raw);
        return 2;
    }

    int target = vp_front_parse_category(cli->find_seed);
    if (target < 0)
        return seed_bad_category(cli->find_seed);

    uint64_t start = cli->seed_start;
    uint64_t end   = cli->seed_end_set ? cli->seed_end : VP_SEED_END_DEFAULT;
    if (start > end) {
        fprintf(stderr, "videopoker: --seed-start %llu is above --seed-end "
                        "%llu (the range is inclusive)\n",
                (unsigned long long)start, (unsigned long long)end);
        return 2;
    }

    bool machine = cli->quiet || cli->json;
    bool found = vp_seed_find(target, cli->after_draw, start, end,
                              machine ? NULL : &rep, &hit);
    const char *mode_word = cli->after_draw ? "after_draw" : "initial_deal";

    if (!found) {
        if (cli->json) {
            printf("{\"game\":\"videopoker\",\"search\":\"seed\","
                   "\"target\":");
            json_string(stdout, VP_TOKEN[target]);
            printf(",\"mode\":\"%s\",\"seed_start\":%llu,\"seed_end\":%llu,"
                   "\"found\":false}\n", mode_word,
                   (unsigned long long)start, (unsigned long long)end);
        } else if (cli->quiet) {
            printf("found=0 category=%s mode=%s seed_start=%llu "
                   "seed_end=%llu\n", VP_TOKEN[target], mode_word,
                   (unsigned long long)start, (unsigned long long)end);
        } else {
            printf("No %s seed found from %llu through %llu.\n",
                   VP_TOKEN[target], (unsigned long long)start,
                   (unsigned long long)end);
        }
        return 1;
    }

    if (cli->json) {
        printf("{\"game\":\"videopoker\",\"search\":\"seed\",\"target\":");
        json_string(stdout, VP_TOKEN[target]);
        printf(",\"mode\":\"%s\",\"seed\":%llu,\"initial\":", mode_word,
               (unsigned long long)hit.seed);
        hand_json(stdout, hit.initial);
        printf(",\"hold_mask\":%u,\"final\":", hit.hold);
        hand_json(stdout, hit.final5);
        printf(",\"result\":");
        json_string(stdout, VP_TOKEN[hit.cat]);
        printf(",\"found\":true}\n");
    } else if (cli->quiet) {
        printf("seed=%llu category=%s mode=%s\n",
               (unsigned long long)hit.seed, VP_TOKEN[hit.cat], mode_word);
    } else {
        printf("Target: %s\n", VP_TOKEN[target]);
        printf("Mode: %s\n", cli->after_draw ? "optimal draw"
                                             : "initial deal");
        printf("Seed: %llu\n", (unsigned long long)hit.seed);
        if (!cli->after_draw) {
            printf("Hand: ");
            seed_cards(stdout, hit.initial, 0x1fu);
        } else {
            printf("Initial: ");
            seed_cards(stdout, hit.initial, 0x1fu);
            /* bit per card, position 1 leftmost - the same orientation
             * as hold:1,2 and as the JSON hold_mask value */
            printf("Hold mask: %s (positions %s)\n",
                   mask_bits(hit.hold, bits),
                   mask_positions(hit.hold, pos, sizeof pos));
            printf("Held: ");
            seed_cards(stdout, hit.initial, hit.hold);
            printf("Final: ");
            seed_cards(stdout, hit.final5, 0x1fu);
            printf("Result: %s\n", VP_TOKEN[hit.cat]);
        }
    }
    return 0;
}

/* ---- interactive strategy trainer -------------------------------------- */

static volatile sig_atomic_t trainer_stop = 0;

static void trainer_sigint(int sig)
{
    (void)sig;
    trainer_stop = 1;
}

static int trainer_run(const cli_t *cli, rng_t *rng)
{
    (void)cli;
    long hands = 0, optimal = 0;
    double ev_lost_total = 0.0;

    /* no SA_RESTART: Ctrl+C makes the pending fgets return NULL so the
     * session ends cleanly with its statistics */
    struct sigaction sa = { 0 };
    sa.sa_handler = trainer_sigint;
    sigaction(SIGINT, &sa, NULL);

    puts("VIDEO POKER TRAINER (Jacks or Better)");
    puts("Enter your hold, see the solver's optimal play. "
         "Ctrl+C or EOF ends the session.");

    while (!trainer_stop) {
        card_t hand[5];
        shoe_t shoe;
        bool held[5] = { false };

        vp_front_deal(rng, &shoe, hand);

        printf("\n");
        print_hand(stdout, hand);

        if (!interactive_holds(stdout, held) || trainer_stop)
            break;

        uint32_t user = 0;
        for (int i = 0; i < 5; i++)
            if (held[i])
                user |= 1u << i;

        vp_hold_ev_t evs[VP_NMASKS];
        vp_solve(hand, vp_pay, evs);
        int best = vp_solve_best(evs);
        bool ok = vp_ev_equal(&evs[user], &evs[best]);
        double lost = vp_ev(&evs[best]) - vp_ev(&evs[user]);

        hands++;
        optimal += ok;
        ev_lost_total += lost;

        char yours[32], opt[32];
        printf("\n%s\n", ok ? "OPTIMAL" : "SUBOPTIMAL");
        printf("Your hold:    %s\n",
               mask_cards(hand, user, yours, sizeof yours));
        printf("Optimal hold: %s\n",
               mask_cards(hand, (uint32_t)best, opt, sizeof opt));
        printf("Your EV:      %.4f\n", vp_ev(&evs[user]));
        printf("Optimal EV:   %.4f\n", vp_ev(&evs[best]));
        printf("EV lost:      %.4f\n", lost);

        printf("\nPress Enter for next hand...");
        fflush(stdout);
        char line[16];
        if (!fgets(line, sizeof line, stdin))
            break;
    }

    printf("\nTRAINER SESSION\n");
    printf("Hands:             %ld\n", hands);
    printf("Optimal decisions: %ld\n", optimal);
    if (hands > 0) {
        printf("Accuracy:          %.1f%%\n",
               100.0 * (double)optimal / (double)hands);
        printf("Total EV lost:     %.4f\n", ev_lost_total);
        printf("Average EV lost:   %.4f\n",
               ev_lost_total / (double)hands);
    }
    return 0;
}

int videopoker_run(const cli_t *cli, rng_t *rng)
{
    bool scripted = false, fixed_deal = false, solve_mode = false;
    bool held[5] = { false };
    card_t fixed[5];

    /* the search runs its own seeds and plays no round here, so it is
     * settled before any of the normal modes are considered */
    if (cli->find_seed || cli->after_draw ||
        cli->seed_start_set || cli->seed_end_set)
        return seed_search(cli);

    if (cli->gui) {
        if (cli->nbets != 0 || cli->quiet || cli->json || cli->stats ||
            cli->trainer || cli->iterations != 1) {
            fprintf(stderr, "videopoker: --gui takes no other arguments "
                            "(only --optimal and --seed)\n");
            return 2;
        }
#ifdef CASINO_GUI
        return vp_gui_run(rng, cli->optimal);
#else
        fprintf(stderr, "videopoker: this build has no GUI support "
                        "(install raylib and run make again)\n");
        return 2;
#endif
    }

    if (cli->optimal) {
        fprintf(stderr, "videopoker: --optimal is a GUI training mode; "
                        "use --gui --optimal (or --trainer in the "
                        "terminal)\n");
        return 2;
    }

    if (cli->trainer) {
        if (cli->nbets != 0 || cli->quiet || cli->json || cli->stats ||
            cli->iterations != 1) {
            fprintf(stderr, "videopoker: --trainer is interactive only "
                            "(no bets, --quiet, --json, --stats or "
                            "--iterations)\n");
            return 2;
        }
        return trainer_run(cli, rng);
    }

    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];
        if (bet_is(b, "solve")) {
            if (solve_mode || cli->nbets != 1 ||
                parse_deal(b->vraw, fixed) < 0) {
                fprintf(stderr, "videopoker: '%s': expected 5 distinct "
                                "cards like solve:ah,10c,7d,kh,2s "
                                "(must be the only argument)\n", b->raw);
                return 2;
            }
            solve_mode = true;
        } else if (bet_is(b, "hold")) {
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
    if (solve_mode)
        return solve_print(fixed);

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
            vp_front_deal(rng, &shoe, initial);
            memcpy(final5, initial, sizeof final5);
        }

        if (display || interactive)
            print_hand(display ? stdout : disp, initial);

        if (interactive && !interactive_holds(disp, held)) {
            /* EOF: keep the dealt hand */
            for (int i = 0; i < 5; i++)
                held[i] = true;
        }

        if (!fixed_deal) {
            uint32_t hold = 0;
            for (int i = 0; i < 5; i++)
                if (held[i])
                    hold |= 1u << i;
            bool drew = vp_front_draw(&shoe, hold, final5);
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
    puts("  videopoker solve:as,7d,kh,2c,5s   EV of all 32 holds for a "
         "hand");
    puts("  videopoker --trainer       interactive strategy trainer "
         "(optimal-hold EV)");
    puts("  videopoker --gui           graphical video poker machine");
    puts("  videopoker --gui --optimal graphical trainer: live "
         "optimal/sub-optimal feedback");
    puts("seed search (--find-seed CATEGORY): brute-force deterministic "
         "seeds");
    puts("  videopoker --find-seed royal_flush");
    puts("  videopoker --find-seed four_of_a_kind --seed-end 5000000");
    puts("  videopoker --find-seed royal_flush --after-draw");
    puts("  videopoker --find-seed full_house --seed-start 100000 "
         "--seed-end 200000");
    puts("  categories: the names above in lower case "
         "(royal_flush ... high_card),");
    puts("              plus the aliases royal, quads and trips");
    puts("  the search looks at the initial five-card deal by default;");
    puts("  --after-draw instead judges the hand left after the built-in "
         "optimal");
    puts("  solver picks the hold and the replacements are drawn, which "
         "costs a");
    puts("  full 32-mask solve per seed and is therefore far slower");
    puts("  the first (lowest) matching seed in the range is reported; "
         "the range");
    puts("  --seed-start N .. --seed-end N includes both ends "
         "(default 0..10000000)");
    puts("  the hold is printed as five bits with position 1 leftmost, "
         "next to the");
    puts("  positions themselves; JSON reports the same mask as a number "
         "(bit i set");
    puts("  = position i+1 kept), which is the orientation hold:1,3 uses");
    puts("  a found seed replays with 'videopoker --seed N' (add the same "
         "hold:...");
    puts("  for an --after-draw seed); exit 0 = found, 1 = no match in "
         "the range");
    puts("  results are deterministic for this RNG and shuffle: changing "
         "either");
    puts("  changes which seeds produce which hands");
    puts("payouts (informational, 1-unit bet):");
    puts("  ROYAL_FLUSH 250   STRAIGHT_FLUSH 50   FOUR_OF_A_KIND 25");
    puts("  FULL_HOUSE 9      FLUSH 6             STRAIGHT 4");
    puts("  THREE_OF_A_KIND 3 TWO_PAIR 2          JACKS_OR_BETTER 1");
    puts("  LOW_PAIR 0        HIGH_CARD 0");
    puts("notes: a pair of J/Q/K/A qualifies; ace plays high or low in "
         "straights");
}
