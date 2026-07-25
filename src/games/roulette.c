#include "roulette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "output.h"

/* European wheel: single zero, numbers 0..36. */

typedef enum {
    RB_RED, RB_BLACK, RB_ODD, RB_EVEN, RB_LOW, RB_HIGH,
    RB_STRAIGHT, RB_SPLIT, RB_STREET, RB_CORNER, RB_SIXLINE,
    RB_DOZEN, RB_COLUMN
} rbet_kind_t;

typedef struct {
    rbet_kind_t  kind;
    const bet_t *bet;
    int          payout;  /* pays N:1 */
} rbet_t;

static const int RED[] = { 1, 3, 5, 7, 9, 12, 14, 16, 18,
                           19, 21, 23, 25, 27, 30, 32, 34, 36 };

static bool is_red(int n)
{
    for (size_t i = 0; i < sizeof RED / sizeof RED[0]; i++)
        if (RED[i] == n)
            return true;
    return false;
}

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

/* ---- validation -------------------------------------------------- */

static int verr(char *err, size_t len, const char *raw, const char *msg)
{
    snprintf(err, len, "bet '%s': %s", raw, msg);
    return -1;
}

static int validate_simple(const bet_t *b, rbet_t *rb, rbet_kind_t kind,
                           char *err, size_t len)
{
    if (bet_has_value(b))
        return verr(err, len, b->raw, "this bet takes no value");
    rb->kind = kind;
    rb->payout = (kind == RB_DOZEN || kind == RB_COLUMN) ? 2 : 1;
    return 0;
}

static int validate_numbers(const bet_t *b, int want, char *err, size_t len)
{
    char msg[64];
    if (b->nvalues != want) {
        snprintf(msg, sizeof msg, "expects exactly %d number%s",
                 want, want == 1 ? "" : "s");
        return verr(err, len, b->raw, msg);
    }
    for (int i = 0; i < b->nvalues; i++)
        if (b->values[i] < 0 || b->values[i] > 36)
            return verr(err, len, b->raw, "numbers must be 0-36");
    for (int i = 0; i < b->nvalues; i++)
        for (int j = i + 1; j < b->nvalues; j++)
            if (b->values[i] == b->values[j])
                return verr(err, len, b->raw, "duplicate number");
    return 0;
}

/* Two numbers adjacent on the layout (incl. 0-1, 0-2, 0-3). */
static bool split_ok(int a, int b)
{
    if (a > b) { int t = a; a = b; b = t; }
    if (a == 0)
        return b >= 1 && b <= 3;
    if (b == a + 3)
        return true;                       /* vertical  */
    return b == a + 1 && (a - 1) / 3 == (b - 1) / 3; /* horizontal */
}

static int roulette_validate(const bet_t *b, rbet_t *rb,
                             char *err, size_t len)
{
    rb->bet = b;

    if (bet_is(b, "red"))   return validate_simple(b, rb, RB_RED,   err, len);
    if (bet_is(b, "black")) return validate_simple(b, rb, RB_BLACK, err, len);
    if (bet_is(b, "odd"))   return validate_simple(b, rb, RB_ODD,   err, len);
    if (bet_is(b, "even"))  return validate_simple(b, rb, RB_EVEN,  err, len);
    if (bet_is(b, "low"))   return validate_simple(b, rb, RB_LOW,   err, len);
    if (bet_is(b, "high"))  return validate_simple(b, rb, RB_HIGH,  err, len);

    if (bet_is(b, "straight")) {
        if (validate_numbers(b, 1, err, len))
            return -1;
        rb->kind = RB_STRAIGHT;
        rb->payout = 35;
        return 0;
    }
    if (bet_is(b, "split")) {
        if (validate_numbers(b, 2, err, len))
            return -1;
        if (!split_ok(b->values[0], b->values[1]))
            return verr(err, len, b->raw,
                        "numbers are not adjacent on the layout");
        rb->kind = RB_SPLIT;
        rb->payout = 17;
        return 0;
    }
    if (bet_is(b, "street")) {
        if (validate_numbers(b, 3, err, len))
            return -1;
        int v[3];
        memcpy(v, b->values, sizeof v);
        qsort(v, 3, sizeof v[0], cmp_int);
        if (v[0] < 1 || v[0] % 3 != 1 || v[1] != v[0] + 1 ||
            v[2] != v[0] + 2)
            return verr(err, len, b->raw,
                        "must be a full row, e.g. street:16,17,18");
        rb->kind = RB_STREET;
        rb->payout = 11;
        return 0;
    }
    if (bet_is(b, "corner")) {
        if (validate_numbers(b, 4, err, len))
            return -1;
        int v[4];
        memcpy(v, b->values, sizeof v);
        qsort(v, 4, sizeof v[0], cmp_int);
        bool basket = v[0] == 0 && v[1] == 1 && v[2] == 2 && v[3] == 3;
        bool square = v[0] >= 1 && v[0] % 3 != 0 && v[0] + 4 <= 36 &&
                      v[1] == v[0] + 1 && v[2] == v[0] + 3 &&
                      v[3] == v[0] + 4;
        if (!basket && !square)
            return verr(err, len, b->raw,
                        "must be 4 numbers forming a square, "
                        "e.g. corner:16,17,19,20 (or corner:0,1,2,3)");
        rb->kind = RB_CORNER;
        rb->payout = 8;
        return 0;
    }
    if (bet_is(b, "sixline")) {
        if (validate_numbers(b, 6, err, len))
            return -1;
        int v[6];
        memcpy(v, b->values, sizeof v);
        qsort(v, 6, sizeof v[0], cmp_int);
        bool ok = v[0] >= 1 && v[0] % 3 == 1 && v[0] + 5 <= 36;
        for (int i = 1; ok && i < 6; i++)
            ok = v[i] == v[0] + i;
        if (!ok)
            return verr(err, len, b->raw,
                        "must be two adjacent rows, "
                        "e.g. sixline:13,14,15,16,17,18");
        rb->kind = RB_SIXLINE;
        rb->payout = 5;
        return 0;
    }
    if (bet_is(b, "dozen") || bet_is(b, "column")) {
        if (b->nvalues != 1 || b->values[0] < 1 || b->values[0] > 3)
            return verr(err, len, b->raw, "expects a value of 1, 2 or 3");
        rb->kind = bet_is(b, "dozen") ? RB_DOZEN : RB_COLUMN;
        rb->payout = 2;
        return 0;
    }

    snprintf(err, len, "unknown roulette bet '%s' (see --list-bets)",
             b->raw);
    return -1;
}

/* ---- resolution -------------------------------------------------- */

static bool rbet_wins(const rbet_t *rb, int spin)
{
    const bet_t *b = rb->bet;
    switch (rb->kind) {
    case RB_RED:    return spin != 0 && is_red(spin);
    case RB_BLACK:  return spin != 0 && !is_red(spin);
    case RB_ODD:    return spin != 0 && spin % 2 == 1;
    case RB_EVEN:   return spin != 0 && spin % 2 == 0;
    case RB_LOW:    return spin >= 1 && spin <= 18;
    case RB_HIGH:   return spin >= 19 && spin <= 36;
    case RB_DOZEN:  return spin >= 1 && (spin - 1) / 12 == b->values[0] - 1;
    case RB_COLUMN: return spin >= 1 && spin % 3 == b->values[0] % 3;
    case RB_STRAIGHT:
    case RB_SPLIT:
    case RB_STREET:
    case RB_CORNER:
    case RB_SIXLINE:
        for (int i = 0; i < b->nvalues; i++)
            if (b->values[i] == spin)
                return true;
        return false;
    }
    return false;
}

/* ---- output ------------------------------------------------------ */

static const char *spin_color(int n)
{
    return n == 0 ? "GREEN" : is_red(n) ? "RED" : "BLACK";
}

static void spin_json(FILE *f, int spin)
{
    fprintf(f, "{\"number\":%d,\"color\":\"%s\"", spin,
            spin == 0 ? "green" : is_red(spin) ? "red" : "black");
    if (spin != 0)
        fprintf(f, ",\"parity\":\"%s\",\"range\":\"%s\","
                   "\"dozen\":%d,\"column\":%d",
                spin % 2 ? "odd" : "even",
                spin <= 18 ? "low" : "high",
                (spin - 1) / 12 + 1,
                spin % 3 == 0 ? 3 : spin % 3);
    fputc('}', f);
}

/* ---- driver ------------------------------------------------------ */

int roulette_run(const cli_t *cli, rng_t *rng)
{
    rbet_t rbets[CLI_MAX_BETS];
    char err[192];

    for (int i = 0; i < cli->nbets; i++) {
        if (roulette_validate(&cli->bets[i], &rbets[i],
                              err, sizeof err) < 0) {
            fprintf(stderr, "roulette: %s\n", err);
            return 2;
        }
    }

    long wins[CLI_MAX_BETS] = { 0 };
    long net[CLI_MAX_BETS] = { 0 };
    bool per_round = !cli->stats;

    for (long it = 0; it < cli->iterations; it++) {
        int spin = (int)rng_below(rng, 37);

        bet_line_t lines[CLI_MAX_BETS];
        char payouts[CLI_MAX_BETS][12];
        for (int i = 0; i < cli->nbets; i++) {
            bool w = rbet_wins(&rbets[i], spin);
            snprintf(payouts[i], sizeof payouts[i], "%d:1",
                     rbets[i].payout);
            lines[i] = (bet_line_t){ cli->bets[i].raw, w, payouts[i] };
            wins[i] += w;
            net[i] += w ? rbets[i].payout : -1;
        }

        if (!per_round)
            continue;

        if (cli->json) {
            printf("{\"game\":\"roulette\",\"spin\":");
            spin_json(stdout, spin);
            printf(",\"bets\":");
            out_bet_json(stdout, lines, cli->nbets);
            printf("}\n");
        } else if (cli->quiet || cli->iterations > 1) {
            printf("%d %s", spin, spin_color(spin));
            out_bet_quiet(stdout, lines, cli->nbets);
            printf("\n");
        } else {
            printf("Spin: %d %s\n", spin, spin_color(spin));
            out_bet_table(stdout, lines, cli->nbets);
        }
    }

    if (cli->stats) {
        if (cli->json) {
            printf("{\"game\":\"roulette\",\"iterations\":%ld,\"bets\":[",
                   cli->iterations);
            for (int i = 0; i < cli->nbets; i++) {
                if (i)
                    printf(",");
                printf("{\"bet\":");
                json_string(stdout, cli->bets[i].raw);
                printf(",\"payout\":\"%d:1\",\"plays\":%ld,\"wins\":%ld,"
                       "\"hit_rate\":%.6f,\"net_units\":%ld}",
                       rbets[i].payout, cli->iterations, wins[i],
                       (double)wins[i] / (double)cli->iterations, net[i]);
            }
            printf("]}\n");
        } else {
            printf("Iterations: %ld\n", cli->iterations);
            size_t w = strlen("BET");
            for (int i = 0; i < cli->nbets; i++)
                if (strlen(cli->bets[i].raw) > w)
                    w = strlen(cli->bets[i].raw);
            printf("%-*s  %-8s  %-10s  %-8s  %s\n", (int)w, "BET",
                   "PAYOUT", "WINS", "HIT%", "NET(units)");
            for (int i = 0; i < cli->nbets; i++) {
                char pay[12];
                snprintf(pay, sizeof pay, "%d:1", rbets[i].payout);
                printf("%-*s  %-8s  %-10ld  %-8.4f  %+ld\n",
                       (int)w, cli->bets[i].raw, pay, wins[i],
                       100.0 * (double)wins[i] / (double)cli->iterations,
                       net[i]);
            }
        }
    }
    return 0;
}

void roulette_list_bets(void)
{
    puts("roulette (European, single zero) bets:");
    puts("  red | black            even money   1:1");
    puts("  odd | even             even money   1:1");
    puts("  low | high             1-18 / 19-36 1:1");
    puts("  dozen:1..3             12 numbers   2:1");
    puts("  column:1..3            12 numbers   2:1");
    puts("  straight:N             one number   35:1  (N = 0-36)");
    puts("  split:A,B              2 adjacent   17:1  (e.g. split:17,20)");
    puts("  street:A,B,C           full row     11:1  (e.g. street:16,17,18)");
    puts("  corner:A,B,C,D         square       8:1   (e.g. corner:16,17,19,20)");
    puts("  sixline:A,..,F         two rows     5:1   (e.g. sixline:13,14,15,16,17,18)");
}
