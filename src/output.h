#ifndef CASINO_OUTPUT_H
#define CASINO_OUTPUT_H

#include <stdbool.h>
#include <stdio.h>

/*
 * Shared presentation helpers so every game renders bets identically.
 * A payout of NULL means "not applicable / informational only".
 */
typedef struct bet_line {
    const char *bet;      /* raw bet token */
    bool        win;
    const char *payout;   /* e.g. "35:1", or NULL */
} bet_line_t;

/* Normal mode: aligned BET / RESULT / PAYOUT table. */
void out_bet_table(FILE *f, const bet_line_t *lines, int n);

/* Quiet mode: " name=W" / " name=L" fragments on the current line. */
void out_bet_quiet(FILE *f, const bet_line_t *lines, int n);

/* JSON: emits [{"bet":...,"win":...,"payout":...},...] (no newline). */
void out_bet_json(FILE *f, const bet_line_t *lines, int n);

/* Escape and print a JSON string, including surrounding quotes. */
void json_string(FILE *f, const char *s);

#endif
