/* The budget guard: every scene, in both palettes, with the demo data and with the worst case the limits allow, in the memory
 * the board will have (320 commands, 1.5 KB of text, 64 state entries). A screen that does not fit fails here, not on the board. */

#include <stdio.h>
#include <string.h>

#include "cydui_mem.h"
#include "board_ui.h"
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

typedef struct {
    uint32_t cmds;   /* commands of the busiest frame in the last ten */
    uint32_t text;   /* bytes of the text memory of that frame, at most */
    uint32_t state;  /* state entries in use at the end */
    uint32_t errors; /* flags raised over the scene, without the informative one */
} measure;

static measure run(const char *scene, todo_palette palette, bool worst)
{
    measure m = {0, 0, 0, 0};
    uint32_t t = 0;
    int i;

    cydui_mem_init(&mem, fb, 320, 240);
    todo_memrepo_init(&mem_repo, SCENE_NOW);
    scene_data(&mem_repo, worst);
    todo_app_init(&app, &mem_repo.repo, SCENE_OFFSET);
    app.palette = palette;
    todo_theme_quest_init(&theme, palette);
    TEST_ASSERT_EQUAL_UINT32(0, board_ui_init(&ui, &mem.backend, &theme));
    for (i = 0; i < 40; i++, t += 16, mem.now += 16) {
        if (i == 8) {
            TEST_ASSERT_TRUE_MESSAGE(scene_apply(&app, scene), scene);
        }
        todo_app_set_time(&app, SCENE_NOW + t / 1000, true);
        cydui_ui_begin(&ui, 0);
        todo_app_frame(&app, &ui);
        cydui_ui_end(&ui);
        if (i == 29) {
            cydui_ui_stats_reset(&ui); /* the peaks of the last ten frames, when the screen has settled */
        }
    }
    {
        cydui_ui_stats st;
        cydui_ui_stats_get(&ui, &st);
        m.cmds = st.cmds_peak;
        m.text = st.text_peak;
        m.state = st.state_used;
    }
    m.errors = cydui_ui_errors(&ui) & ~(uint32_t)CYDUI_ERR_TEXT_TRUNCATED;
    return m;
}

static void test_every_scene_fits_the_board_with_demo_data_and_with_the_worst_case(void)
{
    uint32_t most = 0, most_text = 0, most_state = 0;
    const char *most_scene = "";
    int s, p, w;

    for (s = 0; s < scene_count(); s++) {
        for (p = 0; p < TODO_PALETTE_COUNT; p++) {
            for (w = 0; w < 2; w++) {
                char msg[96];
                measure m = run(scene_name(s), (todo_palette)p, w == 1);
                snprintf(msg, sizeof(msg), "%s, palette %d, %s: %u commands, %u text bytes, %u state entries, errors 0x%x", scene_name(s), p,
                         w ? "worst case" : "demo", (unsigned)m.cmds, (unsigned)m.text, (unsigned)m.state, (unsigned)m.errors);
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, m.errors, msg); /* no flag, and so no full state table either */
                TEST_ASSERT_TRUE_MESSAGE(m.cmds > 0 && m.cmds <= 320, msg);
                TEST_ASSERT_TRUE_MESSAGE(m.text > 0 && m.text <= 1536, msg);
                TEST_ASSERT_TRUE_MESSAGE(m.state <= 64, msg);
                if (m.text > most_text) {
                    most_text = m.text;
                }
                if (m.state > most_state) {
                    most_state = m.state;
                }
                if (m.cmds > most) {
                    most = m.cmds;
                    most_scene = scene_name(s);
                }
            }
        }
    }
    printf("budget: the busiest frame is %u of 320 commands (%s), %u of 1536 text bytes, %u of 64 state entries\n", (unsigned)most, most_scene,
           (unsigned)most_text, (unsigned)most_state);
}

static void test_the_worst_case_data_really_fills_the_caches(void)
{
    run("quests", TODO_PALETTE_PARCHMENT, true);
    TEST_ASSERT_EQUAL_INT(TODO_MAX_QUESTS, app.store.n_quests);
    TEST_ASSERT_TRUE(app.store.quests_truncated);
    run("agenda", TODO_PALETTE_PARCHMENT, true);
    TEST_ASSERT_EQUAL_INT(TODO_MAX_EVENTS, app.store.n_events);
    TEST_ASSERT_TRUE(app.store.events_truncated);
    run("calendar-six-rows", TODO_PALETTE_PARCHMENT, true);
    TEST_ASSERT_EQUAL_INT(8, app.cal_month);
    TEST_ASSERT_TRUE_MESSAGE(app.store.n_events >= 31, "August's events were asked for");
}

static void test_the_scenes_are_deterministic(void)
{
    uint64_t a, b;
    run("agenda", TODO_PALETTE_CANDLELIGHT, true);
    a = cydui_mem_hash(&mem);
    run("agenda", TODO_PALETTE_CANDLELIGHT, true);
    b = cydui_mem_hash(&mem);
    TEST_ASSERT_EQUAL_UINT64(a, b);
}

static void test_an_unknown_scene_is_refused(void)
{
    run("quests", TODO_PALETTE_PARCHMENT, false);
    TEST_ASSERT_FALSE(scene_apply(&app, "nothing"));
    TEST_ASSERT_NULL(scene_name(-1));
    TEST_ASSERT_NULL(scene_name(scene_count()));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_scene_fits_the_board_with_demo_data_and_with_the_worst_case);
    RUN_TEST(test_the_worst_case_data_really_fills_the_caches);
    RUN_TEST(test_the_scenes_are_deterministic);
    RUN_TEST(test_an_unknown_scene_is_refused);
    return UNITY_END();
}
