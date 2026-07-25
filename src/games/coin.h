#ifndef CASINO_COIN_H
#define CASINO_COIN_H

#include "cli.h"
#include "rng.h"

int  coin_run(const cli_t *cli, rng_t *rng);
void coin_list_bets(void);

#endif
