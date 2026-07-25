#ifndef CASINO_SLOTS_H
#define CASINO_SLOTS_H

#include "cli.h"
#include "rng.h"

int  slots_run(const cli_t *cli, rng_t *rng);
void slots_list_bets(void);

#endif
