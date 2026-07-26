#ifndef CASINO_VIDEOPOKER_GUI_H
#define CASINO_VIDEOPOKER_GUI_H

#include <stdbool.h>

#include "rng.h"

/* Graphical Jacks or Better video poker, built on the reusable GUI
 * primitives in gui.h.  Hand evaluation, the pay table and the hold
 * solver come from the engine via vp_front_*.  With `optimal` the machine
 * also runs as a strategy trainer (live optimal/sub-optimal feedback, EVs,
 * an advisory SHOW OPTIMAL hint and session accuracy); otherwise it plays
 * exactly as before.  Returns 0 on a clean exit, 1 if the GUI could not
 * start (missing assets, no display). */
int vp_gui_run(rng_t *rng, bool optimal);

#endif
