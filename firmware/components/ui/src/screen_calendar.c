/* The Calendar tab: the month, and a line about the selected day. The agenda of a day is a popup (overlays.c). */

#include <stdio.h>

#include "internal.h"

static void weekday_letters(cydui_ui *ui, todo_palette pal)
{
    static const char *const letters[7] = {"M", "T", "W", "T", "F", "S", "S"};
    const todo_colors *k = todo_colors_of(pal);
    cydui_rect r = cydui_alloc_rect(ui, SCREEN_W, 14);
    int c, x0 = (SCREEN_W - GRID_W) / 2;

    for (c = 0; c < 7; c++) {
        ui_text_center(ui, &todo_serif_12, rect_of(x0 + c * GRID_CELL_W, r.y, GRID_CELL_W, 14), letters[c], k->muted);
    }
}

/* How many quests are due on a day. */
int quests_due_on(const todo_store *s, int32_t due)
{
    int i, n = 0;
    for (i = 0; i < s->n_quests; i++) {
        n += s->quests[i].due == due;
    }
    return n;
}

void screen_calendar(todo_app *a, cydui_ui *ui)
{
    const todo_colors *k = todo_colors_of(a->palette);
    int dir, tapped, today_day = 0, rows, grid_h, i, n_events = 0;
    todo_local t;
    char line[64], day[24];
    cydui_rect r;

    if (!a->time_valid || a->cal_year == 0) {
        r = cydui_alloc_rect(ui, SCREEN_W, CONTENT_H);
        ui_text_center(ui, &todo_serif_16, r, "Waiting for time", k->muted);
        return;
    }
    dir = ui_month_header(ui, a->palette, a->cal_year, a->cal_month);
    if (dir < 0 && !(a->cal_year == 2000 && a->cal_month == 1)) {
        todo_month_prev(&a->cal_year, &a->cal_month);
    } else if (dir > 0 && !(a->cal_year == 2099 && a->cal_month == 12)) {
        todo_month_next(&a->cal_year, &a->cal_month);
    }
    if (dir != 0) {
        a->cal_day = 0;
        app_want_events(a);
    }
    weekday_letters(ui, a->palette);

    todo_local_fields(a->now, a->off_min, &t);
    if (t.year == a->cal_year && t.month == a->cal_month) {
        today_day = t.day;
    }
    rows = cal_rows(a->cal_year, a->cal_month);
    grid_h = rows * cal_cell_h(rows);
    if (ui_month_grid(ui, a->palette, a->cal_year, a->cal_month, cal_event_days(&a->store, a->cal_year, a->cal_month, a->off_min), today_day,
                      a->cal_day, &tapped)) {
        a->cal_day = tapped;
        a->overlay = TODO_OV_AGENDA;
    }

    r = cydui_alloc_rect(ui, SCREEN_W, CONTENT_H - TAB_H - 14 - grid_h);
    if (a->cal_day) {
        const int64_t d0 = todo_local_midnight(a->cal_year, a->cal_month, a->cal_day, a->off_min);
        for (i = 0; i < a->store.n_events; i++) {
            n_events += event_on_day(&a->store.events[i], d0, d0 + TODO_DAY);
        }
        day_title(a->cal_year, a->cal_month, a->cal_day, day, sizeof(day));
        snprintf(line, sizeof(line), "%s: %d event%s, %d quest%s due", day, n_events, n_events == 1 ? "" : "s",
                 quests_due_on(&a->store, todo_due_pack(a->cal_year, a->cal_month, a->cal_day)),
                 quests_due_on(&a->store, todo_due_pack(a->cal_year, a->cal_month, a->cal_day)) == 1 ? "" : "s");
    } else {
        snprintf(line, sizeof(line), "Tap a day to see what is planned");
    }
    ui_text_center(ui, &todo_serif_12, r, line, k->muted);
}
