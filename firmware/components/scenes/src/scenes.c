#include "scenes.h"

#include <stdio.h>
#include <string.h>

#include "todo/datetime.h"

static int64_t at(int y, int m, int d, int hh, int mm)
{
    return todo_local_midnight(y, m, d, SCENE_OFFSET) + hh * 3600 + mm * 60;
}

void scene_data(todo_memrepo *m, bool worst)
{
    char title[TODO_TITLE_MAX + 1], notes[TODO_TEXT_MAX + 1];
    int i;

    if (!worst) {
        todo_memrepo_seed_demo(m);
        return;
    }
    memset(title, 'W', TODO_TITLE_MAX);
    title[TODO_TITLE_MAX] = '\0';
    for (i = 0; i < TODO_TEXT_MAX; i++) {
        notes[i] = (char)(i % 17 == 16 ? ' ' : 'M');
    }
    notes[TODO_TEXT_MAX] = '\0';
    for (i = 0; i < 60; i++) { /* more than the cache holds, a third of them done */
        todo_memrepo_add_quest(m, title, notes, 5, 20261201 + i % 28);
        if (i % 3 == 0) {
            m->quests[m->n_quests - 1].done = true;
            m->quests[m->n_quests - 1].done_at = m->now - i * 60;
        }
    }
    for (i = 0; i < 40; i++) { /* a crowded day, and every kind of event around it */
        todo_memrepo_add_event(m, title, notes, at(2026, 12, 3, 0, 0) + i * 1800, at(2026, 12, 3, 0, 0) + i * 1800 + 3600 + (i % 3) * 86400);
    }
    for (i = 0; i < 20; i++) {
        todo_memrepo_add_event(m, title, notes, at(2026, 12, 1 + i, 9, 0), at(2026, 12, 1 + i, 10, 0));
    }
    for (i = 1; i <= 31; i++) { /* August 2026 has six rows: a dot on every day */
        todo_memrepo_add_event(m, "Every day", "", at(2026, 8, i, 9, 0), at(2026, 8, i, 10, 0));
    }
}

static const char *const NAMES[] = {"quests",  "calendar",     "calendar-six-rows", "agenda",        "detail-quest", "detail-event",
                                    "confirm", "settings",     "confirm-wifi",      "qr-quest",      "qr-event",     "setup",
                                    "setup-address", "setup-waiting", "joining", "toast"};

int scene_count(void)
{
    return (int)(sizeof(NAMES) / sizeof(NAMES[0]));
}

static const char *const WORST[] = {"quests", "agenda", "detail-quest", "calendar-six-rows", "setup"};

int scene_worst_count(void)
{
    return (int)(sizeof(WORST) / sizeof(WORST[0]));
}

const char *scene_worst_name(int i)
{
    return i >= 0 && i < scene_worst_count() ? WORST[i] : NULL;
}

const char *scene_name(int i)
{
    return i >= 0 && i < scene_count() ? NAMES[i] : NULL;
}

bool scene_apply(todo_app *a, const char *name)
{
    if (!strcmp(name, "quests")) {
        return true;
    }
    if (!strcmp(name, "calendar") || !strcmp(name, "calendar-six-rows") || !strcmp(name, "agenda") || !strcmp(name, "detail-event")) {
        a->tab = TODO_TAB_CALENDAR;
    }
    if (!strcmp(name, "calendar-six-rows")) {
        a->cal_year = 2026;
        a->cal_month = 8;
        a->events_dirty = true;
        return true;
    }
    if (!strcmp(name, "calendar")) {
        return true;
    }
    if (!strcmp(name, "agenda")) {
        a->cal_day = 3;
        a->overlay = TODO_OV_AGENDA;
        return true;
    }
    if (!strcmp(name, "detail-event")) {
        a->cal_day = 3;
        a->overlay = TODO_OV_DETAIL;
        a->item_kind = TODO_ITEM_EVENT;
        a->item_id = a->store.n_events ? a->store.events[0].id : 0;
        return true;
    }
    if (!strcmp(name, "detail-quest") || !strcmp(name, "confirm")) {
        a->overlay = !strcmp(name, "confirm") ? TODO_OV_CONFIRM : TODO_OV_DETAIL;
        a->item_kind = TODO_ITEM_QUEST;
        a->item_id = a->store.n_quests ? a->store.quests[0].id : 0;
        return true;
    }
    if (!strcmp(name, "settings") || !strcmp(name, "confirm-wifi")) {
        a->tab = TODO_TAB_SETTINGS;
        a->overlay = !strcmp(name, "confirm-wifi") ? TODO_OV_CONFIRM_WIFI : TODO_OV_NONE;
        return true;
    }
    if (!strcmp(name, "qr-quest") || !strcmp(name, "qr-event")) {
        todo_app_open_link(a, !strcmp(name, "qr-event") ? TODO_LINK_EVENT : TODO_LINK_QUEST);
        return true;
    }
    if (!strcmp(name, "setup") || !strcmp(name, "setup-address")) {
        todo_app_enter_setup(a, "cyd-todo-4F2A", "k7Qm29xT", "Could not join the network called home-net", false);
        a->setup_address = !strcmp(name, "setup-address");
        return true;
    }
    if (!strcmp(name, "setup-waiting")) { /* the platform has been asked for an access point and has not answered yet */
        todo_app_set_ap_on_demand(a, true);
        todo_app_enter_setup(a, "", "", "", true);
        return true;
    }
    if (!strcmp(name, "joining")) {
        todo_app_set_join(a, true, "home-net");
        return true;
    }
    if (!strcmp(name, "toast")) {
        snprintf(a->toast, sizeof(a->toast), "+50 XP  Level 12!");
        a->toast_until = a->ms + 600000u;
        return true;
    }
    return false;
}
