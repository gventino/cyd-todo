#include "todo/theme_quest.h"

#include <string.h>

#include "todo/model.h"

#define RGB CYDUI_RGB

static const todo_colors COLORS[TODO_PALETTE_COUNT] = {
    /* parchment */
    {.bg = RGB(0xE4, 0xD3, 0xA8), .page = RGB(0xF3, 0xE7, 0xC4), .card = RGB(0xF8, 0xEF, 0xD3), .card_done = RGB(0xDD, 0xCF, 0xA9),
     .text = RGB(0x3B, 0x2A, 0x1A), .muted = RGB(0x5E, 0x49, 0x2E),
     .gold = RGB(0xC8, 0x96, 0x1E), .gold_dark = RGB(0x8A, 0x62, 0x0C), .gold_light = RGB(0xE8, 0xC4, 0x6A), .on_gold = RGB(0x2E, 0x1F, 0x10),
     .wood = RGB(0x4A, 0x32, 0x20), .wood_light = RGB(0x7A, 0x56, 0x36), .on_wood = RGB(0xF0, 0xE0, 0xB8),
     .wax = RGB(0x9B, 0x22, 0x26), .wax_dark = RGB(0x5E, 0x12, 0x16), .on_wax = RGB(0xF8, 0xEF, 0xD3), .alert = RGB(0x9B, 0x22, 0x26),
     .xp_from = RGB(0xE8, 0xB4, 0x2C), .xp_to = RGB(0xB0, 0x6A, 0x10), .star_off = RGB(0xD6, 0xC6, 0x9A)},
    /* candlelight */
    {.bg = RGB(0x1C, 0x15, 0x10), .page = RGB(0x2C, 0x21, 0x18), .card = RGB(0x2A, 0x20, 0x18), .card_done = RGB(0x1F, 0x18, 0x12),
     .text = RGB(0xE8, 0xD5, 0xA8), .muted = RGB(0xA0, 0x8C, 0x6A),
     .gold = RGB(0xE0, 0xA5, 0x26), .gold_dark = RGB(0x8A, 0x62, 0x0C), .gold_light = RGB(0xF2, 0xC8, 0x6A), .on_gold = RGB(0x24, 0x18, 0x0C),
     .wood = RGB(0x12, 0x0C, 0x08), .wood_light = RGB(0x4A, 0x36, 0x24), .on_wood = RGB(0xE8, 0xD5, 0xA8),
     .wax = RGB(0xB0, 0x30, 0x30), .wax_dark = RGB(0x6A, 0x14, 0x14), .on_wax = RGB(0xF8, 0xEF, 0xD3), .alert = RGB(0xE8, 0x70, 0x66),
     .xp_from = RGB(0xF0, 0xB8, 0x30), .xp_to = RGB(0xC0, 0x78, 0x18), .star_off = RGB(0x4A, 0x3A, 0x2A)},
};

const todo_colors *todo_colors_of(todo_palette p)
{
    return &COLORS[p >= 0 && p < TODO_PALETTE_COUNT ? p : TODO_PALETTE_PARCHMENT];
}

/* The colors of the theme being painted: the theme carries them in `user` (the library never reads it), so a painter finds
 * the set it was built from without a lookup by name. */
static const todo_colors *colors_of_theme(const cydui_theme *t)
{
    return t->user ? (const todo_colors *)t->user : todo_colors_of(TODO_PALETTE_PARCHMENT);
}

/* ... and which palette that is, for the helpers that take a palette (the pointer says it, no comparing of names). */
static todo_palette palette_of(const cydui_theme *t)
{
    return colors_of_theme(t) == todo_colors_of(TODO_PALETTE_CANDLELIGHT) ? TODO_PALETTE_CANDLELIGHT : TODO_PALETTE_PARCHMENT;
}

/* ---- drawing helpers ---- */

static cydui_rect inset(cydui_rect r, int d)
{
    r.x = (int16_t)(r.x + d);
    r.y = (int16_t)(r.y + d);
    r.w = (int16_t)(r.w > 2 * d ? r.w - 2 * d : 0);
    r.h = (int16_t)(r.h > 2 * d ? r.h - 2 * d : 0);
    return r;
}

static void fill(cydui_ctx *c, cydui_rect r, cydui_color col)
{
    cydui_draw_rect(c, r, col, 255);
}

/* A face of `face` inside an edge of `edge`, `bw` pixels thick (a stack of two rectangles). */
static void boxed(cydui_ctx *c, cydui_rect r, int bw, cydui_color edge, cydui_color face)
{
    fill(c, r, edge);
    fill(c, inset(r, bw), face);
}

static void entry(cydui_theme *t, cydui_widget_kind k, int sel, cydui_widget_state st, cydui_color fl, cydui_color border,
                  cydui_color text, cydui_color accent)
{
    cydui_wstyle *w = &t->table[k][sel][st];
    w->fill = fl;
    w->border = border;
    w->text = text;
    w->accent = accent;
    w->border_w = 0;
    w->radius = 0;
    w->shadow = 0;
}

/* ---- the painters ---- */

/* A button of gold: a dark edge, a light line along the top while it stands out, the face with the label. */
static void paint_button(cydui_ui *ui, const cydui_theme *t, const cydui_paint_info *p)
{
    const todo_colors *k = colors_of_theme(t);
    const bool down = p->state == CYDUI_STATE_PRESSED || cydui_paint_amount(p) > CYDUI_ANIM_ONE / 2;
    cydui_paint_info face = *p;

    fill(&ui->ctx, p->rect, k->gold_dark);
    face.rect = inset(p->rect, 2);
    if (!down && face.rect.h > 2) {
        cydui_rect hi = {face.rect.x, face.rect.y, face.rect.w, 1};
        face.rect.y++;
        face.rect.h--;
        cydui_paint_default(ui, t, &face);
        fill(&ui->ctx, hi, k->gold_light);
        return;
    }
    cydui_paint_default(ui, t, &face);
}

/* A tab: wood, and the active one is the page itself with a gold bar on top. */
static void paint_tab(cydui_ui *ui, const cydui_theme *t, const cydui_paint_info *p)
{
    const todo_colors *k = colors_of_theme(t);
    cydui_paint_info face = *p;
    const bool active = cydui_paint_amount(p) > CYDUI_ANIM_ONE / 2;

    fill(&ui->ctx, p->rect, active ? k->page : k->wood);
    face.rect = inset(p->rect, 1);
    if (active) {
        cydui_rect bar = {p->rect.x, p->rect.y, p->rect.w, 3};
        face.rect.y = (int16_t)(face.rect.y + 2);
        face.rect.h = (int16_t)(face.rect.h > 2 ? face.rect.h - 2 : 0);
        cydui_paint_default(ui, t, &face);
        fill(&ui->ctx, bar, k->gold);
        return;
    }
    cydui_paint_default(ui, t, &face);
}

/* A panel or a popup: a double border (wood, then a gold line) round the page. */
static void paint_page(cydui_ui *ui, const cydui_theme *t, const cydui_paint_info *p)
{
    const todo_colors *k = colors_of_theme(t);
    cydui_paint_info face = *p;

    boxed(&ui->ctx, p->rect, 2, k->wood, k->gold_dark);
    face.rect = inset(p->rect, 3);
    cydui_paint_default(ui, t, &face);
}

static void paint_checkbox(cydui_ui *ui, const cydui_theme *t, const cydui_paint_info *p)
{
    const todo_colors *k = colors_of_theme(t);
    cydui_paint_info face = *p;

    fill(&ui->ctx, p->rect, k->wood_light);
    face.rect = inset(p->rect, 2);
    cydui_paint_default(ui, t, &face);
}

static void paint_progress(cydui_ui *ui, const cydui_theme *t, const cydui_paint_info *p)
{
    todo_palette pal = palette_of(t);
    int64_t v = p->value < 0 ? 0 : p->value, m = p->max > 0 ? p->max : 1;

    todo_paint_xp_bar(&ui->ctx, pal, p->rect, (int)((v > m ? m : v) * 4096 / m));
}

static void paint_scrollbar(cydui_ui *ui, const cydui_theme *t, const cydui_paint_info *p)
{
    const todo_colors *k = colors_of_theme(t);
    int64_t span = p->max > 0 ? p->max : 0, pos = p->value < 0 ? 0 : p->value, total = span + p->aux.h;
    cydui_rect thumb = p->rect;

    if (p->rect.w <= 0 || p->rect.h <= 0 || span == 0 || total <= 0) {
        return;
    }
    pos = pos > span ? span : pos;
    thumb.h = (int16_t)((int64_t)p->rect.h * p->aux.h / total);
    if (thumb.h < 16) {
        thumb.h = p->rect.h < 16 ? p->rect.h : 16;
    }
    thumb.y = (int16_t)(p->rect.y + (p->rect.h - thumb.h) * pos / span);
    boxed(&ui->ctx, thumb, 1, k->gold_dark, p->state == CYDUI_STATE_PRESSED ? k->gold_dark : k->gold);
}

/* ---- the theme ---- */

void todo_theme_quest_init(cydui_theme *t, todo_palette p)
{
    const todo_colors *k = todo_colors_of(p);
    cydui_style *s;
    int sel, kind;

    memset(t, 0, sizeof(*t));
    t->name = p == TODO_PALETTE_CANDLELIGHT ? "quest-candlelight" : "quest-parchment";
    t->user = k;
    s = &t->style;
    s->bg = k->bg;
    s->panel = k->page;
    s->text = k->text;
    s->text_muted = k->muted;
    s->accent = k->gold;
    s->accent_text = k->on_gold;
    s->border = k->wood_light;
    s->font = &todo_serif_16;
    s->radius = 0;
    s->pad_x = 10;
    s->pad_y = 6;
    s->spacing = 0; /* also the margin of the root layout: the screen is edge to edge; containers that want a gap ask for one */
    s->min_target = 32;
    s->anim_ms = 120;
    cydui_theme_derive_table(t);

    for (sel = 0; sel < 2; sel++) {
        entry(t, CYDUI_WIDGET_BUTTON, sel, CYDUI_STATE_NORMAL, k->gold, k->gold_dark, k->on_gold, k->gold_light);
        entry(t, CYDUI_WIDGET_BUTTON, sel, CYDUI_STATE_PRESSED, k->gold_dark, k->gold_dark, k->on_wood, k->gold_light);
        entry(t, CYDUI_WIDGET_BUTTON, sel, CYDUI_STATE_DISABLED, k->card_done, k->wood_light, k->muted, k->gold_light);
        entry(t, CYDUI_WIDGET_KEY, sel, CYDUI_STATE_NORMAL, sel ? k->gold : k->card, k->wood_light, sel ? k->on_gold : k->text, k->gold);
        entry(t, CYDUI_WIDGET_KEY, sel, CYDUI_STATE_PRESSED, k->gold_dark, k->wood_light, k->on_wood, k->gold);
        /* a tab: wood with light text; the active one is the page with dark text */
        entry(t, CYDUI_WIDGET_TAB, sel, CYDUI_STATE_NORMAL, sel ? k->page : k->wood, k->wood, sel ? k->text : k->on_wood, k->gold);
        entry(t, CYDUI_WIDGET_TAB, sel, CYDUI_STATE_PRESSED, sel ? k->page : k->wood_light, k->wood, sel ? k->text : k->on_wood, k->gold);
        entry(t, CYDUI_WIDGET_TAB, sel, CYDUI_STATE_DISABLED, k->wood, k->wood, k->muted, k->gold);
        entry(t, CYDUI_WIDGET_PANEL, sel, CYDUI_STATE_NORMAL, sel ? k->card_done : k->page, k->wood, k->text, k->gold);
        entry(t, CYDUI_WIDGET_POPUP, sel, CYDUI_STATE_NORMAL, k->page, k->wood, k->text, k->gold);
        entry(t, CYDUI_WIDGET_CHECKBOX, sel, CYDUI_STATE_NORMAL, k->card, k->wood_light, sel ? k->gold_dark : k->text, k->gold);
        entry(t, CYDUI_WIDGET_CHECKBOX, sel, CYDUI_STATE_PRESSED, k->card_done, k->wood_light, k->gold_dark, k->gold);
        entry(t, CYDUI_WIDGET_CHECKBOX, sel, CYDUI_STATE_DISABLED, k->card_done, k->wood_light, k->muted, k->gold);
        entry(t, CYDUI_WIDGET_PROGRESS, sel, CYDUI_STATE_NORMAL, k->wood, k->wood_light, k->text, k->gold);
        entry(t, CYDUI_WIDGET_PROGRESS, sel, CYDUI_STATE_DISABLED, k->card_done, k->wood_light, k->muted, k->muted);
        entry(t, CYDUI_WIDGET_SCROLLBAR, sel, CYDUI_STATE_NORMAL, k->bg, k->wood_light, k->text, k->gold);
        entry(t, CYDUI_WIDGET_SCROLLBAR, sel, CYDUI_STATE_PRESSED, k->bg, k->wood_light, k->text, k->gold_dark);
        entry(t, CYDUI_WIDGET_TOGGLE, sel, CYDUI_STATE_NORMAL, sel ? k->gold : k->wood_light, k->wood, k->page, k->gold);
        entry(t, CYDUI_WIDGET_SLIDER, sel, CYDUI_STATE_NORMAL, k->wood_light, k->wood, k->gold, k->gold);
    }
    for (kind = 0; kind < CYDUI_WIDGET_COUNT; kind++) {
        for (sel = 0; sel < 2; sel++) {
            t->table[kind][sel][CYDUI_STATE_HOVER] = t->table[kind][sel][CYDUI_STATE_NORMAL];
            if (kind != CYDUI_WIDGET_TEXT_INPUT) {
                t->table[kind][sel][CYDUI_STATE_FOCUSED] = t->table[kind][sel][CYDUI_STATE_NORMAL];
            }
        }
    }
    t->paint[CYDUI_WIDGET_BUTTON] = paint_button;
    t->paint[CYDUI_WIDGET_KEY] = paint_button;
    t->paint[CYDUI_WIDGET_TAB] = paint_tab;
    t->paint[CYDUI_WIDGET_PANEL] = paint_page;
    t->paint[CYDUI_WIDGET_POPUP] = paint_page;
    t->paint[CYDUI_WIDGET_CHECKBOX] = paint_checkbox;
    t->paint[CYDUI_WIDGET_PROGRESS] = paint_progress;
    t->paint[CYDUI_WIDGET_SCROLLBAR] = paint_scrollbar;
}

/* ---- the helpers ---- */

void todo_paint_card_mix(cydui_ctx *ctx, todo_palette p, cydui_rect r, int amount)
{
    const todo_colors *k = todo_colors_of(p);
    cydui_color face;

    amount = amount < 0 ? 0 : (amount > 255 ? 255 : amount);
    face = amount == 0 ? k->card : (amount == 255 ? k->card_done : cydui_blend(k->card, k->card_done, (uint8_t)amount));
    boxed(ctx, r, 1, k->wood_light, face);
    if (amount < 255) {
        cydui_draw_rect(ctx, (cydui_rect){(int16_t)(r.x + 1), (int16_t)(r.y + 1), 3, (int16_t)(r.h > 2 ? r.h - 2 : 0)}, k->gold, (uint8_t)(255 - amount));
    }
}

void todo_paint_card(cydui_ctx *ctx, todo_palette p, cydui_rect r, bool done)
{
    todo_paint_card_mix(ctx, p, r, done ? 255 : 0);
}

int todo_xp_bar_width(int inner_w, int fraction)
{
    fraction = fraction < 0 ? 0 : (fraction > 4096 ? 4096 : fraction);
    return inner_w > 0 ? (int)((int64_t)inner_w * fraction / 4096) : 0;
}

void todo_paint_xp_bar(cydui_ctx *ctx, todo_palette p, cydui_rect r, int fraction)
{
    const todo_colors *k = todo_colors_of(p);
    cydui_rect in = inset(r, 2), bar = in;

    boxed(ctx, r, 1, k->wood_light, k->wood);
    bar.w = (int16_t)todo_xp_bar_width(in.w, fraction);
    if (bar.w > 0 && in.h > 0) {
        cydui_draw_gradient(ctx, bar, k->xp_from, k->xp_to, true, 255);
    }
}

void todo_paint_wax_button(cydui_ctx *ctx, todo_palette p, cydui_rect r, const char *label, bool pressed)
{
    const todo_colors *k = todo_colors_of(p);
    const cydui_font *f = &todo_serif_bold_16;
    cydui_text_extent e = cydui_text_size(f, label, CYDUI_NTS, 0);
    cydui_rect face = inset(r, 2);

    fill(ctx, r, k->wax_dark);
    fill(ctx, face, pressed ? k->wax_dark : k->wax);
    cydui_draw_text(ctx, f, (int16_t)(r.x + (r.w - e.w) / 2), (int16_t)(r.y + (r.h - e.h) / 2), 0, CYDUI_ALIGN_LEFT, label, CYDUI_NTS,
                    k->on_wax, 255);
}

void todo_draw_stars(cydui_ctx *ctx, int x, int y, int n, int of, cydui_color color, cydui_color faint)
{
    int i;

    n = n < 0 ? 0 : (n > 5 ? 5 : n);
    of = of < n ? n : (of > 5 ? 5 : of);
    for (i = 0; i < of; i++) {
        cydui_draw_bitmap(ctx, (cydui_rect){(int16_t)(x + i * TODO_STAR_STEP), (int16_t)y, TODO_STAR_SIZE, TODO_STAR_SIZE},
                          todo_star_mask, sizeof(todo_star_mask), CYDUI_BITMAP_A4, i < n ? color : faint, 255);
    }
}

/* ---- text with an ellipsis ---- */

int todo_draw_text_ellipsis(cydui_ctx *ctx, const cydui_font *font, int x, int y, int max_w, const char *s, cydui_color color)
{
    char buf[TODO_TEXT_MAX + 4];
    size_t n = cydui_text_ellipsize(font, s, CYDUI_NTS, (int16_t)(max_w > 32767 ? 32767 : max_w), buf, sizeof(buf), NULL);

    cydui_draw_text(ctx, font, (int16_t)x, (int16_t)y, 0, CYDUI_ALIGN_LEFT, buf, n, color, 255);
    return n ? cydui_text_size(font, buf, n, 0).w : 0;
}
