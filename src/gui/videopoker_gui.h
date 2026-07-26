#ifndef CASINO_VIDEOPOKER_GUI_H
#define CASINO_VIDEOPOKER_GUI_H

#include "rng.h"

/* Graphical Jacks or Better video poker, built on the reusable GUI
 * primitives in gui.h.  Hand evaluation and the pay table come from the
 * engine via vp_front_*.  Returns 0 on a clean exit, 1 if the GUI could
 * not start (missing assets, no display). */
int vp_gui_run(rng_t *rng);

#endif
