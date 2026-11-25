/* The popups: the agenda of a day, the detail of a quest or an event, and the question before a delete. */

#include <stdio.h>
#include <string.h>

#include "internal.h"

/* ---- the agenda: the events of the day first, then the quests due that day ---- */

/* The k-th item of the day: an event (index into the cache) or a quest. */
static bool agenda_item(const todo_app *a, int64_t d0, int32_t due, int k, bool *is_event, int *index)
{
    int i;

    for (i = 0; i < a->store.n_events; i++) {
        if (event_on_day(&a->store.events[i], d0, d0 + TODO_DAY) && k-- == 0) {
            *is_event = true;
            *index = i;
            return true;
        }
    }
    for (i = 0; i < a->store.n_quests; i++) {
        if (a->store.quests[i].due == due && k-- == 0) {
            *is_event = false;
            *index = i;
            return true;
        }
    }
    return false;
}

static int agenda_count(const todo_app *a, int64_t d0, int32_t due)
{
    int i, n = quests_due_on(&a->store, due);

    for (i = 0; i < a->store.n_events; i++) {
        n += event_on_day(&a->store.events[i], d0, d0 + TODO_DAY);
    }
    return n;
}

static void agenda(todo_app *a, cydui_ui *ui)
{
    const todo_colors *k = todo_colors_of(a->palette);
    const int64_t d0 = todo_local_midnight(a->cal_year, a->cal_month, a->cal_day, a->off_min);
    const int32_t due = todo_due_pack(a->cal_year, a->cal_month, a->cal_day);
    const int count = agenda_count(a, d0, due);
    bool open = true, close = false, add = false;
    int32_t first, last, i;
    todo_item_kind open_kind = TODO_ITEM_QUEST;
    int32_t open_id = 0;
    char title[32];

    if (cydui_popup_begin(ui, "agenda", &open, 312, 224, 0)) {
        cydui_rect r = cydui_alloc_rect(ui, cydui_layout_avail(ui).w, 24);
        day_title(a->cal_year, a->cal_month, a->cal_day, title, sizeof(title));
        ui_text(ui, &todo_serif_bold_16, r.x, r.y + 2, title, k->text);
        if (count == 0) {
            r = cydui_alloc_rect(ui, cydui_layout_avail(ui).w, 4 * CARD_H);
            ui_text_center(ui, &todo_serif_16, r, "Nothing planned", k->muted);
        } else {
            cydui_list_begin(ui, "agenda-list", count, CARD_H, 4 * CARD_H, &first, &last);
            for (i = first; i < last; i++) {
                bool is_event;
                int idx;
                char sub[48];
                cydui_begin_list_item(ui, i);
                cydui_next_stretch(ui);
                if (agenda_item(a, d0, due, (int)i, &is_event, &idx)) {
                    if (is_event) {
                        event_when(&a->store.events[idx], d0, a->off_min, sub, sizeof(sub));
                        if (ui_row_button(ui, a->palette, a->store.events[idx].title, sub, false)) {
                            open_kind = TODO_ITEM_EVENT;
                            open_id = a->store.events[idx].id;
                        }
                    } else {
                        const todo_quest *q = &a->store.quests[idx];
                        snprintf(sub, sizeof(sub), "%s", q->done ? "Quest, done" : "Quest due");
                        if (ui_row_button(ui, a->palette, q->title, sub, q->done)) {
                            open_kind = TODO_ITEM_QUEST;
                            open_id = q->id;
                        }
                    }
                }
                cydui_end_list_item(ui);
            }
            cydui_list_end(ui);
        }
        cydui_spacer(ui, 6);
        cydui_begin_row(ui);
        cydui_set_spacing(ui, 8);
        cydui_next_width(ui, 130);
        add = cydui_button(ui, "Add event");
        cydui_next_width(ui, 100);
        close = cydui_button(ui, "Close");
        cydui_end_row(ui);
        cydui_popup_end(ui);
    }
    if (add) {
        app_open_qr(a, TODO_LINK_EVENT);
    } else if (open_id) {
        app_open_detail(a, open_kind, open_id, true);
    } else if (!open || close) {
        a->overlay = TODO_OV_NONE;
    }
}

/* ---- the detail ---- */

void app_open_detail(todo_app *a, todo_item_kind kind, int32_t id, bool from_agenda)
{
    a->overlay = TODO_OV_DETAIL;
    a->item_kind = kind;
    a->item_id = id;
    a->detail_from_agenda = from_agenda;
}

static void leave_detail(todo_app *a)
{
    a->overlay = a->detail_from_agenda ? TODO_OV_AGENDA : TODO_OV_NONE;
}

static void detail(todo_app *a, cydui_ui *ui)
{
    const todo_colors *k = todo_colors_of(a->palette);
    const todo_quest *q = a->item_kind == TODO_ITEM_QUEST ? todo_find_quest(&a->store, a->item_id) : NULL;
    const todo_event *e = a->item_kind == TODO_ITEM_EVENT ? todo_find_event(&a->store, a->item_id) : NULL;
    bool open = true, del = false, close = false;
    const char *text;
    char line[48];

    if (!q && !e) { /* it was deleted somewhere else */
        leave_detail(a);
        return;
    }
    if (cydui_popup_begin(ui, "detail", &open, 296, 200, 0)) {
        cydui_scroll_begin(ui, "detail-body", 128);
        ui_wrapped(ui, &todo_serif_bold_16, k->text, q ? q->title : e->title);
        if (q) {
            cydui_rect r = cydui_alloc_rect(ui, cydui_layout_avail(ui).w, 20);
            todo_draw_stars(&ui->ctx, r.x, r.y + 4, q->difficulty, 5, k->gold_dark, k->star_off);
            if (q->due) {
                int y, m, d;
                todo_due_unpack(q->due, &y, &m, &d);
                snprintf(line, sizeof(line), "Due %s %d", todo_month_name(m), d);
                ui_text(ui, &todo_serif_12, r.x + 80, r.y + 3, line, k->muted);
            }
            if (q->done) {
                ui_text(ui, &todo_serif_12, r.x + r.w - 44, r.y + 3, "Done", k->muted);
            }
            text = q->notes;
        } else {
            cydui_rect r = cydui_alloc_rect(ui, cydui_layout_avail(ui).w, 32);
            instant_text(e->start, a->off_min, line, sizeof(line));
            ui_text(ui, &todo_serif_12, r.x, r.y + 1, "From", k->muted);
            ui_text(ui, &todo_serif_12, r.x + 34, r.y + 1, line, k->text);
            instant_text(e->end, a->off_min, line, sizeof(line));
            ui_text(ui, &todo_serif_12, r.x, r.y + 16, "To", k->muted);
            ui_text(ui, &todo_serif_12, r.x + 34, r.y + 16, line, k->text);
            text = e->description;
        }
        if (text[0]) {
            ui_wrapped(ui, &todo_serif_16, k->text, text);
        }
        cydui_scroll_end(ui);
        cydui_spacer(ui, 8);
        cydui_begin_row(ui);
        cydui_set_spacing(ui, 8);
        del = ui_wax_button(ui, a->palette, "Delete", 110, 32);
        cydui_next_width(ui, 100);
        close = cydui_button(ui, "Close");
        cydui_end_row(ui);
        cydui_popup_end(ui);
    }
    if (del) {
        a->overlay = TODO_OV_CONFIRM;
    } else if (!open || close) {
        leave_detail(a);
    }
}

/* ---- the question before a delete ---- */

static void confirm(todo_app *a, cydui_ui *ui)
{
    const todo_colors *k = todo_colors_of(a->palette);
    const todo_quest *q = a->item_kind == TODO_ITEM_QUEST ? todo_find_quest(&a->store, a->item_id) : NULL;
    const todo_event *e = a->item_kind == TODO_ITEM_EVENT ? todo_find_event(&a->store, a->item_id) : NULL;
    bool open = true, yes = false, no = false;
    char line[40];

    if (!q && !e) {
        a->overlay = TODO_OV_NONE;
        return;
    }
    if (cydui_popup_begin(ui, "confirm", &open, 264, 142, CYDUI_POPUP_MODAL)) {
        cydui_rect r;
        snprintf(line, sizeof(line), "Delete this %s?", q ? "quest" : "event");
        r = cydui_alloc_rect(ui, cydui_layout_avail(ui).w, 24);
        ui_text(ui, &todo_serif_bold_16, r.x, r.y + 2, line, k->text);
        r = cydui_alloc_rect(ui, cydui_layout_avail(ui).w, 44);
        todo_draw_text_ellipsis(&ui->ctx, &todo_serif_16, r.x, r.y + 2, r.w, q ? q->title : e->title, k->text);
        ui_text(ui, &todo_serif_12, r.x, r.y + 24, "This cannot be undone.", k->muted);
        cydui_begin_row(ui);
        cydui_set_spacing(ui, 8);
        cydui_next_width(ui, 100);
        no = cydui_button(ui, "Cancel");
        yes = ui_wax_button(ui, a->palette, "Delete", 110, 32);
        cydui_end_row(ui);
        cydui_popup_end(ui);
    }
    if (yes) {
        todo_request req = todo_req(q ? TODO_REQ_DELETE_QUEST : TODO_REQ_DELETE_EVENT);
        req.id = a->item_id;
        if (app_submit(a, &req)) {
            a->overlay = TODO_OV_NONE;
        } else {
            app_toast(a, "Busy, try again");
        }
    } else if (no || !open) {
        a->overlay = TODO_OV_DETAIL;
    }
}

/* ---- "Change Wi-Fi?" ---- */

static void confirm_wifi(todo_app *a, cydui_ui *ui)
{
    const todo_colors *k = todo_colors_of(a->palette);
    bool open = true, yes = false, no = false;

    if (cydui_popup_begin(ui, "confirm-wifi", &open, 272, 168, CYDUI_POPUP_MODAL)) {
        cydui_rect r = cydui_alloc_rect(ui, cydui_layout_avail(ui).w, 24);
        ui_text(ui, &todo_serif_bold_16, r.x, r.y + 2, "Change the Wi-Fi?", k->text);
        ui_wrapped(ui, &todo_serif_12, k->text, "The board opens its own network, and you join it from your phone to pick another one.");
        ui_wrapped(ui, &todo_serif_12, k->muted, "Nothing is erased until the new one works.");
        cydui_spacer(ui, 10);
        cydui_begin_row(ui);
        cydui_set_spacing(ui, 8);
        cydui_next_width(ui, 100);
        no = cydui_button(ui, "Cancel");
        cydui_next_width(ui, 120);
        yes = cydui_button(ui, "Continue");
        cydui_end_row(ui);
        cydui_popup_end(ui);
    }
    if (yes) {
        app_request_setup(a);
        todo_app_enter_setup(a, NULL, NULL, "", a->has_credentials || a->wifi_name[0] != '\0');
    } else if (no || !open) {
        a->overlay = TODO_OV_NONE;
    }
}

void overlays_frame(todo_app *a, cydui_ui *ui)
{
    switch (a->overlay) {
    case TODO_OV_AGENDA:
        agenda(a, ui);
        break;
    case TODO_OV_DETAIL:
        detail(a, ui);
        break;
    case TODO_OV_CONFIRM:
        confirm(a, ui);
        break;
    case TODO_OV_CONFIRM_WIFI:
        confirm_wifi(a, ui);
        break;
    default:
        break;
    }
}
