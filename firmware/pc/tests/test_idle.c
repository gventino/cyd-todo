/* The backlight policy: dim at the timeout, wake on a touch, never for "Never". */

#include "todo/settings.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t T[3] = {30000u, 60000u, 300000u};

static void test_each_timeout_dims_exactly_at_its_millisecond(void)
{
    int b;
    for (b = 0; b < 3; b++) {
        todo_idle i;
        todo_idle_init(&i, 1000);
        TEST_ASSERT_EQUAL_UINT32(T[b], todo_idle_timeout_ms(b));
        TEST_ASSERT_EQUAL_INT(100, todo_idle_level(&i, 1000, b));
        TEST_ASSERT_EQUAL_INT(100, todo_idle_level(&i, 1000 + T[b] - 1, b));
        TEST_ASSERT_EQUAL_INT(TODO_DIM_PERCENT, todo_idle_level(&i, 1000 + T[b], b));
        TEST_ASSERT_EQUAL_INT(TODO_DIM_PERCENT, todo_idle_level(&i, 1000 + 10 * T[b], b));
    }
}

static void test_never_never_dims(void)
{
    todo_idle i;
    todo_idle_init(&i, 0);
    TEST_ASSERT_EQUAL_UINT32(0, todo_idle_timeout_ms(3));
    TEST_ASSERT_EQUAL_INT(100, todo_idle_level(&i, 0xFFFFFFF0u, 3));
}

static void test_a_touch_restarts_the_clock_and_wakes_a_dimmed_screen(void)
{
    todo_idle i;
    todo_idle_init(&i, 0);
    TEST_ASSERT_EQUAL_INT(TODO_DIM_PERCENT, todo_idle_level(&i, 40000, 0));
    todo_idle_touch(&i, 40000);
    TEST_ASSERT_EQUAL_INT(100, todo_idle_level(&i, 40000, 0));
    TEST_ASSERT_EQUAL_INT(100, todo_idle_level(&i, 69999, 0));
    TEST_ASSERT_EQUAL_INT(TODO_DIM_PERCENT, todo_idle_level(&i, 70000, 0));
}

static void test_the_clock_wrapping_round_does_not_dim_or_wake_by_mistake(void)
{
    todo_idle i;
    todo_idle_init(&i, 0xFFFFFF00u);
    TEST_ASSERT_EQUAL_INT(100, todo_idle_level(&i, 0xFFFFFF00u + 29999u, 0)); /* past zero */
    TEST_ASSERT_EQUAL_INT(TODO_DIM_PERCENT, todo_idle_level(&i, 0xFFFFFF00u + 30000u, 0));
}

static void test_a_changed_timeout_takes_effect_at_once(void)
{
    todo_idle i;
    todo_idle_init(&i, 0);
    TEST_ASSERT_EQUAL_INT(TODO_DIM_PERCENT, todo_idle_level(&i, 45000, 0));
    TEST_ASSERT_EQUAL_INT_MESSAGE(100, todo_idle_level(&i, 45000, 1), "a longer timeout wakes it");
    TEST_ASSERT_EQUAL_INT_MESSAGE(100, todo_idle_level(&i, 45000, 3), "never wakes it");
    TEST_ASSERT_EQUAL_INT_MESSAGE(TODO_DIM_PERCENT, todo_idle_level(&i, 61000, 1), "and a shorter one dims at once");
}

static void test_an_index_outside_the_list_never_dims(void)
{
    todo_idle i;
    todo_idle_init(&i, 0);
    TEST_ASSERT_EQUAL_INT(100, todo_idle_level(&i, 999999, -1));
    TEST_ASSERT_EQUAL_INT(100, todo_idle_level(&i, 999999, 4));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_timeout_dims_exactly_at_its_millisecond);
    RUN_TEST(test_never_never_dims);
    RUN_TEST(test_a_touch_restarts_the_clock_and_wakes_a_dimmed_screen);
    RUN_TEST(test_the_clock_wrapping_round_does_not_dim_or_wake_by_mistake);
    RUN_TEST(test_a_changed_timeout_takes_effect_at_once);
    RUN_TEST(test_an_index_outside_the_list_never_dims);
    return UNITY_END();
}
