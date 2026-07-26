#ifndef CASINO_RIDETHEBUS_GUI_H
#define CASINO_RIDETHEBUS_GUI_H

#include "rng.h"

/* Graphical Ride the Bus, built on the reusable GUI primitives in gui.h.
 * All rules, card dealing and payouts come from the engine through the
 * rtb_front_* session API; this frontend only draws and takes input.
 * Returns 0 on a clean exit, 1 if the GUI could not start (missing
 * assets, no display). */
int rtb_gui_run(rng_t *rng);

#endif
