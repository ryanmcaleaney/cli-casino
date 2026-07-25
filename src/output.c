#include "output.h"

#include <string.h>

void out_bet_table(FILE *f, const bet_line_t *lines, int n)
{
    if (n == 0)
        return;
    size_t w = strlen("BET");
    for (int i = 0; i < n; i++) {
        size_t l = strlen(lines[i].bet);
        if (l > w)
            w = l;
    }
    fprintf(f, "%-*s  %-6s  %s\n", (int)w, "BET", "RESULT", "PAYOUT");
    for (int i = 0; i < n; i++)
        fprintf(f, "%-*s  %-6s  %s\n", (int)w, lines[i].bet,
                lines[i].win ? "WIN" : "LOSS",
                lines[i].win && lines[i].payout ? lines[i].payout : "-");
}

void out_bet_quiet(FILE *f, const bet_line_t *lines, int n)
{
    for (int i = 0; i < n; i++)
        fprintf(f, " %s=%c", lines[i].bet, lines[i].win ? 'W' : 'L');
}

void out_bet_json(FILE *f, const bet_line_t *lines, int n)
{
    fputc('[', f);
    for (int i = 0; i < n; i++) {
        if (i)
            fputc(',', f);
        fputs("{\"bet\":", f);
        json_string(f, lines[i].bet);
        fprintf(f, ",\"win\":%s,\"payout\":",
                lines[i].win ? "true" : "false");
        if (lines[i].payout)
            json_string(f, lines[i].payout);
        else
            fputs("null", f);
        fputc('}', f);
    }
    fputc(']', f);
}

void json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\n': fputs("\\n", f);  break;
        case '\t': fputs("\\t", f);  break;
        default:
            if (c < 0x20)
                fprintf(f, "\\u%04x", c);
            else
                fputc(c, f);
        }
    }
    fputc('"', f);
}
