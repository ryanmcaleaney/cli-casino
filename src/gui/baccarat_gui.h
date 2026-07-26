#ifndef CASINO_BACCARAT_GUI_H
#define CASINO_BACCARAT_GUI_H

#include "rng.h"

/* Graphical Punto Banco baccarat, built on the reusable GUI primitives
 * in gui.h.  Dealing, the third-card rules and the outcome all come from
 * the engine via bac_front_*.  Returns 0 on a clean exit, 1 if the GUI
 * could not start (missing assets, no display). */
int bac_gui_run(rng_t *rng);

#endif
