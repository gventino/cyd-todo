/* What the calendar needs to know about days, as pure functions (tested without a screen). */

#include <stdio.h>
#include <string.h>

#include "internal.h"

int cal_rows(int y, int m)
{
    int first = todo_weekday(todo_days_from_civil(y, m, 1));
    return (first + todo_days_in_month(y, m) + 6) / 7;
}

int cal_cell_h(int rows)
{
    return rows <= 5 ? 26 : 22;
}

bool cal_cell_at(int rows, int cell_h, int x, int y, int *row, int *col)
{
    if (x < 0 || y < 0 || x >= GRID_W || y >= rows * cell_h) {
        return false;
    }
    *col = x / GRID_CELL_W;
    *row = y / cell_h;
    return *col < 7;
}

bool event_on_day(const todo_event *e, int64_t d0, int64_t d1)
{
    return e->start < d1 && e->end > d0;
}

uint32_t cal_event_days(const todo_store *s, int y, int m, int off_min)
{
    uint32_t mask = 0;
    int64_t from = todo_local_midnight(y, m, 1, off_min);
    int n = todo_days_in_month(y, m), i, d;

    for (i = 0; i < s->n_events; i++) {
        for (d = 1; d <= n; d++) {
            int64_t d0 = from + (int64_t)(d - 1) * TODO_DAY;
            if (event_on_day(&s->events[i], d0, d0 + TODO_DAY)) {
                mask |= 1u << d;
            }
        }
    }
    return mask;
}

/* "Wed 2" style pieces for a UTC instant */
static void day_hhmm(int64_t t, int off_min, char *out, size_t cap)
{
    todo_format_hhmm(out, cap, t, off_min);
}

void event_when(const todo_event *e, int64_t d0, int off_min, char *out, size_t cap)
{
    const int64_t d1 = d0 + TODO_DAY;
    char a[8], b[8];
    todo_local t;

    if (e->start < d0 && e->end > d1) {
        snprintf(out, cap, "All day");
    } else if (e->start < d0) { /* started earlier, ends today */
        if (e->end == d1) {
            snprintf(out, cap, "Until midnight");
        } else {
            day_hhmm(e->end, off_min, b, sizeof(b));
            snprintf(out, cap, "Until %s", b);
        }
    } else if (e->end <= d1) { /* starts and ends today */
        day_hhmm(e->start, off_min, a, sizeof(a));
        if (e->end == d1) {
            snprintf(out, cap, "%s until midnight", a);
        } else {
            day_hhmm(e->end, off_min, b, sizeof(b));
            snprintf(out, cap, "%s-%s", a, b);
        }
    } else { /* starts today, ends another day */
        day_hhmm(e->start, off_min, a, sizeof(a));
        todo_local_fields(e->end, off_min, &t);
        snprintf(out, cap, "%s until %s %02d:%02d", a, todo_weekday_short(t.weekday), t.hour, t.minute);
    }
}

void day_title(int y, int m, int d, char *out, size_t cap)
{
    snprintf(out, cap, "%s, %.3s %d", todo_weekday_short(todo_weekday(todo_days_from_civil(y, m, d))), todo_month_name(m), d);
}

void instant_text(int64_t t, int off_min, char *out, size_t cap)
{
    todo_local l;
    todo_local_fields(t, off_min, &l);
    snprintf(out, cap, "%s, %.3s %d, %02d:%02d", todo_weekday_short(l.weekday), todo_month_name(l.month), l.day, l.hour, l.minute);
}
