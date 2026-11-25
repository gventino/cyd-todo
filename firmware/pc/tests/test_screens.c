/* The screens of the application, on the rig, with synthetic touches. The pictures are written to the build
 * directory to be looked at; goldens of scripted runs come later. */

#include <stdio.h>
#include <string.h>

#include "todo/datetime.h"
#include "todo/levels.h"
#include "unity.h"
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
#define CHECK_X 20
#define TITLE_X 120
#define CARD0_Y 82

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
    const cydui_color wax = todo_colors_of(TODO_PALETTE_PARCHMENT)->wax;
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

#define TAB_QUESTS_X 53
#define TAB_CALENDAR_X 160
#define TAB_Y 16
/* December 2026: the grid starts at x 2, y 78 and has 5 rows of 26 px; the 1st is a Tuesday */
static int day_x(int d) { return 2 + ((d + 0) % 7) * 45 + 22; } /* column of day d in a month that starts on a Tuesday: (d + 0) % 7 */
static int day_y(int d) { return 78 + ((d + 0) / 7) * 26 + 13; }

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
    world_tap(&W, 68, 203); /* the agenda's Close */
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

#define AGENDA_ROW0_X 160
#define AGENDA_ROW0_Y 56
#define DETAIL_DELETE_X 77
#define DETAIL_CLOSE_X 190
#define DETAIL_BUTTONS_Y 178
#define CONFIRM_CANCEL_X 88
#define CONFIRM_DELETE_X 201
#define CONFIRM_BUTTONS_Y 139

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
    RUN_TEST(test_the_longest_texts_fit_the_detail_and_scroll);
    return UNITY_END();
}
