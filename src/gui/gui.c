#include "gui.h"

#include <stdio.h>

/* ---- text --------------------------------------------------------------- */

void gui_text(const gui_ctx_t *g, bool bold, const char *s, float x,
              float y, float size, Color col)
{
    DrawTextEx(bold ? g->as->font_bold : g->as->font, s,
               (Vector2){ x, y }, size, 1, col);
}

float gui_text_width(const gui_ctx_t *g, bool bold, const char *s,
                     float size)
{
    return MeasureTextEx(bold ? g->as->font_bold : g->as->font, s,
                         size, 1).x;
}

void gui_text_centered(const gui_ctx_t *g, bool bold, const char *s,
                       float cx, float y, float size, Color col)
{
    gui_text(g, bold, s, cx - gui_text_width(g, bold, s, size) / 2, y,
             size, col);
}

/* ---- widgets ------------------------------------------------------------ */

void gui_play_click(const gui_ctx_t *g)
{
    PlaySound(g->as->snd_click);
}

bool gui_button(const gui_ctx_t *g, Rectangle r, const char *label,
                bool enabled)
{
    bool hover = enabled && CheckCollisionPointRec(g->mouse, r);
    Color fill = enabled
                     ? (hover ? (Color){ 30, 120, 74, 255 } : GUI_FELT_DARK)
                     : (Color){ 40, 60, 48, 255 };

    DrawRectangleRec((Rectangle){ r.x + 3, r.y + 3, r.width, r.height },
                     GUI_SHADOW);
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, 2, enabled ? GUI_GOLD : GUI_DIM);
    gui_text_centered(g, true, label, r.x + r.width / 2,
                      r.y + (r.height - 24) / 2, 24,
                      enabled ? GUI_CREAM : GUI_DIM);

    if (enabled && hover && g->clicked) {
        gui_play_click(g);
        return true;
    }
    return false;
}

/* ---- cards -------------------------------------------------------------- */

Rectangle gui_card_row(const gui_ctx_t *g, int index, int count,
                       float height, float cx, float y, float gap)
{
    float scale = height / (float)g->as->back.height;
    float w = (float)g->as->back.width * scale;
    float total = count * w + (count - 1) * gap;
    float x0 = cx - total / 2;

    return (Rectangle){ x0 + index * (w + gap), y, w, height };
}

Rectangle gui_card_rect(const gui_ctx_t *g, int index, int count,
                        float height, float y, float gap)
{
    return gui_card_row(g, index, count, height, GUI_CANVAS_W / 2.0f, y,
                        gap);
}

void gui_draw_card(const gui_ctx_t *g, Rectangle r, const card_t *card)
{
    const Texture2D *tex = card ? asset_card(g->as, *card) : &g->as->back;

    DrawRectangleRec((Rectangle){ r.x + 5, r.y + 5, r.width, r.height },
                     GUI_SHADOW);
    DrawTexturePro(*tex,
                   (Rectangle){ 0, 0, (float)tex->width,
                                (float)tex->height },
                   r, (Vector2){ 0, 0 }, 0, WHITE);
}

/* ---- staggered reveal --------------------------------------------------- */

void gui_reveal_start(gui_reveal_t *rv, int n, const bool *already,
                      double stagger)
{
    double now = GetTime();
    int slot = 0;

    if (n > GUI_REVEAL_MAX)
        n = GUI_REVEAL_MAX;
    rv->n = n;
    for (int i = 0; i < n; i++) {
        if (already && already[i]) {
            rv->shown[i] = true;
            rv->at[i] = now;
        } else {
            rv->shown[i] = false;
            rv->at[i] = now + slot++ * stagger;
        }
    }
}

bool gui_reveal_update(gui_reveal_t *rv, const gui_ctx_t *g)
{
    double now = GetTime();
    bool all = true;

    for (int i = 0; i < rv->n; i++) {
        if (rv->shown[i])
            continue;
        if (now >= rv->at[i]) {
            rv->shown[i] = true;
            PlaySound(g->as->snd_deal);
        } else {
            all = false;
        }
    }
    return all;
}

bool gui_reveal_shown(const gui_reveal_t *rv, int i)
{
    return i >= 0 && i < rv->n && rv->shown[i];
}

/* ---- runtime ------------------------------------------------------------ */

int gui_run(const char *title, gui_frame_fn frame, void *state)
{
    if (!assets_check())
        return 1;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(GUI_CANVAS_W, GUI_CANVAS_H, title);
    if (!IsWindowReady()) {
        fprintf(stderr, "gui: could not open a window "
                        "(no display available?)\n");
        return 1;
    }
    InitAudioDevice();
    SetTargetFPS(60);
    SetExitKey(KEY_ESCAPE);

    gui_assets_t as;
    if (!assets_load(&as)) {
        assets_unload(&as);
        CloseAudioDevice();
        CloseWindow();
        return 1;
    }

    RenderTexture2D canvas = LoadRenderTexture(GUI_CANVAS_W, GUI_CANVAS_H);
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_POINT);

    while (!WindowShouldClose()) {
        /* window -> canvas coordinate mapping (letterboxed) */
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        float scale = sw / GUI_CANVAS_W < sh / GUI_CANVAS_H
                          ? sw / GUI_CANVAS_W : sh / GUI_CANVAS_H;
        float ox = (sw - GUI_CANVAS_W * scale) / 2;
        float oy = (sh - GUI_CANVAS_H * scale) / 2;

        gui_ctx_t g = { .as = &as };
        Vector2 m = GetMousePosition();
        g.mouse = (Vector2){ (m.x - ox) / scale, (m.y - oy) / scale };
        g.clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

        BeginTextureMode(canvas);
        ClearBackground(GUI_FELT);
        frame(&g, state);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(canvas.texture,
                       (Rectangle){ 0, 0, GUI_CANVAS_W, -GUI_CANVAS_H },
                       (Rectangle){ ox, oy, GUI_CANVAS_W * scale,
                                    GUI_CANVAS_H * scale },
                       (Vector2){ 0, 0 }, 0, WHITE);
        EndDrawing();
    }

    UnloadRenderTexture(canvas);
    assets_unload(&as);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
