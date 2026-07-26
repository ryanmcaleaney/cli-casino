#ifndef CASINO_GUI_H
#define CASINO_GUI_H

#include <stdbool.h>

#include "raylib.h"

#include "assets.h"
#include "cards.h"

/*
 * Reusable card-game GUI primitives (raylib).
 *
 * Games draw on a fixed logical canvas which gui_run() scales and
 * letterboxes to the real window, so layout code can use plain pixel
 * coordinates and stay resolution-independent.  A game supplies one
 * frame callback plus its own state; nothing here knows any game rules.
 */

#define GUI_CANVAS_W 1280
#define GUI_CANVAS_H 720

/* shared casino-table theme */
#define GUI_FELT      (Color){ 20, 90, 55, 255 }
#define GUI_FELT_DARK (Color){ 12, 60, 38, 255 }
#define GUI_CREAM     (Color){ 235, 232, 210, 255 }
#define GUI_GOLD      (Color){ 255, 210, 80, 255 }
#define GUI_DIM       (Color){ 160, 190, 170, 255 }
#define GUI_SHADOW    (Color){ 0, 0, 0, 90 }

/* Per-frame context: the shared assets plus input already mapped into
 * canvas coordinates. */
typedef struct {
    gui_assets_t *as;
    Vector2       mouse;
    bool          clicked;
} gui_ctx_t;

/* ---- text (bold selects PixelOperator-Bold) ---------------------------- */

void  gui_text(const gui_ctx_t *g, bool bold, const char *s, float x,
               float y, float size, Color col);
float gui_text_width(const gui_ctx_t *g, bool bold, const char *s,
                     float size);
void  gui_text_centered(const gui_ctx_t *g, bool bold, const char *s,
                        float cx, float y, float size, Color col);

/* ---- widgets ----------------------------------------------------------- */

/* Immediate-mode button: returns true on the frame it is clicked and
 * plays the click sound itself. */
bool gui_button(const gui_ctx_t *g, Rectangle r, const char *label,
                bool enabled);

void gui_play_click(const gui_ctx_t *g);

/* ---- cards -------------------------------------------------------------- */

/* Geometry for one card in a row of `count` cards centred on `cx` and
 * drawn at `y` with the given card height; aspect ratio is preserved.
 * Games with two hands (baccarat, blackjack) place one row per side. */
Rectangle gui_card_row(const gui_ctx_t *g, int index, int count,
                       float height, float cx, float y, float gap);

/* The same row centred on the canvas. */
Rectangle gui_card_rect(const gui_ctx_t *g, int index, int count,
                        float height, float y, float gap);

/* Draw one card with its drop shadow; pass NULL for a face-down card. */
void gui_draw_card(const gui_ctx_t *g, Rectangle r, const card_t *card);

/* ---- staggered reveal (non-blocking, deal sound per card) -------------- */

#define GUI_REVEAL_MAX 12

typedef struct {
    double at[GUI_REVEAL_MAX];
    bool   shown[GUI_REVEAL_MAX];
    int    n;
} gui_reveal_t;

/* Start revealing n cards `stagger` seconds apart.  Entries flagged in
 * `already` (may be NULL) are shown at once and do not animate, so cards
 * a game is keeping stay put while the rest are dealt. */
void gui_reveal_start(gui_reveal_t *rv, int n, const bool *already,
                      double stagger);

/* Advance the animation, playing the deal sound as each card lands.
 * Returns true once every card is visible. */
bool gui_reveal_update(gui_reveal_t *rv, const gui_ctx_t *g);
bool gui_reveal_shown(const gui_reveal_t *rv, int i);

/* ---- runtime ------------------------------------------------------------ */

/*
 * Open the window, load the shared assets, call `frame` once per frame
 * until the window closes (Escape quits), then tear everything down.
 * Returns 0 on a clean exit, or 1 if the GUI could not start.
 */
typedef void (*gui_frame_fn)(const gui_ctx_t *g, void *state);
int gui_run(const char *title, gui_frame_fn frame, void *state);

#endif
