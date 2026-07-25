#ifndef CASINO_CRAPS_H
#define CASINO_CRAPS_H

#include "cli.h"
#include "rng.h"

int  craps_run(const cli_t *cli, rng_t *rng);
void craps_list_bets(void);

#endif
