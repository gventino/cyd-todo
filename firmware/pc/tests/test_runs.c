/* Scripted runs: a story told with touches, the server and the clock, with a golden picture at every checkpoint.
 * Each run ends by asserting what the state must be, so a picture that is wrong in the same way twice cannot hide a bug. */

#include <stdio.h>
#include <string.h>

#include "coords.h"
#include "unity.h"
#include "world.h"

void setUp(void) {}
void tearDown(void) {}

static world W;

/* ---- the steps ---- */

static void tap(int x, int y) { world_tap(&W, x, y); }
static void wait_ms(int ms) { world_frames(&W, ms / 16); }

static void drag(int x0, int y0, int x1, int y1)
{
    int i;
    rig_down(&W.r, x0, y0);
    world_frame(&W);
    for (i = 1; i <= 8; i++) {
        rig_move(&W.r, x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8);
        world_frame(&W);
    }
    rig_up(&W.r, x1, y1);
    world_frame(&W);
}

static char run_name[48];

static void begin(const char *name, todo_palette p, bool demo)
{
    snprintf(run_name, sizeof(run_name), "%s", name);
    world_start(&W, p, demo);
    wait_ms(120);
}

/* A checkpoint taken at once (for a state that is still moving: a request in flight, a toast just shown) ... */
static void check_now(int n, const char *label)
{
    char name[96];
    snprintf(name, sizeof(name), "run_%s_%d_%s", run_name, n, label);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, W.errors, name);
    golden_check(name, &W.r);
}

/* ... and one taken when the screen has settled: popups open a frame after the tap that asked, and widgets take 120 ms to change look. */
static void check(int n, const char *label)
{
    wait_ms(130);
    check_now(n, label);
}

static int quests_due(int32_t due)
{
    int i, n = 0;
    for (i = 0; i < W.app.store.n_quests; i++) {
        n += W.app.store.quests[i].due == due;
    }
    return n;
}

static void status_is(const char *want)
{
    char s[48];
    todo_app_status_text(&W.app, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING(want, s);
}

/* ---- the runs ---- */

static void test_run_complete_a_quest(void)
{
    begin("complete", TODO_PALETTE_PARCHMENT, true);
    check(1, "loaded");
    tap(CHECK_X, CARD0_Y);
    check_now(2, "pending");
    TEST_ASSERT_TRUE(W.app.store.quests[0].done);
    wait_ms(600);
    check(3, "answered");
    TEST_ASSERT_EQUAL_INT32(50, W.app.store.xp);
    TEST_ASSERT_FALSE_MESSAGE(W.app.store.quests[0].done, "the finished quest went below the open ones");
    TEST_ASSERT_EQUAL_INT(3, W.app.store.open);
    wait_ms(3000);
    check(4, "toast_gone");
    status_is("Synced 14:00");
}

static void test_run_level_up_at_night(void)
{
    begin("levelup", TODO_PALETTE_CANDLELIGHT, true);
    todo_memrepo_add_quest(&W.mem, "Old victory", "", 5, 0);
    W.mem.quests[W.mem.n_quests - 1].done = true;
    W.mem.quests[W.mem.n_quests - 1].done_at = WORLD_NOW - 3600;
    W.app.quests_dirty = true;
    wait_ms(300);
    TEST_ASSERT_EQUAL_INT32(70, W.app.store.xp);
    check(1, "before");
    tap(CHECK_X, CARD0_Y);
    check_now(2, "toast");
    TEST_ASSERT_EQUAL_STRING("+30 XP  Level 2!", W.app.toast);
    wait_ms(600);
    check(3, "settled");
    TEST_ASSERT_EQUAL_INT32(100, W.app.store.xp);
}

static void test_run_a_save_that_fails(void)
{
    begin("failsave", TODO_PALETTE_PARCHMENT, true);
    W.mem.fail_next = 1;
    tap(CHECK_X, CARD0_Y);
    check_now(1, "flipped");
    wait_ms(400);
    check(2, "back");
    TEST_ASSERT_FALSE(W.app.store.quests[0].done);
    TEST_ASSERT_EQUAL_INT32(20, W.app.store.xp);
    status_is("Could not save");
    wait_ms(6000);
    check(3, "recovered");
    status_is("Synced 14:00");
}

static void test_run_delete_a_quest(void)
{
    begin("delquest", TODO_PALETTE_PARCHMENT, true);
    tap(TITLE_X, CARD0_Y);
    check(1, "detail");
    tap(DETAIL_DELETE_X, DETAIL_BUTTONS_Y);
    check(2, "confirm");
    tap(CONFIRM_DELETE_X, CONFIRM_BUTTONS_Y);
    wait_ms(200);
    check(3, "deleted");
    TEST_ASSERT_EQUAL_INT(5, W.app.store.n_quests);
    TEST_ASSERT_EQUAL_STRING("Deleted", W.app.toast);
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
}

static void test_run_delete_an_event(void)
{
    begin("delevent", TODO_PALETTE_PARCHMENT, true);
    tap(TAB_CALENDAR_X, TAB_Y);
    wait_ms(200);
    check(1, "calendar");
    tap(day_x(3), day_y(3));
    check(2, "agenda");
    tap(AGENDA_ROW0_X, AGENDA_ROW0_Y);
    check(3, "detail");
    tap(DETAIL_DELETE_X, DETAIL_BUTTONS_Y);
    tap(CONFIRM_DELETE_X, CONFIRM_BUTTONS_Y);
    wait_ms(300);
    check(4, "calendar_after");
    TEST_ASSERT_EQUAL_INT(3, W.app.store.n_events);
    TEST_ASSERT_EQUAL_INT(3, W.mem.n_events);
    tap(day_x(3), day_y(3));
    check(5, "agenda_after");
}

static void test_run_a_day_with_an_event_and_an_open_quest(void)
{
    begin("dayboth", TODO_PALETTE_PARCHMENT, true);
    tap(TAB_CALENDAR_X, TAB_Y);
    tap(day_x(4), day_y(4)); /* "Slay the inbox" is due, and there is an event */
    wait_ms(100);
    check(1, "agenda");
    TEST_ASSERT_EQUAL_INT(TODO_OV_AGENDA, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(1, quests_due(20261204));
    tap(AGENDA_ROW0_X, AGENDA_ROW0_Y + 36); /* the second row: the quest */
    check(2, "quest_detail");
    TEST_ASSERT_EQUAL_INT(TODO_ITEM_QUEST, W.app.item_kind);
    tap(DETAIL_CLOSE_X, DETAIL_BUTTONS_Y);
    check(3, "back_in_the_agenda");
    TEST_ASSERT_EQUAL_INT(TODO_OV_AGENDA, W.app.overlay);
}

static void test_run_a_walk_through_the_months(void)
{
    begin("months", TODO_PALETTE_PARCHMENT, true);
    tap(TAB_CALENDAR_X, TAB_Y);
    tap(CAL_NEXT_X, CAL_ARROW_Y);
    tap(CAL_NEXT_X, CAL_ARROW_Y);
    check(1, "february");
    tap(CAL_PREV_X, CAL_ARROW_Y);
    wait_ms(200);
    check(2, "january");
    TEST_ASSERT_EQUAL_INT(2027, W.app.cal_year);
    TEST_ASSERT_EQUAL_INT(1, W.app.cal_month);
    tap(cell_x(4, 5), cell_y(4, 5)); /* January 2027 begins on a Friday */
    check(3, "empty_day");
    TEST_ASSERT_EQUAL_INT(TODO_OV_AGENDA, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(5, W.app.cal_day);
    tap(AGENDA_CLOSE_X, AGENDA_BUTTONS_Y);
    check(4, "closed");
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
}

static void test_run_a_new_quest_written_on_the_phone_at_night(void)
{
    begin("phone", TODO_PALETTE_CANDLELIGHT, true);
    tap(NEW_QUEST_X, NEW_QUEST_Y);
    wait_ms(200);
    check(1, "code");
    TEST_ASSERT_TRUE(W.app.qr_ready);
    todo_memrepo_add_quest(&W.mem, "Written on the phone", "From the sofa.", 2, 20261203);
    wait_ms(4000);
    check(2, "closed");
    TEST_ASSERT_EQUAL_INT(TODO_OV_NONE, W.app.overlay);
    TEST_ASSERT_EQUAL_INT(7, W.app.store.n_quests);
    TEST_ASSERT_NOT_NULL(strstr(W.app.store.quests[0].title, "Written"));
}

static void test_run_a_link_that_is_refused_and_a_server_that_is_gone(void)
{
    begin("links", TODO_PALETTE_PARCHMENT, true);
    W.mem.fail_next = 1;
    tap(NEW_QUEST_X, NEW_QUEST_Y);
    wait_ms(200);
    check(1, "refused");
    TEST_ASSERT_TRUE(W.app.qr_failed);
    tap(QR_BUTTON_X, QR_RETRY_Y);
    wait_ms(200);
    check(2, "retried");
    TEST_ASSERT_TRUE(W.app.qr_ready);
    tap(QR_BUTTON_X, QR_BUTTON_Y);
    W.mem.offline = true;
    tap(NEW_QUEST_X, NEW_QUEST_Y);
    wait_ms(200);
    check(3, "offline");
    TEST_ASSERT_TRUE(W.app.qr_failed);
    status_is("Offline");
}

static void test_run_settings_and_the_theme(void)
{
    int i;
    begin("settings", TODO_PALETTE_PARCHMENT, true);
    tap(TAB_SETTINGS_X, TAB_Y);
    wait_ms(200);
    check(1, "settings");
    for (i = 0; i < 6; i++) {
        tap(SET_NEXT_X, SET_ZONE_Y);
    }
    tap(SET_NEXT_X, SET_BACKLIGHT_Y);
    wait_ms(200);
    check(2, "changed");
    TEST_ASSERT_EQUAL_INT(180, W.app.off_min);
    TEST_ASSERT_EQUAL_INT(2, W.app.backlight_index);
    tap(TAB_CALENDAR_X, TAB_Y);
    wait_ms(400);
    check(3, "calendar_in_utc_plus_3");
    tap(TAB_SETTINGS_X, TAB_Y);
    tap(160, SET_THEME_Y);
    wait_ms(300);
    check(4, "night");
    TEST_ASSERT_EQUAL_INT(TODO_PALETTE_CANDLELIGHT, W.app.palette);
    tap(160, SET_THEME_Y);
    wait_ms(300);
    check(5, "day_again");
    TEST_ASSERT_EQUAL_INT(TODO_PALETTE_PARCHMENT, W.app.palette);
}

static void test_run_changing_the_wifi(void)
{
    begin("wifi", TODO_PALETTE_PARCHMENT, true);
    todo_app_set_ap(&W.app, "cyd-todo-4F2A", "k7Qm29xT");
    tap(TAB_SETTINGS_X, TAB_Y);
    tap(160, SET_WIFI_Y);
    check(1, "asked");
    tap(CONFIRM_WIFI_CONTINUE_X, CONFIRM_WIFI_Y);
    wait_ms(100);
    check(2, "setup");
    TEST_ASSERT_EQUAL_INT(TODO_OV_SETUP, W.app.overlay);
    tap(QR_BUTTON_X, 114);
    wait_ms(50);
    check(3, "address");
    TEST_ASSERT_EQUAL_STRING("http://192.168.4.1", W.app.qr_text);
    rig_advance(&W.r, 601000);
    wait_ms(100);
    check(4, "timed_out");
    tap(TRY_AGAIN_X, TRY_AGAIN_Y);
    wait_ms(50);
    check(5, "again");
    TEST_ASSERT_EQUAL_INT(TODO_OV_SETUP, W.app.overlay);
}

static void test_run_waiting_for_the_time(void)
{
    world_start(&W, TODO_PALETTE_PARCHMENT, true);
    snprintf(run_name, sizeof(run_name), "notime");
    todo_app_set_time(&W.app, 0, false);
    wait_ms(200);
    check(1, "quests");
    status_is("Waiting for time");
    tap(TAB_CALENDAR_X, TAB_Y);
    wait_ms(100);
    check(2, "calendar");
    todo_app_set_time(&W.app, WORLD_NOW, true);
    wait_ms(300);
    check(3, "arrived");
    TEST_ASSERT_EQUAL_INT(2026, W.app.cal_year);
    TEST_ASSERT_EQUAL_INT(4, W.app.store.n_events);
}

static void test_run_a_long_list_is_scrolled_by_a_drag(void)
{
    int i;
    char t[40];
    begin("scroll", TODO_PALETTE_PARCHMENT, true);
    for (i = 0; i < 8; i++) {
        snprintf(t, sizeof(t), "Side quest number %d", i + 1);
        todo_memrepo_add_quest(&W.mem, t, "", 1 + i % 5, 20261210 + i);
    }
    W.app.quests_dirty = true;
    wait_ms(300);
    check(1, "top");
    drag(160, 200, 160, 90);
    wait_ms(1500);
    check(2, "scrolled");
    TEST_ASSERT_EQUAL_INT(14, W.app.store.n_quests);
    TEST_ASSERT_FALSE_MESSAGE(W.app.store.quests[0].done, "a drag does not tick a box");
    drag(160, 90, 160, 220);
    wait_ms(1500);
    check(3, "top_again");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_run_complete_a_quest);
    RUN_TEST(test_run_level_up_at_night);
    RUN_TEST(test_run_a_save_that_fails);
    RUN_TEST(test_run_delete_a_quest);
    RUN_TEST(test_run_delete_an_event);
    RUN_TEST(test_run_a_day_with_an_event_and_an_open_quest);
    RUN_TEST(test_run_a_walk_through_the_months);
    RUN_TEST(test_run_a_new_quest_written_on_the_phone_at_night);
    RUN_TEST(test_run_a_link_that_is_refused_and_a_server_that_is_gone);
    RUN_TEST(test_run_settings_and_the_theme);
    RUN_TEST(test_run_changing_the_wifi);
    RUN_TEST(test_run_waiting_for_the_time);
    RUN_TEST(test_run_a_long_list_is_scrolled_by_a_drag);
    return UNITY_END();
}
