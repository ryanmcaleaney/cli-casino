#ifndef CASINO_THREECARD_GUI_H
#define CASINO_THREECARD_GUI_H

#include "rng.h"

/* Graphical three card poker, built on the reusable GUI primitives in
 * gui.h.  Dealing, hand ranking, dealer qualification, the pay tables and
 * every credit of settlement come from the engine via the tc_* session
 * and tc_front_* interfaces; nothing here knows a rule.  Returns 0 on a
 * clean exit, 1 if the GUI could not start (missing assets, no display). */
int tc_gui_run(rng_t *rng);

#endif
