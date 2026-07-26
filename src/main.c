#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cli.h"
#include "game.h"
#include "rng.h"

#include "games/baccarat.h"
#include "games/blackjack.h"
#include "games/coin.h"
#include "games/craps.h"
#include "games/gdice.h"
#include "games/ridethebus.h"
#include "games/roulette.h"
#include "games/slots.h"
#include "games/videopoker.h"

const game_t GAMES[] = {
    { "roulette",  "European roulette",       roulette_run, roulette_list_bets },
    { "coin",      "coin flip",               coin_run,     coin_list_bets },
    { "dice",      "generic dice (NdM)",      gdice_run,    gdice_list_bets },
    { "sicbo",     "sic bo (planned)",        NULL, NULL },
    { "baccarat",  "Punto Banco baccarat",    baccarat_run, baccarat_list_bets },
    { "blackjack", "blackjack (single deck, S17)", blackjack_run,
                                                   blackjack_list_bets },
    { "craps",     "craps (pass-line rounds)", craps_run,   craps_list_bets },
    { "slots",     "3-reel slot machine (3x3 window)", slots_run, slots_list_bets },
    { "videopoker","Jacks or Better video poker", videopoker_run,
                                                  videopoker_list_bets },
    { "ridethebus","four-stage red/black, high/low, inside/outside, suit",
                                                  ridethebus_run,
                                                  ridethebus_list_bets },
    { "war",       "casino war (planned)",    NULL, NULL },
    { "threecard", "three-card poker (planned)", NULL, NULL },
    { "chuckaluck","chuck-a-luck (planned)",  NULL, NULL },
    { "bigsix",    "big six wheel (planned)", NULL, NULL },
};
const int NGAMES = (int)(sizeof GAMES / sizeof GAMES[0]);

const game_t *game_find(const char *name)
{
    for (int i = 0; i < NGAMES; i++)
        if (strcasecmp(GAMES[i].name, name) == 0)
            return &GAMES[i];
    return NULL;
}

static void global_usage(FILE *f)
{
    fprintf(f,
        "usage: casino <game> [bets...] [options]\n"
        "       <game> [bets...] [options]        (via symlink)\n"
        "\n"
        "options:\n"
        "  --help          this help / per-game help\n"
        "  --list-bets     show bet syntax for the selected game\n"
        "  --quiet         compact one-line output\n"
        "  --json          structured JSON output (NDJSON per round)\n"
        "  --seed N        deterministic PRNG seed (reproducible)\n"
        "  --iterations N  play N rounds\n"
        "  --stats         summary statistics instead of per-round output\n"
        "  --runs N        shorthand for --iterations N --stats\n"
        "  --trainer       interactive strategy trainer (videopoker only)\n"
        "  --gui           graphical frontend (videopoker only)\n"
        "\n"
        "games:\n");
    for (int i = 0; i < NGAMES; i++)
        fprintf(f, "  %-12s %s\n", GAMES[i].name, GAMES[i].help);
    fprintf(f,
        "\nbet syntax: name | name:V | name:V,V,...   e.g. red, "
        "straight:17, split:17,20\n");
}

int main(int argc, char **argv)
{
    /* argv[0] basename selects the game when invoked via symlink. */
    char self[256];
    snprintf(self, sizeof self, "%s", argv[0]);
    const char *prog = basename(self);

    const game_t *game = game_find(prog);
    int argshift = 1;

    if (!game) {
        if (argc < 2) {
            global_usage(stderr);
            return 2;
        }
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            global_usage(stdout);
            return 0;
        }
        game = game_find(argv[1]);
        if (!game) {
            fprintf(stderr, "casino: unknown game '%s' "
                            "(run 'casino --help' for the list)\n",
                    argv[1]);
            return 2;
        }
        argshift = 2;
    }

    cli_t cli;
    char err[160];
    if (cli_parse(argc - argshift, argv + argshift, &cli,
                  err, sizeof err) < 0) {
        fprintf(stderr, "%s: %s\n", game->name, err);
        return 2;
    }

    if (cli.help) {
        printf("%s - %s\n\n", game->name, game->help);
        if (game->list_bets)
            game->list_bets();
        printf("\n");
        global_usage(stdout);
        return 0;
    }
    if (cli.list_bets) {
        if (game->list_bets)
            game->list_bets();
        else
            printf("%s: not implemented yet\n", game->name);
        return 0;
    }
    if (!game->run) {
        fprintf(stderr, "%s: not implemented yet\n", game->name);
        return 2;
    }
    if (cli.trainer && strcmp(game->name, "videopoker") != 0) {
        fprintf(stderr, "%s: --trainer is only available for videopoker\n",
                game->name);
        return 2;
    }
    if (cli.gui && strcmp(game->name, "videopoker") != 0) {
        fprintf(stderr, "%s: --gui is only available for videopoker\n",
                game->name);
        return 2;
    }

    rng_t rng;
    rng_init(&rng, cli.seeded, cli.seed);

    return game->run(&cli, &rng);
}
