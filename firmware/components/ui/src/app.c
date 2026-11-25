#include "todo/app.h"

#include <stdio.h>
#include <string.h>

#include "internal.h"

#define POLL_IDLE_MS 60000u
#define POLL_QR_MS 3000u
#define ERROR_MS 5000u
#define TOAST_MS 2200u
#define ANSWERS_PER_FRAME 4

void todo_app_init(todo_app *a, todo_repo *repo, int off_min)
{
    memset(a, 0, sizeof(*a));
    a->repo = repo;
    a->off_min = off_min;
    a->palette = TODO_PALETTE_PARCHMENT;
    a->applied_palette = -1;
    snprintf(a->wifi_name, sizeof(a->wifi_name), "home-net");
    snprintf(a->server, sizeof(a->server), "192.168.1.50:8080");
}

void todo_app_set_time(todo_app *a, int64_t epoch, bool valid)
{
    a->now = epoch;
    a->time_valid = valid;
}

int32_t app_today(const todo_app *a)
{
    todo_local t;

    if (!a->time_valid) {
        return 0;
    }
    todo_local_fields(a->now, a->off_min, &t);
    return todo_due_pack(t.year, t.month, t.day);
}

void app_toast(todo_app *a, const char *text)
{
    todo_copy(a->toast, sizeof(a->toast), text);
    a->toast_until = a->ms + TOAST_MS;
}

bool app_submit(todo_app *a, const todo_request *req)
{
    if (!todo_repo_submit(a->repo, req)) {
        return false;
    }
    a->inflight++;
    return true;
}

void app_refresh_quests(todo_app *a)
{
    app_submit(a, &(todo_request){TODO_REQ_REFRESH_QUESTS, 0, 0, 0, false, TODO_LINK_HOME});
}

static void fail(todo_app *a, const char *text)
{
    todo_copy(a->error, sizeof(a->error), text);
    a->error_until = a->ms + ERROR_MS;
}

static void on_result(todo_app *a, const todo_result *r)
{
    if (a->inflight > 0) {
        a->inflight--;
    }
    if (r->offline) {
        a->link = TODO_LINK_OFFLINE;
        return;
    }
    if (!r->ok) {
        a->link = TODO_LINK_ERROR;
        if (r->kind == TODO_REQ_SET_QUEST_DONE || r->kind == TODO_REQ_DELETE_QUEST || r->kind == TODO_REQ_DELETE_EVENT) {
            fail(a, "Could not save");
            app_refresh_quests(a); /* the card goes back to what the server has */
        } else {
            fail(a, "Server error");
        }
        return;
    }
    a->link = TODO_LINK_OK;
    a->synced_at = a->now;
    if (r->kind == TODO_REQ_POLL_CHANGES && r->changed) {
        app_refresh_quests(a);
    }
}

static void schedule(todo_app *a)
{
    if (!a->started) {
        a->started = true;
        app_refresh_quests(a);
        a->next_poll = a->ms + POLL_IDLE_MS;
    }
    if (a->ms >= a->next_poll) {
        todo_request q = todo_req(TODO_REQ_POLL_CHANGES);
        q.a = a->store.rev;
        if (app_submit(a, &q)) {
            a->next_poll = a->ms + (a->overlay == TODO_OV_QR ? POLL_QR_MS : POLL_IDLE_MS);
        }
    }
}

size_t todo_app_status_text(const todo_app *a, char *buf, size_t cap)
{
    todo_local t;
    int n;

    if (!a->time_valid) {
        n = snprintf(buf, cap, "Waiting for time");
    } else if (a->error[0] && a->ms < a->error_until) {
        n = snprintf(buf, cap, "%s", a->error);
    } else if (a->link == TODO_LINK_OFFLINE) {
        n = snprintf(buf, cap, "Offline");
    } else if (a->inflight > 0 || a->synced_at == 0) {
        n = snprintf(buf, cap, "Syncing\xE2\x80\xA6");
    } else {
        todo_local_fields(a->synced_at, a->off_min, &t);
        n = snprintf(buf, cap, "Synced %02d:%02d", t.hour, t.minute);
    }
    return n > 0 ? (size_t)n : 0;
}

static void apply_theme(todo_app *a, cydui_ui *ui)
{
    if (a->applied_palette != (int)a->palette) {
        todo_theme_quest_init(&a->theme, a->palette);
        cydui_set_theme(ui, &a->theme);
        a->applied_palette = (int)a->palette;
    }
}

void todo_app_frame(todo_app *a, cydui_ui *ui)
{
    static const char *const tabs[TODO_TAB_COUNT] = {"Quests", "Calendar", "Settings"};
    todo_result r;
    char status[48];
    int tab = (int)a->tab, answers = 0;

    a->ms = cydui_now(ui);
    apply_theme(a, ui);
    cydui_draw_rect(&ui->ctx, rect_of(0, 0, SCREEN_W, SCREEN_H), todo_colors_of(a->palette)->bg, 255);
    while (answers++ < ANSWERS_PER_FRAME && todo_repo_poll(a->repo, &a->store, &r)) {
        on_result(a, &r);
    }
    schedule(a);

    cydui_set_spacing(ui, 0);
    cydui_tabs(ui, tabs, TODO_TAB_COUNT, &tab);
    a->tab = (todo_tab)tab;
    if (a->tab == TODO_TAB_QUESTS) {
        screen_quests(a, ui);
    }

    todo_app_status_text(a, status, sizeof(status));
    ui_status_line(ui, a->palette, status);
    if (a->toast[0] && a->ms < a->toast_until) {
        ui_toast(ui, a->palette, a->toast);
    }
}
