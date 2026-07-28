#include "caribbeanstud_gui.h"

#include <stdio.h>

#include "gui.h"
#include "cards.h"
#include "games/caribbeanstud.h"

/* ---- layout ------------------------------------------------------------- */

#define TABLE_CX       690          /* clear of the pay table on the left */

#define DEALER_LBL_Y    60
#define DEALER_Y        86
#define PLAYER_LBL_Y   296
#define PLAYER_Y       322
#define CARD_H         148
#define CARD_GAP        12

#define PAY_X           30
#define PAY_W          270
#define PANEL_X        1000         /* result breakdown, clear of the cards */
#define PANEL_W        250

#define STATUS_Y       518
#define MSG_Y          552
#define BTN_Y          600
#define BTN_H           48
#define HINT_Y         664

#define DEAL_STAGGER  0.12
#define BET_STEP         5

/*
 * The felt's own states.  CS_BET, CS_DECISION and CS_RESULT track the
 * engine's phases one for one; CS_DEALING and CS_REVEAL are animation
 * only and wrap a result the engine has already decided.
 */
typedef enum {
    CS_ST_BET,          /* setting the ante */
    CS_ST_DEALING,      /* the player's five cards are landing */
    CS_ST_DECISION,     /* raise or fold */
    CS_ST_REVEAL,       /* the dealer's four hole cards are turning over */
    CS_ST_RESULT        /* settled: breakdown on the felt */
} csstate_t;

typedef struct {
    csstate_t    st;
    rng_t       *rng;
    cs_session_t s;
    gui_reveal_t player_rv, dealer_rv;
    bool         has_round;         /* a hand has been dealt at least once */
    long         view_bankroll;     /* what the felt shows: never early */
} csgui_t;

/* ---- helpers ------------------------------------------------------------ */

static bool betting(const csgui_t *v)
{
    return v->st == CS_ST_BET || v->st == CS_ST_RESULT;
}

static void start_deal(csgui_t *v)
{
    if (!cs_can_deal(&v->s))
        return;
    cs_deal(&v->s, v->rng);
    gui_reveal_start(&v->player_rv, CS_CARDS, NULL, DEAL_STAGGER);
    v->has_round = true;
    v->view_bankroll = v->s.bankroll;
    v->st = CS_ST_DEALING;
}

/*
 * The engine settles the moment the decision is made; the felt catches up
 * as the hole cards turn over, so nothing gives the result away.  The
 * up-card is already face up and stays put.
 */
static void decide(csgui_t *v, cs_action_t act)
{
    static const bool already[CS_CARDS] = { true, false, false, false, false };

    if (v->st != CS_ST_DECISION)
        return;
    if (act == CS_ACT_RAISE && !cs_can_raise(&v->s))
        return;
    cs_decide(&v->s, act);
    gui_reveal_start(&v->dealer_rv, CS_CARDS, already, DEAL_STAGGER);
    v->st = CS_ST_REVEAL;
}

static bool can_raise_ante(const csgui_t *v)
{
    const cs_session_t *s = &v->s;
    long next = s->ante + BET_STEP;

    return next <= CS_ANTE_MAX &&
           next + cs_raise_amount(next) <= s->bankroll;
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
 * The raise pay table, straight out of the engine: every row and every
 * number comes from cs_raise_multiplier(), so the felt cannot drift from
 * the rules.  The winning row lights up.
 */
static void draw_paytable(const gui_ctx_t *g, const csgui_t *v)
{
    const cs_round_t *r = &v->s.round;
    bool  settled = v->st == CS_ST_RESULT;
    char  val[16];
    float y = 106;

    gui_text(g, true, "RAISE PAYS", PAY_X, 76, 24, GUI_GOLD);
    for (int c = POKER_ROYAL_FLUSH; c >= POKER_HIGH_CARD; c--) {
        bool hit = settled && r->raise_net > 0 && r->pev.cat == (poker_cat_t)c;

        snprintf(val, sizeof val, "%d:1",
                 cs_raise_multiplier((poker_cat_t)c));
        if (hit)
            DrawRectangle((int)PAY_X - 6, (int)y - 3, PAY_W + 12, 26,
                          (Color){ 255, 210, 80, 60 });
        draw_row(g, PAY_X, y, PAY_W, cs_front_cat_name((poker_cat_t)c), val,
                 22, hit, hit ? GUI_GOLD : GUI_CREAM);
        y += 26;
    }
    gui_text(g, false, "ANTE ALWAYS PAYS 1:1", PAY_X, y + 8, 20, GUI_DIM);
    gui_text(g, false, "DEALER NEEDS ACE-KING", PAY_X, y + 32, 20, GUI_DIM);
}

static void draw_hands(const gui_ctx_t *g, const csgui_t *v)
{
    const cs_round_t *r = &v->s.round;
    char line[64];

    gui_text_centered(g, true, "DEALER", TABLE_CX, DEALER_LBL_Y, 26,
                      GUI_CREAM);
    for (int i = 0; i < CS_CARDS; i++) {
        Rectangle rc = gui_card_row(g, i, CS_CARDS, CARD_H, TABLE_CX,
                                    DEALER_Y, CARD_GAP);
        /* the engine decides what is public; the animation only decides
         * when the felt catches up with it */
        const card_t *c = v->has_round ? cs_dealer_visible(r, i) : NULL;
        bool up = c && (v->st != CS_ST_REVEAL ||
                        gui_reveal_shown(&v->dealer_rv, i));

        gui_draw_card(g, rc, up ? c : NULL);
    }
    if (v->st == CS_ST_RESULT) {
        snprintf(line, sizeof line, "DEALER: %s",
                 cs_front_cat_token(r->dev.cat));
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
    for (int i = 0; i < CS_CARDS; i++) {
        Rectangle rc = gui_card_row(g, i, CS_CARDS, CARD_H, TABLE_CX,
                                    PLAYER_Y, CARD_GAP);
        bool up = v->has_round &&
                  (v->st != CS_ST_DEALING ||
                   gui_reveal_shown(&v->player_rv, i));

        gui_draw_card(g, rc, up ? &r->player[i] : NULL);
    }
    if (v->has_round && v->st != CS_ST_DEALING) {
        snprintf(line, sizeof line, "PLAYER: %s",
                 cs_front_cat_token(r->pev.cat));
        gui_text_centered(g, true, line, TABLE_CX, PLAYER_Y + CARD_H + 6, 26,
                          GUI_GOLD);
    }
}

static void draw_status(const gui_ctx_t *g, const csgui_t *v)
{
    char buf[80], money[24];

    cs_front_credits(money, sizeof money, v->view_bankroll, false);
    snprintf(buf, sizeof buf, "BANKROLL: %s", money);
    gui_text(g, true, buf, 40, STATUS_Y, 26, GUI_CREAM);

    snprintf(buf, sizeof buf, "ANTE: %ld   RAISE: %ld   EXPOSURE: %ld",
             v->s.ante, cs_raise_amount(v->s.ante), cs_max_exposure(&v->s));
    gui_text_centered(g, true, buf, TABLE_CX, STATUS_Y, 26, GUI_CREAM);
}

/* The payout breakdown, one line per wager, exactly as the engine
 * settled it. */
static void draw_breakdown(const gui_ctx_t *g, const csgui_t *v)
{
    const cs_round_t *r = &v->s.round;
    static const char *const LBL[] = { "ANTE", "RAISE" };
    long vals[2];
    char val[24];
    float y = 106;

    vals[0] = r->ante_net;
    vals[1] = r->raise_net;

    gui_text(g, true, cs_front_outcome_word(r->outcome), PANEL_X, 76, 24,
             GUI_GOLD);
    for (int i = 0; i < 2; i++) {
        cs_front_credits(val, sizeof val, vals[i], true);
        draw_row(g, PANEL_X, y, PANEL_W, LBL[i], val, 22, false,
                 vals[i] > 0 ? GUI_GOLD : GUI_CREAM);
        y += 26;
    }
    cs_front_credits(val, sizeof val, r->wagered, false);
    draw_row(g, PANEL_X, y, PANEL_W, "WAGERED", val, 22, false, GUI_CREAM);
    y += 26;
    cs_front_credits(val, sizeof val, r->returned, false);
    draw_row(g, PANEL_X, y, PANEL_W, "RETURNED", val, 22, false, GUI_CREAM);
    y += 26;

    DrawRectangle(PANEL_X, (int)y + 4, PANEL_W, 2, GUI_DIM);
    cs_front_credits(val, sizeof val, r->returned - r->wagered, true);
    draw_row(g, PANEL_X, y + 14, PANEL_W, "NET", val, 26, true,
             r->returned > r->wagered ? GUI_GOLD : GUI_CREAM);

    if (r->outcome == CS_OUT_FOLD)
        gui_text(g, false, "FOLDED - ANTE LOST", PANEL_X, y + 52, 20,
                 GUI_DIM);
    else if (r->outcome == CS_OUT_NO_QUALIFY)
        gui_text(g, false, "RAISE PUSHES", PANEL_X, y + 52, 20, GUI_DIM);
}

/* ---- one frame ---------------------------------------------------------- */

static void cs_frame(const gui_ctx_t *g, void *state)
{
    csgui_t *v = state;

    /* ---- animation ---- */
    if (v->st == CS_ST_DEALING && gui_reveal_update(&v->player_rv, g))
        v->st = CS_ST_DECISION;
    if (v->st == CS_ST_REVEAL && gui_reveal_update(&v->dealer_rv, g)) {
        v->st = CS_ST_RESULT;
        v->view_bankroll = v->s.bankroll;
    }

    /* ---- keyboard ---- */
    if (betting(v)) {
        if ((IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_EQUAL) ||
             IsKeyPressed(KEY_KP_ADD)) && can_raise_ante(v)) {
            cs_set_ante(&v->s, v->s.ante + BET_STEP);
            gui_play_click(g);
        }
        if ((IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_MINUS) ||
             IsKeyPressed(KEY_KP_SUBTRACT)) && v->s.ante > CS_ANTE_MIN) {
            cs_set_ante(&v->s, v->s.ante - BET_STEP);
            gui_play_click(g);
        }
        if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) &&
            cs_can_deal(&v->s)) {
            gui_play_click(g);
            start_deal(v);
        }
        if (IsKeyPressed(KEY_N) && !cs_can_deal(&v->s)) {
            gui_play_click(g);
            cs_bankroll_reset(&v->s);
            v->view_bankroll = v->s.bankroll;
        }
    } else if (v->st == CS_ST_DECISION) {
        if (IsKeyPressed(KEY_R) && cs_can_raise(&v->s)) {
            gui_play_click(g);
            decide(v, CS_ACT_RAISE);
        }
        if (IsKeyPressed(KEY_F)) {
            gui_play_click(g);
            decide(v, CS_ACT_FOLD);
        }
    }

    /* ---- draw ---- */
    DrawRectangle(0, 0, GUI_CANVAS_W, 52, GUI_FELT_DARK);
    gui_text_centered(g, true, "CARIBBEAN STUD POKER", GUI_CANVAS_W / 2, 10,
                      34, GUI_GOLD);

    draw_paytable(g, v);
    draw_hands(g, v);
    draw_status(g, v);
    if (v->st == CS_ST_RESULT)
        draw_breakdown(g, v);

    /* ---- controls ---- */
    if (v->st == CS_ST_DECISION) {
        char label[32];
        bool can = cs_can_raise(&v->s);

        snprintf(label, sizeof label, "RAISE %ld",
                 cs_raise_amount(v->s.round.ante));
        gui_text_centered(g, true, "RAISE OR FOLD", TABLE_CX, MSG_Y, 28,
                          GUI_GOLD);
        if (gui_button(g, (Rectangle){ 450, BTN_Y, 220, BTN_H }, label, can))
            decide(v, CS_ACT_RAISE);
        if (gui_button(g, (Rectangle){ 710, BTN_Y, 220, BTN_H }, "FOLD",
                       cs_can_fold(&v->s)))
            decide(v, CS_ACT_FOLD);
        gui_text_centered(g, false,
                          can ? "R RAISES 2X THE ANTE - F FOLDS"
                              : "NOT ENOUGH CREDITS TO RAISE - F FOLDS",
                          TABLE_CX, HINT_Y, 22, GUI_DIM);
    } else if (betting(v)) {
        if (gui_button(g, (Rectangle){ 390, BTN_Y, 150, BTN_H }, "ANTE -",
                       v->s.ante > CS_ANTE_MIN))
            cs_set_ante(&v->s, v->s.ante - BET_STEP);
        if (gui_button(g, (Rectangle){ 560, BTN_Y, 150, BTN_H }, "ANTE +",
                       can_raise_ante(v)))
            cs_set_ante(&v->s, v->s.ante + BET_STEP);
        if (gui_button(g, (Rectangle){ 730, BTN_Y, 220, BTN_H },
                       v->st == CS_ST_RESULT ? "DEAL AGAIN" : "DEAL",
                       cs_can_deal(&v->s)))
            start_deal(v);

        if (!cs_can_deal(&v->s)) {
            gui_text_centered(g, true, "OUT OF CREDITS - PRESS N", TABLE_CX,
                              MSG_Y, 28, GUI_GOLD);
        } else if (v->st == CS_ST_BET) {
            gui_text_centered(g, false, "SPACE DEALS - ARROWS SET THE ANTE",
                              TABLE_CX, HINT_Y, 22, GUI_DIM);
        }
    }
}

int cs_gui_run(rng_t *rng)
{
    csgui_t v = { 0 };

    v.rng = rng;
    v.st = CS_ST_BET;
    cs_session_start(&v.s);
    v.view_bankroll = v.s.bankroll;

    return gui_run("casino - caribbean stud poker", cs_frame, &v);
}
