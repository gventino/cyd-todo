/* Calendar and zone arithmetic: against the vectors shared with the server, and by hand. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "todo/datetime.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define VECTORS REPO_ROOT "/docs/vectors/time.tsv"

/* Runs `fn` for every line of the vectors of one kind; `fields` are the tab separated columns after the kind. */
typedef void (*line_fn)(char **f, int n, int line);

static int for_each(const char *kind, line_fn fn)
{
    FILE *fp = fopen(VECTORS, "r");
    char buf[256];
    int line = 0, count = 0;

    TEST_ASSERT_NOT_NULL_MESSAGE(fp, "docs/vectors/time.tsv is missing");
    while (fgets(buf, sizeof(buf), fp)) {
        char *f[8];
        int n = 0;
        char *p = buf;
        line++;
        buf[strcspn(buf, "\r\n")] = '\0';
        if (buf[0] == '#' || buf[0] == '\0') {
            continue;
        }
        f[n++] = p;
        while (*p && n < 8) {
            if (*p == '\t') {
                *p = '\0';
                f[n++] = p + 1;
            }
            p++;
        }
        if (strcmp(f[0], kind) == 0) {
            fn(f + 1, n - 1, line);
            count++;
        }
    }
    fclose(fp);
    return count;
}

static void civil_line(char **f, int n, int line)
{
    long long days = atoll(f[0]);
    int y, m, d;
    char msg[64];

    (void)n;
    snprintf(msg, sizeof(msg), "vector line %d", line);
    todo_civil_from_days(days, &y, &m, &d);
    TEST_ASSERT_EQUAL_INT_MESSAGE(atoi(f[1]), y, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(atoi(f[2]), m, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(atoi(f[3]), d, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(atoi(f[4]), todo_weekday(days), msg);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(days, todo_days_from_civil(y, m, d), msg);
}

static void test_every_civil_vector(void)
{
    TEST_ASSERT_TRUE(for_each("civil", civil_line) > 1000);
}

static void local_line(char **f, int n, int line)
{
    long long epoch = atoll(f[0]);
    int off = atoi(f[1]);
    char out[32], msg[64];
    todo_local t;
    int64_t back;

    (void)n;
    snprintf(msg, sizeof(msg), "vector line %d", line);
    TEST_ASSERT_EQUAL_UINT(16, todo_format_local(out, sizeof(out), epoch, off));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(f[2], out, msg);
    /* reading it back gives the same minute */
    TEST_ASSERT_TRUE_MESSAGE(todo_parse_local(out, off, &back), msg);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(epoch - ((epoch % 60) + 60) % 60, back, msg); /* the minute it was in */
    todo_local_fields(epoch, off, &t);
    snprintf(out, sizeof(out), "%02d:%02d", t.hour, t.minute);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(f[2] + 11, out, msg);
}

static void test_every_local_vector(void)
{
    TEST_ASSERT_TRUE(for_each("local", local_line) > 300);
}

static void parse_line(char **f, int n, int line)
{
    int64_t got = 0;
    bool ok = todo_parse_local(f[0], atoi(f[1]), &got);
    char msg[96];

    (void)n;
    snprintf(msg, sizeof(msg), "vector line %d: \"%s\"", line, f[0]);
    if (strcmp(f[2], "none") == 0) {
        TEST_ASSERT_FALSE_MESSAGE(ok, msg);
    } else {
        TEST_ASSERT_TRUE_MESSAGE(ok, msg);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(atoll(f[2]), got, msg);
    }
}

static void test_every_parse_vector(void)
{
    TEST_ASSERT_TRUE(for_each("parse", parse_line) > 50);
}

static void window_line(char **f, int n, int line)
{
    int64_t from, to;
    char msg[64];

    (void)n;
    snprintf(msg, sizeof(msg), "vector line %d", line);
    todo_month_window(atoi(f[0]), atoi(f[1]), atoi(f[2]), &from, &to);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(atoll(f[3]), from, msg);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(atoll(f[4]), to, msg);
}

static void test_every_month_window_vector(void)
{
    TEST_ASSERT_TRUE(for_each("window", window_line) > 200);
}

static void test_the_whole_supported_range_round_trips(void)
{
    int64_t z;
    for (z = todo_days_from_civil(1999, 12, 1); z < todo_days_from_civil(2101, 1, 1); z++) {
        int y, m, d;
        todo_civil_from_days(z, &y, &m, &d);
        TEST_ASSERT_EQUAL_INT64(z, todo_days_from_civil(y, m, d));
        TEST_ASSERT_TRUE(m >= 1 && m <= 12 && d >= 1 && d <= todo_days_in_month(y, m));
    }
    TEST_ASSERT_EQUAL_INT(0, todo_days_in_month(2026, 0));
    TEST_ASSERT_EQUAL_INT(0, todo_days_in_month(2026, 13));
    TEST_ASSERT_EQUAL_INT(29, todo_days_in_month(2028, 2));
    TEST_ASSERT_EQUAL_INT(28, todo_days_in_month(2100, 2));
}

static void test_an_instant_past_2038_does_not_overflow(void)
{
    todo_local t;
    todo_local_fields(4102444799LL, 0, &t); /* 2099-12-31 23:59:59 */
    TEST_ASSERT_EQUAL_INT(2099, t.year);
    TEST_ASSERT_EQUAL_INT(12, t.month);
    TEST_ASSERT_EQUAL_INT(31, t.day);
    TEST_ASSERT_EQUAL_INT(23, t.hour);
}

static void test_the_month_grid_is_monday_first_and_blank_outside_the_month(void)
{
    /* December 2026 starts on a Tuesday and has 31 days */
    TEST_ASSERT_EQUAL_INT(0, todo_grid_day(2026, 12, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, todo_grid_day(2026, 12, 0, 1));
    TEST_ASSERT_EQUAL_INT(6, todo_grid_day(2026, 12, 0, 6));
    TEST_ASSERT_EQUAL_INT(7, todo_grid_day(2026, 12, 1, 0));
    TEST_ASSERT_EQUAL_INT(31, todo_grid_day(2026, 12, 4, 3));
    TEST_ASSERT_EQUAL_INT(0, todo_grid_day(2026, 12, 4, 4));
    TEST_ASSERT_EQUAL_INT(0, todo_grid_day(2026, 12, 5, 0));
    TEST_ASSERT_EQUAL_INT(0, todo_grid_day(2026, 12, 6, 0));
    TEST_ASSERT_EQUAL_INT(0, todo_grid_day(2026, 12, -1, 0));
    TEST_ASSERT_EQUAL_INT(0, todo_grid_day(2026, 12, 0, 7));
    /* every day of every month appears exactly once */
    for (int y = 2026; y <= 2028; y++) {
        for (int m = 1; m <= 12; m++) {
            int seen[32] = {0};
            for (int r = 0; r < 6; r++) {
                for (int c = 0; c < 7; c++) {
                    int d = todo_grid_day(y, m, r, c);
                    if (d) {
                        seen[d]++;
                    }
                }
            }
            for (int d = 1; d <= todo_days_in_month(y, m); d++) {
                TEST_ASSERT_EQUAL_INT(1, seen[d]);
            }
        }
    }
}

static void test_a_month_that_needs_six_rows_has_them(void)
{
    /* August 2026 starts on a Saturday and has 31 days: 6 rows */
    TEST_ASSERT_EQUAL_INT(1, todo_grid_day(2026, 8, 0, 5));
    TEST_ASSERT_EQUAL_INT(31, todo_grid_day(2026, 8, 5, 0));
}

static void test_months_walk_across_the_year_end(void)
{
    int y = 2026, m = 12;
    todo_month_next(&y, &m);
    TEST_ASSERT_EQUAL_INT(2027, y);
    TEST_ASSERT_EQUAL_INT(1, m);
    todo_month_prev(&y, &m);
    TEST_ASSERT_EQUAL_INT(2026, y);
    TEST_ASSERT_EQUAL_INT(12, m);
    m = 1;
    todo_month_prev(&y, &m);
    TEST_ASSERT_EQUAL_INT(11 + 1, m);
    TEST_ASSERT_EQUAL_INT(2025, y);
}

static void test_a_due_date_packs_unpacks_and_parses_strictly(void)
{
    int y, m, d;
    TEST_ASSERT_EQUAL_INT32(20261224, todo_due_pack(2026, 12, 24));
    todo_due_unpack(20261224, &y, &m, &d);
    TEST_ASSERT_EQUAL_INT(2026, y);
    TEST_ASSERT_EQUAL_INT(12, m);
    TEST_ASSERT_EQUAL_INT(24, d);
    TEST_ASSERT_EQUAL_INT32(20280229, todo_due_parse("2028-02-29"));
    TEST_ASSERT_EQUAL_INT32(20000101, todo_due_parse("2000-01-01"));
    TEST_ASSERT_EQUAL_INT32(20991231, todo_due_parse("2099-12-31"));
    const char *bad[] = {"", "2027-02-29", "2100-01-01", "1999-12-31", "2026-13-01", "2026-00-10", "2026-01-00", "2026-04-31", "2026-1-01", "2026/01/01",
                         "2026-01-011", "2026-01-0", " 2026-01-01", "abcd-ef-gh", "2026-01-01 "};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_EQUAL_INT32_MESSAGE(0, todo_due_parse(bad[i]), bad[i]);
    }
    TEST_ASSERT_EQUAL_INT32(0, todo_due_parse(NULL));
}

static void test_names_are_never_null(void)
{
    TEST_ASSERT_EQUAL_STRING("December", todo_month_name(12));
    TEST_ASSERT_EQUAL_STRING("January", todo_month_name(1));
    TEST_ASSERT_EQUAL_STRING("?", todo_month_name(0));
    TEST_ASSERT_EQUAL_STRING("?", todo_month_name(13));
    TEST_ASSERT_EQUAL_STRING("Mon", todo_weekday_short(0));
    TEST_ASSERT_EQUAL_STRING("Sun", todo_weekday_short(6));
    TEST_ASSERT_EQUAL_STRING("?", todo_weekday_short(-1));
    TEST_ASSERT_EQUAL_STRING("?", todo_weekday_short(7));
}

static void test_formatting_needs_room_and_says_zero_without_it(void)
{
    char b[16];
    TEST_ASSERT_EQUAL_UINT(0, todo_format_hhmm(NULL, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT(0, todo_format_hhmm(b, 5, 0, 0));
    TEST_ASSERT_EQUAL_UINT(5, todo_format_hhmm(b, 6, 1796230800LL, -180));
    TEST_ASSERT_EQUAL_STRING("14:00", b);
    TEST_ASSERT_EQUAL_UINT(0, todo_format_local(b, 16, 0, 0));
    TEST_ASSERT_FALSE(todo_parse_local(NULL, 0, &(int64_t){0}));
}

static void test_local_fields_know_the_weekday_across_midnight(void)
{
    todo_local t;
    int64_t midnight = todo_local_midnight(2026, 12, 2, -180);
    TEST_ASSERT_EQUAL_INT64(todo_days_from_civil(2026, 12, 2) * TODO_DAY + 3 * 3600, midnight);
    /* an hour before local midnight is still the 1st (a Tuesday) at 23:00 */
    todo_local_fields(midnight - 3600, -180, &t);
    TEST_ASSERT_EQUAL_INT(1, t.day);
    TEST_ASSERT_EQUAL_INT(23, t.hour);
    TEST_ASSERT_EQUAL_INT(1, t.weekday);
    todo_local_fields(midnight, -180, &t);
    TEST_ASSERT_EQUAL_INT(2, t.day);
    TEST_ASSERT_EQUAL_INT(0, t.hour);
    TEST_ASSERT_EQUAL_INT(2, t.weekday);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_civil_vector);
    RUN_TEST(test_every_local_vector);
    RUN_TEST(test_every_parse_vector);
    RUN_TEST(test_every_month_window_vector);
    RUN_TEST(test_the_whole_supported_range_round_trips);
    RUN_TEST(test_an_instant_past_2038_does_not_overflow);
    RUN_TEST(test_the_month_grid_is_monday_first_and_blank_outside_the_month);
    RUN_TEST(test_a_month_that_needs_six_rows_has_them);
    RUN_TEST(test_months_walk_across_the_year_end);
    RUN_TEST(test_a_due_date_packs_unpacks_and_parses_strictly);
    RUN_TEST(test_names_are_never_null);
    RUN_TEST(test_formatting_needs_room_and_says_zero_without_it);
    RUN_TEST(test_local_fields_know_the_weekday_across_midnight);
    return UNITY_END();
}
