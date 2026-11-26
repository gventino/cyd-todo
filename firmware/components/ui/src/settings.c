/* The Settings tab: what the board shows about itself and the few things that can be changed on it. */

#include <stdio.h>

#include "internal.h"
#include "todo/settings.h"

int tz_step(int off_min, int dir)
{
    int i;

    if (dir > 0) {
        for (i = 0; i < todo_zone_count(); i++) {
            if (todo_zone_at(i) > off_min) {
                return todo_zone_at(i);
            }
        }
        return todo_zone_at(0);
    }
    for (i = todo_zone_count() - 1; i >= 0; i--) {
        if (todo_zone_at(i) < off_min) {
            return todo_zone_at(i);
        }
    }
    return todo_zone_at(todo_zone_count() - 1);
}

void tz_text(int off_min, char *out, size_t cap)
{
    int a = off_min < 0 ? -off_min : off_min;
    snprintf(out, cap, "UTC%c%02d:%02d", off_min < 0 ? '-' : '+', a / 60, a % 60);
}

static const char *const BACKLIGHT[] = {"30 s", "1 min", "5 min", "Never"};

int backlight_count(void)
{
    return (int)(sizeof(BACKLIGHT) / sizeof(BACKLIGHT[0]));
}

const char *backlight_text(int index)
{
    return index >= 0 && index < backlight_count() ? BACKLIGHT[index] : "?";
}

void screen_settings(todo_app *a, cydui_ui *ui)
{
    char zone[16];
    int d;

    cydui_spacer(ui, 3);
    ui_setting_row(ui, a->palette, "Wi-Fi", a->wifi_name, ROW_INFO);
    ui_setting_row(ui, a->palette, "Server", a->server, ROW_INFO);
    tz_text(a->off_min, zone, sizeof(zone));
    d = ui_setting_row(ui, a->palette, "Time zone", zone, ROW_ARROWS);
    if (d) {
        a->off_min = tz_step(a->off_min, d);
        a->settings_rev++;
        app_want_events(a); /* the days shift, so the window of events does too */
    }
    d = ui_setting_row(ui, a->palette, "Backlight", backlight_text(a->backlight_index), ROW_ARROWS);
    if (d) {
        a->backlight_index = (a->backlight_index + d + backlight_count()) % backlight_count();
        a->settings_rev++;
    }
    if (ui_setting_row(ui, a->palette, "Theme", a->palette == TODO_PALETTE_PARCHMENT ? "Day" : "Night", ROW_TAP)) {
        a->palette = a->palette == TODO_PALETTE_PARCHMENT ? TODO_PALETTE_CANDLELIGHT : TODO_PALETTE_PARCHMENT;
        a->settings_rev++;
    }
    if (ui_setting_row(ui, a->palette, "Change Wi-Fi", "Start setup", ROW_TAP)) {
        a->overlay = TODO_OV_CONFIRM_WIFI;
    }
}
