#ifndef TODO_APP_H
#define TODO_APP_H

/*
 * The application's user interface: everything it remembers (todo_app) and one call per frame. It talks to the
 * world only through a todo_repo (requests that return at once, answers that arrive later) and a clock that the
 * caller gives it (todo_app_set_time): it never blocks, never reads the real time and never allocates.
 *
 *     todo_app_init(&app, &repo, -180);
 *     for (;;) {
 *         todo_app_set_time(&app, epoch, valid);
 *         cydui_ui_begin(&ui, 0);
 *         todo_app_frame(&app, &ui);
 *         if (!cydui_ui_end(&ui)) sleep_until_event();
 *     }
 */

#include <stdbool.h>
#include <stdint.h>

#include "cydui/ui.h"
#include "todo/model.h"
#include "todo/qr.h"
#include "todo/repo.h"
#include "todo/theme_quest.h"

#define TODO_TOAST_MAX 40
#define TODO_VERSION "0.1.0-dev"

typedef enum { TODO_TAB_QUESTS, TODO_TAB_CALENDAR, TODO_TAB_SETTINGS, TODO_TAB_COUNT } todo_tab;

typedef enum {
    TODO_OV_NONE,
    TODO_OV_DETAIL,       /* a quest or an event, in full */
    TODO_OV_CONFIRM,      /* "Delete this ...?" */
    TODO_OV_AGENDA,       /* one day of the calendar */
    TODO_OV_QR,           /* the one-time link of a phone page */
    TODO_OV_SETUP,        /* the Wi-Fi setup page */
    TODO_OV_CONFIRM_WIFI  /* "Change Wi-Fi?" */
} todo_overlay;

typedef enum { TODO_ITEM_QUEST, TODO_ITEM_EVENT } todo_item_kind;

typedef enum { TODO_LINK_OK, TODO_LINK_OFFLINE, TODO_LINK_ERROR } todo_link_state;

typedef struct {
    todo_repo *repo;
    todo_store store; /* the cache; the repository writes into it while polled */

    /* settings (kept in memory here; persisted by the board) */
    int off_min;           /* the zone dates are shown in, minutes east of UTC */
    int backlight_index;   /* into the timeouts of the Settings tab; applied by the board */
    todo_palette palette;

    /* the clock, given by the caller */
    int64_t now; /* UTC epoch seconds */
    bool time_valid;
    uint32_t ms; /* the UI's own clock at the last frame, for timers */

    /* navigation */
    todo_tab tab;
    int cal_year, cal_month, cal_day; /* the month shown and the selected day (0: none) */
    todo_overlay overlay;
    todo_item_kind item_kind; /* what the detail and confirm overlays are about */
    int32_t item_id;
    bool detail_from_agenda; /* closing the detail goes back to the agenda */

    /* what the status line says */
    int inflight; /* requests submitted and not yet answered */
    todo_link_state link;
    int64_t synced_at; /* epoch of the last good answer, 0 before one */
    uint32_t error_until;
    char error[TODO_TOAST_MAX];

    char toast[TODO_TOAST_MAX];
    uint32_t toast_until;

    /* the one-time link, and the setup page */
    todo_qr qr;
    todo_link_kind qr_kind;
    bool qr_ready, qr_failed;
    uint32_t qr_since;
    int64_t qr_rev; /* the revision when the page opened: a different one means the phone saved something */
    char ssid[33], password[64], reason[48];
    bool has_credentials, setup_address;
    char wifi_name[33], server[33]; /* what Settings shows */

    /* sync */
    bool started;
    bool quests_dirty, events_dirty; /* a refresh is wanted and not yet submitted */
    uint32_t next_poll;
    int64_t window_from, window_to; /* the events window that was asked for */

    /* the theme the UI uses, rebuilt when the palette changes */
    cydui_theme theme;
    int applied_palette;
} todo_app;

/* An app that shows `repo`'s data in the zone `off_min`. Time is invalid until todo_app_set_time. */
void todo_app_init(todo_app *a, todo_repo *repo, int off_min);

/* The clock: UTC epoch seconds, and whether it is real (SNTP answered). Dates are not drawn before it is. */
void todo_app_set_time(todo_app *a, int64_t epoch, bool valid);

/* Builds one frame: between cydui_ui_begin and cydui_ui_end. Polls the repository (a few answers at most). */
void todo_app_frame(todo_app *a, cydui_ui *ui);

/* Starts the Wi-Fi setup page (what the board does when it has no network). `reason` may be NULL or "". */
void todo_app_enter_setup(todo_app *a, const char *ssid, const char *password, const char *reason, bool has_credentials);

/* The text of the status line, for the frame at `ms`. */
size_t todo_app_status_text(const todo_app *a, char *buf, size_t cap);

#endif /* TODO_APP_H */
