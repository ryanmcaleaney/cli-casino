#ifndef CASINO_BLACKJACK_GUI_H
#define CASINO_BLACKJACK_GUI_H

#include <stdbool.h>

#include "rng.h"

/* Graphical blackjack, built on the reusable GUI primitives in gui.h.
 * Every rule, wager and settlement comes from the engine through the
 * bj_* frontend API.  With `counting` the table also runs as a Hi-Lo
 * trainer, showing the running and true count of the cards the player
 * has actually been shown; otherwise it plays exactly as before.
 * Returns 0 on a clean exit, 1 if the GUI could not start (missing
 * assets, no display). */
int bj_gui_run(rng_t *rng, bool counting);

#endif
