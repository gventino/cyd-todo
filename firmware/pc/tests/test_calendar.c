/* The pure helpers of the calendar: rows, cells, event days and the words that describe an event. */

#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define OFF (-180)

static int64_t at(int y, int m, int d, int hh, int mm) /* a local time as a UTC instant */
{
    return todo_local_midnight(y, m, d, OFF) + hh * 3600 + mm * 60;
}

static void test_a_month_has_the_rows_it_needs(void)
{
    TEST_ASSERT_EQUAL_INT(5, cal_rows(2026, 12)); /* starts Tuesday, 31 days */
    TEST_ASSERT_EQUAL_INT(4, cal_rows(2027, 2));  /* starts Monday, 28 days */
    TEST_ASSERT_EQUAL_INT(6, cal_rows(2026, 8));  /* starts Saturday, 31 days */
    TEST_ASSERT_EQUAL_INT(5, cal_rows(2026, 2));  /* starts Sunday, 28 days */
    for (int y = 2026; y <= 2030; y++) {
        for (int m = 1; m <= 12; m++) {
            int rows = cal_rows(y, m), last = 0;
            TEST_ASSERT_TRUE(rows >= 4 && rows <= 6);
            for (int r = 0; r < 6; r++) {
                for (int c = 0; c < 7; c++) {
                    if (todo_grid_day(y, m, r, c) == todo_days_in_month(y, m)) {
                        last = r + 1;
                    }
                }
            }
            TEST_ASSERT_EQUAL_INT_MESSAGE(last, rows, "the last day is in the last row");
        }
    }
}

static void test_six_rows_get_shorter_cells_so_that_they_fit(void)
{
    TEST_ASSERT_EQUAL_INT(26, cal_cell_h(4));
    TEST_ASSERT_EQUAL_INT(26, cal_cell_h(5));
    TEST_ASSERT_EQUAL_INT(22, cal_cell_h(6));
    TEST_ASSERT_TRUE(32 + 14 + 6 * cal_cell_h(6) <= CONTENT_H);
    TEST_ASSERT_TRUE(32 + 14 + 5 * cal_cell_h(5) <= CONTENT_H);
}

static void test_every_pixel_of_the_grid_maps_to_its_cell_and_the_outside_to_nothing(void)
{
    int row, col, x, y;
    for (int rows = 4; rows <= 6; rows++) {
        int ch = cal_cell_h(rows);
        for (y = 0; y < rows * ch; y++) {
            for (x = 0; x < GRID_W; x++) {
                TEST_ASSERT_TRUE(cal_cell_at(rows, ch, x, y, &row, &col));
                TEST_ASSERT_EQUAL_INT(y / ch, row);
                TEST_ASSERT_EQUAL_INT(x / GRID_CELL_W, col);
            }
        }
        TEST_ASSERT_FALSE(cal_cell_at(rows, ch, -1, 0, &row, &col));
        TEST_ASSERT_FALSE(cal_cell_at(rows, ch, 0, -1, &row, &col));
        TEST_ASSERT_FALSE(cal_cell_at(rows, ch, GRID_W, 0, &row, &col));
        TEST_ASSERT_FALSE(cal_cell_at(rows, ch, 0, rows * ch, &row, &col));
    }
}

static todo_event ev(int64_t s, int64_t e)
{
    todo_event x;
    memset(&x, 0, sizeof(x));
    x.start = s;
    x.end = e;
    return x;
}

static void test_an_event_is_on_a_day_if_it_overlaps_it_and_touching_does_not_count(void)
{
    int64_t d0 = at(2026, 12, 5, 0, 0), d1 = d0 + TODO_DAY;
    todo_event inside = ev(d0 + 3600, d0 + 7200), before = ev(d0 - 7200, d0), after = ev(d1, d1 + 60), across = ev(d0 - 60, d1 + 60);
    TEST_ASSERT_TRUE(event_on_day(&inside, d0, d1));
    TEST_ASSERT_FALSE_MESSAGE(event_on_day(&before, d0, d1), "ends exactly at midnight: yesterday's");
    TEST_ASSERT_FALSE(event_on_day(&after, d0, d1));
    TEST_ASSERT_TRUE(event_on_day(&across, d0, d1));
}

static void test_the_days_of_a_month_that_have_events_across_midnight_and_month_ends(void)
{
    static todo_store s;
    uint32_t mask;
    todo_store_clear(&s);
    s.events[s.n_events++] = ev(at(2026, 12, 31, 22, 0), at(2027, 1, 1, 2, 0)); /* crosses the year */
    s.events[s.n_events++] = ev(at(2026, 12, 10, 9, 0), at(2026, 12, 10, 10, 0));
    s.events[s.n_events++] = ev(at(2026, 12, 20, 22, 0), at(2026, 12, 22, 1, 0)); /* three days */
    s.events[s.n_events++] = ev(at(2026, 11, 30, 23, 0), at(2026, 12, 1, 0, 0));   /* ends at midnight: not December */
    mask = cal_event_days(&s, 2026, 12, OFF);
    TEST_ASSERT_TRUE(mask & (1u << 31));
    TEST_ASSERT_TRUE(mask & (1u << 10));
    TEST_ASSERT_TRUE((mask & (1u << 20)) && (mask & (1u << 21)) && (mask & (1u << 22)));
    TEST_ASSERT_FALSE(mask & (1u << 1));
    TEST_ASSERT_FALSE(mask & (1u << 11));
    TEST_ASSERT_FALSE(mask & (1u << 23));
    mask = cal_event_days(&s, 2027, 1, OFF);
    TEST_ASSERT_TRUE(mask & (1u << 1));
    TEST_ASSERT_FALSE(mask & (1u << 2));
    TEST_ASSERT_EQUAL_UINT32(0, cal_event_days(&s, 2026, 7, OFF));
}

static void test_the_same_instant_is_another_day_in_another_zone(void)
{
    static todo_store s;
    todo_store_clear(&s);
    s.events[s.n_events++] = ev(at(2026, 12, 10, 22, 0), at(2026, 12, 10, 23, 0)); /* 22:00 in UTC-3 = 01:00 UTC on the 11th */
    TEST_ASSERT_TRUE(cal_event_days(&s, 2026, 12, OFF) & (1u << 10));
    TEST_ASSERT_TRUE(cal_event_days(&s, 2026, 12, 0) & (1u << 11));
    TEST_ASSERT_FALSE(cal_event_days(&s, 2026, 12, 0) & (1u << 10));
}

static void when(todo_event e, int d, char *out)
{
    event_when(&e, at(2026, 12, d, 0, 0), OFF, out, 48);
}

static void test_an_event_says_when_in_the_words_of_the_design(void)
{
    char s[48];
    when(ev(at(2026, 12, 9, 10, 0), at(2026, 12, 9, 10, 45)), 9, s);
    TEST_ASSERT_EQUAL_STRING("10:00-10:45", s);
    when(ev(at(2026, 12, 9, 22, 0), at(2026, 12, 10, 1, 30)), 9, s);
    TEST_ASSERT_EQUAL_STRING("22:00 until Thu 01:30", s);
    when(ev(at(2026, 12, 9, 22, 0), at(2026, 12, 10, 1, 30)), 10, s);
    TEST_ASSERT_EQUAL_STRING("Until 01:30", s);
    when(ev(at(2026, 12, 9, 8, 0), at(2026, 12, 11, 8, 0)), 10, s);
    TEST_ASSERT_EQUAL_STRING("All day", s);
    when(ev(at(2026, 12, 9, 20, 0), at(2026, 12, 10, 0, 0)), 9, s);
    TEST_ASSERT_EQUAL_STRING("20:00 until midnight", s);
    when(ev(at(2026, 12, 9, 20, 0), at(2026, 12, 11, 0, 0)), 10, s);
    TEST_ASSERT_EQUAL_STRING("Until midnight", s);
    when(ev(at(2026, 12, 10, 0, 0), at(2026, 12, 11, 0, 0)), 10, s);
    TEST_ASSERT_EQUAL_STRING("00:00 until midnight", s);
}

static void test_days_and_instants_are_written_with_names(void)
{
    char s[40];
    day_title(2026, 12, 2, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Wed, Dec 2", s);
    day_title(2028, 2, 29, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Tue, Feb 29", s);
    instant_text(at(2026, 12, 2, 14, 5), OFF, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("Wed, Dec 2, 14:05", s);
}

static void test_counting_the_quests_due_on_a_day(void)
{
    static todo_store s;
    todo_store_clear(&s);
    s.quests[0].due = 20261204;
    s.quests[1].due = 20261204;
    s.quests[2].due = 20261205;
    s.quests[3].due = 0;
    s.n_quests = 4;
    TEST_ASSERT_EQUAL_INT(2, quests_due_on(&s, 20261204));
    TEST_ASSERT_EQUAL_INT(1, quests_due_on(&s, 20261205));
    TEST_ASSERT_EQUAL_INT(0, quests_due_on(&s, 20261206));
}

static void test_the_zone_list_steps_east_and_west_and_wraps_round(void)
{
    TEST_ASSERT_EQUAL_INT(-120, tz_step(-180, 1));
    TEST_ASSERT_EQUAL_INT(-210, tz_step(-180, -1));
    TEST_ASSERT_EQUAL_INT(345, tz_step(330, 1));
    TEST_ASSERT_EQUAL_INT(330, tz_step(345, -1));
    TEST_ASSERT_EQUAL_INT(-720, tz_step(840, 1));
    TEST_ASSERT_EQUAL_INT(840, tz_step(-720, -1));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, tz_step(-30, 1), "a value that is not in the list goes to the next one");
    TEST_ASSERT_EQUAL_INT(-60, tz_step(-30, -1));
    for (int z = -720; z <= 840; z++) { /* walking east visits strictly increasing zones until it wraps */
        int n = tz_step(z, 1);
        TEST_ASSERT_TRUE(n > z || n == -720);
    }
}

static void test_a_zone_is_written_as_utc_with_hours_and_minutes(void)
{
    char s[16];
    tz_text(-180, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("UTC-03:00", s);
    tz_text(330, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("UTC+05:30", s);
    tz_text(0, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("UTC+00:00", s);
    tz_text(-570, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("UTC-09:30", s);
    TEST_ASSERT_EQUAL_STRING("1 min", backlight_text(1));
    TEST_ASSERT_EQUAL_STRING("Never", backlight_text(backlight_count() - 1));
    TEST_ASSERT_EQUAL_STRING("?", backlight_text(-1));
    TEST_ASSERT_EQUAL_STRING("?", backlight_text(backlight_count()));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_month_has_the_rows_it_needs);
    RUN_TEST(test_six_rows_get_shorter_cells_so_that_they_fit);
    RUN_TEST(test_every_pixel_of_the_grid_maps_to_its_cell_and_the_outside_to_nothing);
    RUN_TEST(test_an_event_is_on_a_day_if_it_overlaps_it_and_touching_does_not_count);
    RUN_TEST(test_the_days_of_a_month_that_have_events_across_midnight_and_month_ends);
    RUN_TEST(test_the_same_instant_is_another_day_in_another_zone);
    RUN_TEST(test_an_event_says_when_in_the_words_of_the_design);
    RUN_TEST(test_days_and_instants_are_written_with_names);
    RUN_TEST(test_counting_the_quests_due_on_a_day);
    RUN_TEST(test_the_zone_list_steps_east_and_west_and_wraps_round);
    RUN_TEST(test_a_zone_is_written_as_utc_with_hours_and_minutes);
    return UNITY_END();
}
