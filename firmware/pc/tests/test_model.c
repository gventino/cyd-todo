/* The cache, the string copy, the levels. */

#include <string.h>

#include "todo/levels.h"
#include "todo/model.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_a_string_that_fits_is_copied_whole(void)
{
    char b[8];
    TEST_ASSERT_FALSE(todo_copy(b, sizeof(b), "abcdefg"));
    TEST_ASSERT_EQUAL_STRING("abcdefg", b);
    TEST_ASSERT_FALSE(todo_copy(b, sizeof(b), ""));
    TEST_ASSERT_EQUAL_STRING("", b);
}

static void test_a_string_that_does_not_fit_is_cut_and_says_so(void)
{
    char b[8];
    TEST_ASSERT_TRUE(todo_copy(b, sizeof(b), "abcdefgh"));
    TEST_ASSERT_EQUAL_STRING("abcdefg", b);
}

static void test_a_character_is_never_cut_in_two(void)
{
    char b[6];
    /* "aaaé" is 5 bytes (é is 2): it fits in 6. "aaaaé" is 6 bytes: 5 + NUL would cut the é */
    TEST_ASSERT_FALSE(todo_copy(b, sizeof(b), "aaa\xC3\xA9"));
    TEST_ASSERT_TRUE(todo_copy(b, sizeof(b), "aaaa\xC3\xA9"));
    TEST_ASSERT_EQUAL_STRING("aaaa", b);
    /* a four-byte character */
    TEST_ASSERT_TRUE(todo_copy(b, sizeof(b), "aa\xF0\x9F\x98\x80z"));
    TEST_ASSERT_EQUAL_STRING("aa", b);
    /* a three-byte one that ends exactly at the limit */
    TEST_ASSERT_TRUE(todo_copy(b, 6, "a\xE2\x82\xAC\xE2\x82\xAC"));
    TEST_ASSERT_EQUAL_STRING("a\xE2\x82\xAC", b);
}

static void test_the_limits_of_the_contract_are_the_buffer_sizes(void)
{
    todo_quest q;
    char title[200], notes[400];
    memset(title, 'a', sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    memset(notes, 'n', sizeof(notes) - 1);
    notes[sizeof(notes) - 1] = '\0';
    TEST_ASSERT_TRUE(todo_copy(q.title, sizeof(q.title), title));
    TEST_ASSERT_EQUAL_UINT(64, strlen(q.title));
    TEST_ASSERT_TRUE(todo_copy(q.notes, sizeof(q.notes), notes));
    TEST_ASSERT_EQUAL_UINT(256, strlen(q.notes));
}

static void test_a_missing_destination_or_source_is_safe(void)
{
    char b[4] = "xyz";
    TEST_ASSERT_TRUE(todo_copy(NULL, 0, "abc"));
    TEST_ASSERT_FALSE(todo_copy(NULL, 0, ""));
    TEST_ASSERT_TRUE(todo_copy(b, 0, "abc"));
    TEST_ASSERT_FALSE(todo_copy(b, sizeof(b), NULL));
    TEST_ASSERT_EQUAL_STRING("", b);
    TEST_ASSERT_TRUE(todo_copy(b, 1, "abc"));
    TEST_ASSERT_EQUAL_STRING("", b);
}

static void test_the_store_finds_by_id_and_clears(void)
{
    static todo_store s;
    todo_store_clear(&s);
    s.quests[0].id = 7;
    s.quests[1].id = 9;
    s.n_quests = 2;
    s.events[0].id = 3;
    s.n_events = 1;
    TEST_ASSERT_EQUAL_PTR(&s.quests[1], todo_find_quest(&s, 9));
    TEST_ASSERT_NULL(todo_find_quest(&s, 8));
    TEST_ASSERT_EQUAL_PTR(&s.events[0], todo_find_event(&s, 3));
    TEST_ASSERT_NULL(todo_find_event(&s, 4));
    todo_store_clear(&s);
    TEST_ASSERT_EQUAL_INT(0, s.n_quests);
    TEST_ASSERT_NULL(todo_find_quest(&s, 7));
}

static void test_the_cache_is_small_enough_for_the_board(void)
{
    TEST_ASSERT_TRUE_MESSAGE(sizeof(todo_store) < 40 * 1024, "the model must stay well under the heap budget");
}

static void test_levels_start_where_the_design_says(void)
{
    TEST_ASSERT_EQUAL_INT32(0, todo_xp_for_level(1));
    TEST_ASSERT_EQUAL_INT32(100, todo_xp_for_level(2));
    TEST_ASSERT_EQUAL_INT32(300, todo_xp_for_level(3));
    TEST_ASSERT_EQUAL_INT32(600, todo_xp_for_level(4));
    TEST_ASSERT_EQUAL_INT32(1000, todo_xp_for_level(5));
    TEST_ASSERT_EQUAL_INT32(0, todo_xp_for_level(0));
    TEST_ASSERT_EQUAL_INT32(0, todo_xp_for_level(-5));
}

static void test_the_level_of_an_xp_total(void)
{
    TEST_ASSERT_EQUAL_INT(1, todo_level_for_xp(-10));
    TEST_ASSERT_EQUAL_INT(1, todo_level_for_xp(0));
    TEST_ASSERT_EQUAL_INT(1, todo_level_for_xp(99));
    TEST_ASSERT_EQUAL_INT(2, todo_level_for_xp(100));
    TEST_ASSERT_EQUAL_INT(2, todo_level_for_xp(299));
    TEST_ASSERT_EQUAL_INT(3, todo_level_for_xp(300));
    TEST_ASSERT_EQUAL_INT(4, todo_level_for_xp(600));
    TEST_ASSERT_EQUAL_INT(TODO_LEVEL_MAX, todo_level_for_xp(INT32_MAX));
    /* every threshold is the first XP of its level */
    for (int l = 2; l <= TODO_LEVEL_MAX; l++) {
        TEST_ASSERT_EQUAL_INT(l, todo_level_for_xp(todo_xp_for_level(l)));
        TEST_ASSERT_EQUAL_INT(l - 1, todo_level_for_xp(todo_xp_for_level(l) - 1));
    }
}

static void test_the_bar_fraction_runs_from_empty_to_full_inside_a_level(void)
{
    TEST_ASSERT_EQUAL_INT(0, todo_level_fraction(0));
    TEST_ASSERT_EQUAL_INT(2048, todo_level_fraction(50));  /* half of level 1 (0..100) */
    TEST_ASSERT_EQUAL_INT(0, todo_level_fraction(100));    /* the start of level 2 */
    TEST_ASSERT_EQUAL_INT(2048, todo_level_fraction(200)); /* half of level 2 (100..300) */
    TEST_ASSERT_TRUE(todo_level_fraction(299) < TODO_FRACTION_ONE);
    TEST_ASSERT_EQUAL_INT(TODO_FRACTION_ONE, todo_level_fraction(INT32_MAX));
    TEST_ASSERT_EQUAL_INT(0, todo_level_fraction(-4));
    for (int32_t xp = 0; xp < 5000; xp += 7) {
        int f = todo_level_fraction(xp);
        TEST_ASSERT_TRUE(f >= 0 && f <= TODO_FRACTION_ONE);
    }
}

static void test_the_xp_of_a_quest_is_ten_times_its_difficulty_clamped(void)
{
    TEST_ASSERT_EQUAL_INT32(10, todo_xp_for_quest(1));
    TEST_ASSERT_EQUAL_INT32(50, todo_xp_for_quest(5));
    TEST_ASSERT_EQUAL_INT32(10, todo_xp_for_quest(0));
    TEST_ASSERT_EQUAL_INT32(50, todo_xp_for_quest(9));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_string_that_fits_is_copied_whole);
    RUN_TEST(test_a_string_that_does_not_fit_is_cut_and_says_so);
    RUN_TEST(test_a_character_is_never_cut_in_two);
    RUN_TEST(test_the_limits_of_the_contract_are_the_buffer_sizes);
    RUN_TEST(test_a_missing_destination_or_source_is_safe);
    RUN_TEST(test_the_store_finds_by_id_and_clears);
    RUN_TEST(test_the_cache_is_small_enough_for_the_board);
    RUN_TEST(test_levels_start_where_the_design_says);
    RUN_TEST(test_the_level_of_an_xp_total);
    RUN_TEST(test_the_bar_fraction_runs_from_empty_to_full_inside_a_level);
    RUN_TEST(test_the_xp_of_a_quest_is_ten_times_its_difficulty_clamped);
    return UNITY_END();
}
