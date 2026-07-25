#define _DEFAULT_SOURCE

#include "cardart.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* suit order matches cards.h: 0=clubs 1=diamonds 2=hearts 3=spades */
static const char *const SUIT_SYM[] = { "♣", "♦", "♥", "♠" };

bool cardart_enabled(FILE *f)
{
    const char *mode = getenv("CASINO_CARDS");
    if (mode) {
        if (strcmp(mode, "art") == 0)
            return true;
        if (strcmp(mode, "plain") == 0)
            return false;
    }
    return isatty(fileno(f)) == 1;
}

void cardart_hand(FILE *f, const card_t *cards, const bool *hidden, int n)
{
    for (int row = 0; row < CARDART_ROWS; row++) {
        for (int i = 0; i < n; i++) {
            bool hid = hidden && hidden[i];
            const char *sep = i ? " " : "";
            switch (row) {
            case 0:
                fprintf(f, "%s┌─────────┐", sep);
                break;
            case 6:
                fprintf(f, "%s└─────────┘", sep);
                break;
            case 1:
                if (hid)
                    fprintf(f, "%s│░░░░░░░░░│", sep);
                else
                    fprintf(f, "%s│%-9s│", sep, card_rank_str(cards[i]));
                break;
            case 5:
                if (hid)
                    fprintf(f, "%s│░░░░░░░░░│", sep);
                else
                    fprintf(f, "%s│%9s│", sep, card_rank_str(cards[i]));
                break;
            case 3:
                if (hid)
                    fprintf(f, "%s│░░░░░░░░░│", sep);
                else
                    fprintf(f, "%s│    %s    │", sep,
                            SUIT_SYM[cards[i].suit]);
                break;
            default:            /* rows 2 and 4 */
                fprintf(f, hid ? "%s│░░░░░░░░░│" : "%s│         │", sep);
                break;
            }
        }
        fputc('\n', f);
    }
}
