/* The widgets of the application: written with the library's recipe for a widget of one's own. */

#include <stdio.h>
#include <string.h>

#include "internal.h"

void ui_text(cydui_ui *ui, const cydui_font *f, int x, int y, const char *s, cydui_color c)
{
    cydui_draw_text(&ui->ctx, f, (int16_t)x, (int16_t)y, 0, CYDUI_ALIGN_LEFT, s, CYDUI_NTS, c, 255);
}

void ui_text_center(cydui_ui *ui, const cydui_font *f, cydui_rect box, const char *s, cydui_color c)
{
    cydui_text_extent e = cydui_text_size(f, s, CYDUI_NTS, 0);
    ui_text(ui, f, box.x + (box.w - e.w) / 2, box.y + (box.h - e.h) / 2, s, c);
}

/* "Dec 4" */
static void short_date(int32_t due, char *out, size_t cap)
{
    int y, m, d;
    todo_due_unpack(due, &y, &m, &d);
    snprintf(out, cap, "%.3s %d", todo_month_name(m), d);
}

/*
 * A quest card: a checkbox zone on the left and the title zone, two targets in one widget. `toggle` is set by a tap on
 * the checkbox, `open` by a tap on the rest. Returns true if either happened.
 */
bool ui_quest_card(cydui_ui *ui, todo_palette pal, const todo_quest *q, int32_t today, bool time_valid, bool *toggle, bool *open)
{
    const todo_colors *k = todo_colors_of(pal);
    cydui_mods m;
    cydui_id id_check, id_title;
    cydui_response rc, rt;
    cydui_rect r, check_zone, title_zone, box;
    cydui_paint_info pi;
    char date[24];
    int stars_x, title_w;
    bool overdue;

    *toggle = *open = false;
    if (!cydui_widget_begin(ui)) {
        return false;
    }
    m = cydui_take_mods(ui);
    cydui_push_id_int(ui, q->id);
    id_check = cydui_id_str(ui, "check");
    id_title = cydui_id_str(ui, "title");
    cydui_id_claim(ui, id_check);
    cydui_id_claim(ui, id_title);

    r = cydui_alloc(ui, &m, cydui_layout_avail(ui).w, CARD_H);
    r = rect_of(r.x + 4, r.y + 1, r.w - 8, CARD_H - 2);
    check_zone = rect_of(r.x, r.y, 36, r.h);
    title_zone = rect_of(r.x + 36, r.y, r.w - 36, r.h);
    rc = cydui_interact(ui, id_check, check_zone, CYDUI_SENSE_CLICK);
    rt = cydui_interact(ui, id_title, title_zone, CYDUI_SENSE_CLICK);
    *toggle = rc.clicked;
    *open = rt.clicked;

    todo_paint_card(&ui->ctx, pal, r, q->done);

    memset(&pi, 0, sizeof(pi));
    pi.kind = CYDUI_WIDGET_CHECKBOX;
    pi.selected = q->done;
    pi.state = rc.pressed ? CYDUI_STATE_PRESSED : CYDUI_STATE_NORMAL;
    pi.rect = rect_of(r.x + 10, r.y + (r.h - 20) / 2, 20, 20);
    cydui_paint(ui, &pi);

    stars_x = r.x + r.w - 8 - 5 * TODO_STAR_STEP + 1;
    title_w = stars_x - (title_zone.x + 4) - 6;
    box = rect_of(title_zone.x + 4, r.y + 1, title_w, 18);
    todo_draw_text_ellipsis(&ui->ctx, &todo_serif_16, box.x, box.y, box.w, q->title, q->done ? k->muted : k->text);
    todo_draw_stars(&ui->ctx, stars_x, r.y + (r.h - TODO_STAR_SIZE) / 2, q->difficulty, 5, q->done ? k->muted : k->gold_dark, k->star_off);
    if (q->due) {
        overdue = time_valid && !q->done && q->due < today;
        short_date(q->due, date, sizeof(date));
        ui_text(ui, &todo_serif_12, box.x, r.y + 19, date, overdue ? k->wax : k->muted);
    }
    cydui_pop_id(ui);
    return *toggle || *open;
}

/* "Lv 2", the bar, "70 / 200": level, and how far through it. */
void ui_xp_header(cydui_ui *ui, todo_palette pal, int32_t xp)
{
    const todo_colors *k = todo_colors_of(pal);
    cydui_mods m = cydui_take_mods(ui);
    int level = todo_level_for_xp(xp), target = todo_level_fraction(xp), shown;
    cydui_rect r;
    char buf[32];

    if (!cydui_widget_begin(ui)) {
        return;
    }
    r = cydui_alloc(ui, &m, 216, TAB_H);
    shown = cydui_animate(ui, cydui_id_str(ui, "xp-bar"), target, target, 300, CYDUI_EASE_OUT_CUBIC);
    snprintf(buf, sizeof(buf), "Lv %d", level);
    ui_text(ui, &todo_serif_bold_16, r.x + 8, r.y + 7, buf, k->text);
    todo_paint_xp_bar(&ui->ctx, pal, rect_of(r.x + 54, r.y + 10, 100, 12), shown);
    if (level >= TODO_LEVEL_MAX) {
        snprintf(buf, sizeof(buf), "MAX");
    } else {
        snprintf(buf, sizeof(buf), "%ld / %ld", (long)(xp - todo_xp_for_level(level)), (long)(todo_xp_for_level(level + 1) - todo_xp_for_level(level)));
    }
    ui_text(ui, &todo_serif_12, r.x + 160, r.y + 9, buf, k->muted);
}

void ui_status_line(cydui_ui *ui, todo_palette pal, const char *text)
{
    const todo_colors *k = todo_colors_of(pal);
    cydui_draw_rect(&ui->ctx, rect_of(0, SCREEN_H - STATUS_H, SCREEN_W, STATUS_H), k->wood, 255);
    ui_text(ui, &todo_serif_12, 6, SCREEN_H - STATUS_H + 1, text, k->on_wood);
    ui_text(ui, &todo_serif_12, SCREEN_W - 6 - cydui_text_size(&todo_serif_12, TODO_VERSION, CYDUI_NTS, 0).w, SCREEN_H - STATUS_H + 1, TODO_VERSION,
            k->wood_light);
}

void ui_toast(cydui_ui *ui, todo_palette pal, const char *text)
{
    const todo_colors *k = todo_colors_of(pal);
    cydui_text_extent e = cydui_text_size(&todo_serif_bold_16, text, CYDUI_NTS, 0);
    int w = e.w + 28, h = 26;
    cydui_rect r = rect_of((SCREEN_W - w) / 2, SCREEN_H - STATUS_H - h - 6, w, h);

    cydui_draw_rect(&ui->ctx, r, k->gold_dark, 255);
    cydui_draw_rect(&ui->ctx, rect_of(r.x + 2, r.y + 2, r.w - 4, r.h - 4), k->gold, 255);
    ui_text_center(ui, &todo_serif_bold_16, r, text, k->on_gold);
}

bool ui_wax_button(cydui_ui *ui, todo_palette pal, const char *label, int w, int h)
{
    cydui_mods m;
    cydui_id id;
    cydui_response rc;
    cydui_rect r;

    if (!cydui_widget_begin(ui)) {
        return false;
    }
    m = cydui_take_mods(ui);
    id = cydui_id_str(ui, label);
    cydui_id_claim(ui, id);
    r = cydui_alloc(ui, &m, w, h);
    rc = cydui_interact(ui, id, r, CYDUI_SENSE_CLICK);
    todo_paint_wax_button(&ui->ctx, pal, r, label, rc.pressed);
    return rc.clicked;
}

static void arrow_button(cydui_ui *ui, cydui_rect r, const char *label, bool pressed)
{
    cydui_paint_info pi;

    memset(&pi, 0, sizeof(pi));
    pi.kind = CYDUI_WIDGET_BUTTON;
    pi.state = pressed ? CYDUI_STATE_PRESSED : CYDUI_STATE_NORMAL;
    pi.rect = r;
    pi.text = label;
    pi.text_len = strlen(label);
    pi.text_extent = cydui_text_size(&todo_serif_16, label, pi.text_len, 0);
    cydui_paint(ui, &pi);
}

/* "<  December 2026  >": returns -1 for the previous month, +1 for the next, 0 for neither. */
int ui_month_header(cydui_ui *ui, todo_palette pal, int y, int m)
{
    const todo_colors *k = todo_colors_of(pal);
    cydui_mods mods;
    cydui_id id_prev, id_next;
    cydui_rect r, prev, next;
    cydui_response rp, rn;
    char title[32];

    if (!cydui_widget_begin(ui)) {
        return 0;
    }
    mods = cydui_take_mods(ui);
    cydui_push_id_str(ui, "month");
    id_prev = cydui_id_str(ui, "prev");
    id_next = cydui_id_str(ui, "next");
    cydui_id_claim(ui, id_prev);
    cydui_id_claim(ui, id_next);
    r = cydui_alloc(ui, &mods, SCREEN_W, TAB_H);
    prev = rect_of(r.x + 4, r.y + 3, 48, 26);
    next = rect_of(r.x + r.w - 52, r.y + 3, 48, 26);
    rp = cydui_interact(ui, id_prev, rect_of(r.x, r.y, 56, r.h), CYDUI_SENSE_CLICK);
    rn = cydui_interact(ui, id_next, rect_of(r.x + r.w - 56, r.y, 56, r.h), CYDUI_SENSE_CLICK);
    arrow_button(ui, prev, "<", rp.pressed);
    arrow_button(ui, next, ">", rn.pressed);
    snprintf(title, sizeof(title), "%s %d", todo_month_name(m), y);
    ui_text_center(ui, &todo_serif_bold_16, r, title, k->text);
    cydui_pop_id(ui);
    return rp.clicked ? -1 : (rn.clicked ? 1 : 0);
}

/*
 * The month: one target over the whole grid, the cell is worked out from where the finger let go. A square at the corner
 * of a day with an event, a ring round today, a gold block on the selected day. `tapped_day` is the day tapped (1..31),
 * 0 for a blank cell.
 */
bool ui_month_grid(cydui_ui *ui, todo_palette pal, int y, int m, uint32_t event_days, int today_day, int selected_day, int *tapped_day)
{
    const todo_colors *k = todo_colors_of(pal);
    const int rows = cal_rows(y, m), ch = cal_cell_h(rows);
    cydui_mods mods;
    cydui_id id;
    cydui_response rc;
    cydui_rect r;
    int row, col, d;
    bool tapped = false;
    char num[4];

    *tapped_day = 0;
    if (!cydui_widget_begin(ui)) {
        return false;
    }
    mods = cydui_take_mods(ui);
    id = cydui_id_str(ui, "grid");
    cydui_id_claim(ui, id);
    r = cydui_alloc(ui, &mods, SCREEN_W, rows * ch);
    r.x = (int16_t)((SCREEN_W - GRID_W) / 2);
    rc = cydui_interact(ui, id, r, CYDUI_SENSE_CLICK);
    if (rc.clicked && cal_cell_at(rows, ch, rc.x - r.x, rc.y - r.y, &row, &col)) {
        *tapped_day = todo_grid_day(y, m, row, col);
        tapped = *tapped_day != 0;
    }

    cydui_draw_rect(&ui->ctx, rect_of(r.x, r.y, GRID_W, rows * ch), k->card, 255);
    for (row = 0; row < rows; row++) {
        for (col = 0; col < 7; col++) {
            cydui_rect cell = rect_of(r.x + col * GRID_CELL_W, r.y + row * ch, GRID_CELL_W, ch);
            d = todo_grid_day(y, m, row, col);
            if (d == 0) {
                continue;
            }
            if (d == selected_day) {
                cydui_draw_rect(&ui->ctx, rect_of(cell.x + 3, cell.y + 1, cell.w - 6, cell.h - 2), k->gold, 255);
            }
            if (d == today_day) { /* a ring of four thin bars */
                cydui_rect o = rect_of(cell.x + 3, cell.y + 1, cell.w - 6, cell.h - 2);
                cydui_draw_rect(&ui->ctx, rect_of(o.x, o.y, o.w, 2), k->gold_dark, 255);
                cydui_draw_rect(&ui->ctx, rect_of(o.x, o.y + o.h - 2, o.w, 2), k->gold_dark, 255);
                cydui_draw_rect(&ui->ctx, rect_of(o.x, o.y, 2, o.h), k->gold_dark, 255);
                cydui_draw_rect(&ui->ctx, rect_of(o.x + o.w - 2, o.y, 2, o.h), k->gold_dark, 255);
            }
            snprintf(num, sizeof(num), "%d", d);
            ui_text_center(ui, &todo_serif_16, cell, num, d == selected_day ? k->on_gold : k->text);
            if (event_days & (1u << d)) {
                cydui_draw_rect(&ui->ctx, rect_of(cell.x + cell.w - 11, cell.y + 5, 5, 5), d == selected_day ? k->on_gold : k->wax, 255);
            }
        }
    }
    return tapped;
}

/* A row of a list in a popup: a card with a title, and a line of small text below it. */
bool ui_row_button(cydui_ui *ui, todo_palette pal, const char *title, const char *sub, bool muted)
{
    const todo_colors *k = todo_colors_of(pal);
    cydui_mods mods;
    cydui_id id;
    cydui_rect r;
    cydui_response rc;

    if (!cydui_widget_begin(ui)) {
        return false;
    }
    mods = cydui_take_mods(ui);
    id = cydui_id_str(ui, "row");
    cydui_id_claim(ui, id);
    r = cydui_alloc(ui, &mods, cydui_layout_avail(ui).w, CARD_H);
    r = rect_of(r.x, r.y + 1, r.w, CARD_H - 2);
    rc = cydui_interact(ui, id, r, CYDUI_SENSE_CLICK);
    todo_paint_card(&ui->ctx, pal, r, muted);
    todo_draw_text_ellipsis(&ui->ctx, &todo_serif_16, r.x + 10, r.y + 1, r.w - 20, title, muted ? k->muted : k->text);
    todo_draw_text_ellipsis(&ui->ctx, &todo_serif_12, r.x + 10, r.y + 19, r.w - 20, sub, k->muted);
    return rc.clicked;
}

/* Text that wraps at the width the container has, in any font and color (the library's own label is in the theme's font). */
void ui_wrapped(cydui_ui *ui, const cydui_font *f, cydui_color c, const char *s)
{
    cydui_mods m = cydui_take_mods(ui);
    int w = cydui_layout_avail(ui).w;
    cydui_text_extent e = cydui_text_size(f, s, CYDUI_NTS, (int16_t)w);
    cydui_rect r;

    if (!cydui_widget_begin(ui)) {
        return;
    }
    r = cydui_alloc(ui, &m, w, e.h);
    cydui_draw_text(&ui->ctx, f, r.x, r.y, (int16_t)w, CYDUI_ALIGN_LEFT, s, CYDUI_NTS, c, 255);
}
