#ifndef CASINO_LETITRIDE_GUI_H
#define CASINO_LETITRIDE_GUI_H

#include "rng.h"

/* Graphical Let It Ride, built on the reusable GUI primitives in gui.h.
 * The deck, the wagers, both decisions, the card reveals, the hand
 * ranking, the pay table and every credit of settlement come from the
 * engine via the lir_* session and lir_front_* interfaces; nothing here
 * knows a rule, and a community card can only be drawn once the engine
 * has turned it over.  Returns 0 on a clean exit, 1 if the GUI could not
 * start (missing assets, no display). */
int lir_gui_run(rng_t *rng);

#endif
