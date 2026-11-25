#ifndef TODO_SCENES_H
#define TODO_SCENES_H

/*
 * Named states of the application, for the PC program and for the tests that guard the screens' budget: the demo data
 * or the worst case the limits allow, a made-up clock, and the UI built with the memory the board will have.
 */

#include <stdbool.h>
#include <stdint.h>

#include "cydui/ui.h"
#include "todo/app.h"
#include "todo/memrepo.h"

#define SCENE_NOW 1796230800LL /* 2026-12-02 17:00 UTC, 14:00 in UTC-3 */
#define SCENE_OFFSET (-180)

/* A UI over the board's budget: 320 commands, 1.5 KB of text, 64 state entries, 20-row strips. One at a time. */
uint32_t board_ui_init(cydui_ui *ui, const cydui_backend *backend, const cydui_theme *theme);

/* Fills the in-memory server: the demo data, or the worst case (60 quests and events with the longest texts). */
void scene_data(todo_memrepo *m, bool worst);

int scene_count(void);
const char *scene_name(int i);
/* Puts the application in the state of the scene (after it has loaded). False for an unknown name. */
bool scene_apply(todo_app *a, const char *name);

#endif /* TODO_SCENES_H */
