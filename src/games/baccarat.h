#ifndef CASINO_BACCARAT_H
#define CASINO_BACCARAT_H

#include "cli.h"
#include "rng.h"

int  baccarat_run(const cli_t *cli, rng_t *rng);
void baccarat_list_bets(void);

#endif
