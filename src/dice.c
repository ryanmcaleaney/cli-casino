#include "dice.h"

int dice_roll(rng_t *r, int sides)
{
    if (sides < 1)
        sides = 1;
    return 1 + (int)rng_below(r, (uint32_t)sides);
}

int dice_roll_many(rng_t *r, int count, int sides, int *out)
{
    int sum = 0;
    for (int i = 0; i < count; i++) {
        int v = dice_roll(r, sides);
        if (out)
            out[i] = v;
        sum += v;
    }
    return sum;
}
