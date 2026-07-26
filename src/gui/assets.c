#include "assets.h"

#include <stdio.h>

/* Set by the Makefile.  install.sh supplies PREFIX/share/casino/Assets so
 * the GUI does not depend on the caller's current working directory. */
#ifndef CASINO_ASSET_ROOT
#define CASINO_ASSET_ROOT "Assets"
#endif
#define ASSET_ROOT CASINO_ASSET_ROOT
#define CARD_DIR   ASSET_ROOT "/kenney_playing-cards-pack/PNG/Cards (large)"
#define FONT_DIR   ASSET_ROOT "/Fonts/pixel_operator"
#define AUDIO_DIR  ASSET_ROOT "/Audio"

#define FONT_REGULAR  FONT_DIR "/PixelOperator.ttf"
#define FONT_BOLD     FONT_DIR "/PixelOperator-Bold.ttf"
#define SND_DEAL      AUDIO_DIR "/card deal_s.ogg"
#define SND_CLICK     AUDIO_DIR "/click3.ogg"

/* Glyph size the pixel fonts are rasterised at; draw at multiples. */
#define FONT_BAKE_PX 32

/* card_t -> Kenney sprite filename, e.g. "card_spades_A.png",
 * "card_hearts_02.png", "card_clubs_10.png". */
static const char *card_file(card_t c)
{
    static const char *const SUIT[] = {
        "clubs", "diamonds", "hearts", "spades"
    };
    static char buf[512];
    char rank[4];

    if (c.rank == 1)
        snprintf(rank, sizeof rank, "A");
    else if (c.rank == 11)
        snprintf(rank, sizeof rank, "J");
    else if (c.rank == 12)
        snprintf(rank, sizeof rank, "Q");
    else if (c.rank == 13)
        snprintf(rank, sizeof rank, "K");
    else
        snprintf(rank, sizeof rank, "%02d", c.rank);

    snprintf(buf, sizeof buf, "%s/card_%s_%s.png", CARD_DIR, SUIT[c.suit],
             rank);
    return buf;
}

static int card_index(card_t c)
{
    return (c.rank - 1) * 4 + c.suit;
}

static bool require(const char *path)
{
    if (!FileExists(path)) {
        fprintf(stderr, "gui: missing asset: %s\n", path);
        return false;
    }
    return true;
}

bool assets_check(void)
{
    for (int r = 1; r <= 13; r++) {
        for (int s = 0; s < 4; s++) {
            card_t c = { (unsigned char)r, (unsigned char)s };
            if (!require(card_file(c)))
                return false;
        }
    }
    return require(CARD_DIR "/card_back.png") && require(FONT_REGULAR) &&
           require(FONT_BOLD) && require(SND_DEAL) && require(SND_CLICK);
}

static bool load_texture(Texture2D *t, const char *path)
{
    *t = LoadTexture(path);
    if (t->id == 0) {
        fprintf(stderr, "gui: failed to load texture: %s\n", path);
        return false;
    }
    SetTextureFilter(*t, TEXTURE_FILTER_POINT);
    return true;
}

bool assets_load(gui_assets_t *a)
{
    *a = (gui_assets_t){ 0 };

    for (int r = 1; r <= 13; r++) {
        for (int s = 0; s < 4; s++) {
            card_t c = { (unsigned char)r, (unsigned char)s };
            if (!load_texture(&a->cards[card_index(c)], card_file(c)))
                return false;
        }
    }
    if (!load_texture(&a->back, CARD_DIR "/card_back.png"))
        return false;

    a->font = LoadFontEx(FONT_REGULAR, FONT_BAKE_PX, NULL, 0);
    a->font_bold = LoadFontEx(FONT_BOLD, FONT_BAKE_PX, NULL, 0);
    if (a->font.texture.id == 0 || a->font_bold.texture.id == 0) {
        fprintf(stderr, "gui: failed to load fonts from %s\n", FONT_DIR);
        return false;
    }
    SetTextureFilter(a->font.texture, TEXTURE_FILTER_POINT);
    SetTextureFilter(a->font_bold.texture, TEXTURE_FILTER_POINT);

    a->snd_deal = LoadSound(SND_DEAL);
    a->snd_click = LoadSound(SND_CLICK);
    if (a->snd_deal.frameCount == 0 || a->snd_click.frameCount == 0) {
        fprintf(stderr, "gui: failed to load sounds from %s\n", AUDIO_DIR);
        return false;
    }
    return true;
}

void assets_unload(gui_assets_t *a)
{
    for (int i = 0; i < 52; i++)
        if (a->cards[i].id)
            UnloadTexture(a->cards[i]);
    if (a->back.id)
        UnloadTexture(a->back);
    if (a->font.texture.id)
        UnloadFont(a->font);
    if (a->font_bold.texture.id)
        UnloadFont(a->font_bold);
    if (a->snd_deal.frameCount)
        UnloadSound(a->snd_deal);
    if (a->snd_click.frameCount)
        UnloadSound(a->snd_click);
    *a = (gui_assets_t){ 0 };
}

const Texture2D *asset_card(const gui_assets_t *a, card_t c)
{
    return &a->cards[card_index(c)];
}
