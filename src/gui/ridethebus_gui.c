#include "ridethebus_gui.h"

#include <stdio.h>

#include "gui.h"
#include "cards.h"
#include "games/ridethebus.h"

/* ---- layout ------------------------------------------------------------- */

#define CARD_H       230
#define CARD_GAP     24
#define CARD_Y       116

#define BTN_W        200
#define BTN_H        52
#define BTN_GAP      20
#define BTN_Y        470

#define BET_Y        656
#define BET_BTN_W    120
#define BET_BTN_H    44

#define DEAL_LEAD    0.14       /* beat before the new card lands */
#define DEAL_STAGGER 0.08
/* Only long enough to stop the click that ended a game from rolling
 * straight into the next one; the outcome itself stays on screen. */
#define RESULT_HOLD  0.3

/* GUI-session bankroll; the CLI wagering model is untouched. */
#define BANKROLL_START 1000
#define BET_STEP       25
#define BET_MIN        1

typedef enum { RS_BETTING, RS_DEALING, RS_CHOOSING, RS_RESULT } rstate_t;

typedef struct {
    rstate_t     st;
    rng_t       *rng;
    rtb_game_t   g;
    gui_reveal_t reveal;
    bool         shown[RTB_ROUNDS];  /* cards already on the table */
    /*
     * What the table is showing.  The engine resolves a round the moment
     * the choice is made, so the felt tracks the revealed cards instead:
     * until the new card lands the player still sees the round they just
     * played, and no payout or range gives the result away early.
     */
    rtb_stage_t  view_stage;
    long         view_payout;
    double       deal_at;            /* >0 while the next card is pending */
    double       result_until;
    long         bankroll;
    long         bet;
    char         result_text[32];
} rtbgui_t;

/* ---- bankroll ----------------------------------------------------------- */

static long bet_ceiling(const rtbgui_t *v)
{
    return v->bankroll < RTB_BET_MAX ? v->bankroll : RTB_BET_MAX;
}

static void clamp_bet(rtbgui_t *v)
{
    long hi = bet_ceiling(v);

    if (v->bet > hi)
        v->bet = hi;
    if (v->bet < BET_MIN)
        v->bet = BET_MIN;
}

static void change_bet(rtbgui_t *v, const gui_ctx_t *g, long delta)
{
    long before = v->bet;

    v->bet += delta;
    clamp_bet(v);
    if (v->bet != before)
        gui_play_click(g);
}

/* ---- game flow ---------------------------------------------------------- */

/* The engine has already dealt and resolved the card; the GUI only holds
 * it back for a beat so the reveal reads as a deal.  Cards already on the
 * table are passed as `already`, so none of them animates twice. */
static void deal_next(rtbgui_t *v)
{
    v->deal_at = GetTime() + DEAL_LEAD;
    v->st = RS_DEALING;
}

static void end_game(rtbgui_t *v, const char *text, long credit)
{
    v->bankroll += credit;
    snprintf(v->result_text, sizeof v->result_text, "%s", text);
    v->result_until = GetTime() + RESULT_HOLD;
    v->st = RS_RESULT;
}

/* Called once the new card is face up. */
static void resolve_step(rtbgui_t *v)
{
    if (!rtb_front_over(&v->g)) {
        v->view_stage = rtb_front_stage(&v->g);
        v->view_payout = v->g.payout;
        v->st = RS_CHOOSING;
        return;
    }
    if (v->g.outcome == RTB_BUS)
        end_game(v, "YOU RODE THE BUS!", v->g.payout);
    else
        end_game(v, "YOU LOSE", 0);
}

/* Round 1: picking a colour is the whole bet-and-deal action. */
static void start_game(rtbgui_t *v, int guess)
{
    if (v->bankroll < v->bet)
        return;

    v->bankroll -= v->bet;
    rtb_front_start(&v->g, v->bet, v->rng);
    for (int i = 0; i < RTB_ROUNDS; i++)
        v->shown[i] = false;
    gui_reveal_start(&v->reveal, 0, NULL, DEAL_STAGGER);   /* clear */
    v->result_text[0] = '\0';
    v->view_stage = RTB_RED_BLACK;
    v->view_payout = 0;

    rtb_front_guess(&v->g, guess);
    deal_next(v);
}

/* Both buttons and keys land here: choosing is what keeps the game
 * going, there is no separate ride confirmation. */
static void act_guess(rtbgui_t *v, int guess)
{
    if (v->st == RS_BETTING) {
        start_game(v, guess);
        return;
    }
    if (v->st != RS_CHOOSING)
        return;
    rtb_front_guess(&v->g, guess);
    deal_next(v);
}

static void act_cash(rtbgui_t *v)
{
    if (v->st != RS_CHOOSING || !rtb_front_can_cash(&v->g))
        return;
    rtb_front_cash_out(&v->g);
    end_game(v, "CASHED OUT", v->g.payout);
}

/* ---- drawing ------------------------------------------------------------ */

static void upper(const char *s, char *buf, size_t len)
{
    size_t i = 0;

    for (; s[i] && i + 1 < len; i++)
        buf[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    buf[i] = '\0';
}

/* Four fixed slots: dealt cards keep their place for the whole game and
 * the rest of the deck waits face down. */
static void draw_cards(const gui_ctx_t *g, const rtbgui_t *v)
{
    for (int i = 0; i < RTB_ROUNDS; i++) {
        Rectangle r = gui_card_rect(g, i, RTB_ROUNDS, CARD_H, CARD_Y,
                                    CARD_GAP);
        bool face_up = i < v->g.ncards &&
                       (v->shown[i] || gui_reveal_shown(&v->reveal, i));

        gui_draw_card(g, r, face_up ? &v->g.cards[i] : NULL);
    }
}

/* Just enough context for the round in play. */
static void draw_context(const gui_ctx_t *g, const rtbgui_t *v,
                         rtb_stage_t st)
{
    char line[64], a[8], b[8];

    if (v->st == RS_BETTING) {
        snprintf(line, sizeof line, "PLACE YOUR BET AND CALL THE COLOUR");
    } else if (v->st == RS_RESULT) {
        return;
    } else if (st == RTB_HIGH_LOW) {
        card_name(v->g.cards[0], a, sizeof a);
        snprintf(line, sizeof line, "CURRENT CARD: %s", a);
    } else if (st == RTB_INSIDE_OUTSIDE) {
        card_t lo, hi;
        rtb_front_range(&v->g, &lo, &hi);
        card_name(lo, a, sizeof a);
        card_name(hi, b, sizeof b);
        snprintf(line, sizeof line, "RANGE: %s - %s", a, b);
    } else if (st == RTB_SUIT) {
        snprintf(line, sizeof line, "PICK THE SUIT");
    } else {
        return;
    }
    gui_text_centered(g, false, line, GUI_CANVAS_W / 2, 366, 24, GUI_DIM);
}

static Rectangle btn_rect(int i, int n)
{
    float total = n * BTN_W + (n - 1) * BTN_GAP;

    return (Rectangle){ (GUI_CANVAS_W - total) / 2 + i * (BTN_W + BTN_GAP),
                        BTN_Y, BTN_W, BTN_H };
}

/*
 * Cash out (from round 2 on) sits beside this round's own guesses, whose
 * labels come from the engine so they always match its guess values.
 */
static void draw_choices(const gui_ctx_t *g, rtbgui_t *v, rtb_stage_t st)
{
    static const int KEYS[RTB_ROUNDS][4] = {
        { KEY_R, KEY_B, 0, 0 },             /* red, black */
        { KEY_H, KEY_L, 0, 0 },             /* higher, lower */
        { KEY_I, KEY_O, 0, 0 },             /* inside, outside */
        { KEY_C, KEY_D, KEY_H, KEY_S },     /* clubs .. spades */
    };
    bool live = v->st == RS_CHOOSING ||
                (v->st == RS_BETTING && v->bankroll >= v->bet);
    /* cash out is offered from round 2 on; keeping its slot while a card
     * is landing stops the button row from reflowing mid-deal */
    bool cash = v->st != RS_BETTING && st != RTB_RED_BLACK;
    int n = rtb_front_nchoices(st) + (cash ? 1 : 0);
    int slot = 0;
    char label[16];

    /* the finished game stays on screen, but its choices are gone */
    if (v->st == RS_RESULT || st >= RTB_COMPLETE)
        return;

    if (cash) {
        bool live_cash = v->st == RS_CHOOSING && rtb_front_can_cash(&v->g);
        if (gui_button(g, btn_rect(slot++, n), "CASH OUT", live_cash) ||
            (live_cash && IsKeyPressed(KEY_X)))
            act_cash(v);
    }
    for (int i = 0; i < rtb_front_nchoices(st); i++) {
        upper(rtb_front_guess_word(st, i), label, sizeof label);
        if (gui_button(g, btn_rect(slot++, n), label, live) ||
            (live && IsKeyPressed(KEYS[st][i])))
            act_guess(v, i);
    }
}

/* ---- one frame ---------------------------------------------------------- */

static void rtb_frame(const gui_ctx_t *gin, void *state)
{
    rtbgui_t *v = state;
    /* local copy so a click that dismisses the result is not also spent
     * on the buttons that appear underneath it */
    gui_ctx_t ctx = *gin;
    const gui_ctx_t *g = &ctx;
    /* between games the table is back at round 1, whatever the last game
     * happened to reach */
    rtb_stage_t st = v->st == RS_BETTING ? RTB_RED_BLACK : v->view_stage;
    char info[64];

    /* ---- timing: the pending card lands, then the round is presented --- */
    if (v->st == RS_DEALING) {
        if (v->deal_at > 0) {
            if (GetTime() >= v->deal_at) {
                gui_reveal_start(&v->reveal, v->g.ncards, v->shown,
                                 DEAL_STAGGER);
                v->deal_at = 0;
            }
        } else if (gui_reveal_update(&v->reveal, g)) {
            v->shown[v->g.ncards - 1] = true;
            resolve_step(v);
        }
    }
    /* the result clears itself, or on any click or key so an impatient
     * player is never held up by it */
    if (v->st == RS_RESULT &&
        (GetTime() >= v->result_until || g->clicked ||
         IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))) {
        clamp_bet(v);
        v->st = RS_BETTING;
        ctx.clicked = false;
    }

    /* ---- betting input ---- */
    if (v->st == RS_BETTING) {
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_UP))
            change_bet(v, g, BET_STEP);
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_DOWN))
            change_bet(v, g, -BET_STEP);
        if (v->bankroll <= 0 && IsKeyPressed(KEY_N)) {
            v->bankroll = BANKROLL_START;   /* the RNG is left alone */
            clamp_bet(v);
            gui_play_click(g);
        }
    }

    /* ---- draw ---- */
    DrawRectangle(0, 0, GUI_CANVAS_W, 60, GUI_FELT_DARK);
    gui_text_centered(g, true, "RIDE THE BUS", GUI_CANVAS_W / 2, 14, 40,
                      GUI_GOLD);

    if (v->st != RS_RESULT && st < RTB_COMPLETE) {
        snprintf(info, sizeof info, "ROUND %d - %s", (int)st + 1,
                 rtb_front_stage_title(st));
        gui_text_centered(g, true, info, GUI_CANVAS_W / 2, 70, 28,
                          GUI_CREAM);
    }

    draw_cards(g, v);
    draw_context(g, v, st);

    if (v->st == RS_BETTING && v->bankroll < BET_MIN) {
        gui_text_centered(g, true, "OUT OF CREDITS - PRESS N",
                          GUI_CANVAS_W / 2, 404, 40, GUI_GOLD);
    } else if (v->result_text[0]) {
        /* the outcome stays up, with its cards, until the next game is
         * started: the table is ready to bet long before it clears */
        bool won = v->g.outcome != RTB_LOSS;
        if (won)
            snprintf(info, sizeof info, "%s  PAID %ld", v->result_text,
                     v->g.payout);
        else
            snprintf(info, sizeof info, "%s", v->result_text);
        gui_text_centered(g, true, info, GUI_CANVAS_W / 2, 404, 40,
                          won ? GUI_GOLD : GUI_CREAM);
    } else if (v->view_payout > 0 &&
               (v->st == RS_DEALING || v->st == RS_CHOOSING)) {
        snprintf(info, sizeof info, "CURRENT PAYOUT: %ld", v->view_payout);
        gui_text_centered(g, true, info, GUI_CANVAS_W / 2, 404, 40,
                          GUI_GOLD);
    }

    draw_choices(g, v, st);

    /* keyboard crib for the buttons on screen */
    if (v->st == RS_BETTING)
        gui_text_centered(g, false, "[R]ED  [B]LACK   BET: ARROW KEYS",
                          GUI_CANVAS_W / 2, 560, 20, GUI_DIM);
    else if (v->st == RS_CHOOSING && st == RTB_HIGH_LOW)
        gui_text_centered(g, false, "[H]IGHER  [L]OWER  CASH OUT [X]",
                          GUI_CANVAS_W / 2, 560, 20, GUI_DIM);
    else if (v->st == RS_CHOOSING && st == RTB_INSIDE_OUTSIDE)
        gui_text_centered(g, false, "[I]NSIDE  [O]UTSIDE  CASH OUT [X]",
                          GUI_CANVAS_W / 2, 560, 20, GUI_DIM);
    else if (v->st == RS_CHOOSING && st == RTB_SUIT)
        gui_text_centered(g, false,
                          "[C]LUBS  [D]IAMONDS  [H]EARTS  [S]PADES  "
                          "CASH OUT [X]",
                          GUI_CANVAS_W / 2, 560, 20, GUI_DIM);

    snprintf(info, sizeof info, "BANKROLL: %ld", v->bankroll);
    gui_text(g, true, info, 150, 606, 32, GUI_CREAM);
    snprintf(info, sizeof info, "BET: %ld", v->bet);
    gui_text(g, true, info, 1130 - gui_text_width(g, true, info, 32), 606,
             32, GUI_CREAM);

    bool betting = v->st == RS_BETTING;
    if (gui_button(g, (Rectangle){ 150, BET_Y, BET_BTN_W, BET_BTN_H },
                   "BET -", betting && v->bet > BET_MIN))
        change_bet(v, g, -BET_STEP);
    if (gui_button(g, (Rectangle){ 290, BET_Y, BET_BTN_W, BET_BTN_H },
                   "BET +", betting && v->bet < bet_ceiling(v)))
        change_bet(v, g, BET_STEP);
}

int rtb_gui_run(rng_t *rng)
{
    rtbgui_t v = { 0 };

    v.st = RS_BETTING;
    v.rng = rng;
    v.bankroll = BANKROLL_START;
    v.bet = RTB_BET_DEFAULT;
    clamp_bet(&v);

    return gui_run("casino - ride the bus", rtb_frame, &v);
}
