#ifndef TODO_THEME_QUEST_H
#define TODO_THEME_QUEST_H

/*
 * The look of cyd-todo: a parchment page, dark wood tabs, a gold accent, red wax for what destroys, a serif
 * font. Two palettes: PARCHMENT (day) and CANDLELIGHT (night). The theme is a table plus painters made of
 * stacked rectangles; the helpers below are for the app's own widgets (cards, the XP bar, stars) so that they
 * paint with the same colors.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cydui/ui.h"
#include "todo_serif_12.h"
#include "todo_serif_16.h"
#include "todo_serif_bold_16.h"
#include "todo_serif_bold_24.h"

typedef enum { TODO_PALETTE_PARCHMENT, TODO_PALETTE_CANDLELIGHT, TODO_PALETTE_COUNT } todo_palette;

typedef struct {
    cydui_color bg;        /* behind everything */
    cydui_color page;      /* the page of a popup and the active tab */
    cydui_color card;      /* a quest card, an input */
    cydui_color card_done; /* a finished quest */
    cydui_color text, muted;
    cydui_color gold, gold_dark, gold_light, on_gold; /* the accent, its edges, the text on it */
    cydui_color wood, wood_light, on_wood;            /* tabs, borders */
    cydui_color wax, wax_dark, on_wax;                /* destructive buttons */
    cydui_color alert;                                /* red as text or a mark on the page: lighter than the wax at night */
    cydui_color xp_from, xp_to;                       /* the XP bar gradient */
    cydui_color star_off;                             /* a star that is not earned */
} todo_colors;

#define TODO_STAR_SIZE 12
#define TODO_STAR_STEP 13 /* from one star to the next */
extern const uint8_t todo_star_mask[TODO_STAR_SIZE / 2 * TODO_STAR_SIZE];

const todo_colors *todo_colors_of(todo_palette p);

/* Fills `t` (any previous content is lost). The theme's own font is the 16 px serif. */
void todo_theme_quest_init(cydui_theme *t, todo_palette p);

/* ---- helpers for the app's widgets (they draw with the theme `t`'s palette `p`) ---- */

/* A card: a gold bar on the left while the quest is open; faded for a finished one. 3 commands. */
void todo_paint_card(cydui_ctx *ctx, todo_palette p, cydui_rect r, bool done);
/* A card part of the way from the open look (0) to the done look (255); the two ends are todo_paint_card exactly. */
void todo_paint_card_mix(cydui_ctx *ctx, todo_palette p, cydui_rect r, int amount);
/* The XP bar, `fraction` of TODO_FRACTION_ONE (4096) full. At most 4 commands. */
void todo_paint_xp_bar(cydui_ctx *ctx, todo_palette p, cydui_rect r, int fraction);
/* How many pixels of an inner width are filled at `fraction` of 4096 (clamped to 0..4096). */
int todo_xp_bar_width(int inner_w, int fraction);
/* A button of red wax with `label` (centered, in the bold 16 px). */
void todo_paint_wax_button(cydui_ctx *ctx, todo_palette p, cydui_rect r, const char *label, bool pressed);
/* `n` stars (0..5) in a row from (x, y); the empty ones up to `of` are drawn faintly. */
void todo_draw_stars(cydui_ctx *ctx, int x, int y, int n, int of, cydui_color color, cydui_color faint);
/*
 * `s` at (x, y) in `font`; if it is wider than `max_w` it is cut on a character boundary and ends in an ellipsis,
 * so that the result fits. Returns the width drawn.
 */
int todo_draw_text_ellipsis(cydui_ctx *ctx, const cydui_font *font, int x, int y, int max_w, const char *s, cydui_color color);
/* The bytes of `s` that fit `max_w` with "…" after them (the whole length when all of it fits). Pure, for tests. */
size_t todo_fit_ellipsis(const cydui_font *font, const char *s, int max_w, bool *cut);

#endif /* TODO_THEME_QUEST_H */
