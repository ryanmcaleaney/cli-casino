#ifndef CASINO_BLACKJACK_GUI_H
#define CASINO_BLACKJACK_GUI_H

#include "rng.h"

/* Graphical blackjack, built on the reusable GUI primitives in gui.h.
 * Every rule, wager and settlement comes from the engine through the
 * bj_* frontend API.  Returns 0 on a clean exit, 1 if the GUI could not
 * start (missing assets, no display). */
int bj_gui_run(rng_t *rng);

#endif
