/* The QR codes as the screens will show them, on the 320x240 rig. Each image is golden-checked and a sidecar
 * <name>.txt with the text that was encoded is written next to the PNG: decode_qr.sh reads the PNG with a real
 * decoder and compares. */

#include <stdio.h>
#include <string.h>

#include "rig.h"
#include "todo/qr.h"
#include "todo/theme_quest.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static rig R;
static todo_qr Q;
static const char *const PAL[TODO_PALETTE_COUNT] = {"parchment", "candlelight"};

static void line(const cydui_font *f, int y, const char *s, cydui_color c)
{
    cydui_text_extent e = cydui_text_size(f, s, CYDUI_NTS, 0);
    cydui_draw_text(&R.ui.ctx, f, (int16_t)((RIG_W - e.w) / 2), (int16_t)y, 0, CYDUI_ALIGN_LEFT, s, CYDUI_NTS, c, 255);
}

static void sidecar(const char *name, const char *text)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s.txt", GOLDEN_OUT_DIR, name);
    f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(text, f);
    fclose(f);
}

static void sheet(const char *kind, todo_palette p, const char *text, const char *l1, const char *l2)
{
    cydui_theme t;
    const todo_colors *k = todo_colors_of(p);
    char name[64];

    todo_theme_quest_init(&t, p);
    TEST_ASSERT_EQUAL_UINT32(0, rig_start(&R, &t));
    TEST_ASSERT_TRUE_MESSAGE(todo_qr_set(&Q, &R.ui, text), text);
    cydui_ui_begin(&R.ui, t.style.bg);
    todo_qr_draw(&R.ui, &Q, (RIG_W - Q.px) / 2, 4);
    line(&todo_serif_bold_16, 4 + Q.px + 4, l1, k->text);
    line(&todo_serif_12, 4 + Q.px + 26, l2, k->muted);
    TEST_ASSERT_TRUE(cydui_cmd_count(&R.ui.ctx) <= 10);
    cydui_ui_end(&R.ui);
    snprintf(name, sizeof(name), "quest_qr_%s_%s", kind, PAL[p]);
    golden_check(name, &R);
    sidecar(name, text);
}

#define LINK "http://192.168.1.50:8080/m/K3H9Q7ZB2VN4X8D5WMT6RYE1GA"

static void test_link_code(void)
{
    int p;
    TEST_ASSERT_EQUAL_UINT(53, strlen(LINK));
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        sheet("link", (todo_palette)p, LINK, "Scan to write it on your phone", "The link works once, for 10 minutes");
    }
}

static void test_link_code_of_the_longest_allowed_length(void)
{
    /* 61 bytes: a PUBLIC_URL of 32 bytes + "/m/" + 26 characters */
    static const char *const longest = "http://board.home.example.lan:80/m/K3H9Q7ZB2VN4X8D5WMT6RYE1GA";
    int p;
    TEST_ASSERT_EQUAL_UINT(61, strlen(longest));
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        sheet("link61", (todo_palette)p, longest, "Scan to write it on your phone", "The link works once, for 10 minutes");
    }
}

static void test_wifi_code_with_characters_that_need_escaping(void)
{
    char text[96];
    int p;
    TEST_ASSERT_TRUE(todo_qr_wifi_text("cyd-todo-4F2A", "k7Qm;29x", text, sizeof(text)));
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        sheet("wifi", (todo_palette)p, text, "1. Scan to join the board's Wi-Fi", "2. Your phone opens the setup form");
    }
}

static void test_address_code(void)
{
    int p;
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        sheet("addr", (todo_palette)p, "http://192.168.4.1", "Or open this address by hand", "http://192.168.4.1");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_link_code);
    RUN_TEST(test_link_code_of_the_longest_allowed_length);
    RUN_TEST(test_wifi_code_with_characters_that_need_escaping);
    RUN_TEST(test_address_code);
    return UNITY_END();
}
