#ifndef CASINO_VIDEOPOKER_H
#define CASINO_VIDEOPOKER_H

#include "cli.h"
#include "rng.h"

int  videopoker_run(const cli_t *cli, rng_t *rng);
void videopoker_list_bets(void);

#endif
