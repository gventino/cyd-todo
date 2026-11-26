/* The two full-screen pages with a QR code: the one-time link of a phone page, and the Wi-Fi setup. */

#include <stdio.h>
#include <string.h>

#include "internal.h"

#define PAGE_MS 600000u /* a link lives 10 minutes; so does the setup window */
#define POLL_QR_MS 3000u
#define SETUP_ADDRESS "http://192.168.4.1"

void todo_app_set_ap(todo_app *a, const char *ssid, const char *password)
{
    todo_copy(a->ssid, sizeof(a->ssid), ssid);
    todo_copy(a->password, sizeof(a->password), password);
}

void todo_app_enter_setup(todo_app *a, const char *ssid, const char *password, const char *reason, bool has_credentials)
{
    if (ssid) {
        todo_copy(a->ssid, sizeof(a->ssid), ssid);
    }
    if (password) {
        todo_copy(a->password, sizeof(a->password), password);
    }
    todo_copy(a->reason, sizeof(a->reason), reason ? reason : "");
    a->has_credentials = has_credentials;
    a->setup_address = false;
    a->overlay = TODO_OV_SETUP;
    a->qr_since = a->ms;
    a->qr_text[0] = '\0';
    a->qr.size = 0;
}

void app_open_qr(todo_app *a, todo_link_kind kind)
{
    todo_request q = todo_req(TODO_REQ_NEW_LINK);

    a->overlay = TODO_OV_QR;
    a->qr_kind = kind;
    a->qr_ready = false;
    a->qr_failed = false;
    a->qr_url_pending = false;
    a->qr_since = a->ms;
    a->qr_text[0] = '\0';
    a->qr.size = 0;
    q.link = kind;
    if (!app_submit(a, &q)) {
        a->qr_failed = true;
    }
    if (a->next_poll > a->ms + POLL_QR_MS) { /* the phone is waited for: look every few seconds */
        a->next_poll = a->ms + POLL_QR_MS;
    }
}

todo_event_kind todo_app_take_event(todo_app *a)
{
    unsigned bit;

    for (bit = 1; bit <= TODO_EVT_SETUP_CANCELLED; bit <<= 1) {
        if (a->events & bit) {
            a->events &= ~bit;
            return (todo_event_kind)bit;
        }
    }
    return TODO_EVT_NONE;
}

void todo_app_set_ap_on_demand(todo_app *a, bool on_demand)
{
    a->ap_on_demand = on_demand;
}

void todo_app_set_join(todo_app *a, bool joining, const char *ssid)
{
    a->joining = joining;
    todo_copy(a->join_ssid, sizeof(a->join_ssid), joining && ssid ? ssid : "");
}

void todo_app_set_wifi_name(todo_app *a, const char *ssid)
{
    todo_copy(a->wifi_name, sizeof(a->wifi_name), ssid ? ssid : "");
}

/* The user asked for a (new) access point: with a platform that makes it, forget the old one and say that it is coming. */
void app_request_setup(todo_app *a)
{
    a->events |= TODO_EVT_SETUP_REQUESTED;
    if (a->ap_on_demand) {
        a->ssid[0] = '\0';
        a->password[0] = '\0';
    }
}

void todo_app_get_settings(const todo_app *a, todo_settings *out)
{
    out->zone_min = (int16_t)a->off_min;
    out->backlight = (uint8_t)a->backlight_index;
    out->palette = (uint8_t)a->palette;
}

void todo_app_apply_settings(todo_app *a, const todo_settings *s)
{
    if (!todo_settings_valid(s)) {
        return;
    }
    a->off_min = s->zone_min;
    a->backlight_index = s->backlight;
    a->palette = s->palette == 1 ? TODO_PALETTE_CANDLELIGHT : TODO_PALETTE_PARCHMENT;
    a->events_dirty = true; /* the zone may differ from the one the first window was asked for */
}

void todo_app_refresh_now(todo_app *a)
{
    a->quests_dirty = true;
    a->events_dirty = true;
    a->next_poll = a->ms;
}

void todo_app_leave_setup(todo_app *a)
{
    if (a->overlay == TODO_OV_SETUP) {
        a->overlay = TODO_OV_NONE;
    }
}

void todo_app_open_link(todo_app *a, todo_link_kind kind)
{
    app_open_qr(a, kind);
}

static void mmss(uint32_t ms, char *out, size_t cap)
{
    uint32_t s = ms / 1000;
    snprintf(out, cap, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

/* The code is only re-encoded when the text changed. */
static void sync_code(todo_app *a, cydui_ui *ui, const char *text)
{
    if (text[0] && strcmp(text, a->qr_text) != 0) {
        todo_copy(a->qr_text, sizeof(a->qr_text), text);
        todo_qr_set(&a->qr, ui, text);
    }
}

static void left_zone(todo_app *a, cydui_ui *ui, const char *placeholder)
{
    const todo_colors *k = todo_colors_of(a->palette);
    const int px = a->qr.size > 0 ? a->qr.px : 164;
    cydui_rect r = cydui_alloc_rect(ui, 8 + px + 8, SCREEN_H - STATUS_H);

    if (a->qr.size > 0) {
        todo_qr_draw(ui, &a->qr, r.x + 8, (r.h - px) / 2);
    } else {
        cydui_rect box = rect_of(r.x + 8, (r.h - px) / 2, px, px);
        cydui_draw_rect(&ui->ctx, box, k->card_done, 255);
        ui_text_center(ui, &todo_serif_16, box, placeholder, k->muted);
    }
}

static int column_w(const todo_app *a)
{
    const int px = a->qr.size > 0 ? a->qr.px : 164;
    return SCREEN_W - 8 - px - 8 - 8;
}

static void link_page(todo_app *a, cydui_ui *ui, uint32_t elapsed)
{
    const todo_colors *k = todo_colors_of(a->palette);
    const int colw = column_w(a);
    bool cancel = false, retry = false;
    char left[16], line[48];

    if (a->qr_url_pending) {
        a->qr_url_pending = false;
        sync_code(a, ui, a->qr_url);
        a->qr_ready = a->qr.size > 0;
        a->qr_failed = !a->qr_ready;
    }
    cydui_begin_row(ui);
    left_zone(a, ui, a->qr_failed ? "No link" : "Preparing\xE2\x80\xA6");
    cydui_next_width(ui, colw);
    cydui_begin_column(ui);
    cydui_spacer(ui, 14);
    ui_wrapped(ui, &todo_serif_bold_16, k->text, a->qr_kind == TODO_LINK_EVENT ? "New event" : (a->qr_kind == TODO_LINK_QUEST ? "New quest" : "Phone"));
    cydui_spacer(ui, 6);
    if (a->qr_failed) {
        ui_wrapped(ui, &todo_serif_12, k->alert, a->link == TODO_LINK_OFFLINE ? "The server cannot be reached." : "Could not get a link.");
    } else {
        ui_wrapped(ui, &todo_serif_12, k->text, "Scan this with your phone's camera and write it there.");
        cydui_spacer(ui, 6);
        mmss(elapsed < PAGE_MS ? PAGE_MS - elapsed : 0, left, sizeof(left));
        snprintf(line, sizeof(line), "The link works once, for %s.", left);
        ui_wrapped(ui, &todo_serif_12, k->muted, line);
    }
    cydui_spacer(ui, 14);
    if (a->qr_failed) {
        cydui_next_width(ui, colw - 8);
        retry = cydui_button(ui, "Retry");
        cydui_spacer(ui, 6);
    }
    cydui_next_width(ui, colw - 8);
    cancel = cydui_button(ui, "Cancel");
    cydui_end_column(ui);
    cydui_end_row(ui);
    if (retry) {
        app_open_qr(a, a->qr_kind);
    } else if (cancel) {
        a->overlay = TODO_OV_NONE;
    }
}

static void setup_page(todo_app *a, cydui_ui *ui, uint32_t elapsed)
{
    const todo_colors *k = todo_colors_of(a->palette);
    const int colw = column_w(a);
    bool cancel = false, toggle = false, again = false;
    char text[96], left[16], line[48];

    if (elapsed >= PAGE_MS) {
        cydui_rect r = cydui_alloc_rect(ui, SCREEN_W, 120);
        ui_text_center(ui, &todo_serif_bold_16, r, "Setup timed out", k->text);
        cydui_spacer(ui, 10);
        cydui_begin_row(ui);
        cydui_spacer(ui, 80);
        cydui_next_width(ui, 160);
        again = cydui_button(ui, "Try again");
        cydui_end_row(ui);
        if (again) {
            a->qr_since = a->ms;
            app_request_setup(a);
        }
        return;
    }
    if (a->setup_address) {
        snprintf(text, sizeof(text), "%s", SETUP_ADDRESS);
    } else if (a->ssid[0] == '\0' || !todo_qr_wifi_text(a->ssid, a->password, text, sizeof(text))) { /* no access point yet: no code */
        text[0] = '\0';
    }
    sync_code(a, ui, text);

    cydui_begin_row(ui);
    left_zone(a, ui, "No code");
    cydui_next_width(ui, colw);
    cydui_begin_column(ui);
    cydui_spacer(ui, 8);
    ui_wrapped(ui, &todo_serif_bold_16, k->text, a->setup_address ? "Open the address" : "Set up the Wi-Fi");
    cydui_spacer(ui, 4);
    if (a->reason[0]) {
        ui_wrapped(ui, &todo_serif_12, k->alert, a->reason);
        cydui_spacer(ui, 4);
    }
    ui_wrapped(ui, &todo_serif_12, k->text,
               a->setup_address ? "On the phone that joined the board's network, open this address in the browser."
                                : "1. Scan the code to join the board's network.\n2. Fill in the form that opens.");
    cydui_spacer(ui, 4);
    mmss(PAGE_MS - elapsed, left, sizeof(left));
    snprintf(line, sizeof(line), "Closes in %s", left);
    ui_wrapped(ui, &todo_serif_12, k->muted, line);
    cydui_spacer(ui, 8);
    cydui_next_width(ui, colw - 8);
    toggle = cydui_button(ui, a->setup_address ? "Wi-Fi code" : "Address");
    if (a->has_credentials) {
        cydui_spacer(ui, 6);
        cydui_next_width(ui, colw - 8);
        cancel = cydui_button(ui, "Cancel");
    }
    cydui_end_column(ui);
    cydui_end_row(ui);
    if (toggle) {
        a->setup_address = !a->setup_address;
    }
    if (cancel) {
        a->overlay = TODO_OV_NONE;
        a->events |= TODO_EVT_SETUP_CANCELLED;
    }
}

void qr_page_frame(todo_app *a, cydui_ui *ui)
{
    const uint32_t elapsed = a->ms - a->qr_since;

    cydui_draw_rect(&ui->ctx, rect_of(0, 0, SCREEN_W, SCREEN_H), todo_colors_of(a->palette)->bg, 255);
    if (a->overlay == TODO_OV_QR) {
        if (elapsed >= PAGE_MS) {
            a->overlay = TODO_OV_NONE;
            app_toast(a, "Link expired");
            return;
        }
        link_page(a, ui, elapsed);
    } else {
        setup_page(a, ui, elapsed);
    }
}
