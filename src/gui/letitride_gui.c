#include "letitride_gui.h"

#include <stdio.h>

#include "gui.h"
#include "cards.h"
#include "games/letitride.h"

/* ---- layout ------------------------------------------------------------- */

#define TABLE_CX       700          /* clear of the pay table on the left */

#define COMM_LBL_Y      60
#define COMM_Y          86
#define PLAYER_LBL_Y   240
#define PLAYER_Y       264
#define CARD_H         140
#define CARD_GAP        14

#define PAY_X           30
#define PAY_W          300
#define PANEL_X        990          /* result breakdown */
#define PANEL_W        260

#define WAGER_Y        444
#define WAGER_W        140
#define WAGER_H         60
#define WAGER_GAP       20

#define STATUS_Y       518
#define MSG_Y          552
#define BTN_Y          600
#define BTN_H           48
#define HINT_Y         664

#define DEAL_STAGGER  0.12
#define REVEAL_HOLD   0.10          /* beat before a community card lands */

#define BET_STEP         5

typedef enum {
    LS_BET,
    LS_DEALING,         /* the player's three cards are landing */
    LS_DECISION1,       /* pull bet 1, or let it ride */
    LS_REVEAL1,         /* the first community card is turning over */
    LS_DECISION2,       /* pull bet 2, or let it ride */
    LS_REVEAL2,         /* the second community card is turning over */
    LS_RESULT
} lstate_t;

typedef struct {
    lstate_t      st;
    rng_t        *rng;
    lir_session_t s;
    gui_reveal_t  rv;               /* the deal, then one card at a time */
    bool          comm_shown[LIR_COMMUNITY];
    bool          has_round;
    long          view_bankroll;    /* what the felt shows: never early */
} lirgui_t;

/* ---- helpers ------------------------------------------------------------ */

static bool betting(const lirgui_t *v)
{
    return v->st == LS_BET || v->st == LS_RESULT;
}

static void start_deal(lirgui_t *v)
{
    if (!lir_can_deal(&v->s))
        return;
    lir_deal(&v->s, v->rng);
    gui_reveal_start(&v->rv, LIR_PLAYER_CARDS, NULL, DEAL_STAGGER);
    v->comm_shown[0] = v->comm_shown[1] = false;
    v->has_round = true;
    v->view_bankroll = v->s.bankroll;
    v->st = LS_DEALING;
}

/*
 * Hand the decision to the engine, then let the community card it turned
 * over arrive with the usual deal animation.  The engine settles on the
 * second decision, so the bankroll on the felt waits for the reveal.
 */
static void decide(lirgui_t *v, bool pull)
{
    if (v->st == LS_DECISION1) {
        lir_decide(&v->s, pull);
        gui_reveal_start(&v->rv, 1, NULL, REVEAL_HOLD);
        v->st = LS_REVEAL1;
        v->view_bankroll = v->s.bankroll;   /* only a pull refund so far */
    } else if (v->st == LS_DECISION2) {
        lir_decide(&v->s, pull);
        gui_reveal_start(&v->rv, 1, NULL, REVEAL_HOLD);
        v->st = LS_REVEAL2;
    }
}

static bool can_raise_bet(const lirgui_t *v)
{
    return v->s.bet < LIR_BET_MAX &&
           (v->s.bet + BET_STEP) * LIR_BETS <= v->s.bankroll;
}

/* What each wager brings back, from the engine's own pay table. */
static long bet_return(const lir_round_t *r, int i)
{
    int pay = lir_front_payout(r->cat);

    if (!r->riding[i] || pay == 0)
        return 0;
    return r->bet + (long)pay * r->bet;
}

/* ---- drawing ------------------------------------------------------------ */

static void draw_row(const gui_ctx_t *g, float x, float y, float w,
                     const char *label, const char *value, float size,
                     bool bold, Color col)
{
    gui_text(g, bold, label, x, y, size, col);
    gui_text(g, bold, value, x + w - gui_text_width(g, bold, value, size), y,
             size, col);
}

/* Every row and every number comes from lir_front_*, so the GUI cannot
 * drift from the engine's pay table. */
static void draw_paytable(const gui_ctx_t *g, const lirgui_t *v)
{
    char  val[16];
    float y = 102;

    gui_text(g, true, "PAYS PER WAGER", PAY_X, 74, 24, GUI_GOLD);
    for (int c = LIR_NCATS - 1; c >= 1; c--) {
        bool hit = v->st == LS_RESULT && v->s.round.cat == (lir_cat_t)c;

        snprintf(val, sizeof val, "%d:1", lir_front_payout((lir_cat_t)c));
        if (hit)
            DrawRectangle((int)PAY_X - 6, (int)y - 3, PAY_W + 12, 26,
                          (Color){ 255, 210, 80, 60 });
        draw_row(g, PAY_X, y, PAY_W, lir_front_cat_token((lir_cat_t)c), val,
                 21, hit, hit ? GUI_GOLD : GUI_CREAM);
        y += 26;
    }
    gui_text(g, false, "BELOW A PAIR OF TENS LOSES", PAY_X, y + 6, 19,
             GUI_DIM);
}

static void draw_cards(const gui_ctx_t *g, const lirgui_t *v)
{
    const lir_round_t *r = &v->s.round;

    gui_text_centered(g, true, "COMMUNITY", TABLE_CX, COMM_LBL_Y, 26,
                      GUI_CREAM);
    for (int i = 0; i < LIR_COMMUNITY; i++) {
        Rectangle rc = gui_card_row(g, i, LIR_COMMUNITY, CARD_H, TABLE_CX,
                                    COMM_Y, CARD_GAP);
        /* the engine gate and the animation gate must both agree */
        const card_t *c = v->has_round && v->comm_shown[i]
                              ? lir_community_visible(r, i) : NULL;
        gui_draw_card(g, rc, c);
    }

    gui_text_centered(g, true, "PLAYER", TABLE_CX, PLAYER_LBL_Y, 26,
                      GUI_CREAM);
    for (int i = 0; i < LIR_PLAYER_CARDS; i++) {
        Rectangle rc = gui_card_row(g, i, LIR_PLAYER_CARDS, CARD_H,
                                    TABLE_CX, PLAYER_Y, CARD_GAP);
        bool up = v->has_round &&
                  (v->st != LS_DEALING || gui_reveal_shown(&v->rv, i));
        gui_draw_card(g, rc, up ? &r->player[i] : NULL);
    }
}

/* The three wagers, with a pulled one clearly out of the game. */
static void draw_wagers(const gui_ctx_t *g, const lirgui_t *v)
{
    const lir_round_t *r = &v->s.round;
    float total = LIR_BETS * WAGER_W + (LIR_BETS - 1) * WAGER_GAP;
    float x0 = TABLE_CX - total / 2;
    char  label[16], amount[24];

    for (int i = 0; i < LIR_BETS; i++) {
        Rectangle box = { x0 + i * (WAGER_W + WAGER_GAP), WAGER_Y, WAGER_W,
                          WAGER_H };
        bool riding = !v->has_round || r->riding[i];
        Color edge = riding ? GUI_GOLD : GUI_DIM;

        DrawRectangleRec(box, GUI_FELT_DARK);
        DrawRectangleLinesEx(box, 2, edge);
        snprintf(label, sizeof label, "BET %d", i + 1);
        gui_text_centered(g, true, label, box.x + box.width / 2, box.y + 8,
                          22, riding ? GUI_CREAM : GUI_DIM);
        /* the session wager is locked while a decision is pending, so this
         * is the round's own stake mid-hand and the next stake once the
         * bet buttons come back */
        if (riding)
            snprintf(amount, sizeof amount, "%ld", v->s.bet);
        else
            snprintf(amount, sizeof amount, "PULLED");
        gui_text_centered(g, true, amount, box.x + box.width / 2,
                          box.y + 32, 24, riding ? GUI_GOLD : GUI_DIM);
    }
}

static void draw_status(const gui_ctx_t *g, const lirgui_t *v)
{
    const lir_round_t *r = &v->s.round;
    char buf[64], money[24];
    long at_risk = v->has_round ? (long)r->nriding * r->bet
                                : (long)LIR_BETS * v->s.bet;

    lir_front_credits(money, sizeof money, v->view_bankroll, false);
    snprintf(buf, sizeof buf, "BANKROLL: %s", money);
    gui_text(g, true, buf, 40, STATUS_Y, 26, GUI_CREAM);

    lir_front_credits(money, sizeof money, at_risk, false);
    snprintf(buf, sizeof buf, "COMMITTED: %s", money);
    gui_text_centered(g, true, buf, TABLE_CX, STATUS_Y, 26, GUI_CREAM);
}

static void draw_result(const gui_ctx_t *g, const lirgui_t *v)
{
    const lir_round_t *r = &v->s.round;
    char  line[48], val[24];
    float y = 74;
    int   pay = lir_front_payout(r->cat);

    gui_text(g, true, lir_front_cat_token(r->cat), PANEL_X, y, 24,
             pay > 0 ? GUI_GOLD : GUI_CREAM);
    y += 28;
    if (pay > 0)
        snprintf(line, sizeof line, "PAYS %d:1", pay);
    else
        snprintf(line, sizeof line, "NO PAY");
    gui_text(g, false, line, PANEL_X, y, 22, GUI_DIM);
    y += 40;

    for (int i = 0; i < LIR_BETS; i++) {
        long got = bet_return(r, i);

        snprintf(line, sizeof line, "BET %d", i + 1);
        if (!r->riding[i])
            snprintf(val, sizeof val, "PULLED");
        else
            lir_front_credits(val, sizeof val, got, false);
        draw_row(g, PANEL_X, y, PANEL_W, line, val, 22,
                 got > 0, got > 0 ? GUI_GOLD : GUI_CREAM);
        y += 26;
    }

    DrawRectangle(PANEL_X, (int)y + 6, PANEL_W, 2, GUI_DIM);
    lir_front_credits(val, sizeof val, r->returned - r->wagered, true);
    draw_row(g, PANEL_X, y + 16, PANEL_W, "NET", val, 26, true,
             r->returned > r->wagered ? GUI_GOLD : GUI_CREAM);
}

/* ---- one frame ---------------------------------------------------------- */

static void lir_frame(const gui_ctx_t *g, void *state)
{
    lirgui_t *v = state;

    /* ---- animation ---- */
    if (v->st == LS_DEALING && gui_reveal_update(&v->rv, g))
        v->st = LS_DECISION1;
    if (v->st == LS_REVEAL1 && gui_reveal_update(&v->rv, g)) {
        v->comm_shown[0] = true;
        v->st = LS_DECISION2;
    }
    if (v->st == LS_REVEAL2 && gui_reveal_update(&v->rv, g)) {
        v->comm_shown[1] = true;
        v->view_bankroll = v->s.bankroll;
        v->st = LS_RESULT;
    }

    /* ---- keyboard ---- */
    if (betting(v)) {
        if (IsKeyPressed(KEY_RIGHT) && can_raise_bet(v)) {
            lir_set_bet(&v->s, v->s.bet + BET_STEP);
            gui_play_click(g);
        }
        if (IsKeyPressed(KEY_LEFT) && v->s.bet > LIR_BET_MIN) {
            lir_set_bet(&v->s, v->s.bet - BET_STEP);
            gui_play_click(g);
        }
        if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) &&
            lir_can_deal(&v->s)) {
            gui_play_click(g);
            start_deal(v);
        }
        if (IsKeyPressed(KEY_N) && !lir_can_deal(&v->s)) {
            gui_play_click(g);
            lir_bankroll_reset(&v->s);
            v->view_bankroll = v->s.bankroll;
        }
    } else if (v->st == LS_DECISION1 || v->st == LS_DECISION2) {
        if (IsKeyPressed(KEY_R)) {
            gui_play_click(g);
            decide(v, false);
        } else if (IsKeyPressed(KEY_P)) {
            gui_play_click(g);
            decide(v, true);
        }
    }

    /* ---- draw ---- */
    DrawRectangle(0, 0, GUI_CANVAS_W, 52, GUI_FELT_DARK);
    gui_text_centered(g, true, "LET IT RIDE", GUI_CANVAS_W / 2, 10, 34,
                      GUI_GOLD);

    draw_paytable(g, v);
    draw_cards(g, v);
    draw_wagers(g, v);
    draw_status(g, v);
    if (v->st == LS_RESULT)
        draw_result(g, v);

    /* ---- controls ---- */
    if (v->st == LS_DECISION1 || v->st == LS_DECISION2) {
        int n = v->st == LS_DECISION1 ? 1 : 2;
        char pull[24];

        snprintf(pull, sizeof pull, "PULL BET %d", n);
        gui_text_centered(g, true,
                          n == 1 ? "PULL BET 1, OR LET IT RIDE"
                                 : "PULL BET 2, OR LET IT RIDE",
                          TABLE_CX, MSG_Y, 26, GUI_GOLD);
        if (gui_button(g, (Rectangle){ 440, BTN_Y, 230, BTN_H }, pull, true))
            decide(v, true);
        if (gui_button(g, (Rectangle){ 700, BTN_Y, 230, BTN_H },
                       "LET IT RIDE", true))
            decide(v, false);
        gui_text_centered(g, false, "R RIDES - P PULLS", TABLE_CX, HINT_Y,
                          22, GUI_DIM);
    } else if (betting(v)) {
        if (gui_button(g, (Rectangle){ 440, BTN_Y, 130, BTN_H }, "BET -",
                       v->s.bet > LIR_BET_MIN))
            lir_set_bet(&v->s, v->s.bet - BET_STEP);
        if (gui_button(g, (Rectangle){ 586, BTN_Y, 130, BTN_H }, "BET +",
                       can_raise_bet(v)))
            lir_set_bet(&v->s, v->s.bet + BET_STEP);
        if (gui_button(g, (Rectangle){ 732, BTN_Y, 200, BTN_H }, "DEAL",
                       lir_can_deal(&v->s)))
            start_deal(v);

        if (!lir_can_deal(&v->s))
            gui_text_centered(g, true, "OUT OF CREDITS - PRESS N",
                              TABLE_CX, MSG_Y, 28, GUI_GOLD);
        else if (v->st == LS_BET)
            gui_text_centered(g, false,
                              "SPACE DEALS - ARROWS SET EACH WAGER",
                              TABLE_CX, HINT_Y, 22, GUI_DIM);
    }
}

int lir_gui_run(rng_t *rng)
{
    lirgui_t v = { 0 };

    v.rng = rng;
    v.st = LS_BET;
    lir_session_start(&v.s);
    v.view_bankroll = v.s.bankroll;

    return gui_run("casino - let it ride", lir_frame, &v);
}
