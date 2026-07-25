#ifndef CASINO_BLACKJACK_H
#define CASINO_BLACKJACK_H

#include "cli.h"
#include "rng.h"

int  blackjack_run(const cli_t *cli, rng_t *rng);
void blackjack_list_bets(void);

#endif
