/* The screens of the application, on the rig, with synthetic touches. The pictures are written to the build
 * directory to be looked at; goldens of scripted runs come later. */

#include <stdio.h>
#include <string.h>

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
    return UNITY_END();
}
