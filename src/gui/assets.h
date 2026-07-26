#ifndef CASINO_GUI_ASSETS_H
#define CASINO_GUI_ASSETS_H

#include <stdbool.h>

#include "raylib.h"

#include "cards.h"

/*
 * GUI asset registry.  All asset paths are centralised here; everything
 * is loaded once at start-up and unloaded once on exit.
 */
typedef struct {
    Texture2D cards[52];        /* index: (rank-1)*4 + suit */
    Texture2D back;
    Font      font;             /* PixelOperator */
    Font      font_bold;        /* PixelOperator-Bold */
    Sound     snd_deal;
    Sound     snd_click;
} vp_assets_t;

/* Resolve a path relative to the asset root (currently the repository
 * root's Assets/ directory; installed locations can be added here). */
const char *asset_path(const char *rel);

/* Verify every required asset file exists.  Prints the first missing
 * path to stderr and returns false.  Safe to call before InitWindow. */
bool assets_check(void);

/* Load everything (window + audio device must be initialised). */
bool assets_load(vp_assets_t *a);
void assets_unload(vp_assets_t *a);

const Texture2D *asset_card(const vp_assets_t *a, card_t c);

#endif
