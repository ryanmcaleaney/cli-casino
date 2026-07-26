#include "baccarat_gui.h"

#include <stdio.h>

#include "gui.h"
#include "cards.h"
#include "games/baccarat.h"

/* ---- baccarat specific layout ------------------------------------------ */

#define CARD_H       200
#define CARD_GAP     16
#define CARD_Y       250
#define PLAYER_CX    360
#define BANKER_CX    920
#define DEAL_STAGGER 0.08

#define BET_W  220
#define BET_H  52
#define BET_Y  648

typedef enum { BS_IDLE, BS_DEALING, BS_RESULT } bstate_t;

typedef struct {
    bstate_t     st;
    rng_t       *rng;
    bac_round_t  round;
    bac_side_t   bet;
    bac_result_t result;
    gui_reveal_t reveal;
    /* reveal slot of each card, so the two hands animate in deal order */
    int          pslot[BAC_MAX_CARDS];
    int          bslot[BAC_MAX_CARDS];
} bacgui_t;

/* ---- round flow --------------------------------------------------------- */

/* Deal order: player 1, banker 1, player 2, banker 2, then the third
 * cards the engine drew (player's first). */
static void build_slots(bacgui_t *v)
{
    int slot = 0;

    for (int i = 0; i < 2; i++) {
        v->pslot[i] = slot++;
        v->bslot[i] = slot++;
    }
    if (v->round.nplayer > 2)
        v->pslot[2] = slot++;
    if (v->round.nbanker > 2)
        v->bslot[2] = slot++;
}

static void start_round(bacgui_t *v, bac_side_t side)
{
    v->bet = side;
    bac_front_round(v->rng, &v->round);
    build_slots(v);
    gui_reveal_start(&v->reveal, v->round.nplayer + v->round.nbanker, NULL,
                     DEAL_STAGGER);
    v->st = BS_DEALING;
}

/* ---- drawing ------------------------------------------------------------ */

/*
 * Cards of one side.  The row is laid out for the hand's final size so
 * nothing shifts when a third card lands, but unrevealed cards are not
 * drawn at all: that keeps a coming third card from being telegraphed.
 * Returns how many cards are face up.
 */
static int draw_hand(const gui_ctx_t *g, const bacgui_t *v, bool banker)
{
    const card_t *cards = banker ? v->round.banker : v->round.player;
    const int *slots = banker ? v->bslot : v->pslot;
    int n = banker ? v->round.nbanker : v->round.nplayer;
    float cx = banker ? BANKER_CX : PLAYER_CX;
    int shown = 0;

    if (v->st == BS_IDLE) {
        /* idle table: two face-down cards a side */
        for (int i = 0; i < 2; i++)
            gui_draw_card(g, gui_card_row(g, i, 2, CARD_H, cx, CARD_Y,
                                          CARD_GAP), NULL);
        return 0;
    }

    for (int i = 0; i < n; i++) {
        if (!gui_reveal_shown(&v->reveal, slots[i]))
            continue;
        gui_draw_card(g, gui_card_row(g, i, n, CARD_H, cx, CARD_Y,
                                      CARD_GAP), &cards[i]);
        shown++;
    }
    return shown;
}

static void draw_side(const gui_ctx_t *g, const bacgui_t *v, bool banker)
{
    const card_t *cards = banker ? v->round.banker : v->round.player;
    float cx = banker ? BANKER_CX : PLAYER_CX;
    bool winner = v->st == BS_RESULT &&
                  v->round.outcome == (banker ? BAC_BANKER : BAC_PLAYER);
    char buf[32];

    gui_text_centered(g, true, banker ? "BANKER" : "PLAYER", cx, 196, 32,
                      winner ? GUI_GOLD : GUI_CREAM);

    int shown = draw_hand(g, v, banker);

    if (shown > 0) {
        snprintf(buf, sizeof buf, "%d", bac_front_total(cards, shown));
        gui_text_centered(g, true, buf, cx, CARD_Y + CARD_H + 14, 40,
                          winner ? GUI_GOLD : GUI_CREAM);
    }
}

/* ---- one frame ---------------------------------------------------------- */

static void bac_frame(const gui_ctx_t *g, void *state)
{
    bacgui_t *v = state;
    bool can_bet = v->st != BS_DEALING;

    /* keyboard: P / T / B pick a side and deal immediately */
    if (can_bet) {
        bac_side_t pick;
        bool picked = true;
        if (IsKeyPressed(KEY_P))
            pick = BAC_PLAYER;
        else if (IsKeyPressed(KEY_T))
            pick = BAC_TIE;
        else if (IsKeyPressed(KEY_B))
            pick = BAC_BANKER;
        else
            picked = false;
        if (picked) {
            gui_play_click(g);
            start_round(v, pick);
            can_bet = false;
        }
    }

    if (v->st == BS_DEALING && gui_reveal_update(&v->reveal, g)) {
        v->result = bac_front_result(v->round.outcome, v->bet);
        v->st = BS_RESULT;
    }

    /* ---- draw ---- */
    DrawRectangle(0, 0, GUI_CANVAS_W, 60, GUI_FELT_DARK);
    gui_text_centered(g, true, "BACCARAT", GUI_CANVAS_W / 2, 14, 40,
                      GUI_GOLD);
    gui_text_centered(g, false, "PUNTO BANCO", GUI_CANVAS_W / 2, 66, 24,
                      GUI_DIM);

    draw_side(g, v, false);
    draw_side(g, v, true);

    if (v->st == BS_RESULT) {
        char line[48];
        if (v->round.outcome == BAC_TIE)
            snprintf(line, sizeof line, "TIE");
        else
            snprintf(line, sizeof line, "%s WINS",
                     bac_front_side_word(v->round.outcome));
        gui_text_centered(g, true, line, GUI_CANVAS_W / 2, 512, 40,
                          GUI_GOLD);

        snprintf(line, sizeof line, "BET %s - %s",
                 bac_front_side_word(v->bet),
                 bac_front_result_word(v->result));
        gui_text_centered(g, true, line, GUI_CANVAS_W / 2, 560, 32,
                          v->result == BAC_WIN ? GUI_GOLD : GUI_CREAM);
    } else if (v->st == BS_IDLE) {
        gui_text_centered(g, false,
                          "CHOOSE A BET TO DEAL - [P]LAYER  [T]IE  [B]ANKER",
                          GUI_CANVAS_W / 2, 520, 24, GUI_DIM);
    } else {
        gui_text_centered(g, false, "DEALING...", GUI_CANVAS_W / 2, 520, 24,
                          GUI_DIM);
    }

    /* bet buttons: clicking one selects it and deals in the same action */
    static const struct { const char *label; bac_side_t side; float x; }
    BETS[] = {
        { "PLAYER", BAC_PLAYER, 180 },
        { "TIE",    BAC_TIE,    530 },
        { "BANKER", BAC_BANKER, 880 },
    };
    for (int i = 0; i < 3; i++) {
        Rectangle r = { BETS[i].x, BET_Y, BET_W, BET_H };
        if (gui_button(g, r, BETS[i].label, can_bet))
            start_round(v, BETS[i].side);
        /* mark the standing bet once a round has been played */
        if (v->st != BS_IDLE && v->bet == BETS[i].side)
            DrawRectangleLinesEx(
                (Rectangle){ r.x - 3, r.y - 3, r.width + 6, r.height + 6 },
                3, GUI_GOLD);
    }
}

int bac_gui_run(rng_t *rng)
{
    bacgui_t v = { 0 };

    v.st = BS_IDLE;
    v.rng = rng;

    return gui_run("casino - baccarat", bac_frame, &v);
}
