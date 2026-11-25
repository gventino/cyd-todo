#ifndef TODO_UI_INTERNAL_H
#define TODO_UI_INTERNAL_H

/* Shared by the files of the UI component; not part of its interface. */

#include <stddef.h>

#include "todo/app.h"
#include "todo/datetime.h"
#include "todo/levels.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define TAB_H 32
#define STATUS_H 16
#define CONTENT_H (SCREEN_H - TAB_H - STATUS_H)
#define CARD_H 36
#define GRID_CELL_W 45
#define GRID_W (7 * GRID_CELL_W)

static inline cydui_rect rect_of(int x, int y, int w, int h)
{
    return (cydui_rect){(int16_t)x, (int16_t)y, (int16_t)(w > 0 ? w : 0), (int16_t)(h > 0 ? h : 0)};
}

/* the clock */
int32_t app_today(const todo_app *a);
void app_toast(todo_app *a, const char *text);

/* calendar.c: pure helpers */
int cal_rows(int y, int m);                       /* rows of the month grid: 4 to 6 */
int cal_cell_h(int rows);                         /* 26 px, or 22 when six rows must fit */
bool cal_cell_at(int rows, int cell_h, int x, int y, int *row, int *col); /* x, y relative to the grid */
bool event_on_day(const todo_event *e, int64_t d0, int64_t d1);
uint32_t cal_event_days(const todo_store *s, int y, int m, int off_min); /* bit d set: some event touches day d */
void event_when(const todo_event *e, int64_t day_start, int off_min, char *out, size_t cap); /* "10:00-10:45", "Until 01:30" ... */
int quests_due_on(const todo_store *s, int32_t due);
void day_title(int y, int m, int d, char *out, size_t cap);       /* "Wed, Dec 2" */
void instant_text(int64_t t, int off_min, char *out, size_t cap); /* "Wed, Dec 2, 10:00" */

/* requests */
bool app_submit(todo_app *a, const todo_request *req);
void app_want_quests(todo_app *a);
void app_want_events(todo_app *a);
void app_open_detail(todo_app *a, todo_item_kind kind, int32_t id, bool from_agenda);

/* widgets.c */
void ui_text(cydui_ui *ui, const cydui_font *f, int x, int y, const char *s, cydui_color c);
void ui_text_center(cydui_ui *ui, const cydui_font *f, cydui_rect box, const char *s, cydui_color c);
bool ui_quest_card(cydui_ui *ui, todo_palette pal, const todo_quest *q, int32_t today, bool time_valid, bool *toggle, bool *open);
void ui_xp_header(cydui_ui *ui, todo_palette pal, int32_t xp);
void ui_wrapped(cydui_ui *ui, const cydui_font *f, cydui_color c, const char *s);
bool ui_wax_button(cydui_ui *ui, todo_palette pal, const char *label, int w, int h);
int ui_month_header(cydui_ui *ui, todo_palette pal, int y, int m);
bool ui_month_grid(cydui_ui *ui, todo_palette pal, int y, int m, uint32_t event_days, int today_day, int selected_day, int *tapped_day);
bool ui_row_button(cydui_ui *ui, todo_palette pal, const char *title, const char *sub, bool muted);
void ui_status_line(cydui_ui *ui, todo_palette pal, const char *text);
void ui_toast(cydui_ui *ui, todo_palette pal, const char *text);

/* screens */
void screen_quests(todo_app *a, cydui_ui *ui);
void screen_calendar(todo_app *a, cydui_ui *ui);
void overlays_frame(todo_app *a, cydui_ui *ui); /* whatever popup or page is open */

#endif /* TODO_UI_INTERNAL_H */
