/* Every named scene, pinned: both palettes with the demo data, and the worst case the limits allow for the scenes that
 * stress the layout. The same numbers are what the board must show (A4). The QR scenes also leave the text they encode
 * next to the picture, so that decode_qr.sh can read the real screens with a decoder. */

#include <stdio.h>
#include <string.h>

#include "cydui_mem.h"
#include "rig.h"
#include "scenes.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static cydui_color fb[320 * 240];
static todo_memrepo mem_repo;
static todo_app app;
static cydui_ui ui;
static cydui_theme theme;
static cydui_mem mem;

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

static void pin(const char *scene, todo_palette palette, bool worst)
{
    char name[96];
    uint32_t t = 0, errors;
    int i;

    cydui_mem_init(&mem, fb, 320, 240);
    todo_memrepo_init(&mem_repo, SCENE_NOW);
    scene_data(&mem_repo, worst);
    todo_app_init(&app, &mem_repo.repo, SCENE_OFFSET);
    app.palette = palette;
    todo_theme_quest_init(&theme, palette);
    TEST_ASSERT_EQUAL_UINT32(0, board_ui_init(&ui, &mem.backend, &theme));
    for (i = 0; i < 32; i++, t += 16, mem.now += 16) { /* the same 8 + 24 frames as todo_pc --scene */
        if (i == 8) {
            TEST_ASSERT_TRUE_MESSAGE(scene_apply(&app, scene), scene);
        }
        todo_app_set_time(&app, SCENE_NOW + t / 1000, true);
        cydui_ui_begin(&ui, 0);
        todo_app_frame(&app, &ui);
        cydui_ui_end(&ui);
    }
    errors = cydui_ui_errors(&ui);
    snprintf(name, sizeof(name), "scene_%s_%s%s", scene, palette == TODO_PALETTE_PARCHMENT ? "day" : "night", worst ? "_worst" : "");
    golden_check_mem(name, &mem, errors);
    if ((app.overlay == TODO_OV_QR || app.overlay == TODO_OV_SETUP) && app.qr_text[0]) {
        sidecar(name, app.qr_text);
    }
}

static void test_every_scene_with_the_demo_data_in_both_palettes(void)
{
    int s;
    for (s = 0; s < scene_count(); s++) {
        pin(scene_name(s), TODO_PALETTE_PARCHMENT, false);
        pin(scene_name(s), TODO_PALETTE_CANDLELIGHT, false);
    }
}

static void test_the_scenes_that_stress_the_layout_with_the_worst_case_data(void)
{
    static const char *const stress[] = {"quests", "agenda", "detail-quest", "calendar-six-rows", "setup"};
    size_t i;
    for (i = 0; i < sizeof(stress) / sizeof(stress[0]); i++) {
        pin(stress[i], TODO_PALETTE_PARCHMENT, true);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_scene_with_the_demo_data_in_both_palettes);
    RUN_TEST(test_the_scenes_that_stress_the_layout_with_the_worst_case_data);
    return UNITY_END();
}
