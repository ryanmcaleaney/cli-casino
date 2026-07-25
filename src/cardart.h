#ifndef CASINO_CARDART_H
#define CASINO_CARDART_H

#include <stdbool.h>
#include <stdio.h>

#include "cards.h"

/*
 * ASCII-art card rendering (presentation only, UTF-8 terminal).
 * Cards are CARDART_WIDTH display columns wide and CARDART_ROWS tall,
 * rendered side by side with a single-space separator.
 */
#define CARDART_WIDTH 11
#define CARDART_ROWS  7

/* Render n cards side by side.  hidden may be NULL (all face up) or a
 * per-card mask; hidden cards render as a card back. */
void cardart_hand(FILE *f, const card_t *cards, const bool *hidden, int n);

/* True when f should use ASCII-art cards: CASINO_CARDS=art forces on,
 * CASINO_CARDS=plain forces off, otherwise on only when f is a TTY. */
bool cardart_enabled(FILE *f);

#endif
