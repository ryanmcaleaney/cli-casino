#include "blackjack_gui.h"

#include <stdio.h>

#include "gui.h"
#include "cards.h"
#include "games/bj_strategy.h"
#include "games/blackjack.h"

/* ---- layout ------------------------------------------------------------- */

#define DEALER_CX     640
#define DEALER_Y      96
#define DEALER_CARD_H 170

#define PLAYER_Y      330
#define PLAYER_CARD_H 170
#define SPLIT_CARD_H  120          /* smaller when several hands share the felt */
#define CARD_GAP      14

#define DEAL_STAGGER  0.10
#define SHUFFLE_MSG   1.0          /* seconds the shuffle banner stays up */

#define BTN_Y   648
#define BTN_H   48

/* Trainer panel: left felt, clear of the dealer's two cards (which sit
 * around x 463..817 while the player is acting), the player rows below
 * y 330, the centred banners and the status line. */
#define STRAT_X       40
#define STRAT_Y       110
#define FEEDBACK_HOLD 1.2          /* seconds the verdict stays up */

typedef struct {
    rng_t        *rng;
    bj_session_t  s;
    gui_reveal_t  reveal;          /* opening deal: player, dealer, ... */
    bool          dealing;
    int           seen_cards;      /* cards already announced with a sound */
    double        shuffle_until;
    bool          counting;        /* --counting Hi-Lo trainer */
    bj_count_t    count;
    /* basic-strategy trainer, --counting only.  The recommendation itself
     * is never stored: it is read back from the engine each frame. */
    long          strat_total;     /* graded decisions this process */
    long          strat_correct;
    bj_strategy_action_t fb_rec;   /* the advice the last decision faced */
    bool          fb_correct;
    bool          fb_valid;
    double        fb_until;
} bjgui_t;

/* ---- helpers ------------------------------------------------------------ */

static int round_card_count(const bj_session_t *s)
{
    return bj_round_cards(s);
}

static void start_deal(bjgui_t *v)
{
    if (!bj_can_deal(&v->s))
        return;
    bj_deal(&v->s, v->rng);
    if (v->s.shuffled) {
        v->shuffle_until = GetTime() + SHUFFLE_MSG;
        bj_count_reset(&v->count);   /* a fresh shoe starts from zero */
    }
    /* opening deal is player, dealer, player, dealer hole */
    gui_reveal_start(&v->reveal, 4, NULL, DEAL_STAGGER);
    v->dealing = true;
    v->seen_cards = round_card_count(&v->s);
}

/* Any card added after the opening deal (hits, doubles, split draws and
 * the dealer's own draws) just clicks in with the deal sound. */
static void note_new_cards(bjgui_t *v, const gui_ctx_t *g)
{
    int n = round_card_count(&v->s);

    if (n > v->seen_cards) {
        PlaySound(g->as->snd_deal);
        v->seen_cards = n;
    }
}

/*
 * The one path a player action takes, from the keyboard or from a button.
 * Grading happens against the advice for the state the action is taken
 * from, so it must be read before the engine moves on.  `click` is false
 * for the button path because gui_button() has already made the sound.
 * Card sounds stay with note_new_cards().
 */
static void perform_player_action(bjgui_t *v, const gui_ctx_t *g,
                                  bj_action_t a, bool click)
{
    if (!bj_legal(&v->s, a))
        return;                     /* dead key or disabled button */

    if (v->counting) {
        bj_strategy_action_t rec = bj_basic_strategy(&v->s);

        if (rec != BJ_STRAT_NONE) {
            bool ok = bj_strategy_agrees(rec, a);

            v->strat_total++;
            if (ok)
                v->strat_correct++;
            v->fb_rec = rec;
            v->fb_correct = ok;
            v->fb_valid = true;
            v->fb_until = GetTime() + FEEDBACK_HOLD;
        }
    }
    if (click)
        gui_play_click(g);
    bj_act(&v->s, a, v->rng);
}

/* During the opening animation only the first `shown` cards exist. */
static int opening_shown(const bjgui_t *v)
{
    int shown = 0;

    for (int i = 0; i < 4; i++)
        shown += gui_reveal_shown(&v->reveal, i);
    return shown;
}

/* Opening deal order: player 0, dealer 0, player 1, dealer 1. */
static int visible_player(const bjgui_t *v, int hand, int cards)
{
    if (!v->dealing || hand != 0)
        return cards;
    int shown = opening_shown(v);
    return shown >= 3 ? 2 : shown >= 1 ? 1 : 0;
}

static int visible_dealer(const bjgui_t *v, int cards)
{
    if (!v->dealing)
        return cards;
    int shown = opening_shown(v);
    return shown >= 4 ? 2 : shown >= 2 ? 1 : 0;
}

/* ---- Hi-Lo counting: which dealt cards the player has been shown ------- */

/* The round's first card, for lining the deal animation up with the shoe;
 * the engine works out where the face-down hole card sits. */
static int round_first_dealt(const bjgui_t *v)
{
    return bj_dealt_count(&v->s) - round_card_count(&v->s);
}

static int hole_dealt_index(const bjgui_t *v)
{
    return bj_hole_index(&v->s);
}

/* Cards on the felt, in deal order: the opening four arrive with the
 * reveal animation, everything dealt after it is face up at once. */
static int visible_dealt(const bjgui_t *v)
{
    if (!v->dealing)
        return bj_dealt_count(&v->s);
    return round_first_dealt(v) + opening_shown(v);
}

/* The hole card turns face up when the engine reveals it - but never
 * before the opening animation has put it on the table. */
static bool hole_face_up(const bjgui_t *v)
{
    const bj_round_t *r = &v->s.round;

    if (r->ndealer < 2 || r->hole_hidden)
        return false;
    return !v->dealing || opening_shown(v) >= 4;
}

/* ---- drawing ------------------------------------------------------------ */

static void draw_dealer(const gui_ctx_t *g, const bjgui_t *v)
{
    const bj_round_t *r = &v->s.round;
    char buf[48];
    int n = visible_dealer(v, r->ndealer);

    gui_text_centered(g, true, "DEALER", DEALER_CX, DEALER_Y - 34, 28,
                      GUI_CREAM);
    for (int i = 0; i < n; i++) {
        Rectangle rc = gui_card_row(g, i, n < r->ndealer ? r->ndealer : n,
                                    DEALER_CARD_H, DEALER_CX, DEALER_Y,
                                    CARD_GAP);
        bool hidden = r->hole_hidden && i == 1;
        gui_draw_card(g, rc, hidden ? NULL : &r->dealer[i]);
    }
    if (n > 0 && r->phase != BJ_PHASE_BET) {
        if (r->hole_hidden)
            snprintf(buf, sizeof buf, "%d", bj_total(r->dealer, 1));
        else
            snprintf(buf, sizeof buf, "%d",
                     bj_total(r->dealer, r->ndealer));
        gui_text_centered(g, true, buf, DEALER_CX,
                          DEALER_Y + DEALER_CARD_H + 8, 32, GUI_CREAM);
    }
}

static void draw_hands(const gui_ctx_t *g, const bjgui_t *v)
{
    const bj_round_t *r = &v->s.round;
    int nh = r->nhands;
    float card_h = nh > 1 ? SPLIT_CARD_H : PLAYER_CARD_H;
    char buf[64];

    if (nh == 0)
        return;

    for (int i = 0; i < nh; i++) {
        const bj_hand_t *h = &r->hands[i];
        float cx = nh == 1 ? 640.0f
                           : 200.0f + i * (880.0f / (nh - 1 ? nh - 1 : 1));
        if (nh == 2)
            cx = i == 0 ? 420.0f : 860.0f;
        else if (nh == 3)
            cx = 260.0f + i * 380.0f;
        else if (nh == 4)
            cx = 200.0f + i * 293.0f;

        int shown = visible_player(v, i, h->n);
        bool active = i == r->active && r->phase == BJ_PHASE_PLAYER;

        for (int c = 0; c < shown; c++) {
            Rectangle rc = gui_card_row(g, c, h->n > shown ? h->n : shown,
                                        card_h, cx, PLAYER_Y, CARD_GAP);
            gui_draw_card(g, rc, &h->cards[c]);
        }

        if (shown > 0) {
            snprintf(buf, sizeof buf, "%d",
                     bj_total(h->cards, shown));
            gui_text_centered(g, true, buf, cx, PLAYER_Y + card_h + 6, 32,
                              active ? GUI_GOLD : GUI_CREAM);
        }

        char money[24];
        bj_credits(money, sizeof money, h->wager);
        snprintf(buf, sizeof buf, "%s%s", money,
                 h->doubled ? " (DBL)" : "");
        gui_text_centered(g, false, buf, cx, PLAYER_Y + card_h + 42, 22,
                          GUI_DIM);

        if (active)
            gui_text_centered(g, true, "^", cx, PLAYER_Y + card_h + 66, 24,
                              GUI_GOLD);

        if (r->phase == BJ_PHASE_SETTLED && h->result != BJ_PENDING)
            gui_text_centered(g, true, bj_result_word(h->result), cx,
                              PLAYER_Y - 34, 26,
                              h->result == BJ_WIN ||
                              h->result == BJ_BLACKJACK ? GUI_GOLD
                                                        : GUI_CREAM);
    }
}

static void draw_status(const gui_ctx_t *g, const bjgui_t *v)
{
    char buf[64], money[24];

    bj_credits(money, sizeof money, v->s.bankroll);
    snprintf(buf, sizeof buf, "BANKROLL: %s", money);
    gui_text(g, true, buf, 40, 596, 28, GUI_CREAM);

    bj_credits(money, sizeof money, v->s.base_bet);
    snprintf(buf, sizeof buf, "BET: %s", money);
    gui_text_centered(g, true, buf, 640, 596, 28, GUI_CREAM);

    /* cards remaining is shown on purpose so the player can count */
    snprintf(buf, sizeof buf, "SHOE: %d / %d", bj_remaining(&v->s),
             BJ_SHOE_CARDS);
    gui_text(g, true, buf, 1240 - gui_text_width(g, true, buf, 28), 596,
             28, GUI_DIM);
}

/* Counts read with a sign, and never as "+0". */
static void count_text(char *buf, size_t len, int v)
{
    snprintf(buf, len, "%s%d", v > 0 ? "+" : "", v);
}

/* One decimal place, signed from the rounded value so a hair below zero
 * does not print as "-0.0". */
static void true_text(char *buf, size_t len, double v)
{
    long tenths = (long)(v * 10.0 + (v >= 0 ? 0.5 : -0.5));
    long mag;

    if (tenths > 9999)              /* a sliver of a shoe cannot overflow */
        tenths = 9999;
    else if (tenths < -9999)
        tenths = -9999;
    mag = tenths < 0 ? -tenths : tenths;

    snprintf(buf, len, "%s%ld.%ld", tenths > 0 ? "+" : tenths < 0 ? "-" : "",
             mag / 10, mag % 10);
}

/* Two lines in the title bar: the only part of the table that never has
 * cards, buttons or money on it. */
static void draw_count(const gui_ctx_t *g, const bjgui_t *v)
{
    char line[80], rc[12], tc[12];

    count_text(rc, sizeof rc, v->count.running);
    true_text(tc, sizeof tc, bj_true_count(&v->count, &v->s));
    snprintf(line, sizeof line, "RUNNING COUNT: %s   TRUE COUNT: %s", rc, tc);
    gui_text(g, true, line, 1240 - gui_text_width(g, true, line, 22), 4, 22,
             GUI_CREAM);

    snprintf(line, sizeof line, "DECKS LEFT: %.1f", bj_decks_left(&v->s));
    gui_text(g, true, line, 1240 - gui_text_width(g, true, line, 22), 28, 22,
             GUI_DIM);
}

/*
 * Basic-strategy trainer panel.  The recommendation is only offered while
 * the engine is genuinely waiting on the active hand and the opening deal
 * has finished landing; the running tally and the verdict from the last
 * decision stay up either way, so the panel does not jump around.
 */
static void draw_strategy(const gui_ctx_t *g, const bjgui_t *v)
{
    bj_strategy_action_t rec = bj_basic_strategy(&v->s);
    char line[64];

    gui_text(g, true, "BASIC STRATEGY", STRAT_X, STRAT_Y, 22, GUI_GOLD);

    if (!v->dealing && rec != BJ_STRAT_NONE) {
        snprintf(line, sizeof line, "RECOMMENDED: %s",
                 bj_strategy_word(rec));
        gui_text(g, true, line, STRAT_X, STRAT_Y + 28, 24, GUI_CREAM);
    }

    snprintf(line, sizeof line, "CORRECT: %ld / %ld", v->strat_correct,
             v->strat_total);
    gui_text(g, false, line, STRAT_X, STRAT_Y + 66, 20, GUI_DIM);
    snprintf(line, sizeof line, "ACCURACY: %.1f%%",
             v->strat_total ? 100.0 * (double)v->strat_correct /
                                  (double)v->strat_total
                            : 0.0);
    gui_text(g, false, line, STRAT_X, STRAT_Y + 90, 20, GUI_DIM);

    /* the verdict names the advice as it stood before that action, so a
     * state change cannot rewrite it while it is still on screen */
    if (v->fb_valid && GetTime() < v->fb_until) {
        if (v->fb_correct) {
            gui_text(g, true, "CORRECT", STRAT_X, STRAT_Y + 122, 22,
                     GUI_GOLD);
        } else {
            snprintf(line, sizeof line, "BASIC STRATEGY: %s",
                     bj_strategy_word(v->fb_rec));
            gui_text(g, true, line, STRAT_X, STRAT_Y + 122, 22, GUI_CREAM);
        }
    }
}

/* ---- one frame ---------------------------------------------------------- */

static void bj_frame(const gui_ctx_t *g, void *state)
{
    bjgui_t *v = state;
    bj_round_t *r = &v->s.round;
    bool betting = r->phase == BJ_PHASE_BET || r->phase == BJ_PHASE_SETTLED;

    /* opening deal animation */
    if (v->dealing && gui_reveal_update(&v->reveal, g))
        v->dealing = false;

    /* ---- input ---- */
    if (betting && !v->dealing) {
        long step = 5 * BJ_HALF;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_UP)) {
            bj_set_bet(&v->s, v->s.base_bet + step);
            gui_play_click(g);
        }
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_DOWN)) {
            bj_set_bet(&v->s, v->s.base_bet - step);
            gui_play_click(g);
        }
        if (v->s.bankroll < BJ_BET_MIN && IsKeyPressed(KEY_N)) {
            bj_bankroll_reset(&v->s);
            gui_play_click(g);
        }
        if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            if (bj_can_deal(&v->s)) {
                gui_play_click(g);
                start_deal(v);
            }
        }
    }

    if (r->phase == BJ_PHASE_INSURANCE && !v->dealing) {
        if (IsKeyPressed(KEY_I)) {
            gui_play_click(g);
            bj_insurance(&v->s, true, v->rng);
        } else if (IsKeyPressed(KEY_N)) {
            gui_play_click(g);
            bj_insurance(&v->s, false, v->rng);
        }
    }

    if (r->phase == BJ_PHASE_PLAYER && !v->dealing) {
        static const struct { int key; bj_action_t act; } KEYS[] = {
            { KEY_H, BJ_HIT }, { KEY_S, BJ_STAND }, { KEY_D, BJ_DOUBLE },
            { KEY_P, BJ_SPLIT }, { KEY_R, BJ_SURRENDER },
        };
        for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) {
            if (IsKeyPressed(KEYS[i].key)) {
                perform_player_action(v, g, KEYS[i].act, true);
                break;
            }
        }
    }

    note_new_cards(v, g);

    /* the count follows what is on the felt, card by card */
    if (v->counting)
        bj_count_update(&v->count, &v->s, visible_dealt(v),
                        hole_dealt_index(v), hole_face_up(v));

    /* ---- draw ---- */
    DrawRectangle(0, 0, GUI_CANVAS_W, 56, GUI_FELT_DARK);
    gui_text_centered(g, true, "BLACKJACK", GUI_CANVAS_W / 2, 12, 36,
                      GUI_GOLD);
    if (v->counting) {
        draw_count(g, v);
        draw_strategy(g, v);
    }

    draw_dealer(g, v);
    draw_hands(g, v);
    draw_status(g, v);

    if (GetTime() < v->shuffle_until)
        gui_text_centered(g, true, "SHUFFLING...", 640, 268, 32, GUI_GOLD);

    /* ---- controls ---- */
    if (r->phase == BJ_PHASE_INSURANCE && !v->dealing) {
        gui_text_centered(g, true, "DEALER SHOWS AN ACE", 640, 268, 32,
                          GUI_GOLD);
        if (gui_button(g, (Rectangle){ 380, BTN_Y, 240, BTN_H },
                       "INSURANCE", v->s.bankroll >= v->s.base_bet / 2))
            bj_insurance(&v->s, true, v->rng);
        if (gui_button(g, (Rectangle){ 660, BTN_Y, 240, BTN_H },
                       "NO INSURANCE", true))
            bj_insurance(&v->s, false, v->rng);
    } else if (r->phase == BJ_PHASE_PLAYER && !v->dealing) {
        static const struct { const char *label; bj_action_t act; } ACTS[] = {
            { "HIT", BJ_HIT }, { "STAND", BJ_STAND },
            { "DOUBLE", BJ_DOUBLE }, { "SPLIT", BJ_SPLIT },
            { "SURRENDER", BJ_SURRENDER },
        };
        float x = 90;
        for (size_t i = 0; i < sizeof ACTS / sizeof ACTS[0]; i++) {
            bool ok = bj_legal(&v->s, ACTS[i].act);
            if (gui_button(g, (Rectangle){ x, BTN_Y, 210, BTN_H },
                           ACTS[i].label, ok))
                perform_player_action(v, g, ACTS[i].act, false);
            x += 222;
        }
    } else if (betting && !v->dealing) {
        bool can = bj_can_deal(&v->s);

        if (gui_button(g, (Rectangle){ 300, BTN_Y, 110, BTN_H }, "BET -",
                       v->s.base_bet > BJ_BET_MIN))
            bj_set_bet(&v->s, v->s.base_bet - 5 * BJ_HALF);
        if (gui_button(g, (Rectangle){ 430, BTN_Y, 110, BTN_H }, "BET +",
                       v->s.base_bet < BJ_BET_MAX &&
                       v->s.base_bet + 5 * BJ_HALF <= v->s.bankroll))
            bj_set_bet(&v->s, v->s.base_bet + 5 * BJ_HALF);
        if (gui_button(g, (Rectangle){ 620, BTN_Y, 260, BTN_H }, "DEAL",
                       can))
            start_deal(v);

        if (v->s.bankroll < BJ_BET_MIN)
            gui_text_centered(g, true, "OUT OF CREDITS - PRESS N", 640,
                              540, 32, GUI_GOLD);
        else if (r->phase == BJ_PHASE_BET)
            gui_text_centered(g, false,
                              "SPACE DEALS - ARROWS CHANGE THE BET", 640,
                              548, 22, GUI_DIM);
    }

    /* round summary */
    if (r->phase == BJ_PHASE_SETTLED && !v->dealing) {
        char line[96], money[24];
        bj_credits(money, sizeof money, r->returned - r->wagered);
        if (r->insurance > 0)
            snprintf(line, sizeof line, "NET %s   INSURANCE %s", money,
                     r->insurance_won ? "WON" : "LOST");
        else
            snprintf(line, sizeof line, "NET %s", money);
        gui_text_centered(g, true, line, 640, 540, 30,
                          r->returned > r->wagered ? GUI_GOLD : GUI_CREAM);
    }
}

int bj_gui_run(rng_t *rng, bool counting)
{
    bjgui_t v = { 0 };

    v.rng = rng;
    v.counting = counting;
    bj_count_reset(&v.count);
    bj_session_start(&v.s, rng);

    return gui_run(counting ? "casino - blackjack counting trainer"
                            : "casino - blackjack", bj_frame, &v);
}
