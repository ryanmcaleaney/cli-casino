#include "cli.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fail(char *err, size_t errlen, const char *fmt, const char *a)
{
    snprintf(err, errlen, fmt, a);
    return -1;
}

static int parse_bet_token(const char *tok, bet_t *b, char *err, size_t errlen)
{
    if (strlen(tok) >= sizeof b->raw)
        return fail(err, errlen, "bet '%s': token too long", tok);
    snprintf(b->raw, sizeof b->raw, "%s", tok);
    b->nvalues = 0;
    b->vraw[0] = '\0';

    const char *colon = strchr(tok, ':');
    size_t namelen = colon ? (size_t)(colon - tok) : strlen(tok);
    if (namelen == 0)
        return fail(err, errlen, "bet '%s': empty bet name", tok);
    if (namelen >= sizeof b->name)
        return fail(err, errlen, "bet '%s': name too long", tok);
    for (size_t i = 0; i < namelen; i++) {
        unsigned char c = (unsigned char)tok[i];
        if (!isalnum(c) && c != ',' && c != '-')
            return fail(err, errlen,
                        "bet '%s': invalid character in bet name", tok);
        b->name[i] = (char)tolower(c);
    }
    b->name[namelen] = '\0';

    if (!colon)
        return 0;

    const char *p = colon + 1;
    if (*p == '\0')
        return fail(err, errlen, "bet '%s': missing value after ':'", tok);
    if (strlen(p) >= sizeof b->vraw)
        return fail(err, errlen, "bet '%s': value too long", tok);
    for (size_t i = 0; p[i]; i++)
        b->vraw[i] = (char)tolower((unsigned char)p[i]);
    b->vraw[strlen(p)] = '\0';

    /* Try a strict integer list; anything else stays word-form in vraw
     * (e.g. "hold:none") for the game to interpret or reject. */
    int vals[BET_MAX_VALUES];
    int nv = 0;
    while (*p) {
        if (nv >= BET_MAX_VALUES)
            return 0;
        char *end;
        errno = 0;
        long v = strtol(p, &end, 10);
        if (end == p || errno != 0 || v < INT_MIN || v > INT_MAX)
            return 0;
        vals[nv++] = (int)v;
        p = end;
        if (*p == ',') {
            p++;
            if (*p == '\0')
                return 0;
        } else if (*p != '\0') {
            return 0;
        }
    }
    for (int i = 0; i < nv; i++)
        b->values[i] = vals[i];
    b->nvalues = nv;
    return 0;
}

static int opt_value(int argc, char **argv, int *i, const char *opt,
                     const char **out, char *err, size_t errlen)
{
    const char *arg = argv[*i];
    size_t optlen = strlen(opt);
    if (arg[optlen] == '=') {
        *out = arg + optlen + 1;
        return 0;
    }
    if (*i + 1 >= argc)
        return fail(err, errlen, "%s requires a value", opt);
    *out = argv[++*i];
    return 0;
}

int cli_parse(int argc, char **argv, cli_t *out, char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    out->iterations = 1;

    for (int i = 0; i < argc; i++) {
        const char *arg = argv[i];

        if (arg[0] == '-' && arg[1] == '-') {
            const char *val;
            if (strcmp(arg, "--help") == 0) {
                out->help = true;
            } else if (strcmp(arg, "--list-bets") == 0) {
                out->list_bets = true;
            } else if (strcmp(arg, "--quiet") == 0) {
                out->quiet = true;
            } else if (strcmp(arg, "--json") == 0) {
                out->json = true;
            } else if (strcmp(arg, "--stats") == 0) {
                out->stats = true;
            } else if (strcmp(arg, "--trainer") == 0) {
                out->trainer = true;
            } else if (strcmp(arg, "--gui") == 0) {
                out->gui = true;
            } else if (strcmp(arg, "--optimal") == 0) {
                out->optimal = true;
            } else if (strncmp(arg, "--seed", 6) == 0 &&
                       (arg[6] == '\0' || arg[6] == '=')) {
                if (opt_value(argc, argv, &i, "--seed", &val, err, errlen))
                    return -1;
                char *end;
                errno = 0;
                out->seed = strtoull(val, &end, 0);
                if (end == val || *end != '\0' || errno != 0)
                    return fail(err, errlen,
                                "--seed: '%s' is not a valid integer", val);
                out->seeded = true;
            } else if (strncmp(arg, "--iterations", 12) == 0 &&
                       (arg[12] == '\0' || arg[12] == '=')) {
                if (opt_value(argc, argv, &i, "--iterations", &val,
                              err, errlen))
                    return -1;
                char *end;
                errno = 0;
                long n = strtol(val, &end, 10);
                if (end == val || *end != '\0' || errno != 0 || n < 1)
                    return fail(err, errlen,
                                "--iterations: '%s' must be a positive "
                                "integer", val);
                out->iterations = n;
            } else if (strncmp(arg, "--runs", 6) == 0 &&
                       (arg[6] == '\0' || arg[6] == '=')) {
                /* --runs N == --iterations N with implied --stats */
                if (opt_value(argc, argv, &i, "--runs", &val, err, errlen))
                    return -1;
                char *end;
                errno = 0;
                long n = strtol(val, &end, 10);
                if (end == val || *end != '\0' || errno != 0 || n < 1)
                    return fail(err, errlen,
                                "--runs: '%s' must be a positive integer",
                                val);
                out->iterations = n;
                out->stats = true;
            } else {
                return fail(err, errlen, "unknown option '%s'", arg);
            }
        } else if (arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "-h") == 0)
                out->help = true;
            else
                return fail(err, errlen, "unknown option '%s'", arg);
        } else {
            if (out->nbets >= CLI_MAX_BETS)
                return fail(err, errlen,
                            "too many bets (max %s)", "32");
            if (parse_bet_token(arg, &out->bets[out->nbets], err, errlen))
                return -1;
            out->nbets++;
        }
    }
    return 0;
}

bool bet_is(const bet_t *b, const char *name)
{
    return strcasecmp(b->name, name) == 0;
}

bool bet_has_value(const bet_t *b)
{
    return b->nvalues != 0 || b->vraw[0] != '\0';
}
