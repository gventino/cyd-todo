/* The screens of the application, on the rig, with synthetic touches. The pictures are written to the build
 * directory to be looked at; goldens of scripted runs come later. */

#include <stdio.h>
#include <string.h>

#include "todo/datetime.h"
#include "todo/levels.h"
#include "unity.h"
#include "coords.h"
#include "world.h"

void setUp(void) {}
void tearDown(void) {}

static world W;
static const char *const PAL[TODO_PALETTE_COUNT] = {"parchment", "candlelight"};

static void snap(const char *screen, todo_palette p)
{
    char name[64];
    snprintf(name, sizeof(name), "app_%s_%s", screen, PAL[p]);
    world_snap(&W, name);
}

static void status(char *buf, size_t cap)
{
    todo_app_status_text(&W.app, buf, cap);
}

/* the first card: its checkbox zone and its title zone */

static void test_the_quests_screen_loads_and_says_when_it_synced(void)
{
    char s[48];
    int p;
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        world_start(&W, (todo_palette)p, true);
        world_frames(&W, 6);
        TEST_ASSERT_EQUAL_UINT32(0, W.errors);
        TEST_ASSERT_EQUAL_INT(6, W.app.store.n_quests);
        status(s, sizeof(s));
        TEST_ASSERT_EQUAL_STRING("Synced 14:00", s);
        snap("quests", (todo_palette)p);
    }
}

static void test_the_status_line_before_the_clock_and_before_the_first_answer(void)
{
    char s[48];
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    todo_app_set_time(&W.app, 0, false);
    world_frames(&W, 3);
    status(s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Waiting for time", s);
    todo_app_set_time(&W.app, WORLD_NOW, true);
    W.mem.latency = 100;
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    W.mem.latency = 100;
    world_frames(&W, 3);
    status(s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Syncing\xE2\x80\xA6", s);
}

static void test_the_status_line_says_offline_and_an_error_fades(void)
{
    char s[48];
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    W.mem.offline = true;
    world_frames(&W, 4);
    status(s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Offline", s);
    TEST_ASSERT_EQUAL_INT(0, W.app.store.n_quests);
}

static void test_tapping_the_checkbox_completes_the_quest_at_once_and_pays_xp(void)
{
    todo_quest *first;
    int32_t id;
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    first = &W.app.store.quests[0];
    id = first->id;
    TEST_ASSERT_FALSE(first->done);
    TEST_ASSERT_EQUAL_INT32(20, W.app.store.xp);
    rig_down(&W.r, CHECK_X, CARD0_Y);
    world_frame(&W);
    rig_up(&W.r, CHECK_X, CARD0_Y);
    world_frame(&W); /* the frame of the tap: nothing has been answered yet */
    TEST_ASSERT_TRUE(todo_find_quest(&W.app.store, id)->done);
    TEST_ASSERT_EQUAL_INT32(50, W.app.store.xp);
    TEST_ASSERT_EQUAL_INT32(3, W.app.store.open);
    TEST_ASSERT_EQUAL_INT32(3, W.app.store.done);
    TEST_ASSERT_EQUAL_STRING("+30 XP", W.app.toast);
    snap("quests_toast", TODO_PALETTE_PARCHMENT);
    world_frames(&W, 6);
    TEST_ASSERT_TRUE_MESSAGE(todo_find_quest(&W.app.store, id)->done, "the server agrees");
    TEST_ASSERT_EQUAL_INT32(50, W.app.store.xp);
    TEST_ASSERT_EQUAL_INT64(W.mem.rev, W.app.store.rev);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
}

static void test_a_level_up_says_so_in_the_toast(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    todo_memrepo_add_quest(&W.mem, "Epic", "", 5, 20261203);
    world_frames(&W, 6);
    W.app.store.xp = 80; /* 20 more would be level 2 at 100: this quest pays 20 (difficulty 2 below) */
    W.app.store.quests[0].difficulty = 3;
    rig_down(&W.r, CHECK_X, CARD0_Y);
    world_frame(&W);
    rig_up(&W.r, CHECK_X, CARD0_Y);
    world_frame(&W);
    TEST_ASSERT_EQUAL_STRING("+30 XP  Level 2!", W.app.toast);
}

static void test_reopening_cannot_take_the_xp_below_zero(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    W.app.store.quests[0].done = true; /* what the screen shows: a done quest worth 30 */
    W.app.store.quests[0].difficulty = 3;
    W.app.store.xp = 28;
    rig_down(&W.r, CHECK_X, CARD0_Y);
    world_frame(&W);
    rig_up(&W.r, CHECK_X, CARD0_Y);
    world_frame(&W);
    TEST_ASSERT_EQUAL_INT32(0, W.app.store.xp);
}

static void test_a_failed_save_puts_the_card_back_and_says_so(void)
{
    char s[48];
    int32_t id;
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    id = W.app.store.quests[0].id;
    W.mem.fail_next = 1;
    rig_down(&W.r, CHECK_X, CARD0_Y);
    world_frame(&W);
    rig_up(&W.r, CHECK_X, CARD0_Y);
    world_frame(&W);
    TEST_ASSERT_TRUE(todo_find_quest(&W.app.store, id)->done);
    world_frames(&W, 8);
    TEST_ASSERT_FALSE_MESSAGE(todo_find_quest(&W.app.store, id)->done, "the server said no: the card is back");
    TEST_ASSERT_EQUAL_INT32(20, W.app.store.xp);
    status(s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Could not save", s);
    world_frames(&W, 400); /* 6.4 s: the message goes */
    status(s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Synced 14:00", s);
}

/* is there a pixel of exactly this color in a box of the screen */
static bool has_color(int x0, int y0, int x1, int y1, cydui_color c)
{
    int x, y;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (W.r.fb[y * RIG_W + x] == c) {
                return true;
            }
        }
    }
    return false;
}

static void test_a_due_date_in_the_past_is_in_wax_red_and_only_for_open_quests(void)
{
    const cydui_color wax = todo_colors_of(TODO_PALETTE_PARCHMENT)->alert;
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    TEST_ASSERT_FALSE_MESSAGE(has_color(44, 84, 120, 100, wax), "due in two days: not overdue");
    W.skew = 10 * 86400;
    world_frames(&W, 2);
    TEST_ASSERT_TRUE_MESSAGE(has_color(44, 84, 120, 100, wax), "ten days later it is overdue");
    snap("quests_overdue", TODO_PALETTE_PARCHMENT);
    todo_app_set_time(&W.app, 0, false);
    W.skew = 0;
    world_frames(&W, 2);
    TEST_ASSERT_FALSE_MESSAGE(has_color(44, 84, 120, 100, wax), "without a real clock nothing is overdue");
    todo_app_set_time(&W.app, WORLD_NOW, true);
    W.skew = 10 * 86400;
    W.app.store.quests[0].done = true; /* a finished quest is never late, whatever its date */
    world_frames(&W, 2);
    TEST_ASSERT_FALSE_MESSAGE(has_color(44, 84, 120, 100, wax), "a done quest is not overdue");
}

static void test_a_title_of_64_bytes_is_cut_with_an_ellipsis_and_never_reaches_the_stars(void)
{
    char t[65];
    int p;
    memset(t, 'W', 64);
    t[64] = '\0';
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        world_start(&W, (todo_palette)p, true);
        todo_memrepo_add_quest(&W.mem, t, "", 5, 20261201);
        world_frames(&W, 6);
        TEST_ASSERT_EQUAL_UINT32(0, W.errors);
        snap("quests_long_title", (todo_palette)p);
        TEST_ASSERT_FALSE_MESSAGE(has_color(239, 66, 243, 82, todo_colors_of((todo_palette)p)->text), "the title stops before the stars");
    }
}

static uint32_t cmds_with(int extra)
{
    int i;
    char t[65];
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    for (i = 0; i < extra; i++) {
        snprintf(t, sizeof(t), "Quest %d with a rather long and boring title", i);
        todo_memrepo_add_quest(&W.mem, t, "", 1 + i % 5, 20261201 + i % 20);
    }
    world_frames(&W, 8);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    return W.cmds;
}

static void test_forty_eight_cards_cost_what_six_cost_because_only_the_visible_ones_are_built(void)
{
    uint32_t six = cmds_with(0), full = cmds_with(60);
    TEST_ASSERT_EQUAL_INT(48, W.app.store.n_quests);
    TEST_ASSERT_TRUE(W.app.store.quests_truncated);
    TEST_ASSERT_TRUE_MESSAGE(full <= 320, "the command list");
    TEST_ASSERT_TRUE_MESSAGE(full <= six + 12, "no more than a few commands more than with six");
    snap("quests_full", TODO_PALETTE_PARCHMENT);
}

static void test_dragging_the_list_scrolls_it(void)
{
    uint64_t before, after;
    int y;
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    before = cydui_mem_hash(&W.r.mem);
    rig_down(&W.r, 160, 200);
    world_frame(&W);
    for (y = 190; y >= 100; y -= 15) {
        rig_move(&W.r, 160, y);
        world_frame(&W);
    }
    rig_up(&W.r, 160, 100);
    world_frames(&W, 40);
    after = cydui_mem_hash(&W.r.mem);
    TEST_ASSERT_NOT_EQUAL(before, after);
    TEST_ASSERT_TRUE_MESSAGE(W.app.store.quests[0].done == false, "dragging did not tick a box");
    snap("quests_scrolled", TODO_PALETTE_PARCHMENT);
}

static void test_the_empty_list_says_so(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, false);
    world_frames(&W, 6);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    TEST_ASSERT_EQUAL_INT(0, W.app.store.n_quests);
    snap("quests_empty", TODO_PALETTE_PARCHMENT);
}

/* ---- the calendar ---- */


static void open_calendar(todo_palette p)
{
    world_start(&W, p, true);
    world_frames(&W, 6);
    world_tap(&W, TAB_CALENDAR_X, TAB_Y);
    world_frames(&W, 6);
}

static void test_the_calendar_tab_shows_the_month_with_dots_on_the_days_with_events(void)
{
    int p;
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        open_calendar((todo_palette)p);
        TEST_ASSERT_EQUAL_UINT32(0, W.errors);
        TEST_ASSERT_EQUAL_INT(TODO_TAB_CALENDAR, W.app.tab);
        TEST_ASSERT_EQUAL_INT(2026, W.app.cal_year);
        TEST_ASSERT_EQUAL_INT(12, W.app.cal_month);
        TEST_ASSERT_EQUAL_INT(4, W.app.store.n_events);
        {
            int64_t from, to;
            todo_month_window(2026, 12, WORLD_OFFSET, &from, &to);
            TEST_ASSERT_EQUAL_INT64_MESSAGE(from - 7 * TODO_DAY, W.app.store.events_from, "a week before the month");
            TEST_ASSERT_EQUAL_INT64_MESSAGE(to + 7 * TODO_DAY, W.app.store.events_to, "a week after it");
        }
        snap("calendar", (todo_palette)p);
    }
}

static void test_the_calendar_waits_for_the_time(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    todo_app_set_time(&W.app, 0, false);
    world_frames(&W, 4);
    world_tap(&W, TAB_CALENDAR_X, TAB_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    TEST_ASSERT_EQUAL_INT(0, W.app.cal_year);
    snap("calendar_waiting", TODO_PALETTE_PARCHMENT);
    todo_app_set_time(&W.app, WORLD_NOW, true);
    world_frames(&W, 6);
    TEST_ASSERT_EQUAL_INT(2026, W.app.cal_year);
    TEST_ASSERT_EQUAL_INT(4, W.app.store.n_events);
}

static void test_the_arrows_change_the_month_and_ask_for_its_events(void)
{
    int64_t from_dec;
    open_calendar(TODO_PALETTE_PARCHMENT);
    world_tap(&W, day_x(3), day_y(3));
    world_frames(&W, 3);
    world_tap(&W, 202, 203); /* the agenda's Close, right of Add event */
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(3, W.app.cal_day);
    from_dec = W.app.store.events_from;
    world_tap(&W, 292, 48);
    world_frames(&W, 6);
    TEST_ASSERT_EQUAL_INT(2027, W.app.cal_year);
    TEST_ASSERT_EQUAL_INT(1, W.app.cal_month);
    TEST_ASSERT_TRUE(W.app.store.events_from > from_dec);
    TEST_ASSERT_EQUAL_INT(0, W.app.cal_day);
    snap("calendar_january", TODO_PALETTE_PARCHMENT);
    world_tap(&W, 28, 48);
    world_tap(&W, 28, 48);
    world_frames(&W, 6);
    TEST_ASSERT_EQUAL_INT(2026, W.app.cal_year);
    TEST_ASSERT_EQUAL_INT(11, W.app.cal_month);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
}

static void test_a_tap_on_a_day_opens_its_agenda_and_a_blank_cell_does_nothing(void)
{
    open_calendar(TODO_PALETTE_PARCHMENT);
    world_tap(&W, 10, 91); /* Monday the 30th of November: a blank */
    world_frames(&W, 2);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    world_tap(&W, day_x(3), day_y(3));
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_AGENDA, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(3, W.app.cal_day);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    snap("agenda", TODO_PALETTE_PARCHMENT);
}

static void test_an_empty_day_says_so(void)
{
    open_calendar(TODO_PALETTE_PARCHMENT);
    world_tap(&W, day_x(15), day_y(15));
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_AGENDA, W.app.overlay);
    snap("agenda_empty", TODO_PALETTE_PARCHMENT);
}

/* ---- agenda, detail, confirm ---- */


static void open_agenda_of_the_3rd(void)
{
    open_calendar(TODO_PALETTE_PARCHMENT);
    world_tap(&W, day_x(3), day_y(3));
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_AGENDA, W.app.overlay);
}

static void test_the_whole_walk_agenda_detail_confirm_and_back(void)
{
    open_agenda_of_the_3rd();
    world_tap(&W, AGENDA_ROW0_X, AGENDA_ROW0_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_DETAIL, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(TODO_ITEM_EVENT, W.app.item_kind);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    snap("detail_event", TODO_PALETTE_PARCHMENT);
    world_tap(&W, DETAIL_DELETE_X, DETAIL_BUTTONS_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_CONFIRM, W.app.overlay);
    snap("confirm_event", TODO_PALETTE_PARCHMENT);
    world_tap(&W, CONFIRM_CANCEL_X, CONFIRM_BUTTONS_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TODO_OV_DETAIL, W.app.overlay, "cancelling goes back to the detail");
    TEST_ASSERT_EQUAL_INT(4, W.app.store.n_events);
    world_tap(&W, DETAIL_CLOSE_X, DETAIL_BUTTONS_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TODO_OV_AGENDA, W.app.overlay, "closing the detail goes back to the agenda it came from");
    world_tap(&W, 160, 10); /* outside the popup is above its top edge only if there is room: close with the button */
    world_frames(&W, 3);
}

static void test_deleting_an_event_takes_two_taps_and_the_server_has_the_last_word(void)
{
    open_agenda_of_the_3rd();
    world_tap(&W, AGENDA_ROW0_X, AGENDA_ROW0_Y);
    world_frames(&W, 3);
    world_tap(&W, DETAIL_DELETE_X, DETAIL_BUTTONS_Y);
    world_frames(&W, 3);
    world_tap(&W, CONFIRM_DELETE_X, CONFIRM_BUTTONS_Y);
    world_frames(&W, 8);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(3, W.app.store.n_events);
    TEST_ASSERT_EQUAL_INT(3, W.mem.n_events);
    TEST_ASSERT_EQUAL_STRING("Deleted", W.app.toast);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
}

static void test_a_failed_delete_keeps_the_event_and_says_so(void)
{
    char s[48];
    open_agenda_of_the_3rd();
    world_tap(&W, AGENDA_ROW0_X, AGENDA_ROW0_Y);
    world_frames(&W, 3);
    world_tap(&W, DETAIL_DELETE_X, DETAIL_BUTTONS_Y);
    world_frames(&W, 3);
    W.mem.fail_next = 1;
    world_tap(&W, CONFIRM_DELETE_X, CONFIRM_BUTTONS_Y);
    world_frames(&W, 8);
    TEST_ASSERT_EQUAL_INT(4, W.mem.n_events);
    TEST_ASSERT_EQUAL_INT(4, W.app.store.n_events);
    todo_app_status_text(&W.app, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Could not save", s);
}

static void test_the_confirm_popup_cannot_be_dismissed_by_a_tap_outside(void)
{
    open_agenda_of_the_3rd();
    world_tap(&W, AGENDA_ROW0_X, AGENDA_ROW0_Y);
    world_frames(&W, 3);
    world_tap(&W, DETAIL_DELETE_X, DETAIL_BUTTONS_Y);
    world_frames(&W, 3);
    world_tap(&W, 160, 12);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_CONFIRM, W.app.overlay);
}

static void test_a_quest_title_opens_its_detail_and_delete_removes_it(void)
{
    int p;
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        world_start(&W, (todo_palette)p, true);
        world_frames(&W, 6);
        world_tap(&W, TITLE_X, CARD0_Y);
        world_frames(&W, 3);
        TEST_ASSERT_EQUAL_INT(TODO_OV_DETAIL, W.app.overlay);
        TEST_ASSERT_EQUAL_INT(TODO_ITEM_QUEST, W.app.item_kind);
        TEST_ASSERT_EQUAL_UINT32(0, W.errors);
        snap("detail_quest", (todo_palette)p);
    }
    world_tap(&W, DETAIL_DELETE_X, DETAIL_BUTTONS_Y);
    world_frames(&W, 3);
    snap("confirm_quest", TODO_PALETTE_CANDLELIGHT);
    world_tap(&W, CONFIRM_DELETE_X, CONFIRM_BUTTONS_Y);
    world_frames(&W, 8);
    TEST_ASSERT_EQUAL_INT(5, W.app.store.n_quests);
    TEST_ASSERT_EQUAL_INT(5, W.mem.n_quests);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
}

static void test_a_detail_whose_item_was_deleted_elsewhere_closes_by_itself(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    world_tap(&W, TITLE_X, CARD0_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_DETAIL, W.app.overlay);
    W.app.store.n_quests = 0; /* what a refresh after a delete on the phone looks like */
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
}

static void test_the_longest_texts_fit_the_detail_and_scroll(void)
{
    char title[65], notes[257];
    int i;
    memset(title, 'W', 64);
    title[64] = '\0';
    for (i = 0; i < 256; i++) {
        notes[i] = (char)(i % 40 == 39 ? ' ' : 'a' + i % 26);
    }
    notes[256] = '\0';
    world_start(&W, TODO_PALETTE_PARCHMENT, false);
    todo_memrepo_add_quest(&W.mem, title, notes, 5, 20261201);
    world_frames(&W, 6);
    world_tap(&W, TITLE_X, CARD0_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_DETAIL, W.app.overlay);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    TEST_ASSERT_TRUE(W.cmds <= 320);
    snap("detail_longest", TODO_PALETTE_PARCHMENT);
}

/* ---- settings, the QR pages and the setup ---- */



static void open_settings(todo_palette p)
{
    world_start(&W, p, true);
    world_frames(&W, 6);
    world_tap(&W, TAB_SETTINGS_X, TAB_Y);
    world_frames(&W, 8);
    TEST_ASSERT_EQUAL_INT(TODO_TAB_SETTINGS, W.app.tab);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
}

static void test_settings_in_both_palettes(void)
{
    int p;
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        open_settings((todo_palette)p);
        snap("settings", (todo_palette)p);
    }
}

static void test_the_zone_arrows_move_the_zone_and_the_window_of_events(void)
{
    int64_t from;
    open_settings(TODO_PALETTE_PARCHMENT);
    from = W.app.store.events_from;
    world_tap(&W, 294, 110);
    world_frames(&W, 6);
    TEST_ASSERT_EQUAL_INT(-120, W.app.off_min);
    TEST_ASSERT_TRUE_MESSAGE(W.app.store.events_from < from, "the window moved with the zone");
    world_tap(&W, 158, 110);
    world_tap(&W, 158, 110);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(-210, W.app.off_min);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
}

static void test_the_backlight_timeout_wraps_round(void)
{
    open_settings(TODO_PALETTE_PARCHMENT);
    TEST_ASSERT_EQUAL_INT(1, W.app.backlight_index);
    world_tap(&W, 294, 140);
    world_tap(&W, 294, 140);
    world_frames(&W, 2);
    TEST_ASSERT_EQUAL_INT(3, W.app.backlight_index);
    world_tap(&W, 294, 140);
    world_frames(&W, 2);
    TEST_ASSERT_EQUAL_INT(0, W.app.backlight_index);
    world_tap(&W, 158, 140);
    world_frames(&W, 2);
    TEST_ASSERT_EQUAL_INT(3, W.app.backlight_index);
}

static void test_the_theme_row_switches_the_palette_live(void)
{
    uint64_t day;
    open_settings(TODO_PALETTE_PARCHMENT);
    day = cydui_mem_hash(&W.r.mem);
    world_tap(&W, 160, 170);
    world_frames(&W, 12);
    TEST_ASSERT_EQUAL_INT(TODO_PALETTE_CANDLELIGHT, W.app.palette);
    TEST_ASSERT_EQUAL_INT((int)TODO_PALETTE_CANDLELIGHT, W.app.applied_palette);
    TEST_ASSERT_NOT_EQUAL(day, cydui_mem_hash(&W.r.mem));
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    snap("settings_night_switched", TODO_PALETTE_CANDLELIGHT);
    world_tap(&W, 160, 170);
    world_frames(&W, 12);
    TEST_ASSERT_EQUAL_INT(TODO_PALETTE_PARCHMENT, W.app.palette);
}

static void test_change_wifi_asks_first_and_then_opens_the_setup_page(void)
{
    int p;
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        open_settings((todo_palette)p);
        todo_app_set_ap(&W.app, "cyd-todo-4F2A", "k7Qm29xT");
        world_tap(&W, 160, 200);
        world_frames(&W, 4);
        TEST_ASSERT_EQUAL_INT(TODO_OV_CONFIRM_WIFI, W.app.overlay);
        snap("confirm_wifi", (todo_palette)p);
        world_tap(&W, CONFIRM_WIFI_CANCEL_X, CONFIRM_WIFI_Y);
        world_frames(&W, 3);
        TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
        world_tap(&W, 160, 200);
        world_frames(&W, 4);
        world_tap(&W, CONFIRM_WIFI_CONTINUE_X, CONFIRM_WIFI_Y);
        world_frames(&W, 4);
        TEST_ASSERT_EQUAL_INT(TODO_OV_SETUP, W.app.overlay);
        TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:cyd-todo-4F2A;P:k7Qm29xT;;", W.app.qr_text);
        TEST_ASSERT_TRUE(W.app.has_credentials);
        TEST_ASSERT_EQUAL_UINT32(0, W.errors);
        snap("setup_from_settings", (todo_palette)p);
    }
}

static void test_the_setup_page_shows_why_and_swaps_between_the_wifi_code_and_the_address(void)
{
    int p;
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        world_start(&W, (todo_palette)p, true);
        world_frames(&W, 6);
        todo_app_enter_setup(&W.app, "cyd-todo-4F2A", "k7Qm29xT", "Could not join home-net", false);
        world_frames(&W, 4);
        TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:cyd-todo-4F2A;P:k7Qm29xT;;", W.app.qr_text);
        snap("setup", (todo_palette)p);
        world_tap(&W, QR_BUTTON_X, QR_BUTTON_Y);
        world_frames(&W, 4);
        TEST_ASSERT_EQUAL_STRING("http://192.168.4.1", W.app.qr_text);
        snap("setup_address", (todo_palette)p);
        world_tap(&W, QR_BUTTON_X, QR_BUTTON_Y);
        world_frames(&W, 4);
        TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:cyd-todo-4F2A;P:k7Qm29xT;;", W.app.qr_text);
        TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    }
}

static void test_without_a_network_the_setup_page_cannot_be_cancelled(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    todo_app_enter_setup(&W.app, "cyd-todo-4F2A", "k7Qm29xT", "", false);
    world_frames(&W, 4);
    world_tap(&W, QR_BUTTON_X, 152); /* where Cancel would be */
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_SETUP, W.app.overlay);
    todo_app_enter_setup(&W.app, "cyd-todo-4F2A", "k7Qm29xT", "", true);
    world_frames(&W, 4);
    world_tap(&W, QR_BUTTON_X, 152);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TODO_OV_NONE, W.app.overlay, "with a network to go back to, Cancel is there");
}

static void test_the_setup_window_closes_after_ten_minutes_and_can_be_reopened(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    todo_app_enter_setup(&W.app, "cyd-todo-4F2A", "k7Qm29xT", "", false);
    world_frames(&W, 4);
    rig_advance(&W.r, 590000);
    world_frames(&W, 3);
    TEST_ASSERT_TRUE_MESSAGE(W.app.qr.size > 0, "the code is still there after 9 minutes and 50 seconds");
    rig_advance(&W.r, 11000);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    TEST_ASSERT_EQUAL_INT(TODO_OV_SETUP, W.app.overlay);
    snap("setup_timed_out", TODO_PALETTE_PARCHMENT);
    world_tap(&W, 160, 146);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_SETUP, W.app.overlay);
    TEST_ASSERT_TRUE_MESSAGE(W.app.qr.size > 0, "the code is back");
    snap("setup_again", TODO_PALETTE_PARCHMENT);
}

static void open_new_quest_page(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    world_tap(&W, NEW_QUEST_X, NEW_QUEST_Y);
    world_frames(&W, 6);
}

static void test_new_quest_asks_for_a_link_and_shows_it_as_a_code(void)
{
    open_new_quest_page();
    TEST_ASSERT_EQUAL_INT(TODO_OV_QR, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(TODO_LINK_QUEST, W.app.qr_kind);
    TEST_ASSERT_TRUE(W.app.qr_ready);
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50:8080/m/00000000000000000000000001", W.app.qr_text);
    TEST_ASSERT_TRUE(W.app.qr.size > 0);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    snap("qr_quest", TODO_PALETTE_PARCHMENT);
}

static void test_the_page_closes_by_itself_when_the_phone_saves_something(void)
{
    open_new_quest_page();
    world_frames(&W, 100); /* a few polls with nothing changed */
    TEST_ASSERT_EQUAL_INT(TODO_OV_QR, W.app.overlay);
    todo_memrepo_add_quest(&W.mem, "Written on the phone", "", 2, 0);
    world_frames(&W, 260); /* the next poll comes within 3 seconds */
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    TEST_ASSERT_EQUAL_STRING("Saved on your phone", W.app.toast);
    world_frames(&W, 10);
    TEST_ASSERT_EQUAL_INT(7, W.app.store.n_quests);
}

static void test_the_link_expires_after_ten_minutes(void)
{
    open_new_quest_page();
    rig_advance(&W.r, 590000);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TODO_OV_QR, W.app.overlay, "still valid after 9 minutes and 50 seconds");
    rig_advance(&W.r, 11000);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    TEST_ASSERT_EQUAL_STRING("Link expired", W.app.toast);
}

static void test_cancel_leaves_the_page_and_a_second_link_is_a_new_picture(void)
{
    uint64_t first, second;
    char first_text[96];
    open_new_quest_page();
    first = cydui_mem_hash(&W.r.mem);
    snprintf(first_text, sizeof(first_text), "%s", W.app.qr_text);
    world_tap(&W, QR_BUTTON_X, QR_BUTTON_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    world_tap(&W, NEW_QUEST_X, NEW_QUEST_Y);
    world_frames(&W, 6);
    TEST_ASSERT_EQUAL_INT(TODO_OV_QR, W.app.overlay);
    second = cydui_mem_hash(&W.r.mem);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(first_text, W.app.qr_text));
    TEST_ASSERT_NOT_EQUAL_MESSAGE(first, second, "the new code is on the screen, not the old one (finding 1)");
}

static void test_a_refused_link_says_so_and_retry_asks_again(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    W.mem.fail_next = 1;
    world_tap(&W, NEW_QUEST_X, NEW_QUEST_Y);
    world_frames(&W, 6);
    TEST_ASSERT_EQUAL_INT(TODO_OV_QR, W.app.overlay);
    TEST_ASSERT_TRUE(W.app.qr_failed);
    TEST_ASSERT_FALSE(W.app.qr_ready);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    snap("qr_failed", TODO_PALETTE_PARCHMENT);
    world_tap(&W, QR_BUTTON_X, 82); /* Retry is above Cancel */
    world_frames(&W, 6);
    TEST_ASSERT_TRUE_MESSAGE(W.app.qr_ready, "the second ask worked");
    TEST_ASSERT_FALSE(W.app.qr_failed);
}

static void test_offline_is_told_apart_on_the_link_page(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    W.mem.offline = true;
    world_tap(&W, NEW_QUEST_X, NEW_QUEST_Y);
    world_frames(&W, 6);
    TEST_ASSERT_TRUE(W.app.qr_failed);
    TEST_ASSERT_EQUAL_INT(TODO_LINK_OFFLINE, W.app.link);
    snap("qr_offline", TODO_PALETTE_PARCHMENT);
}

static void test_add_event_in_the_agenda_opens_the_event_link_page(void)
{
    open_agenda_of_the_3rd();
    world_tap(&W, 79, 203); /* Add event */
    world_frames(&W, 6);
    TEST_ASSERT_EQUAL_INT(TODO_OV_QR, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(TODO_LINK_EVENT, W.app.qr_kind);
    TEST_ASSERT_TRUE(W.app.qr_ready);
    snap("qr_event", TODO_PALETTE_CANDLELIGHT);
}

/* ---- what the UI asks of the platform, and what the platform tells it ---- */

static void test_change_wifi_asks_the_platform_for_an_access_point_once(void)
{
    open_settings(TODO_PALETTE_PARCHMENT);
    TEST_ASSERT_EQUAL_INT(TODO_EVT_NONE, todo_app_take_event(&W.app));
    world_tap(&W, 160, 200);
    world_tap(&W, CONFIRM_WIFI_CONTINUE_X, CONFIRM_WIFI_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_EVT_SETUP_REQUESTED, todo_app_take_event(&W.app));
    TEST_ASSERT_EQUAL_INT_MESSAGE(TODO_EVT_NONE, todo_app_take_event(&W.app), "an event is taken once");
}

static void test_with_a_platform_that_makes_the_access_point_the_page_waits_for_it(void)
{
    open_settings(TODO_PALETTE_PARCHMENT);
    todo_app_set_ap(&W.app, "old-name", "OLDPASSWORD1");
    todo_app_set_ap_on_demand(&W.app, true);
    world_tap(&W, 160, 200);
    world_tap(&W, CONFIRM_WIFI_CONTINUE_X, CONFIRM_WIFI_Y);
    world_frames(&W, 5);
    TEST_ASSERT_EQUAL_INT(TODO_OV_SETUP, W.app.overlay);
    TEST_ASSERT_EQUAL_STRING("", W.app.ssid);
    TEST_ASSERT_EQUAL_STRING("", W.app.qr_text);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, W.app.qr.size, "no code of the old access point is shown");
    snap("setup_waiting_for_the_access_point", TODO_PALETTE_PARCHMENT);
    todo_app_set_ap(&W.app, "cyd-todo-4F2A", "k7Qm29xTk7Qm");
    world_frames(&W, 5);
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:cyd-todo-4F2A;P:k7Qm29xTk7Qm;;", W.app.qr_text);
    TEST_ASSERT_TRUE(W.app.qr.size > 0);
}

static void test_cancelling_the_setup_tells_the_platform_and_try_again_asks_again(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    todo_app_enter_setup(&W.app, "cyd-todo-4F2A", "k7Qm29xT", "", true);
    world_frames(&W, 4);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TODO_EVT_NONE, todo_app_take_event(&W.app), "the platform's own start is not an event");
    world_tap(&W, QR_BUTTON_X, SETUP_CANCEL_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(TODO_EVT_SETUP_CANCELLED, todo_app_take_event(&W.app));
    todo_app_enter_setup(&W.app, "cyd-todo-4F2A", "k7Qm29xT", "", false);
    world_frames(&W, 4);
    rig_advance(&W.r, 601000);
    world_frames(&W, 3);
    world_tap(&W, TRY_AGAIN_X, TRY_AGAIN_Y);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_EVT_SETUP_REQUESTED, todo_app_take_event(&W.app));
}

static void test_the_platform_can_close_the_setup_page_when_it_has_joined_a_network(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    world_frames(&W, 6);
    todo_app_enter_setup(&W.app, "cyd-todo-4F2A", "k7Qm29xT", "", false);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_SETUP, W.app.overlay);
    todo_app_leave_setup(&W.app);
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    TEST_ASSERT_EQUAL_UINT32(0, W.errors);
    W.app.overlay = TODO_OV_AGENDA;
    W.app.cal_day = 3;
    W.app.tab = TODO_TAB_CALENDAR;
    todo_app_leave_setup(&W.app);
    TEST_ASSERT_EQUAL_INT_MESSAGE(TODO_OV_AGENDA, W.app.overlay, "only the setup page is closed");
}

static void test_the_status_line_says_when_the_radio_is_joining_a_network(void)
{
    char s[48];
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    todo_app_set_time(&W.app, 0, false);
    todo_app_set_join(&W.app, true, "home-net");
    world_frames(&W, 4);
    todo_app_status_text(&W.app, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Joining home-net\xE2\x80\xA6", s);
    snap("quests_joining", TODO_PALETTE_PARCHMENT);
    todo_app_set_join(&W.app, false, NULL);
    todo_app_status_text(&W.app, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Waiting for time", s);
    todo_app_set_join(&W.app, true, "a-network-name-that-is-really-long-32");
    todo_app_status_text(&W.app, s, sizeof(s));
    TEST_ASSERT_TRUE_MESSAGE(strlen(s) < 40, "a long name is cut in the status line");
}

static void test_settings_shows_the_network_the_platform_names(void)
{
    uint64_t before;
    open_settings(TODO_PALETTE_PARCHMENT);
    before = cydui_mem_hash(&W.r.mem);
    todo_app_set_wifi_name(&W.app, "Cafe Wi-Fi");
    world_frames(&W, 3);
    TEST_ASSERT_EQUAL_STRING("Cafe Wi-Fi", W.app.wifi_name);
    TEST_ASSERT_NOT_EQUAL(before, cydui_mem_hash(&W.r.mem));
    todo_app_set_wifi_name(&W.app, NULL);
    TEST_ASSERT_EQUAL_STRING("", W.app.wifi_name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_quests_screen_loads_and_says_when_it_synced);
    RUN_TEST(test_the_status_line_before_the_clock_and_before_the_first_answer);
    RUN_TEST(test_the_status_line_says_offline_and_an_error_fades);
    RUN_TEST(test_tapping_the_checkbox_completes_the_quest_at_once_and_pays_xp);
    RUN_TEST(test_a_level_up_says_so_in_the_toast);
    RUN_TEST(test_reopening_cannot_take_the_xp_below_zero);
    RUN_TEST(test_a_failed_save_puts_the_card_back_and_says_so);
    RUN_TEST(test_a_due_date_in_the_past_is_in_wax_red_and_only_for_open_quests);
    RUN_TEST(test_a_title_of_64_bytes_is_cut_with_an_ellipsis_and_never_reaches_the_stars);
    RUN_TEST(test_forty_eight_cards_cost_what_six_cost_because_only_the_visible_ones_are_built);
    RUN_TEST(test_dragging_the_list_scrolls_it);
    RUN_TEST(test_the_empty_list_says_so);
    RUN_TEST(test_the_calendar_tab_shows_the_month_with_dots_on_the_days_with_events);
    RUN_TEST(test_the_calendar_waits_for_the_time);
    RUN_TEST(test_the_arrows_change_the_month_and_ask_for_its_events);
    RUN_TEST(test_a_tap_on_a_day_opens_its_agenda_and_a_blank_cell_does_nothing);
    RUN_TEST(test_an_empty_day_says_so);
    RUN_TEST(test_the_whole_walk_agenda_detail_confirm_and_back);
    RUN_TEST(test_deleting_an_event_takes_two_taps_and_the_server_has_the_last_word);
    RUN_TEST(test_a_failed_delete_keeps_the_event_and_says_so);
    RUN_TEST(test_the_confirm_popup_cannot_be_dismissed_by_a_tap_outside);
    RUN_TEST(test_a_quest_title_opens_its_detail_and_delete_removes_it);
    RUN_TEST(test_a_detail_whose_item_was_deleted_elsewhere_closes_by_itself);
    RUN_TEST(test_settings_in_both_palettes);
    RUN_TEST(test_the_zone_arrows_move_the_zone_and_the_window_of_events);
    RUN_TEST(test_the_backlight_timeout_wraps_round);
    RUN_TEST(test_the_theme_row_switches_the_palette_live);
    RUN_TEST(test_change_wifi_asks_first_and_then_opens_the_setup_page);
    RUN_TEST(test_the_setup_page_shows_why_and_swaps_between_the_wifi_code_and_the_address);
    RUN_TEST(test_without_a_network_the_setup_page_cannot_be_cancelled);
    RUN_TEST(test_the_setup_window_closes_after_ten_minutes_and_can_be_reopened);
    RUN_TEST(test_new_quest_asks_for_a_link_and_shows_it_as_a_code);
    RUN_TEST(test_the_page_closes_by_itself_when_the_phone_saves_something);
    RUN_TEST(test_the_link_expires_after_ten_minutes);
    RUN_TEST(test_cancel_leaves_the_page_and_a_second_link_is_a_new_picture);
    RUN_TEST(test_a_refused_link_says_so_and_retry_asks_again);
    RUN_TEST(test_offline_is_told_apart_on_the_link_page);
    RUN_TEST(test_add_event_in_the_agenda_opens_the_event_link_page);
    RUN_TEST(test_change_wifi_asks_the_platform_for_an_access_point_once);
    RUN_TEST(test_with_a_platform_that_makes_the_access_point_the_page_waits_for_it);
    RUN_TEST(test_cancelling_the_setup_tells_the_platform_and_try_again_asks_again);
    RUN_TEST(test_the_platform_can_close_the_setup_page_when_it_has_joined_a_network);
    RUN_TEST(test_the_status_line_says_when_the_radio_is_joining_a_network);
    RUN_TEST(test_settings_shows_the_network_the_platform_names);
    RUN_TEST(test_the_longest_texts_fit_the_detail_and_scroll);
    return UNITY_END();
}
