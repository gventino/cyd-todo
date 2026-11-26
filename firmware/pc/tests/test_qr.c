/* The QR wrapper: the mask is the encoder's matrix, drawn as the screen will draw it. */

#include <stdio.h>
#include <string.h>

#include "qrcodegen.h"
#include "rig.h"
#include "todo/qr.h"
#include "todo/theme_quest.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static todo_qr Q;
static rig R;

/* what the screen draws at pixel (x, y) of the code, quiet zone included: 15 for ink, 0 for paper (the mask is one bit per module at scale 4) */
static int coverage(const todo_qr *q, int x, int y)
{
    int col = x / TODO_QR_MODULE_PX - TODO_QR_QUIET, row = y / TODO_QR_MODULE_PX - TODO_QR_QUIET;
    size_t row_bytes = ((size_t)q->size + 7u) / 8u;

    if (col < 0 || row < 0 || col >= q->size || row >= q->size) {
        return 0;
    }
    return (q->mask[(size_t)row * row_bytes + (size_t)(col / 8)] >> (7 - col % 8)) & 1 ? 15 : 0;
}

static void text_of(char *buf, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        buf[i] = (char)('a' + i % 26);
    }
    buf[n] = '\0';
}

static void test_every_module_of_the_mask_is_the_encoders_module(void)
{
    static const int lens[] = {1, 20, 40, 61, 84};
    size_t k;
    for (k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
        char t[100];
        int row, col, y, x;
        text_of(t, lens[k]);
        TEST_ASSERT_TRUE_MESSAGE(todo_qr_encode(&Q, t), t);
        TEST_ASSERT_EQUAL_INT((Q.size + 8) * 4, Q.px);
        for (row = 0; row < Q.size; row++) {
            for (col = 0; col < Q.size; col++) {
                bool dark = todo_qr_module(&Q, col, row);
                TEST_ASSERT_EQUAL_INT(dark, qrcodegen_getModule(Q.modules, col, row));
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++) {
                        TEST_ASSERT_EQUAL_INT(dark ? 15 : 0, coverage(&Q, (col + 4) * 4 + x, (row + 4) * 4 + y));
                    }
                }
            }
        }
    }
}

static void test_the_quiet_zone_is_empty_on_every_side(void)
{
    int i, j;
    TEST_ASSERT_TRUE(todo_qr_encode(&Q, "http://192.168.1.50:8080/m/00000000000000000000000001"));
    for (i = 0; i < Q.px; i++) {
        for (j = 0; j < 16; j++) {
            TEST_ASSERT_EQUAL_INT(0, coverage(&Q, i, j));
            TEST_ASSERT_EQUAL_INT(0, coverage(&Q, i, Q.px - 1 - j));
            TEST_ASSERT_EQUAL_INT(0, coverage(&Q, j, i));
            TEST_ASSERT_EQUAL_INT(0, coverage(&Q, Q.px - 1 - j, i));
        }
    }
}

static void test_the_finder_pattern_is_where_the_standard_puts_it(void)
{
    int i;
    TEST_ASSERT_TRUE(todo_qr_encode(&Q, "hello"));
    for (i = 0; i < 7; i++) { /* the top row of the top-left finder is dark for 7 modules; the 8th is the separator */
        TEST_ASSERT_TRUE(todo_qr_module(&Q, i, 0));
    }
    TEST_ASSERT_FALSE(todo_qr_module(&Q, 7, 0));
    TEST_ASSERT_TRUE(todo_qr_module(&Q, Q.size - 1, 0)); /* top-right finder */
    TEST_ASSERT_TRUE(todo_qr_module(&Q, 0, Q.size - 1));  /* bottom-left finder */
}

static void test_the_contract_url_fits_a_version_4_code_at_most(void)
{
    char t[100];
    int n;
    for (n = 1; n <= 61; n++) {
        text_of(t, n);
        TEST_ASSERT_TRUE(todo_qr_encode(&Q, t));
        TEST_ASSERT_TRUE_MESSAGE(Q.size <= 33, "version 4 is 33 modules"); /* version 4: 33 */
    }
    TEST_ASSERT_EQUAL_INT(164, (33 + 8) * 4);
}

static void test_text_that_does_not_fit_is_refused_and_leaves_nothing(void)
{
    char t[200];
    text_of(t, 150);
    TEST_ASSERT_TRUE(todo_qr_encode(&Q, "ok"));
    TEST_ASSERT_FALSE(todo_qr_encode(&Q, t));
    TEST_ASSERT_EQUAL_INT(0, Q.size);
    TEST_ASSERT_FALSE(todo_qr_module(&Q, 0, 0));
    TEST_ASSERT_FALSE(todo_qr_encode(&Q, NULL));
    TEST_ASSERT_FALSE(todo_qr_encode(NULL, "x"));
}

static void test_an_empty_string_is_a_valid_code_and_module_reads_are_bounded(void)
{
    TEST_ASSERT_TRUE(todo_qr_encode(&Q, ""));
    TEST_ASSERT_TRUE(Q.size >= 21);
    TEST_ASSERT_FALSE(todo_qr_module(&Q, -1, 0));
    TEST_ASSERT_FALSE(todo_qr_module(&Q, 0, Q.size));
    TEST_ASSERT_FALSE(todo_qr_module(NULL, 0, 0));
}

static void test_the_wifi_text_escapes_what_the_format_reserves(void)
{
    char out[96];
    TEST_ASSERT_TRUE(todo_qr_wifi_text("cyd-todo-4F2A", "k7Qm29xT", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:cyd-todo-4F2A;P:k7Qm29xT;;", out);
    TEST_ASSERT_TRUE(todo_qr_wifi_text("a;b,c:d\\e\"f", "p;q", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:a\\;b\\,c\\:d\\\\e\\\"f;P:p\\;q;;", out);
}

static void test_the_wifi_text_refuses_what_does_not_fit_and_bad_arguments(void)
{
    char out[16];
    TEST_ASSERT_FALSE(todo_qr_wifi_text("cyd-todo-4F2A", "k7Qm29xT", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    /* exactly fitting: the text is 39 bytes + NUL */
    {
        char exact[40], small[39];
        TEST_ASSERT_TRUE(todo_qr_wifi_text("cyd-todo-4F2A", "k7Qm29xT", exact, sizeof(exact)));
        TEST_ASSERT_FALSE(todo_qr_wifi_text("cyd-todo-4F2A", "k7Qm29xT", small, sizeof(small)));
    }
    TEST_ASSERT_FALSE(todo_qr_wifi_text(NULL, "x", out, sizeof(out)));
    TEST_ASSERT_FALSE(todo_qr_wifi_text("x", NULL, out, sizeof(out)));
    TEST_ASSERT_FALSE(todo_qr_wifi_text("x", "y", NULL, 10));
    TEST_ASSERT_FALSE(todo_qr_wifi_text("x", "y", out, 0));
    /* a code made of an escaped text still fits a QR (the worst realistic case: a long ssid with specials) */
    {
        char big[160];
        TEST_ASSERT_TRUE(todo_qr_wifi_text("My;Network:2.4,GHz", "pass;word", big, sizeof(big)));
        TEST_ASSERT_TRUE(todo_qr_encode(&Q, big));
    }
}

static uint32_t draw_frame(const char *a_or_null, todo_qr *q)
{
    cydui_ui_begin(&R.ui, 0);
    (void)a_or_null;
    todo_qr_draw(&R.ui, q, 10, 10);
    cydui_ui_end(&R.ui);
    return cydui_ui_errors(&R.ui);
}

static void test_drawing_is_two_commands_and_nothing_without_a_code(void)
{
    cydui_theme t;
    todo_theme_quest_init(&t, TODO_PALETTE_PARCHMENT);
    TEST_ASSERT_EQUAL_UINT32(0, rig_start(&R, &t));
    TEST_ASSERT_TRUE(todo_qr_encode(&Q, "http://192.168.4.1"));
    cydui_ui_begin(&R.ui, 0);
    todo_qr_draw(&R.ui, &Q, 10, 10);
    TEST_ASSERT_EQUAL_UINT32(2, cydui_cmd_count(&R.ui.ctx));
    cydui_ui_end(&R.ui);
    todo_qr_encode(&Q, NULL);
    cydui_ui_begin(&R.ui, 0);
    todo_qr_draw(&R.ui, &Q, 10, 10);
    TEST_ASSERT_EQUAL_UINT32(0, cydui_cmd_count(&R.ui.ctx));
    cydui_ui_end(&R.ui);
}

static void test_a_new_code_in_the_same_buffer_replaces_the_old_one_on_screen(void)
{
    static cydui_color first[RIG_W * RIG_H];
    cydui_theme t;
    uint32_t err;
    size_t i, diff = 0;

    todo_theme_quest_init(&t, TODO_PALETTE_PARCHMENT);
    TEST_ASSERT_EQUAL_UINT32(0, rig_start(&R, &t));
    TEST_ASSERT_TRUE(todo_qr_set(&Q, &R.ui, "http://192.168.1.50:8080/m/0123456789ABCDEFGHJKMNPQRS"));
    err = draw_frame(NULL, &Q);
    TEST_ASSERT_EQUAL_UINT32(0, err);
    memcpy(first, R.fb, sizeof(first));

    TEST_ASSERT_TRUE(todo_qr_set(&Q, &R.ui, "http://192.168.1.50:8080/m/ABCDEFGHJKMNPQRSTVWXYZ0123"));
    err = draw_frame(NULL, &Q);
    TEST_ASSERT_EQUAL_UINT32(0, err);
    for (i = 0; i < sizeof(first) / sizeof(first[0]); i++) {
        diff += first[i] != R.fb[i];
    }
    TEST_ASSERT_TRUE_MESSAGE(diff > 1000, "the second code must be on screen");

    /* and it is the same picture as a fresh screen that only ever saw the second code */
    {
        static rig fresh;
        static todo_qr q2;
        TEST_ASSERT_EQUAL_UINT32(0, rig_start(&fresh, &t));
        TEST_ASSERT_TRUE(todo_qr_encode(&q2, "http://192.168.1.50:8080/m/ABCDEFGHJKMNPQRSTVWXYZ0123"));
        cydui_ui_begin(&fresh.ui, 0);
        todo_qr_draw(&fresh.ui, &q2, 10, 10);
        cydui_ui_end(&fresh.ui);
        TEST_ASSERT_EQUAL_UINT64(cydui_mem_hash(&fresh.mem), cydui_mem_hash(&R.mem));
    }
}

static void test_without_the_announcement_the_old_picture_would_stay(void)
{
    /* finding 1 of docs/findings.md: the library hashes the pointer (and the size), so a code of the same size in the same buffer is not redrawn by encoding alone */
    cydui_theme t;
    uint64_t before, after;
    todo_theme_quest_init(&t, TODO_PALETTE_PARCHMENT);
    TEST_ASSERT_EQUAL_UINT32(0, rig_start(&R, &t));
    TEST_ASSERT_TRUE(todo_qr_encode(&Q, "http://192.168.1.50:8080/m/0123456789ABCDEFGHJKMNPQRS"));
    draw_frame(NULL, &Q);
    before = cydui_mem_hash(&R.mem);
    TEST_ASSERT_TRUE(todo_qr_encode(&Q, "http://192.168.1.50:8080/m/ABCDEFGHJKMNPQRSTVWXYZ0123")); /* no cydui_invalidate */
    draw_frame(NULL, &Q);
    after = cydui_mem_hash(&R.mem);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(before, after, "if this fails the library now sees the change: finding 1 can be closed");
}

static void test_a_code_of_another_size_is_redrawn_even_without_the_announcement(void)
{
    cydui_theme t;
    uint64_t before;
    todo_theme_quest_init(&t, TODO_PALETTE_PARCHMENT);
    TEST_ASSERT_EQUAL_UINT32(0, rig_start(&R, &t));
    TEST_ASSERT_TRUE(todo_qr_encode(&Q, "http://192.168.4.1"));
    draw_frame(NULL, &Q);
    before = cydui_mem_hash(&R.mem);
    TEST_ASSERT_TRUE(todo_qr_encode(&Q, "http://192.168.1.50:8080/m/0123456789ABCDEFGHJKMNPQRS"));
    draw_frame(NULL, &Q);
    TEST_ASSERT_NOT_EQUAL(before, cydui_mem_hash(&R.mem));
}

static void test_setting_the_same_text_again_does_not_force_a_redraw(void)
{
    cydui_theme t;
    todo_theme_quest_init(&t, TODO_PALETTE_PARCHMENT);
    TEST_ASSERT_EQUAL_UINT32(0, rig_start(&R, &t));
    TEST_ASSERT_TRUE(todo_qr_set(&Q, &R.ui, "http://192.168.4.1"));
    draw_frame(NULL, &Q);
    TEST_ASSERT_TRUE(todo_qr_set(&Q, &R.ui, "http://192.168.4.1"));
    cydui_ui_begin(&R.ui, 0);
    todo_qr_draw(&R.ui, &Q, 10, 10);
    TEST_ASSERT_FALSE_MESSAGE(cydui_ui_end(&R.ui), "nothing changed: nothing is sent to the panel");
    TEST_ASSERT_FALSE(todo_qr_set(NULL, &R.ui, "x"));
    TEST_ASSERT_FALSE(todo_qr_set(&Q, NULL, "x"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_module_of_the_mask_is_the_encoders_module);
    RUN_TEST(test_the_quiet_zone_is_empty_on_every_side);
    RUN_TEST(test_the_finder_pattern_is_where_the_standard_puts_it);
    RUN_TEST(test_the_contract_url_fits_a_version_4_code_at_most);
    RUN_TEST(test_text_that_does_not_fit_is_refused_and_leaves_nothing);
    RUN_TEST(test_an_empty_string_is_a_valid_code_and_module_reads_are_bounded);
    RUN_TEST(test_the_wifi_text_escapes_what_the_format_reserves);
    RUN_TEST(test_the_wifi_text_refuses_what_does_not_fit_and_bad_arguments);
    RUN_TEST(test_drawing_is_two_commands_and_nothing_without_a_code);
    RUN_TEST(test_a_new_code_in_the_same_buffer_replaces_the_old_one_on_screen);
    RUN_TEST(test_without_the_announcement_the_old_picture_would_stay);
    RUN_TEST(test_a_code_of_another_size_is_redrawn_even_without_the_announcement);
    RUN_TEST(test_setting_the_same_text_again_does_not_force_a_redraw);
    return UNITY_END();
}
