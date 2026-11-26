/* The quest theme: the sheets that are looked at, and the properties that need no eyes. */

#include <stdio.h>
#include <string.h>

#include "rig.h"
#include "todo/theme_quest.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static rig R;
static const char *const PAL_NAME[TODO_PALETTE_COUNT] = {"parchment", "candlelight"};

static void frame_begin(todo_palette p, cydui_theme *t)
{
    todo_theme_quest_init(t, p);
    TEST_ASSERT_EQUAL_UINT32(0, rig_start(&R, t));
    cydui_ui_begin(&R.ui, t->style.bg);
}

static void text(const cydui_font *f, int x, int y, const char *s, cydui_color c)
{
    cydui_draw_text(&R.ui.ctx, f, (int16_t)x, (int16_t)y, 0, CYDUI_ALIGN_LEFT, s, CYDUI_NTS, c, 255);
}

static void checkname(const char *base, todo_palette p)
{
    char name[64];
    snprintf(name, sizeof(name), "quest_%s_%s", base, PAL_NAME[p]);
    cydui_ui_end(&R.ui);
    golden_check(name, &R);
}

/* ---- sheets ---- */

/* Every widget kind in its states: normal, pressed, disabled, each unselected and selected. */
static void widgets_sheet(todo_palette p)
{
    static const cydui_widget_kind kinds[] = {CYDUI_WIDGET_BUTTON, CYDUI_WIDGET_TAB, CYDUI_WIDGET_CHECKBOX, CYDUI_WIDGET_PANEL};
    static const char *const names[] = {"Button", "Tab", "Check", "Panel"};
    static const cydui_widget_state states[3] = {CYDUI_STATE_NORMAL, CYDUI_STATE_PRESSED, CYDUI_STATE_DISABLED};
    static const char *const sn[3] = {"normal", "pressed", "disabled"};
    const todo_colors *k = todo_colors_of(p);
    cydui_theme t;
    int i, j, sel;

    frame_begin(p, &t);
    for (i = 0; i < 4; i++) {
        text(&todo_serif_12, 4, 4 + i * 58, names[i], k->muted);
        for (j = 0; j < 3; j++) {
            for (sel = 0; sel < 2; sel++) {
                cydui_paint_info pi;
                char label[16];
                memset(&pi, 0, sizeof(pi));
                pi.kind = kinds[i];
                pi.state = states[j];
                pi.selected = sel == 1;
                pi.rect = (cydui_rect){(int16_t)(4 + j * 106), (int16_t)(18 + i * 58 + sel * 20), 100, 18};
                if (kinds[i] == CYDUI_WIDGET_CHECKBOX) {
                    pi.rect.w = 18;
                }
                snprintf(label, sizeof(label), "%s", sn[j]);
                pi.text = label;
                pi.text_len = strlen(label);
                pi.text_extent = cydui_text_size(&todo_serif_16, label, pi.text_len, 0);
                cydui_paint(&R.ui, &pi);
            }
        }
    }
    checkname("widgets", p);
}

static void test_widgets_sheet_parchment(void) { widgets_sheet(TODO_PALETTE_PARCHMENT); }
static void test_widgets_sheet_candlelight(void) { widgets_sheet(TODO_PALETTE_CANDLELIGHT); }

/* The four sizes of the serif, with what the app will write in them. */
static void type_sheet(todo_palette p)
{
    const todo_colors *k = todo_colors_of(p);
    cydui_theme t;

    frame_begin(p, &t);
    text(&todo_serif_bold_24, 6, 4, "Quest Log", k->text);
    text(&todo_serif_bold_16, 6, 36, "Slay the inbox  (bold 16)", k->text);
    text(&todo_serif_16, 6, 58, "Visit the dentist  (regular 16)", k->text);
    text(&todo_serif_16, 6, 78, "Ação, café, niño, über, ÀÉÎÕÜ 0123456789", k->text);
    text(&todo_serif_12, 6, 100, "Due Dec 9 \xE2\x80\xA2 12:04 Synced \xE2\x80\xA2 10:00-10:45 Dentist (regular 12)", k->muted);
    text(&todo_serif_12, 6, 116, "The quick brown fox jumps over the lazy dog. 0123456789", k->text);
    text(&todo_serif_12, 6, 132, "Waiting for time... Offline  Settings  Wed 9  Lv 12  170/200 XP", k->muted);
    text(&todo_serif_16, 6, 154, "Mon Tue Wed Thu Fri Sat Sun   1 2 3 4 5 6 7 8 9", k->text);
    todo_draw_text_ellipsis(&R.ui.ctx, &todo_serif_16, 6, 176, 150, "A very long quest title that does not fit at all", k->text);
    todo_draw_text_ellipsis(&R.ui.ctx, &todo_serif_16, 6, 196, 150, "Short one", k->text);
    todo_draw_text_ellipsis(&R.ui.ctx, &todo_serif_12, 170, 176, 140, "Another long title that gets cut here", k->muted);
    checkname("type", p);
}

static void test_type_sheet_parchment(void) { type_sheet(TODO_PALETTE_PARCHMENT); }
static void test_type_sheet_candlelight(void) { type_sheet(TODO_PALETTE_CANDLELIGHT); }

/* The pieces the screens are made of. */
static void parts_sheet(todo_palette p)
{
    const todo_colors *k = todo_colors_of(p);
    cydui_theme t;
    cydui_paint_info pi;
    static const int fr[4] = {0, 1024, 2048, 4096};
    int i;

    frame_begin(p, &t);
    /* tabs */
    for (i = 0; i < 3; i++) {
        static const char *const n[3] = {"Quests", "Calendar", "Settings"};
        memset(&pi, 0, sizeof(pi));
        pi.kind = CYDUI_WIDGET_TAB;
        pi.selected = i == 0;
        pi.rect = (cydui_rect){(int16_t)(i * 106), 0, 108, 28};
        pi.text = n[i];
        pi.text_len = strlen(n[i]);
        pi.text_extent = cydui_text_size(&todo_serif_16, n[i], pi.text_len, 0);
        cydui_paint(&R.ui, &pi);
    }
    /* cards: open and done, with a checkbox, a title, stars and a date */
    for (i = 0; i < 2; i++) {
        cydui_rect c = {4, (int16_t)(34 + i * 36), 312, 32};
        todo_paint_card(&R.ui.ctx, p, c, i == 1);
        text(&todo_serif_16, 40, c.y + 7, i ? "Pay the guild fees" : "Slay the inbox", i ? k->muted : k->text);
        todo_draw_stars(&R.ui.ctx, 200, c.y + 10, i ? 1 : 3, 5, i ? k->muted : k->gold_dark, k->star_off);
        text(&todo_serif_12, 272, c.y + 9, i ? "done" : "Dec 4", k->muted);
    }
    /* XP bars */
    for (i = 0; i < 4; i++) {
        todo_paint_xp_bar(&R.ui.ctx, p, (cydui_rect){4, (int16_t)(110 + i * 16), 200, 12}, fr[i]);
    }
    text(&todo_serif_bold_16, 212, 108, "Lv 2", k->text);
    text(&todo_serif_12, 212, 128, "170 / 200 XP", k->muted);
    /* buttons: gold normal and pressed, wax normal and pressed */
    for (i = 0; i < 2; i++) {
        memset(&pi, 0, sizeof(pi));
        pi.kind = CYDUI_WIDGET_BUTTON;
        pi.state = i ? CYDUI_STATE_PRESSED : CYDUI_STATE_NORMAL;
        pi.text = "New quest";
        pi.text_len = 9;
        pi.text_extent = cydui_text_size(&todo_serif_16, "New quest", 9, 0);
        pi.rect = (cydui_rect){(int16_t)(4 + i * 106), 180, 100, 32};
        cydui_paint(&R.ui, &pi);
        todo_paint_wax_button(&R.ui.ctx, p, (cydui_rect){(int16_t)(4 + i * 106), 216, 100, 24}, "Delete", i == 1);
    }
    /* a popup page and a scroll bar */
    memset(&pi, 0, sizeof(pi));
    pi.kind = CYDUI_WIDGET_POPUP;
    pi.rect = (cydui_rect){216, 150, 100, 88};
    cydui_paint(&R.ui, &pi);
    text(&todo_serif_12, 224, 160, "Notes go", k->text);
    text(&todo_serif_12, 224, 174, "on a page", k->text);
    memset(&pi, 0, sizeof(pi));
    pi.kind = CYDUI_WIDGET_SCROLLBAR;
    pi.value = 30;
    pi.max = 100;
    pi.aux = (cydui_rect){0, 0, 100, 100};
    pi.rect = (cydui_rect){306, 150, 8, 88};
    cydui_paint(&R.ui, &pi);
    checkname("parts", p);
}

static void test_parts_sheet_parchment(void) { parts_sheet(TODO_PALETTE_PARCHMENT); }
static void test_parts_sheet_candlelight(void) { parts_sheet(TODO_PALETTE_CANDLELIGHT); }

/* ---- properties ---- */

static int chan(cydui_color c, int which)
{
    int v = which == 0 ? (c >> 11) & 31 : (which == 1 ? (c >> 5) & 63 : c & 31);
    return which == 1 ? (v << 2) | (v >> 4) : (v << 3) | (v >> 2);
}

/* Relative luminance on a 0..65025 scale with gamma 2 (close enough to sRGB for a margin check). */
static int64_t lum(cydui_color c)
{
    int64_t r = chan(c, 0), g = chan(c, 1), b = chan(c, 2);
    return (2126 * r * r + 7152 * g * g + 722 * b * b) / 10000;
}

/* True if the contrast ratio of a and b is at least num/10. */
static bool contrast_at_least(cydui_color a, cydui_color b, int num)
{
    int64_t la = lum(a) + 3251, lb = lum(b) + 3251, hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return hi * 10 >= (int64_t)num * lo;
}

static void test_text_is_readable_on_what_it_is_drawn_on_in_both_palettes(void)
{
    int p;
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        const todo_colors *k = todo_colors_of((todo_palette)p);
        char msg[64];
        snprintf(msg, sizeof(msg), "palette %d", p);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->text, k->bg, 70), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->text, k->page, 70), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->text, k->card, 70), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->muted, k->bg, 45), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->muted, k->card, 45), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->muted, k->card_done, 40), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->on_gold, k->gold, 45), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->on_wood, k->wood, 45), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->on_wax, k->wax, 45), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->alert, k->bg, 45), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->alert, k->card, 45), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->alert, k->card_done, 45), msg);
        TEST_ASSERT_TRUE_MESSAGE(contrast_at_least(k->gold, k->card, 20), msg); /* the stars and the bar edge against the card */
    }
}

static void test_the_contrast_check_can_fail(void)
{
    TEST_ASSERT_FALSE(contrast_at_least(CYDUI_RGB(120, 120, 120), CYDUI_RGB(130, 130, 130), 45));
    TEST_ASSERT_TRUE(contrast_at_least(CYDUI_RGB(0, 0, 0), CYDUI_RGB(255, 255, 255), 100));
}

static void test_both_themes_are_valid_and_use_the_serif(void)
{
    int p;
    for (p = 0; p < TODO_PALETTE_COUNT; p++) {
        cydui_theme t;
        todo_theme_quest_init(&t, (todo_palette)p);
        TEST_ASSERT_EQUAL_INT(CYDUI_THEME_OK, cydui_theme_validate(&t));
        TEST_ASSERT_EQUAL_PTR(&todo_serif_16, t.style.font);
        TEST_ASSERT_TRUE(t.style.min_target >= 32);
        TEST_ASSERT_EQUAL_INT(0, cydui_font_validate(&todo_serif_12));
        TEST_ASSERT_EQUAL_INT(0, cydui_font_validate(&todo_serif_bold_16));
        TEST_ASSERT_EQUAL_INT(0, cydui_font_validate(&todo_serif_bold_24));
    }
}

static void test_the_theme_is_the_same_every_time_it_is_made(void)
{
    cydui_theme a, b;
    todo_theme_quest_init(&a, TODO_PALETTE_PARCHMENT);
    todo_theme_quest_init(&b, TODO_PALETTE_PARCHMENT);
    TEST_ASSERT_EQUAL_MEMORY(&a.table, &b.table, sizeof(a.table));
    todo_theme_quest_init(&b, TODO_PALETTE_CANDLELIGHT);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(&a.table, &b.table, sizeof(a.table)));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a.name, b.name));
}

static void test_the_fonts_have_every_character_the_app_writes(void)
{
    /* the accents of Latin-1, the ellipsis and the bullet, in all four fonts */
    static const uint32_t cps[] = {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xE3, 0xF5, 0xE7, 0xF1, 0xFC, 0xC0, 0xC9, 0xD5, 0x2026, 0x2022, '0', '9', 'A', 'z', '/', ':', '-', ' '};
    const cydui_font *fonts[] = {&todo_serif_12, &todo_serif_16, &todo_serif_bold_16, &todo_serif_bold_24};
    size_t i, j;
    for (i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        for (j = 0; j < sizeof(cps) / sizeof(cps[0]); j++) {
            const cydui_glyph *g = cydui_font_lookup(fonts[i], cps[j]);
            TEST_ASSERT_NOT_NULL(g);
            TEST_ASSERT_TRUE_MESSAGE(g != cydui_font_fallback(fonts[i]), "a character of the app is missing");
        }
    }
}

static void test_the_star_is_a_filled_shape_with_soft_edges(void)
{
    int nonzero = 0, full = 0;
    size_t i;
    TEST_ASSERT_EQUAL_UINT(72, sizeof(todo_star_mask));
    for (i = 0; i < sizeof(todo_star_mask); i++) {
        int hi = todo_star_mask[i] >> 4, lo = todo_star_mask[i] & 15;
        nonzero += (hi != 0) + (lo != 0);
        full += (hi == 15) + (lo == 15);
    }
    TEST_ASSERT_TRUE(nonzero > 50 && nonzero < 110);
    TEST_ASSERT_TRUE(full > 10);
    TEST_ASSERT_TRUE_MESSAGE(todo_star_mask[5 * 6 + 2] >> 4 == 15 || (todo_star_mask[5 * 6 + 2] & 15) == 15, "the middle is solid");
    TEST_ASSERT_EQUAL_HEX8(0, todo_star_mask[0]); /* the corner is empty */
}

static void test_ellipsis_fits_and_never_cuts_a_character(void)
{
    const cydui_font *f = &todo_serif_16;
    const char *titles[] = {"A very long quest title that does not fit", "Ação e emoção \xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9",
                            "\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC", "short", ""};
    size_t t;
    int w, dots = cydui_text_size(f, "\xE2\x80\xA6", 3, 0).w;
    for (t = 0; t < sizeof(titles) / sizeof(titles[0]); t++) {
        for (w = 0; w < 200; w += 3) {
            bool cut;
            char buf[300];
            size_t n = cydui_text_ellipsize(f, titles[t], CYDUI_NTS, (int16_t)w, buf, sizeof(buf), &cut);
            TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
            if (w >= dots || !cut) {
                TEST_ASSERT_TRUE_MESSAGE(cydui_text_size(f, buf, n, 0).w <= w || n == 0, titles[t]);
            }
            if (cut) { /* what is kept is a start of the title, cut between characters, and then the ellipsis */
                TEST_ASSERT_TRUE(n >= 3 && memcmp(buf + n - 3, "\xE2\x80\xA6", 3) == 0);
                TEST_ASSERT_EQUAL_INT(0, strncmp(buf, titles[t], n - 3));
                TEST_ASSERT_TRUE(((unsigned char)titles[t][n - 3] & 0xC0) != 0x80);
            }
        }
    }
}

static void test_a_title_that_fits_is_drawn_whole_and_one_that_does_not_ends_in_an_ellipsis(void)
{
    bool cut;
    char buf[64];
    const cydui_font *f = &todo_serif_16;
    int full = cydui_text_size(f, "Slay the inbox", CYDUI_NTS, 0).w;
    TEST_ASSERT_EQUAL_UINT(14, cydui_text_ellipsize(f, "Slay the inbox", CYDUI_NTS, (int16_t)full, buf, sizeof(buf), &cut));
    TEST_ASSERT_FALSE(cut);
    TEST_ASSERT_EQUAL_STRING("Slay the inbox", buf);
    TEST_ASSERT_TRUE(cydui_text_ellipsize(f, "Slay the inbox", CYDUI_NTS, (int16_t)(full - 1), buf, sizeof(buf), &cut) < 14 + 3);
    TEST_ASSERT_TRUE(cut);
}

static void test_each_part_costs_few_commands(void)
{
    cydui_theme t;
    uint32_t before;
    frame_begin(TODO_PALETTE_PARCHMENT, &t);
    before = cydui_cmd_count(&R.ui.ctx);
    todo_paint_card(&R.ui.ctx, TODO_PALETTE_PARCHMENT, (cydui_rect){0, 0, 300, 32}, false);
    TEST_ASSERT_TRUE(cydui_cmd_count(&R.ui.ctx) - before <= 3);
    before = cydui_cmd_count(&R.ui.ctx);
    todo_paint_xp_bar(&R.ui.ctx, TODO_PALETTE_PARCHMENT, (cydui_rect){0, 40, 200, 12}, 2048);
    TEST_ASSERT_TRUE(cydui_cmd_count(&R.ui.ctx) - before <= 3);
    before = cydui_cmd_count(&R.ui.ctx);
    todo_draw_stars(&R.ui.ctx, 0, 60, 3, 5, 1, 2);
    TEST_ASSERT_EQUAL_UINT32(5, cydui_cmd_count(&R.ui.ctx) - before);
    before = cydui_cmd_count(&R.ui.ctx);
    todo_paint_wax_button(&R.ui.ctx, TODO_PALETTE_PARCHMENT, (cydui_rect){0, 80, 100, 24}, "Delete", false);
    TEST_ASSERT_TRUE(cydui_cmd_count(&R.ui.ctx) - before <= 4);
}

static void test_the_xp_bar_width_is_exact_and_clamped(void)
{
    TEST_ASSERT_EQUAL_INT(196, todo_xp_bar_width(196, 4096));
    TEST_ASSERT_EQUAL_INT(98, todo_xp_bar_width(196, 2048));
    TEST_ASSERT_EQUAL_INT(0, todo_xp_bar_width(196, 0));
    TEST_ASSERT_EQUAL_INT(0, todo_xp_bar_width(196, 1));
    TEST_ASSERT_EQUAL_INT(196, todo_xp_bar_width(196, 9000));
    TEST_ASSERT_EQUAL_INT(0, todo_xp_bar_width(196, -5));
    TEST_ASSERT_EQUAL_INT(4095, todo_xp_bar_width(4095, 4096));
    TEST_ASSERT_EQUAL_INT(0, todo_xp_bar_width(-3, 4096));
}

static void test_a_very_long_text_is_cut_to_the_contract_even_when_the_width_is_huge(void)
{
    char big[281], buf[260];
    size_t n;
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    n = cydui_text_ellipsize(&todo_serif_12, big, CYDUI_NTS, 30000, buf, sizeof(buf), NULL);
    TEST_ASSERT_TRUE(n <= 256);
}

static void test_luminance_weights_blue_is_dark_but_not_black(void)
{
    TEST_ASSERT_TRUE(contrast_at_least(CYDUI_RGB(0, 0, 255), CYDUI_RGB(0, 0, 0), 20));
    TEST_ASSERT_FALSE(contrast_at_least(CYDUI_RGB(0, 0, 255), CYDUI_RGB(0, 0, 0), 30));
    TEST_ASSERT_TRUE(contrast_at_least(CYDUI_RGB(0, 255, 0), CYDUI_RGB(0, 0, 0), 100));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_text_is_readable_on_what_it_is_drawn_on_in_both_palettes);
    RUN_TEST(test_the_contrast_check_can_fail);
    RUN_TEST(test_both_themes_are_valid_and_use_the_serif);
    RUN_TEST(test_the_theme_is_the_same_every_time_it_is_made);
    RUN_TEST(test_the_fonts_have_every_character_the_app_writes);
    RUN_TEST(test_the_star_is_a_filled_shape_with_soft_edges);
    RUN_TEST(test_ellipsis_fits_and_never_cuts_a_character);
    RUN_TEST(test_a_title_that_fits_is_drawn_whole_and_one_that_does_not_ends_in_an_ellipsis);
    RUN_TEST(test_each_part_costs_few_commands);
    RUN_TEST(test_the_xp_bar_width_is_exact_and_clamped);
    RUN_TEST(test_a_very_long_text_is_cut_to_the_contract_even_when_the_width_is_huge);
    RUN_TEST(test_luminance_weights_blue_is_dark_but_not_black);
    RUN_TEST(test_widgets_sheet_parchment);
    RUN_TEST(test_widgets_sheet_candlelight);
    RUN_TEST(test_type_sheet_parchment);
    RUN_TEST(test_type_sheet_candlelight);
    RUN_TEST(test_parts_sheet_parchment);
    RUN_TEST(test_parts_sheet_candlelight);
    return UNITY_END();
}
