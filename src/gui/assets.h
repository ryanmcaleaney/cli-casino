#ifndef CASINO_GUI_ASSETS_H
#define CASINO_GUI_ASSETS_H

#include <stdbool.h>

#include "raylib.h"

#include "cards.h"

/*
 * Shared GUI assets: the card deck, fonts and sounds every card game
 * uses.  All asset paths are centralised in assets.c; everything is
 * loaded once at start-up and unloaded once on exit.  Nothing here is
 * specific to any one game.
 */
typedef struct {
    Texture2D cards[52];        /* index: (rank-1)*4 + suit */
    Texture2D back;
    Font      font;             /* PixelOperator */
    Font      font_bold;        /* PixelOperator-Bold */
    Sound     snd_deal;
    Sound     snd_click;
} gui_assets_t;

/* Verify every required asset file exists.  Prints the first missing
 * path to stderr and returns false.  Safe to call before InitWindow. */
bool assets_check(void);

/* Load everything (window + audio device must be initialised). */
bool assets_load(gui_assets_t *a);
void assets_unload(gui_assets_t *a);

const Texture2D *asset_card(const gui_assets_t *a, card_t c);

#endif
