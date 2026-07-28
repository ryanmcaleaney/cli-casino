#ifndef CASINO_CARIBBEANSTUD_GUI_H
#define CASINO_CARIBBEANSTUD_GUI_H

#include "rng.h"

/* Graphical Caribbean Stud Poker, built on the reusable GUI primitives in
 * gui.h.  The deck, the wagers, the raise/fold decision, dealer
 * qualification, the raise pay table and every credit of settlement come
 * from the engine via the cs_* session and cs_front_* interfaces; nothing
 * here knows a rule, and a dealer hole card can only be drawn once the
 * engine has turned it over (cs_dealer_visible).  Returns 0 on a clean
 * exit, 1 if the GUI could not start (missing assets, no display). */
int cs_gui_run(rng_t *rng);

#endif
