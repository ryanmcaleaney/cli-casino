#ifndef CASINO_ROULETTE_H
#define CASINO_ROULETTE_H

#include "cli.h"
#include "rng.h"

int  roulette_run(const cli_t *cli, rng_t *rng);
void roulette_list_bets(void);

#endif
