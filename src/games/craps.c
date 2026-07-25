#include "craps.h"

#include <stdio.h>
#include <stdlib.h>

#include "dice.h"
#include "output.h"

/* A round essentially never runs this long (P < 1e-30); backstop only. */
#define CRAPS_MAX_ROLLS 256

typedef enum { BET_PASS, BET_DONT, BET_FIELD, BET_HARD } craps_kind_t;
typedef enum { RES_PENDING, RES_WIN, RES_LOSS, RES_PUSH } craps_res_t;

static const char *const RES_WORD[] = { "active", "WIN", "LOSS", "PUSH" };
static const char *const RES_JSON[] = { "pending", "win", "loss", "push" };
static const char RES_LETTER[] = { '?', 'W', 'L', 'P' };

typedef struct {
    craps_kind_t kind;
    int          hard;          /* 4/6/8/10 when kind == BET_HARD */
    const char  *raw;           /* original token, for display */
    craps_res_t  res;
    const char  *payout;        /* informational only, set on WIN */
} craps_bet_t;

typedef enum { ST_COME_OUT, ST_POINT_ON, ST_RESOLVED } craps_state_t;

typedef struct {
    int rolls[CRAPS_MAX_ROLLS][2];
    int nrolls;
    int comeout;                /* total of the come-out roll */
    int point;                  /* 0 = never established */
} craps_round_t;

static int parse_bets(const cli_t *cli, craps_bet_t *bets, int *nbets)
{
    if (cli->nbets == 0) {
        fprintf(stderr, "craps: choose at least one bet "
                        "(valid: pass, dont-pass, field, hard:N)\n");
        return 2;
    }
    for (int i = 0; i < cli->nbets; i++) {
        const bet_t *b = &cli->bets[i];
        craps_bet_t *cb = &bets[i];
        cb->raw = b->raw;
        cb->hard = 0;
        if (bet_is(b, "pass"))
            cb->kind = BET_PASS;
        else if (bet_is(b, "dont-pass") || bet_is(b, "dontpass"))
            cb->kind = BET_DONT;
        else if (bet_is(b, "field"))
            cb->kind = BET_FIELD;
        else if (bet_is(b, "hard"))
            cb->kind = BET_HARD;
        else {
            fprintf(stderr, "craps: unknown bet '%s' "
                            "(valid: pass, dont-pass, field, hard:N)\n",
                    b->raw);
            return 2;
        }
        if (cb->kind == BET_HARD) {
            if (b->nvalues != 1 ||
                (b->values[0] != 4 && b->values[0] != 6 &&
                 b->values[0] != 8 && b->values[0] != 10)) {
                fprintf(stderr, "craps: bet '%s': hardway must be hard:4, "
                                "hard:6, hard:8 or hard:10\n", b->raw);
                return 2;
            }
            cb->hard = b->values[0];
        } else if (bet_has_value(b)) {
            fprintf(stderr, "craps: bet '%s' takes no value\n", b->raw);
            return 2;
        }
    }
    *nbets = cli->nbets;
    return 0;
}

/* Field: one-roll bet on 2,3,4,9,10,11,12. */
static bool field_wins(int total)
{
    return total <= 4 || total >= 9;
}

static const char *field_payout(int total)
{
    return total == 2 ? "2:1" : total == 12 ? "3:1" : "1:1";
}

/* Hardways stand on every roll: 7 loses, the number thrown easy loses,
 * the number thrown as a pair wins. */
static void eval_hardways(craps_bet_t *bets, int n, int d0, int d1)
{
    int total = d0 + d1;
    for (int i = 0; i < n; i++) {
        craps_bet_t *b = &bets[i];
        if (b->kind != BET_HARD || b->res != RES_PENDING)
            continue;
        if (total == 7) {
            b->res = RES_LOSS;
        } else if (total == b->hard) {
            b->res = d0 == d1 ? RES_WIN : RES_LOSS;
            if (b->res == RES_WIN)
                b->payout = (b->hard == 4 || b->hard == 10) ? "7:1" : "9:1";
        }
    }
}

static void set_line(craps_bet_t *bets, int n, craps_kind_t kind,
                     craps_res_t res)
{
    for (int i = 0; i < n; i++) {
        if (bets[i].kind == kind && bets[i].res == RES_PENDING) {
            bets[i].res = res;
            if (res == RES_WIN)
                bets[i].payout = "1:1";
        }
    }
}

/* One full pass-line round: come-out, optional point phase, resolution.
 * Bets left unresolved when the line resolves are a PUSH. */
static void play_round(rng_t *rng, craps_bet_t *bets, int nbets,
                       craps_round_t *rd, FILE *disp, bool display)
{
    craps_state_t state = ST_COME_OUT;
    bool block = nbets > 1 ||
                 (bets[0].kind != BET_PASS && bets[0].kind != BET_DONT);

    rd->nrolls = 0;
    rd->comeout = 0;
    rd->point = 0;
    for (int i = 0; i < nbets; i++) {
        bets[i].res = RES_PENDING;
        bets[i].payout = "-";
    }

    while (state != ST_RESOLVED) {
        if (rd->nrolls >= CRAPS_MAX_ROLLS) {
            fprintf(stderr, "craps: roll limit exceeded\n");
            exit(70);
        }
        int d[2];
        int total = dice_roll_many(rng, 2, 6, d);
        rd->rolls[rd->nrolls][0] = d[0];
        rd->rolls[rd->nrolls][1] = d[1];
        rd->nrolls++;

        if (display)
            fprintf(disp, "%s: %d + %d = %d\n",
                    state == ST_COME_OUT ? "Come-out" : "Roll",
                    d[0], d[1], total);

        if (state == ST_COME_OUT) {
            rd->comeout = total;
            for (int i = 0; i < nbets; i++) {
                if (bets[i].kind == BET_FIELD) {
                    bets[i].res = field_wins(total) ? RES_WIN : RES_LOSS;
                    if (bets[i].res == RES_WIN)
                        bets[i].payout = field_payout(total);
                }
            }
            eval_hardways(bets, nbets, d[0], d[1]);

            if (total == 7 || total == 11) {
                set_line(bets, nbets, BET_PASS, RES_WIN);
                set_line(bets, nbets, BET_DONT, RES_LOSS);
                state = ST_RESOLVED;
            } else if (total == 2 || total == 3 || total == 12) {
                set_line(bets, nbets, BET_PASS, RES_LOSS);
                set_line(bets, nbets, BET_DONT,
                         total == 12 ? RES_PUSH : RES_WIN);
                state = ST_RESOLVED;
            } else {
                rd->point = total;
                state = ST_POINT_ON;
                if (display)
                    fprintf(disp, "Point: %d\n", total);
            }
            if (display && block) {
                fprintf(disp, "\n");
                for (int i = 0; i < nbets; i++)
                    fprintf(disp, "%s: %s\n", bets[i].raw,
                            RES_WORD[bets[i].res]);
            }
            if (display && state == ST_POINT_ON)
                fprintf(disp, "\n");
        } else {
            craps_res_t prev[CLI_MAX_BETS];
            for (int i = 0; i < nbets; i++)
                prev[i] = bets[i].res;
            eval_hardways(bets, nbets, d[0], d[1]);
            if (display)
                for (int i = 0; i < nbets; i++)
                    if (prev[i] != bets[i].res)
                        fprintf(disp, "%s: %s\n", bets[i].raw,
                                RES_WORD[bets[i].res]);

            if (total == rd->point) {
                set_line(bets, nbets, BET_PASS, RES_WIN);
                set_line(bets, nbets, BET_DONT, RES_LOSS);
                state = ST_RESOLVED;
            } else if (total == 7) {
                set_line(bets, nbets, BET_PASS, RES_LOSS);
                set_line(bets, nbets, BET_DONT, RES_WIN);
                state = ST_RESOLVED;
            }
        }
    }

    for (int i = 0; i < nbets; i++)
        if (bets[i].res == RES_PENDING)
            bets[i].res = RES_PUSH;
}

int craps_run(const cli_t *cli, rng_t *rng)
{
    craps_bet_t bets[CLI_MAX_BETS];
    int nbets;
    if (parse_bets(cli, bets, &nbets))
        return 2;

    bool machine = cli->quiet || cli->json || cli->stats;
    bool display = !machine && cli->iterations == 1;

    long wins[CLI_MAX_BETS] = { 0 };
    long losses[CLI_MAX_BETS] = { 0 };
    long pushes[CLI_MAX_BETS] = { 0 };
    long total_rolls = 0;

    for (long it = 0; it < cli->iterations; it++) {
        craps_round_t rd;
        play_round(rng, bets, nbets, &rd, stdout, display);
        total_rolls += rd.nrolls;

        for (int i = 0; i < nbets; i++) {
            if (bets[i].res == RES_WIN)
                wins[i]++;
            else if (bets[i].res == RES_LOSS)
                losses[i]++;
            else
                pushes[i]++;
        }

        if (cli->stats)
            continue;

        if (cli->json) {
            printf("{\"game\":\"craps\",\"comeout\":%d,\"point\":",
                   rd.comeout);
            if (rd.point)
                printf("%d", rd.point);
            else
                printf("null");
            printf(",\"rolls\":[");
            for (int r = 0; r < rd.nrolls; r++)
                printf("%s[%d,%d]", r ? "," : "", rd.rolls[r][0],
                       rd.rolls[r][1]);
            printf("],\"bets\":[");
            for (int i = 0; i < nbets; i++) {
                if (i)
                    printf(",");
                printf("{\"bet\":");
                json_string(stdout, bets[i].raw);
                printf(",\"result\":");
                json_string(stdout, RES_JSON[bets[i].res]);
                printf(",\"payout\":");
                json_string(stdout,
                            bets[i].res == RES_WIN ? bets[i].payout : "-");
                printf("}");
            }
            printf("]}\n");
        } else if (cli->quiet) {
            if (nbets == 1) {
                puts(RES_WORD[bets[0].res]);
            } else {
                for (int i = 0; i < nbets; i++)
                    printf("%s%s=%c", i ? " " : "", bets[i].raw,
                           RES_LETTER[bets[i].res]);
                printf("\n");
            }
        } else if (cli->iterations > 1) {
            printf("comeout=%d", rd.comeout);
            if (rd.point)
                printf(" point=%d", rd.point);
            for (int i = 0; i < nbets; i++)
                printf(" %s=%c", bets[i].raw, RES_LETTER[bets[i].res]);
            printf("\n");
        } else {
            printf("\n");
            for (int i = 0; i < nbets; i++)
                if (bets[i].kind == BET_HARD && bets[i].res == RES_PUSH)
                    printf("%s: PUSH\n", bets[i].raw);
            for (int i = 0; i < nbets; i++) {
                if (bets[i].kind == BET_PASS)
                    printf("PASS %s\n", RES_WORD[bets[i].res]);
                else if (bets[i].kind == BET_DONT)
                    printf("DONT-PASS %s\n", RES_WORD[bets[i].res]);
            }
        }
    }

    if (cli->stats) {
        double avg_rolls = (double)total_rolls / (double)cli->iterations;
        if (cli->json) {
            printf("{\"game\":\"craps\",\"iterations\":%ld,"
                   "\"avg_rolls\":%.4f,\"bets\":[",
                   cli->iterations, avg_rolls);
            for (int i = 0; i < nbets; i++) {
                if (i)
                    printf(",");
                printf("{\"bet\":");
                json_string(stdout, bets[i].raw);
                printf(",\"wins\":%ld,\"losses\":%ld,\"pushes\":%ld,"
                       "\"win_rate\":%.6f}", wins[i], losses[i], pushes[i],
                       (double)wins[i] / (double)cli->iterations);
            }
            printf("]}\n");
        } else if (cli->quiet) {
            for (int i = 0; i < nbets; i++)
                printf("bet=%s runs=%ld wins=%ld losses=%ld pushes=%ld "
                       "win_rate=%.4f avg_rolls=%.4f\n", bets[i].raw,
                       cli->iterations, wins[i], losses[i], pushes[i],
                       (double)wins[i] / (double)cli->iterations,
                       avg_rolls);
        } else {
            printf("Iterations: %ld\n", cli->iterations);
            printf("%-12s %8s %8s %8s %9s\n",
                   "BET", "WINS", "LOSSES", "PUSHES", "WIN%");
            for (int i = 0; i < nbets; i++)
                printf("%-12s %8ld %8ld %8ld %9.4f\n", bets[i].raw,
                       wins[i], losses[i], pushes[i],
                       100.0 * (double)wins[i] / (double)cli->iterations);
            printf("Avg rolls/round: %.4f\n", avg_rolls);
        }
    }
    return 0;
}

void craps_list_bets(void)
{
    puts("craps bets (one pass-line round per play):");
    puts("  pass         1:1        pass line");
    puts("  dont-pass    1:1        don't pass (come-out 12 is a push)");
    puts("  field        1:1        one-roll bet, come-out roll only:");
    puts("                          wins on 2,3,4,9,10,11,12 "
         "(2 pays 2:1, 12 pays 3:1)");
    puts("  hard:N       7:1 / 9:1  hardway on N = 4, 6, 8 or 10:");
    puts("                          pair before a 7 or an easy N");
    puts("notes:");
    puts("  field is evaluated on the come-out roll only");
    puts("  hardways are evaluated on every roll of the round");
    puts("  bets still unresolved when the round ends are a PUSH");
    puts("  payouts are informational only; no money is tracked");
}
