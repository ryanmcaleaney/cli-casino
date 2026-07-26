#include "gui.h"

#include <stdio.h>
#include <string.h>

#include "raylib.h"

#include "assets.h"
#include "cards.h"
#include "games/videopoker.h"

/* ---- logical canvas ---------------------------------------------------- */
/* Everything is laid out on a fixed 1280x720 canvas rendered into a
 * texture and scaled (letterboxed, point-filtered) to the real window. */
#define CANVAS_W 1280
#define CANVAS_H 720

#define FELT       (Color){ 20, 90, 55, 255 }
#define FELT_DARK  (Color){ 12, 60, 38, 255 }
#define CREAM      (Color){ 235, 232, 210, 255 }
#define UI_GOLD    (Color){ 255, 210, 80, 255 }
#define DIM        (Color){ 160, 190, 170, 255 }
#define SHADOW     (Color){ 0, 0, 0, 90 }

#define CARD_GAP   24
#define CARD_Y     236
#define DEAL_STAGGER 0.08

#define BET_MIN 1
#define BET_MAX 5
#define CREDITS_START 100

typedef enum { GS_IDLE, GS_DEALING, GS_HOLD, GS_DRAWING, GS_RESULT } gstate_t;

typedef struct {
    gstate_t st;
    shoe_t   shoe;
    card_t   hand[5];
    bool     held[5];
    bool     visible[5];
    double   reveal_at[5];
    long     credits;
    int      bet;
    long     win;
    int      result_cat;        /* -1 = none */
    char     result_text[48];
} vpgui_t;

typedef struct {
    vp_assets_t *as;
    Vector2      mouse;         /* in canvas coordinates */
    bool         clicked;
} ui_t;

/* ---- small drawing helpers -------------------------------------------- */

static void text_at(const ui_t *ui, bool bold, const char *s, float x,
                    float y, float size, Color col)
{
    DrawTextEx(bold ? ui->as->font_bold : ui->as->font, s,
               (Vector2){ x, y }, size, 1, col);
}

static float text_w(const ui_t *ui, bool bold, const char *s, float size)
{
    return MeasureTextEx(bold ? ui->as->font_bold : ui->as->font, s,
                         size, 1).x;
}

static void text_centered(const ui_t *ui, bool bold, const char *s,
                          float cx, float y, float size, Color col)
{
    text_at(ui, bold, s, cx - text_w(ui, bold, s, size) / 2, y, size, col);
}

static bool button(ui_t *ui, Rectangle r, const char *label, bool enabled)
{
    bool hover = enabled && CheckCollisionPointRec(ui->mouse, r);
    Color fill = enabled ? (hover ? (Color){ 30, 120, 74, 255 } : FELT_DARK)
                         : (Color){ 40, 60, 48, 255 };

    DrawRectangleRec((Rectangle){ r.x + 3, r.y + 3, r.width, r.height },
                     SHADOW);
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, 2, enabled ? UI_GOLD : DIM);
    text_centered(ui, true, label, r.x + r.width / 2,
                  r.y + (r.height - 24) / 2, 24, enabled ? CREAM : DIM);

    if (enabled && hover && ui->clicked) {
        PlaySound(ui->as->snd_click);
        return true;
    }
    return false;
}

/* ---- game state transitions ------------------------------------------- */

static void start_deal(vpgui_t *g, rng_t *rng, ui_t *ui)
{
    g->credits -= g->bet;
    g->win = 0;
    g->result_cat = -1;
    g->result_text[0] = '\0';

    shoe_init(&g->shoe, 1);
    shoe_shuffle(&g->shoe, rng);
    double now = GetTime();
    for (int i = 0; i < 5; i++) {
        g->hand[i] = shoe_draw(&g->shoe);
        g->held[i] = false;
        g->visible[i] = false;
        g->reveal_at[i] = now + i * DEAL_STAGGER;
    }
    g->st = GS_DEALING;
    (void)ui;
}

static void finish_hand(vpgui_t *g)
{
    char desc[32];

    g->result_cat = vp_front_category(g->hand);
    int pay = vp_front_payout(g->result_cat);
    g->win = (long)pay * g->bet;
    g->credits += g->win;

    if (pay > 0) {
        vp_front_describe(g->hand, desc, sizeof desc);
        for (char *p = desc; *p; p++)
            *p = (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
        snprintf(g->result_text, sizeof g->result_text, "%s", desc);
    } else {
        snprintf(g->result_text, sizeof g->result_text, "NO WIN");
    }
    g->st = GS_RESULT;
}

static void start_draw(vpgui_t *g, ui_t *ui)
{
    bool any = false;
    double now = GetTime();
    int n = 0;

    for (int i = 0; i < 5; i++) {
        if (!g->held[i]) {
            g->hand[i] = shoe_draw(&g->shoe);
            g->visible[i] = false;
            g->reveal_at[i] = now + n++ * DEAL_STAGGER;
            any = true;
        }
    }
    (void)ui;
    if (any)
        g->st = GS_DRAWING;
    else
        finish_hand(g);
}

static void update_reveals(vpgui_t *g, ui_t *ui)
{
    bool all = true;
    double now = GetTime();

    for (int i = 0; i < 5; i++) {
        if (!g->visible[i]) {
            if (now >= g->reveal_at[i]) {
                g->visible[i] = true;
                PlaySound(ui->as->snd_deal);
            } else {
                all = false;
            }
        }
    }
    if (all) {
        if (g->st == GS_DEALING)
            g->st = GS_HOLD;
        else if (g->st == GS_DRAWING)
            finish_hand(g);
    }
}

static void toggle_hold(vpgui_t *g, ui_t *ui, int i)
{
    if (g->st != GS_HOLD)
        return;
    g->held[i] = !g->held[i];
    PlaySound(ui->as->snd_click);
}

/* ---- screen sections --------------------------------------------------- */

/* Pay-table rows come straight from the engine, highest first.  The two
 * zero-pay categories (low pair, high card) are not listed. */
static void draw_paytable(const ui_t *ui, const vpgui_t *g)
{
    char name[32], val[16];
    int row = 0;

    text_centered(ui, true, "JACKS OR BETTER", CANVAS_W / 2, 14, 40, UI_GOLD);

    for (int cat = VP_FRONT_NCATS - 1; cat >= 0; cat--) {
        if (vp_front_payout(cat) == 0)
            continue;
        snprintf(name, sizeof name, "%s", vp_front_token(cat));
        for (char *p = name; *p; p++)
            if (*p == '_')
                *p = ' ';
        snprintf(val, sizeof val, "%d", vp_front_payout(cat));

        int col = row / 5;              /* 5 rows left, 4 right */
        float x = col == 0 ? 150 : 700;
        float y = 70 + (row % 5) * 30;
        bool hit = g->st == GS_RESULT && g->result_cat == cat;

        if (hit)
            DrawRectangle((int)x - 8, (int)y - 2, 440, 28,
                          (Color){ 255, 210, 80, 60 });
        text_at(ui, hit, name, x, y, 24, hit ? UI_GOLD : CREAM);
        text_at(ui, hit, val, x + 360 - text_w(ui, hit, val, 24), y, 24,
                hit ? UI_GOLD : CREAM);
        row++;
    }
}

static Rectangle card_rect(const ui_t *ui, int i)
{
    float cw = (float)ui->as->back.width;
    float ch = (float)ui->as->back.height;
    float scale = 250.0f / ch;          /* card height ~250 canvas px */
    float w = cw * scale, h = ch * scale;
    float total = 5 * w + 4 * CARD_GAP;
    float x0 = (CANVAS_W - total) / 2;
    return (Rectangle){ x0 + i * (w + CARD_GAP), CARD_Y, w, h };
}

static void draw_cards(ui_t *ui, vpgui_t *g, rng_t *rng)
{
    (void)rng;
    for (int i = 0; i < 5; i++) {
        Rectangle r = card_rect(ui, i);
        const Texture2D *tex;

        DrawRectangleRec((Rectangle){ r.x + 5, r.y + 5, r.width, r.height },
                         SHADOW);
        if (g->st == GS_IDLE) {
            tex = &ui->as->back;
        } else {
            tex = g->visible[i] ? asset_card(ui->as, g->hand[i])
                                : &ui->as->back;
        }
        DrawTexturePro(*tex,
                       (Rectangle){ 0, 0, (float)tex->width,
                                    (float)tex->height },
                       r, (Vector2){ 0, 0 }, 0, WHITE);

        if (g->held[i]) {
            DrawRectangleLinesEx(
                (Rectangle){ r.x - 3, r.y - 3, r.width + 6, r.height + 6 },
                3, UI_GOLD);
            Rectangle hb = { r.x + r.width / 2 - 44, r.y + r.height + 8,
                             88, 30 };
            DrawRectangleRec(hb, FELT_DARK);
            DrawRectangleLinesEx(hb, 2, UI_GOLD);
            text_centered(ui, true, "HOLD", hb.x + hb.width / 2, hb.y + 3,
                          24, UI_GOLD);
        }

        /* position number under each card (matches CLI hold syntax) */
        char num[2] = { (char)('1' + i), 0 };
        text_centered(ui, false, num, r.x + r.width / 2,
                      r.y + r.height + 42, 24, DIM);

        if (g->st == GS_HOLD && ui->clicked &&
            CheckCollisionPointRec(ui->mouse, r))
            toggle_hold(g, ui, i);
    }
}

/* ---- main loop ---------------------------------------------------------- */

int vp_gui_run(rng_t *rng)
{
    if (!assets_check())
        return 1;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(CANVAS_W, CANVAS_H, "casino - video poker");
    if (!IsWindowReady()) {
        fprintf(stderr, "gui: could not open a window "
                        "(no display available?)\n");
        return 1;
    }
    InitAudioDevice();
    SetTargetFPS(60);
    SetExitKey(KEY_ESCAPE);

    vp_assets_t as;
    if (!assets_load(&as)) {
        assets_unload(&as);
        CloseAudioDevice();
        CloseWindow();
        return 1;
    }

    RenderTexture2D canvas = LoadRenderTexture(CANVAS_W, CANVAS_H);
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_POINT);

    vpgui_t g = { 0 };
    g.st = GS_IDLE;
    g.credits = CREDITS_START;
    g.bet = BET_MAX;
    g.result_cat = -1;

    while (!WindowShouldClose()) {
        /* window -> canvas coordinate mapping (letterboxed) */
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        float scale = sw / CANVAS_W < sh / CANVAS_H ? sw / CANVAS_W
                                                    : sh / CANVAS_H;
        float ox = (sw - CANVAS_W * scale) / 2;
        float oy = (sh - CANVAS_H * scale) / 2;

        ui_t ui = { .as = &as };
        Vector2 m = GetMousePosition();
        ui.mouse = (Vector2){ (m.x - ox) / scale, (m.y - oy) / scale };
        ui.clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

        /* input that isn't tied to a widget */
        if (g.st == GS_HOLD)
            for (int i = 0; i < 5; i++)
                if (IsKeyPressed(KEY_ONE + i))
                    toggle_hold(&g, &ui, i);

        bool bet_ok = g.st == GS_IDLE || g.st == GS_RESULT;
        if (bet_ok) {
            int nb = g.bet;
            if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_UP))
                nb++;
            if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_DOWN))
                nb--;
            nb = nb < BET_MIN ? BET_MIN : nb > BET_MAX ? BET_MAX : nb;
            if (nb != g.bet) {
                g.bet = nb;
                PlaySound(as.snd_click);
            }
            if (g.credits == 0 && IsKeyPressed(KEY_N)) {
                g.credits = CREDITS_START;
                PlaySound(as.snd_click);
            }
        }

        bool can_deal = bet_ok && g.credits >= g.bet;
        bool action_key = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER);
        if (can_deal && action_key)
            start_deal(&g, rng, &ui);
        else if (g.st == GS_HOLD && action_key)
            start_draw(&g, &ui);

        if (g.st == GS_DEALING || g.st == GS_DRAWING)
            update_reveals(&g, &ui);

        /* ---- draw the canvas ---- */
        BeginTextureMode(canvas);
        ClearBackground(FELT);
        DrawRectangle(0, 0, CANVAS_W, 60, FELT_DARK);

        draw_paytable(&ui, &g);
        draw_cards(&ui, &g, rng);

        if (g.st == GS_RESULT)
            text_centered(&ui, true, g.result_text, CANVAS_W / 2, 566, 40,
                          g.win > 0 ? UI_GOLD : CREAM);
        else if (g.st == GS_HOLD)
            text_centered(&ui, false,
                          "CLICK CARDS OR PRESS 1-5 TO HOLD",
                          CANVAS_W / 2, 574, 24, DIM);
        else if (g.st == GS_IDLE && g.credits < g.bet)
            text_centered(&ui, true,
                          g.credits == 0 ? "OUT OF CREDITS - PRESS N"
                                         : "LOWER YOUR BET",
                          CANVAS_W / 2, 566, 32, UI_GOLD);

        char info[96];
        snprintf(info, sizeof info, "CREDIT: %ld", g.credits);
        text_at(&ui, true, info, 150, 624, 32, CREAM);
        snprintf(info, sizeof info, "BET: %d", g.bet);
        text_centered(&ui, true, info, CANVAS_W / 2, 624, 32, CREAM);
        snprintf(info, sizeof info, "WIN: %ld", g.win);
        text_at(&ui, true, info,
                1130 - text_w(&ui, true, info, 32), 624, 32,
                g.win > 0 ? UI_GOLD : CREAM);

        if (button(&ui, (Rectangle){ 150, 664, 120, 44 }, "BET -", bet_ok))
            if (g.bet > BET_MIN) {
                g.bet--;
            }
        if (button(&ui, (Rectangle){ 290, 664, 120, 44 }, "BET +", bet_ok))
            if (g.bet < BET_MAX) {
                g.bet++;
            }

        const char *action = g.st == GS_HOLD || g.st == GS_DEALING
                                 ? "DRAW" : "DEAL";
        bool action_ok = can_deal || g.st == GS_HOLD;
        if (button(&ui, (Rectangle){ 890, 660, 240, 52 }, action,
                   action_ok)) {
            if (g.st == GS_HOLD)
                start_draw(&g, &ui);
            else if (can_deal)
                start_deal(&g, rng, &ui);
        }

        EndTextureMode();

        /* ---- blit, letterboxed ---- */
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(canvas.texture,
                       (Rectangle){ 0, 0, CANVAS_W, -CANVAS_H },
                       (Rectangle){ ox, oy, CANVAS_W * scale,
                                    CANVAS_H * scale },
                       (Vector2){ 0, 0 }, 0, WHITE);
        EndDrawing();
    }

    UnloadRenderTexture(canvas);
    assets_unload(&as);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
