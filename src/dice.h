#ifndef CASINO_DICE_H
#define CASINO_DICE_H

#include "rng.h"

/* One die: uniform in [1, sides]. */
int dice_roll(rng_t *r, int sides);

/* Roll `count` dice into out[] (may be NULL); returns the sum. */
int dice_roll_many(rng_t *r, int count, int sides, int *out);

#endif
