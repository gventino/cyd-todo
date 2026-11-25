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

static inline cydui_rect rect_of(int x, int y, int w, int h)
{
    return (cydui_rect){(int16_t)x, (int16_t)y, (int16_t)(w > 0 ? w : 0), (int16_t)(h > 0 ? h : 0)};
}

/* the clock */
int32_t app_today(const todo_app *a);
void app_toast(todo_app *a, const char *text);

/* requests */
bool app_submit(todo_app *a, const todo_request *req);
void app_refresh_quests(todo_app *a);

/* widgets.c */
void ui_text(cydui_ui *ui, const cydui_font *f, int x, int y, const char *s, cydui_color c);
void ui_text_center(cydui_ui *ui, const cydui_font *f, cydui_rect box, const char *s, cydui_color c);
bool ui_quest_card(cydui_ui *ui, todo_palette pal, const todo_quest *q, int32_t today, bool time_valid, bool *toggle, bool *open);
void ui_xp_header(cydui_ui *ui, todo_palette pal, int32_t xp);
void ui_status_line(cydui_ui *ui, todo_palette pal, const char *text);
void ui_toast(cydui_ui *ui, todo_palette pal, const char *text);

/* screens */
void screen_quests(todo_app *a, cydui_ui *ui);

#endif /* TODO_UI_INTERNAL_H */
