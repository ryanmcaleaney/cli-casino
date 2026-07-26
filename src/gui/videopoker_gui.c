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

/* training mode: first of the three stacked strategy/statistics rows */
#define TRAIN_ROW_Y 554
#define TRAIN_ROW_H 22

/* Training accents.  The solver speaks in teal; gold stays the player's
 * own HOLD outline. */
#define VP_HINT (Color){ 90, 220, 240, 255 }
#define VP_GOOD (Color){ 120, 235, 140, 255 }
#define VP_BAD  (Color){ 255, 120, 110, 255 }

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

    /* --optimal strategy training; all of this stays inert without it */
    bool          train;
    bool          solved;       /* strat holds the initial five cards */
    bool          hint;         /* SHOW OPTIMAL pressed for this hand */
    bool          graded;       /* this hold decision is already scored */
    vp_strategy_t strat;
    long          hands;        /* session statistics, never reset */
    long          optimal;
    double        ev_lost;
} vpgui_t;

static uint32_t hold_mask(const bool held[5])
{
    uint32_t m = 0;

    for (int i = 0; i < 5; i++)
        if (held[i])
            m |= 1u << i;
    return m;
}

/* ---- state transitions -------------------------------------------------- */

static void start_deal(vpgui_t *v)
{
    v->credits -= v->bet;
    v->win = 0;
    v->result_cat = -1;
    v->result_text[0] = '\0';
    /* per-hand strategy state only; session statistics carry over */
    v->solved = false;
    v->hint = false;
    v->graded = false;

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

/* The solver only ever looks at the initial five cards, so it runs once,
 * when the deal animation lands. */
static void enter_hold(vpgui_t *v)
{
    v->st = GS_HOLD;
    if (v->train && !v->solved) {
        vp_front_solve(v->hand, &v->strat);
        v->solved = true;
    }
}

/* Score the hold the player just committed to, exactly once per hand. */
static void grade_hold(vpgui_t *v)
{
    if (!v->train || !v->solved || v->graded)
        return;

    uint32_t m = hold_mask(v->held);
    bool ok = vp_front_hold_optimal(&v->strat, m);

    v->hands++;
    if (ok)
        v->optimal++;           /* any mask tying the optimum counts */
    else
        v->ev_lost += vp_front_best_ev(&v->strat) -
                      vp_front_hold_ev(&v->strat, m);
    v->graded = true;
}

static void start_draw(vpgui_t *v)
{
    bool any = false;

    grade_hold(v);
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

/* The Kenney sprites carry a transparent margin: the printed card fills
 * x 11..53, y 2..62 of every 64x64 tile.  The solver hint hugs that art,
 * well inside the gold HOLD outline drawn around the whole card box, so
 * the two can never be mistaken for each other. */
static Rectangle card_art_rect(Rectangle r)
{
    return (Rectangle){ r.x + r.width * (11.0f / 64.0f),
                        r.y + r.height * (2.0f / 64.0f),
                        r.width * (42.0f / 64.0f),
                        r.height * (60.0f / 64.0f) };
}

/* Advisory only: this never touches held[]. */
static void draw_hint(const gui_ctx_t *g, Rectangle r)
{
    Rectangle a = card_art_rect(r);
    Rectangle tab = { a.x + a.width / 2 - 38, a.y - 18, 76, 24 };

    DrawRectangleLinesEx((Rectangle){ a.x - 6, a.y - 6, a.width + 12,
                                      a.height + 12 }, 4, VP_HINT);
    DrawRectangleRec(tab, GUI_FELT_DARK);
    DrawRectangleLinesEx(tab, 2, VP_HINT);
    gui_text_centered(g, true, "OPT", tab.x + tab.width / 2, tab.y + 2, 20,
                      VP_HINT);
}

static void draw_cards(const gui_ctx_t *g, vpgui_t *v)
{
    uint32_t best = v->hint ? vp_front_best_mask(&v->strat) : 0;

    for (int i = 0; i < 5; i++) {
        Rectangle r = gui_card_rect(g, i, 5, CARD_H, CARD_Y, CARD_GAP);
        bool face_up = v->st != GS_IDLE && gui_reveal_shown(&v->reveal, i);

        gui_draw_card(g, r, face_up ? &v->hand[i] : NULL);

        if (v->hint && v->st == GS_HOLD && (best & (1u << i)))
            draw_hint(g, r);

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

/* ---- training overlay (--optimal only) ---------------------------------- */

static void draw_row_right(const gui_ctx_t *g, const char *s, int row,
                           Color col)
{
    gui_text(g, false, s, 1130 - gui_text_width(g, false, s, 20),
             (float)(TRAIN_ROW_Y + row * TRAIN_ROW_H), 20, col);
}

/* Verdict, EVs and session accuracy.  Everything is recomputed from the
 * cached solve each frame, so the verdict tracks the player's clicks. */
static void draw_training(const gui_ctx_t *g, const vpgui_t *v)
{
    char buf[64];

    gui_text(g, true, "TRAINING", 150, 16, 32, VP_HINT);

    if (v->st == GS_HOLD && v->solved) {
        uint32_t m = hold_mask(v->held);
        bool ok = vp_front_hold_optimal(&v->strat, m);
        double best = vp_front_best_ev(&v->strat);
        double yours = vp_front_hold_ev(&v->strat, m);
        double loss = ok ? 0.0 : best - yours;   /* exact ties lose nothing */
        Color verdict = ok ? VP_GOOD : VP_BAD;
        /* the empty slot under the pay table's short right column */
        Rectangle p = { 690, 187, 450, 36 };

        DrawRectangleRec(p, GUI_FELT_DARK);
        DrawRectangleLinesEx(p, 2, verdict);
        gui_text_centered(g, true, ok ? "OPTIMAL" : "SUB-OPTIMAL",
                          p.x + p.width / 2, p.y + 2, 32, verdict);

        snprintf(buf, sizeof buf, "YOUR EV: %.4f", yours);
        gui_text(g, false, buf, 150, TRAIN_ROW_Y, 20, GUI_CREAM);
        snprintf(buf, sizeof buf, "BEST EV: %.4f", best);
        gui_text(g, false, buf, 150, TRAIN_ROW_Y + TRAIN_ROW_H, 20,
                 GUI_CREAM);
        snprintf(buf, sizeof buf, "EV LOSS: %.4f", loss);
        gui_text(g, false, buf, 150, TRAIN_ROW_Y + 2 * TRAIN_ROW_H, 20,
                 verdict);
    }

    snprintf(buf, sizeof buf, "HANDS: %ld   OPTIMAL: %ld", v->hands,
             v->optimal);
    draw_row_right(g, buf, 0, GUI_CREAM);
    if (v->hands > 0) {
        snprintf(buf, sizeof buf, "ACCURACY: %.1f%%",
                 100.0 * (double)v->optimal / (double)v->hands);
        draw_row_right(g, buf, 1, GUI_CREAM);
        snprintf(buf, sizeof buf, "EV LOST: %.4f   AVG: %.4f", v->ev_lost,
                 v->ev_lost / (double)v->hands);
        draw_row_right(g, buf, 2, GUI_DIM);
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
                enter_hold(v);
            else
                finish_hand(v);
        }
    }

    /* ---- draw ---- */
    DrawRectangle(0, 0, GUI_CANVAS_W, 60, GUI_FELT_DARK);
    draw_paytable(g, v);
    draw_cards(g, v);
    if (v->train)
        draw_training(g, v);

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

    /* advisory hint: it stays up for the rest of this hold phase and
     * changes nothing but the highlight */
    if (v->train &&
        gui_button(g, (Rectangle){ 430, 664, 220, 44 }, "SHOW OPTIMAL",
                   v->st == GS_HOLD && v->solved))
        v->hint = true;

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

int vp_gui_run(rng_t *rng, bool optimal)
{
    vpgui_t v = { 0 };

    v.st = GS_IDLE;
    v.rng = rng;
    v.credits = CREDITS_START;
    v.bet = BET_MAX;
    v.result_cat = -1;
    v.train = optimal;

    return gui_run(optimal ? "casino - video poker trainer"
                           : "casino - video poker", vp_frame, &v);
}
