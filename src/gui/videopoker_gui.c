#include "videopoker_gui.h"

#include <stdio.h>

#include "gui.h"
#include "cards.h"
#include "games/videopoker.h"

/* ---- video poker specific layout and rules ---------------------------- */

#define CARD_GAP     24
#define CARD_H       250
#define CARD_Y       236
#define DEAL_STAGGER 0.08

#define BET_MIN 1
#define BET_MAX 5
#define CREDITS_START 100

typedef enum { GS_IDLE, GS_DEALING, GS_HOLD, GS_DRAWING, GS_RESULT } gstate_t;

typedef struct {
    gstate_t     st;
    rng_t       *rng;
    shoe_t       shoe;
    card_t       hand[5];
    bool         held[5];
    gui_reveal_t reveal;
    long         credits;
    int          bet;
    long         win;
    int          result_cat;    /* -1 = none */
    char         result_text[48];
} vpgui_t;

/* ---- state transitions -------------------------------------------------- */

static void start_deal(vpgui_t *v)
{
    v->credits -= v->bet;
    v->win = 0;
    v->result_cat = -1;
    v->result_text[0] = '\0';

    shoe_init(&v->shoe, 1);
    shoe_shuffle(&v->shoe, v->rng);
    for (int i = 0; i < 5; i++) {
        v->hand[i] = shoe_draw(&v->shoe);
        v->held[i] = false;
    }
    gui_reveal_start(&v->reveal, 5, NULL, DEAL_STAGGER);
    v->st = GS_DEALING;
}

static void finish_hand(vpgui_t *v)
{
    char desc[32];

    v->result_cat = vp_front_category(v->hand);
    int pay = vp_front_payout(v->result_cat);
    v->win = (long)pay * v->bet;
    v->credits += v->win;

    if (pay > 0) {
        vp_front_describe(v->hand, desc, sizeof desc);
        for (char *p = desc; *p; p++)
            *p = (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
        snprintf(v->result_text, sizeof v->result_text, "%s", desc);
    } else {
        snprintf(v->result_text, sizeof v->result_text, "NO WIN");
    }
    v->st = GS_RESULT;
}

static void start_draw(vpgui_t *v)
{
    bool any = false;

    for (int i = 0; i < 5; i++) {
        if (!v->held[i]) {
            v->hand[i] = shoe_draw(&v->shoe);
            any = true;
        }
    }
    if (!any) {
        finish_hand(v);
        return;
    }
    /* held cards stay put; only the replacements animate */
    gui_reveal_start(&v->reveal, 5, v->held, DEAL_STAGGER);
    v->st = GS_DRAWING;
}

static void toggle_hold(vpgui_t *v, const gui_ctx_t *g, int i)
{
    if (v->st != GS_HOLD)
        return;
    v->held[i] = !v->held[i];
    gui_play_click(g);
}

/* ---- drawing ------------------------------------------------------------ */

/* Pay-table rows come straight from the engine, highest first.  The two
 * zero-pay categories (low pair, high card) are not listed. */
static void draw_paytable(const gui_ctx_t *g, const vpgui_t *v)
{
    char name[32], val[16];
    int row = 0;

    gui_text_centered(g, true, "JACKS OR BETTER", GUI_CANVAS_W / 2, 14, 40,
                      GUI_GOLD);

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
        bool hit = v->st == GS_RESULT && v->result_cat == cat;

        if (hit)
            DrawRectangle((int)x - 8, (int)y - 2, 440, 28,
                          (Color){ 255, 210, 80, 60 });
        gui_text(g, hit, name, x, y, 24, hit ? GUI_GOLD : GUI_CREAM);
        gui_text(g, hit, val,
                 x + 360 - gui_text_width(g, hit, val, 24), y, 24,
                 hit ? GUI_GOLD : GUI_CREAM);
        row++;
    }
}

static void draw_cards(const gui_ctx_t *g, vpgui_t *v)
{
    for (int i = 0; i < 5; i++) {
        Rectangle r = gui_card_rect(g, i, 5, CARD_H, CARD_Y, CARD_GAP);
        bool face_up = v->st != GS_IDLE && gui_reveal_shown(&v->reveal, i);

        gui_draw_card(g, r, face_up ? &v->hand[i] : NULL);

        if (v->held[i]) {
            DrawRectangleLinesEx(
                (Rectangle){ r.x - 3, r.y - 3, r.width + 6, r.height + 6 },
                3, GUI_GOLD);
            Rectangle hb = { r.x + r.width / 2 - 44, r.y + r.height + 8,
                             88, 30 };
            DrawRectangleRec(hb, GUI_FELT_DARK);
            DrawRectangleLinesEx(hb, 2, GUI_GOLD);
            gui_text_centered(g, true, "HOLD", hb.x + hb.width / 2,
                              hb.y + 3, 24, GUI_GOLD);
        }

        /* position number under each card (matches CLI hold syntax) */
        char num[2] = { (char)('1' + i), 0 };
        gui_text_centered(g, false, num, r.x + r.width / 2,
                          r.y + r.height + 42, 24, GUI_DIM);

        if (v->st == GS_HOLD && g->clicked &&
            CheckCollisionPointRec(g->mouse, r))
            toggle_hold(v, g, i);
    }
}

/* ---- one frame ---------------------------------------------------------- */

static void vp_frame(const gui_ctx_t *g, void *state)
{
    vpgui_t *v = state;

    /* input that is not tied to a widget */
    if (v->st == GS_HOLD)
        for (int i = 0; i < 5; i++)
            if (IsKeyPressed(KEY_ONE + i))
                toggle_hold(v, g, i);

    bool bet_ok = v->st == GS_IDLE || v->st == GS_RESULT;
    if (bet_ok) {
        int nb = v->bet;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_UP))
            nb++;
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_DOWN))
            nb--;
        nb = nb < BET_MIN ? BET_MIN : nb > BET_MAX ? BET_MAX : nb;
        if (nb != v->bet) {
            v->bet = nb;
            gui_play_click(g);
        }
        if (v->credits == 0 && IsKeyPressed(KEY_N)) {
            v->credits = CREDITS_START;
            gui_play_click(g);
        }
    }

    bool can_deal = bet_ok && v->credits >= v->bet;
    bool action_key = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER);
    if (can_deal && action_key)
        start_deal(v);
    else if (v->st == GS_HOLD && action_key)
        start_draw(v);

    if (v->st == GS_DEALING || v->st == GS_DRAWING) {
        if (gui_reveal_update(&v->reveal, g)) {
            if (v->st == GS_DEALING)
                v->st = GS_HOLD;
            else
                finish_hand(v);
        }
    }

    /* ---- draw ---- */
    DrawRectangle(0, 0, GUI_CANVAS_W, 60, GUI_FELT_DARK);
    draw_paytable(g, v);
    draw_cards(g, v);

    if (v->st == GS_RESULT)
        gui_text_centered(g, true, v->result_text, GUI_CANVAS_W / 2, 566,
                          40, v->win > 0 ? GUI_GOLD : GUI_CREAM);
    else if (v->st == GS_HOLD)
        gui_text_centered(g, false, "CLICK CARDS OR PRESS 1-5 TO HOLD",
                          GUI_CANVAS_W / 2, 574, 24, GUI_DIM);
    else if (v->st == GS_IDLE && v->credits < v->bet)
        gui_text_centered(g, true,
                          v->credits == 0 ? "OUT OF CREDITS - PRESS N"
                                          : "LOWER YOUR BET",
                          GUI_CANVAS_W / 2, 566, 32, GUI_GOLD);

    char info[96];
    snprintf(info, sizeof info, "CREDIT: %ld", v->credits);
    gui_text(g, true, info, 150, 624, 32, GUI_CREAM);
    snprintf(info, sizeof info, "BET: %d", v->bet);
    gui_text_centered(g, true, info, GUI_CANVAS_W / 2, 624, 32, GUI_CREAM);
    snprintf(info, sizeof info, "WIN: %ld", v->win);
    gui_text(g, true, info, 1130 - gui_text_width(g, true, info, 32), 624,
             32, v->win > 0 ? GUI_GOLD : GUI_CREAM);

    if (gui_button(g, (Rectangle){ 150, 664, 120, 44 }, "BET -", bet_ok))
        if (v->bet > BET_MIN)
            v->bet--;
    if (gui_button(g, (Rectangle){ 290, 664, 120, 44 }, "BET +", bet_ok))
        if (v->bet < BET_MAX)
            v->bet++;

    const char *action = v->st == GS_HOLD || v->st == GS_DEALING ? "DRAW"
                                                                 : "DEAL";
    bool action_ok = can_deal || v->st == GS_HOLD;
    if (gui_button(g, (Rectangle){ 890, 660, 240, 52 }, action,
                   action_ok)) {
        if (v->st == GS_HOLD)
            start_draw(v);
        else if (can_deal)
            start_deal(v);
    }
}

int vp_gui_run(rng_t *rng)
{
    vpgui_t v = { 0 };

    v.st = GS_IDLE;
    v.rng = rng;
    v.credits = CREDITS_START;
    v.bet = BET_MAX;
    v.result_cat = -1;

    return gui_run("casino - video poker", vp_frame, &v);
}
