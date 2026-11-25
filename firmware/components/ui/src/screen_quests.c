/* The Quests tab: level and XP, the cards, and the button that asks the phone for a new one. */

#include <stdio.h>

#include "internal.h"

/* Flips a quest at once (the screen does not wait for the server) and asks the server to do the same. */
static void toggle_quest(todo_app *a, todo_quest *q)
{
    const bool done = !q->done;
    const int32_t delta = todo_xp_for_quest(q->difficulty);
    const int level_before = todo_level_for_xp(a->store.xp);
    todo_request req = todo_req(TODO_REQ_SET_QUEST_DONE);
    char text[TODO_TOAST_MAX];

    req.id = q->id;
    req.flag = done;
    if (!app_submit(a, &req)) {
        app_toast(a, "Busy, try again");
        return;
    }
    q->done = done;
    q->done_at = done ? a->now : 0;
    a->store.xp += done ? delta : -delta;
    if (a->store.xp < 0) {
        a->store.xp = 0;
    }
    a->store.open += done ? -1 : 1;
    a->store.done += done ? 1 : -1;
    if (done) {
        const int level = todo_level_for_xp(a->store.xp);
        if (level > level_before) {
            snprintf(text, sizeof(text), "+%ld XP  Level %d!", (long)delta, level);
        } else {
            snprintf(text, sizeof(text), "+%ld XP", (long)delta);
        }
        app_toast(a, text);
    }
}

void screen_quests(todo_app *a, cydui_ui *ui)
{
    int32_t first, last, i;
    const int32_t today = app_today(a);

    cydui_begin_row(ui);
    ui_xp_header(ui, a->palette, a->store.xp);
    cydui_next_width(ui, 96);
    cydui_next_height(ui, 28);
    if (cydui_button(ui, "New quest")) {
        a->overlay = TODO_OV_NONE; /* the QR page comes with step 3 */
    }
    cydui_end_row(ui);

    if (a->store.n_quests == 0) {
        cydui_rect r = cydui_alloc_rect(ui, SCREEN_W, CONTENT_H - TAB_H);
        ui_text_center(ui, &todo_serif_16, r, "No quests yet", todo_colors_of(a->palette)->muted);
        return;
    }
    cydui_list_begin(ui, "quests", a->store.n_quests, CARD_H, CONTENT_H - TAB_H, &first, &last);
    for (i = first; i < last; i++) {
        todo_quest *q = &a->store.quests[i];
        bool toggle, open;
        cydui_begin_list_item(ui, i);
        cydui_next_stretch(ui);
        ui_quest_card(ui, a->palette, q, today, a->time_valid, &toggle, &open);
        cydui_end_list_item(ui);
        if (toggle) {
            toggle_quest(a, q);
        }
        if (open) {
            app_open_detail(a, TODO_ITEM_QUEST, q->id, false);
        }
    }
    cydui_list_end(ui);
}
