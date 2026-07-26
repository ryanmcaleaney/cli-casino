#ifndef CASINO_GUI_H
#define CASINO_GUI_H

#include "rng.h"

/* Graphical Jacks or Better video poker (raylib).  Uses the engine's
 * evaluator/pay table and the passed RNG stream.  Returns 0 on clean
 * exit, 1 if the GUI could not start (missing assets, no display). */
int vp_gui_run(rng_t *rng);

#endif
