#include "world.h"

#include <string.h>

#include "unity.h"

void world_start(world *w, todo_palette p, bool demo)
{
    cydui_theme t;

    todo_theme_quest_init(&t, p);
    memset(w, 0, sizeof(*w));
    TEST_ASSERT_EQUAL_UINT32(0, rig_start(&w->r, &t));
    todo_memrepo_init(&w->mem, WORLD_NOW);
    if (demo) {
        todo_memrepo_seed_demo(&w->mem);
    }
    todo_app_init(&w->app, &w->mem.repo, WORLD_OFFSET);
    w->app.palette = p;
    todo_app_set_time(&w->app, WORLD_NOW, true);
}

void world_frame(world *w)
{
    rig_advance(&w->r, 16);
    todo_app_set_time(&w->app, WORLD_NOW + w->skew + (int64_t)w->r.mem.now / 1000, w->app.time_valid);
    cydui_ui_clear_errors(&w->r.ui);
    cydui_ui_begin(&w->r.ui, 0);
    todo_app_frame(&w->app, &w->r.ui);
    w->cmds = cydui_cmd_count(&w->r.ui.ctx);
    cydui_ui_end(&w->r.ui);
    w->errors = cydui_ui_errors(&w->r.ui) & ~(uint32_t)CYDUI_ERR_TEXT_TRUNCATED;
}

void world_frames(world *w, int n)
{
    while (n-- > 0) {
        world_frame(w);
    }
}

void world_tap(world *w, int x, int y)
{
    rig_down(&w->r, x, y);
    world_frame(w);
    rig_up(&w->r, x, y);
    world_frame(w);
}

void world_snap(const world *w, const char *name)
{
    rig_snapshot(&w->r, name);
}
