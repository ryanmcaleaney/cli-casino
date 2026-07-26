#include "threecard_gui.h"

#include <stdio.h>

#include "gui.h"
#include "cards.h"
#include "games/threecard.h"

/* ---- layout ------------------------------------------------------------- */

#define TABLE_CX      640

#define DEALER_LBL_Y   62
#define DEALER_Y       90
#define PLAYER_LBL_Y  306
#define PLAYER_Y      332
#define CARD_H        155
#define CARD_GAP       14

#define PANEL_X       978           /* result breakdown, clear of the cards */
#define PANEL_W       272
#define PAY_X          30           /* pay tables */
#define PAY_W         272

#define STATUS_Y      528
#define MSG_Y         562
#define BTN_Y         620
#define BTN_H          48
#define HINT_Y        682

#define DEAL_STAGGER  0.12
#define BET_STEP         5

typedef enum {
    TS_BET,         /* setting the ante and the pair plus */
    TS_DEALING,     /* the player's three cards are landing */
    TS_DECISION,    /* play or fold */
    TS_REVEAL,      /* the dealer's three cards are turning over */
    TS_RESULT       /* settled: breakdown on the felt */
} tstate_t;

typedef struct {
    tstate_t     st;
    rng_t       *rng;
    tc_session_t s;
    gui_reveal_t player_rv, dealer_rv;
    bool         has_round;         /* a hand has been dealt at least once */
    long         view_bankroll;     /* what the felt shows: never early */
} tcgui_t;

/* ---- helpers ------------------------------------------------------------ */

static bool betting(const tcgui_t *v)
{
    return v->st == TS_BET || v->st == TS_RESULT;
}

static void start_deal(tcgui_t *v)
{
    if (!tc_can_deal(&v->s))
        return;
    tc_deal(&v->s, v->rng);
    gui_reveal_start(&v->player_rv, TC_CARDS, NULL, DEAL_STAGGER);
    v->has_round = true;
    v->view_bankroll = v->s.bankroll;
    v->st = TS_DEALING;
}

/* The engine settles the moment the decision is made; the felt catches up
 * as the dealer's cards turn over, so nothing gives the result away. */
static void decide(tcgui_t *v, tc_action_t act)
{
    if (v->st != TS_DECISION)
        return;
    if (act == TC_ACT_PLAY && !tc_can_play(&v->s))
        return;
    tc_decide(&v->s, act);
    gui_reveal_start(&v->dealer_rv, TC_CARDS, NULL, DEAL_STAGGER);
    v->st = TS_REVEAL;
}

static bool can_raise_ante(const tcgui_t *v)
{
    const tc_session_t *s = &v->s;

    return s->ante < TC_ANTE_MAX &&
           s->ante + BET_STEP + s->pairplus <= s->bankroll;
}

static bool can_raise_pairplus(const tcgui_t *v)
{
    const tc_session_t *s = &v->s;

    return s->pairplus < TC_ANTE_MAX &&
           s->ante + s->pairplus + BET_STEP <= s->bankroll;
}

/* ---- drawing ------------------------------------------------------------ */

/* One "LABEL      value" row, the value flush right. */
static void draw_row(const gui_ctx_t *g, float x, float y, float w,
                     const char *label, const char *value, float size,
                     bool bold, Color col)
{
    gui_text(g, bold, label, x, y, size, col);
    gui_text(g, bold, value, x + w - gui_text_width(g, bold, value, size), y,
             size, col);
}

/*
 * Both pay tables, straight out of the engine: every row and every number
 * comes from tc_front_*, so the GUI cannot drift from the rules.
 */
static void draw_paytables(const gui_ctx_t *g, const tcgui_t *v)
{
    const tc_round_t *r = &v->s.round;
    bool  settled = v->st == TS_RESULT;
    char  val[16];
    float y;

    gui_text(g, true, "ANTE BONUS", PAY_X, 76, 24, GUI_GOLD);
    y = 106;
    for (int c = TC_NCATS - 1; c >= 0; c--) {
        int pay = tc_front_ante_bonus((tc_cat_t)c);
        bool hit;

        if (!pay)
            continue;
        hit = settled && r->ante_bonus > 0 && r->pev.cat == (tc_cat_t)c;
        snprintf(val, sizeof val, "%d", pay);
        if (hit)
            DrawRectangle((int)PAY_X - 6, (int)y - 3, PAY_W + 12, 26,
                          (Color){ 255, 210, 80, 60 });
        draw_row(g, PAY_X, y, PAY_W, tc_front_cat_name((tc_cat_t)c), val, 22,
                 hit, hit ? GUI_GOLD : GUI_CREAM);
        y += 26;
    }

    gui_text(g, true, "PAIR PLUS", PAY_X, y + 22, 24, GUI_GOLD);
    y += 52;
    for (int c = TC_NCATS - 1; c >= 0; c--) {
        int pay = tc_front_pairplus((tc_cat_t)c);
        bool hit;

        if (!pay)
            continue;
        hit = settled && r->pairplus > 0 && r->pairplus_net > 0 &&
              r->pev.cat == (tc_cat_t)c;
        snprintf(val, sizeof val, "%d", pay);
        if (hit)
            DrawRectangle((int)PAY_X - 6, (int)y - 3, PAY_W + 12, 26,
                          (Color){ 255, 210, 80, 60 });
        draw_row(g, PAY_X, y, PAY_W, tc_front_cat_name((tc_cat_t)c), val, 22,
                 hit, hit ? GUI_GOLD : GUI_CREAM);
        y += 26;
    }
}

static void draw_hands(const gui_ctx_t *g, const tcgui_t *v)
{
    const tc_round_t *r = &v->s.round;
    char line[64];

    gui_text_centered(g, true, "DEALER", TABLE_CX, DEALER_LBL_Y, 26,
                      GUI_CREAM);
    for (int i = 0; i < TC_CARDS; i++) {
        Rectangle rc = gui_card_row(g, i, TC_CARDS, CARD_H, TABLE_CX,
                                    DEALER_Y, CARD_GAP);
        bool up = v->has_round &&
                  (v->st == TS_RESULT ||
                   (v->st == TS_REVEAL && gui_reveal_shown(&v->dealer_rv, i)));
        gui_draw_card(g, rc, up ? &r->dealer[i] : NULL);
    }
    if (v->st == TS_RESULT) {
        snprintf(line, sizeof line, "DEALER: %s",
                 tc_front_cat_token(r->dev.cat));
        gui_text_centered(g, true, line, TABLE_CX, DEALER_Y + CARD_H + 6, 26,
                          GUI_CREAM);
        gui_text_centered(g, true,
                          r->dealer_qualifies ? "DEALER QUALIFIES"
                                              : "DEALER DOES NOT QUALIFY",
                          TABLE_CX, DEALER_Y + CARD_H + 34, 24,
                          r->dealer_qualifies ? GUI_DIM : GUI_GOLD);
    }

    gui_text_centered(g, true, "PLAYER", TABLE_CX, PLAYER_LBL_Y, 26,
                      GUI_CREAM);
    for (int i = 0; i < TC_CARDS; i++) {
        Rectangle rc = gui_card_row(g, i, TC_CARDS, CARD_H, TABLE_CX,
                                    PLAYER_Y, CARD_GAP);
        bool up = v->has_round &&
                  (v->st != TS_DEALING || gui_reveal_shown(&v->player_rv, i));
        gui_draw_card(g, rc, up ? &r->player[i] : NULL);
    }
    if (v->has_round && v->st != TS_DEALING) {
        snprintf(line, sizeof line, "PLAYER: %s",
                 tc_front_cat_token(r->pev.cat));
        gui_text_centered(g, true, line, TABLE_CX, PLAYER_Y + CARD_H + 6, 26,
                          GUI_GOLD);
    }
}

static void draw_status(const gui_ctx_t *g, const tcgui_t *v)
{
    char buf[64], money[24];

    tc_front_credits(money, sizeof money, v->view_bankroll, false);
    snprintf(buf, sizeof buf, "BANKROLL: %s", money);
    gui_text(g, true, buf, 40, STATUS_Y, 26, GUI_CREAM);

    snprintf(buf, sizeof buf, "ANTE: %ld   PAIR+: %ld", v->s.ante,
             v->s.pairplus);
    gui_text_centered(g, true, buf, TABLE_CX, STATUS_Y, 26, GUI_CREAM);
}

/* The payout breakdown, one line per wager, exactly as the engine
 * settled it. */
static void draw_breakdown(const gui_ctx_t *g, const tcgui_t *v)
{
    const tc_round_t *r = &v->s.round;
    static const char *const LBL[] = {
        "ANTE", "PLAY", "ANTE BONUS", "PAIR PLUS"
    };
    long vals[4];
    char val[24];
    float y = 106;

    vals[0] = r->ante_net;
    vals[1] = r->play_net;
    vals[2] = r->ante_bonus;
    vals[3] = r->pairplus_net;

    gui_text(g, true, tc_front_outcome_word(r->outcome), PANEL_X, 76, 24,
             GUI_GOLD);
    for (int i = 0; i < 4; i++) {
        tc_front_credits(val, sizeof val, vals[i], true);
        draw_row(g, PANEL_X, y, PANEL_W, LBL[i], val, 22, false,
                 vals[i] > 0 ? GUI_GOLD : GUI_CREAM);
        y += 26;
    }
    DrawRectangle(PANEL_X, (int)y + 4, PANEL_W, 2, GUI_DIM);
    tc_front_credits(val, sizeof val, r->returned - r->wagered, true);
    draw_row(g, PANEL_X, y + 14, PANEL_W, "NET", val, 26, true,
             r->returned > r->wagered ? GUI_GOLD : GUI_CREAM);
}

/* ---- one frame ---------------------------------------------------------- */

static void tc_frame(const gui_ctx_t *g, void *state)
{
    tcgui_t *v = state;

    /* ---- animation ---- */
    if (v->st == TS_DEALING && gui_reveal_update(&v->player_rv, g))
        v->st = TS_DECISION;
    if (v->st == TS_REVEAL && gui_reveal_update(&v->dealer_rv, g)) {
        v->st = TS_RESULT;
        v->view_bankroll = v->s.bankroll;
    }

    /* ---- keyboard ---- */
    if (betting(v)) {
        if (IsKeyPressed(KEY_RIGHT) && can_raise_ante(v)) {
            tc_set_ante(&v->s, v->s.ante + BET_STEP);
            gui_play_click(g);
        }
        if (IsKeyPressed(KEY_LEFT) && v->s.ante > TC_ANTE_MIN) {
            tc_set_ante(&v->s, v->s.ante - BET_STEP);
            gui_play_click(g);
        }
        if (IsKeyPressed(KEY_UP) && can_raise_pairplus(v)) {
            tc_set_pairplus(&v->s, v->s.pairplus + BET_STEP);
            gui_play_click(g);
        }
        if (IsKeyPressed(KEY_DOWN) && v->s.pairplus > 0) {
            tc_set_pairplus(&v->s, v->s.pairplus - BET_STEP);
            gui_play_click(g);
        }
        if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) &&
            tc_can_deal(&v->s)) {
            gui_play_click(g);
            start_deal(v);
        }
        if (IsKeyPressed(KEY_N) && !tc_can_deal(&v->s)) {
            gui_play_click(g);
            tc_bankroll_reset(&v->s);
            v->view_bankroll = v->s.bankroll;
        }
    } else if (v->st == TS_DECISION) {
        if (IsKeyPressed(KEY_P) && tc_can_play(&v->s)) {
            gui_play_click(g);
            decide(v, TC_ACT_PLAY);
        }
        if (IsKeyPressed(KEY_F)) {
            gui_play_click(g);
            decide(v, TC_ACT_FOLD);
        }
    }

    /* ---- draw ---- */
    DrawRectangle(0, 0, GUI_CANVAS_W, 52, GUI_FELT_DARK);
    gui_text_centered(g, true, "THREE CARD POKER", GUI_CANVAS_W / 2, 10, 34,
                      GUI_GOLD);

    draw_paytables(g, v);
    draw_hands(g, v);
    draw_status(g, v);
    if (v->st == TS_RESULT)
        draw_breakdown(g, v);

    /* ---- controls ---- */
    if (v->st == TS_DECISION) {
        bool can = tc_can_play(&v->s);

        gui_text_centered(g, true, "PLAY OR FOLD", TABLE_CX, MSG_Y, 28,
                          GUI_GOLD);
        if (gui_button(g, (Rectangle){ 400, BTN_Y, 200, BTN_H }, "PLAY",
                       can))
            decide(v, TC_ACT_PLAY);
        if (gui_button(g, (Rectangle){ 680, BTN_Y, 200, BTN_H }, "FOLD",
                       true))
            decide(v, TC_ACT_FOLD);
        gui_text_centered(g, false,
                          can ? "P PLAYS - F FOLDS"
                              : "NOT ENOUGH CREDITS TO PLAY - F FOLDS",
                          TABLE_CX, HINT_Y, 22, GUI_DIM);
    } else if (betting(v)) {
        if (gui_button(g, (Rectangle){ 248, BTN_Y, 130, BTN_H }, "ANTE -",
                       v->s.ante > TC_ANTE_MIN))
            tc_set_ante(&v->s, v->s.ante - BET_STEP);
        if (gui_button(g, (Rectangle){ 394, BTN_Y, 130, BTN_H }, "ANTE +",
                       can_raise_ante(v)))
            tc_set_ante(&v->s, v->s.ante + BET_STEP);
        if (gui_button(g, (Rectangle){ 540, BTN_Y, 130, BTN_H }, "PAIR+ -",
                       v->s.pairplus > 0))
            tc_set_pairplus(&v->s, v->s.pairplus - BET_STEP);
        if (gui_button(g, (Rectangle){ 686, BTN_Y, 130, BTN_H }, "PAIR+ +",
                       can_raise_pairplus(v)))
            tc_set_pairplus(&v->s, v->s.pairplus + BET_STEP);
        if (gui_button(g, (Rectangle){ 832, BTN_Y, 200, BTN_H }, "DEAL",
                       tc_can_deal(&v->s)))
            start_deal(v);

        if (!tc_can_deal(&v->s)) {
            gui_text_centered(g, true, "OUT OF CREDITS - PRESS N", TABLE_CX,
                              MSG_Y, 28, GUI_GOLD);
        } else if (v->st == TS_BET) {
            gui_text_centered(g, false,
                              "SPACE DEALS - ARROWS SET ANTE AND PAIR+",
                              TABLE_CX, HINT_Y, 22, GUI_DIM);
        }
    }
}

int tc_gui_run(rng_t *rng)
{
    tcgui_t v = { 0 };

    v.rng = rng;
    v.st = TS_BET;
    tc_session_start(&v.s);
    v.view_bankroll = v.s.bankroll;

    return gui_run("casino - three card poker", tc_frame, &v);
}
