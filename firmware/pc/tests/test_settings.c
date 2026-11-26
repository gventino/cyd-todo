/* The record that survives a reboot: only a good one is believed. */

#include <string.h>

#include "todo/settings.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static todo_settings S(int zone, int backlight, int palette)
{
    todo_settings s;
    s.zone_min = (int16_t)zone;
    s.backlight = (uint8_t)backlight;
    s.palette = (uint8_t)palette;
    return s;
}

static void test_every_valid_setting_round_trips_through_a_record_of_ten_bytes(void)
{
    int z, b, p, n = 0;
    for (z = 0; z < todo_zone_count(); z++) {
        for (b = 0; b < TODO_BACKLIGHT_COUNT; b++) {
            for (p = 0; p < TODO_PALETTE_KINDS; p++) {
                todo_settings in = S(todo_zone_at(z), b, p), out = S(999, 9, 9);
                uint8_t rec[TODO_SETTINGS_RECORD];
                TEST_ASSERT_TRUE(todo_settings_encode(&in, rec));
                TEST_ASSERT_TRUE(todo_settings_decode(rec, sizeof(rec), &out));
                TEST_ASSERT_EQUAL_INT(in.zone_min, out.zone_min);
                TEST_ASSERT_EQUAL_UINT8(in.backlight, out.backlight);
                TEST_ASSERT_EQUAL_UINT8(in.palette, out.palette);
                n++;
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(38 * 4 * 2, n);
    TEST_ASSERT_EQUAL_INT(10, TODO_SETTINGS_RECORD);
}

static void test_the_record_has_the_documented_layout(void)
{
    uint8_t rec[TODO_SETTINGS_RECORD];
    todo_settings s = S(-180, 2, 1);
    TEST_ASSERT_TRUE(todo_settings_encode(&s, rec));
    TEST_ASSERT_EQUAL_HEX8(1, rec[0]);
    TEST_ASSERT_EQUAL_HEX8(0x4C, rec[1]); /* -180 = 0xFF4C, little endian */
    TEST_ASSERT_EQUAL_HEX8(0xFF, rec[2]);
    TEST_ASSERT_EQUAL_HEX8(2, rec[3]);
    TEST_ASSERT_EQUAL_HEX8(1, rec[4]);
    TEST_ASSERT_EQUAL_HEX8(0, rec[5] | rec[6] | rec[7]);
    TEST_ASSERT_EQUAL_HEX16((uint16_t)(rec[8] | (rec[9] << 8)), todo_crc16(rec, 8));
}

static void test_the_crc_is_the_ccitt_false_one(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1, todo_crc16((const uint8_t *)"123456789", 9)); /* the standard check value */
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, todo_crc16((const uint8_t *)"", 0));
}

static void test_a_single_flipped_bit_anywhere_is_never_believed(void)
{
    todo_settings in = S(330, 3, 1), out = S(7, 7, 7);
    uint8_t rec[TODO_SETTINGS_RECORD];
    size_t byte;
    int bit;
    TEST_ASSERT_TRUE(todo_settings_encode(&in, rec));
    for (byte = 0; byte < sizeof(rec); byte++) {
        for (bit = 0; bit < 8; bit++) {
            uint8_t bad[TODO_SETTINGS_RECORD];
            memcpy(bad, rec, sizeof(rec));
            bad[byte] = (uint8_t)(bad[byte] ^ (1u << bit));
            TEST_ASSERT_FALSE_MESSAGE(todo_settings_decode(bad, sizeof(bad), &out), "a flipped bit");
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, out.zone_min, "out is untouched by a refusal");
}

static void test_a_record_of_any_other_size_is_refused(void)
{
    todo_settings in = S(0, 1, 0), out;
    uint8_t rec[TODO_SETTINGS_RECORD + 4] = {0};
    size_t n;
    TEST_ASSERT_TRUE(todo_settings_encode(&in, rec));
    for (n = 0; n < sizeof(rec); n++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(n == TODO_SETTINGS_RECORD, todo_settings_decode(rec, n, &out), "only exactly ten bytes");
    }
    TEST_ASSERT_FALSE(todo_settings_decode(NULL, 10, &out));
    TEST_ASSERT_FALSE(todo_settings_decode(rec, 10, NULL));
}

/* a record with a good CRC but values the Settings tab cannot produce */
static void forge(uint8_t rec[TODO_SETTINGS_RECORD], int version, int zone, int backlight, int palette, int reserved)
{
    uint16_t crc;
    rec[0] = (uint8_t)version;
    rec[1] = (uint8_t)(zone & 0xFF);
    rec[2] = (uint8_t)((zone >> 8) & 0xFF);
    rec[3] = (uint8_t)backlight;
    rec[4] = (uint8_t)palette;
    rec[5] = (uint8_t)reserved;
    rec[6] = rec[7] = 0;
    crc = todo_crc16(rec, 8);
    rec[8] = (uint8_t)(crc & 0xFF);
    rec[9] = (uint8_t)(crc >> 8);
}

static void test_good_crc_but_values_nobody_could_have_set_are_refused(void)
{
    todo_settings out;
    uint8_t rec[TODO_SETTINGS_RECORD];
    forge(rec, 1, -180, 1, 0, 0);
    TEST_ASSERT_TRUE_MESSAGE(todo_settings_decode(rec, 10, &out), "the forgery works when the values are right");
    forge(rec, 2, -180, 1, 0, 0);
    TEST_ASSERT_FALSE_MESSAGE(todo_settings_decode(rec, 10, &out), "another version");
    forge(rec, 0, -180, 1, 0, 0);
    TEST_ASSERT_FALSE(todo_settings_decode(rec, 10, &out));
    forge(rec, 1, -181, 1, 0, 0);
    TEST_ASSERT_FALSE_MESSAGE(todo_settings_decode(rec, 10, &out), "a zone that is not in the list");
    forge(rec, 1, 900, 1, 0, 0);
    TEST_ASSERT_FALSE(todo_settings_decode(rec, 10, &out));
    forge(rec, 1, -180, 4, 0, 0);
    TEST_ASSERT_FALSE_MESSAGE(todo_settings_decode(rec, 10, &out), "a backlight index past the list");
    forge(rec, 1, -180, 255, 0, 0);
    TEST_ASSERT_FALSE(todo_settings_decode(rec, 10, &out));
    forge(rec, 1, -180, 1, 2, 0);
    TEST_ASSERT_FALSE_MESSAGE(todo_settings_decode(rec, 10, &out), "a third palette");
    forge(rec, 1, -180, 1, 0, 1);
    TEST_ASSERT_FALSE_MESSAGE(todo_settings_decode(rec, 10, &out), "reserved bytes are zero");
}

static void test_invalid_settings_are_not_encoded(void)
{
    uint8_t rec[TODO_SETTINGS_RECORD];
    todo_settings s = S(-181, 1, 0);
    memset(rec, 0xAA, sizeof(rec));
    TEST_ASSERT_FALSE(todo_settings_encode(&s, rec));
    s = S(0, 4, 0);
    TEST_ASSERT_FALSE(todo_settings_encode(&s, rec));
    s = S(0, 1, 2);
    TEST_ASSERT_FALSE(todo_settings_encode(&s, rec));
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xAA, rec[0], "nothing written for a refusal");
}

static void test_the_defaults_are_valid_and_a_bad_default_zone_becomes_utc(void)
{
    todo_settings s;
    todo_settings_default(&s, -180);
    TEST_ASSERT_TRUE(todo_settings_valid(&s));
    TEST_ASSERT_EQUAL_INT(-180, s.zone_min);
    TEST_ASSERT_EQUAL_UINT8(1, s.backlight);
    TEST_ASSERT_EQUAL_UINT8(0, s.palette);
    todo_settings_default(&s, 7);
    TEST_ASSERT_EQUAL_INT(0, s.zone_min);
    TEST_ASSERT_TRUE(todo_settings_valid(&s));
}

static void test_the_zone_list_is_sorted_unique_and_what_the_tab_can_reach(void)
{
    int i;
    for (i = 1; i < todo_zone_count(); i++) {
        TEST_ASSERT_TRUE(todo_zone_at(i) > todo_zone_at(i - 1));
    }
    TEST_ASSERT_EQUAL_INT(-720, todo_zone_at(0));
    TEST_ASSERT_EQUAL_INT(840, todo_zone_at(todo_zone_count() - 1));
    TEST_ASSERT_TRUE(todo_zone_valid(0) && todo_zone_valid(-180) && todo_zone_valid(345) && todo_zone_valid(840));
    TEST_ASSERT_FALSE(todo_zone_valid(1) || todo_zone_valid(-181) || todo_zone_valid(841) || todo_zone_valid(-721));
    TEST_ASSERT_EQUAL_INT(0, todo_zone_at(-1));
    TEST_ASSERT_EQUAL_INT(0, todo_zone_at(todo_zone_count()));
}

static void test_changes_are_saved_once_after_they_have_stopped(void)
{
    todo_debounce d;
    todo_debounce_init(&d);
    TEST_ASSERT_FALSE_MESSAGE(todo_debounce_due(&d, 100000, 2000), "nothing changed, nothing to save");
    todo_debounce_change(&d, 1000);
    TEST_ASSERT_FALSE(todo_debounce_due(&d, 1000, 2000));
    TEST_ASSERT_FALSE(todo_debounce_due(&d, 2999, 2000));
    todo_debounce_change(&d, 2500); /* an arrow held down: another change */
    TEST_ASSERT_FALSE(todo_debounce_due(&d, 4499, 2000));
    TEST_ASSERT_TRUE_MESSAGE(todo_debounce_due(&d, 4500, 2000), "two seconds after the last change");
    TEST_ASSERT_FALSE_MESSAGE(todo_debounce_due(&d, 9000, 2000), "and only once");
}

static void test_the_debounce_survives_the_clock_wrapping_round(void)
{
    todo_debounce d;
    todo_debounce_init(&d);
    todo_debounce_change(&d, 0xFFFFFF00u);
    TEST_ASSERT_FALSE(todo_debounce_due(&d, 0xFFFFFF00u + 1999u, 2000));
    TEST_ASSERT_TRUE(todo_debounce_due(&d, 0xFFFFFF00u + 2000u, 2000)); /* wraps past zero */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_valid_setting_round_trips_through_a_record_of_ten_bytes);
    RUN_TEST(test_the_record_has_the_documented_layout);
    RUN_TEST(test_the_crc_is_the_ccitt_false_one);
    RUN_TEST(test_a_single_flipped_bit_anywhere_is_never_believed);
    RUN_TEST(test_a_record_of_any_other_size_is_refused);
    RUN_TEST(test_good_crc_but_values_nobody_could_have_set_are_refused);
    RUN_TEST(test_invalid_settings_are_not_encoded);
    RUN_TEST(test_the_defaults_are_valid_and_a_bad_default_zone_becomes_utc);
    RUN_TEST(test_the_zone_list_is_sorted_unique_and_what_the_tab_can_reach);
    RUN_TEST(test_changes_are_saved_once_after_they_have_stopped);
    RUN_TEST(test_the_debounce_survives_the_clock_wrapping_round);
    return UNITY_END();
}
