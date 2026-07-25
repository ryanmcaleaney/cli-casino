#ifndef CASINO_GDICE_H
#define CASINO_GDICE_H

#include "cli.h"
#include "rng.h"

int  gdice_run(const cli_t *cli, rng_t *rng);
void gdice_list_bets(void);

#endif
