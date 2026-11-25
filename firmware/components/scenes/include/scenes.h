#ifndef TODO_SCENES_H
#define TODO_SCENES_H

/*
 * Named states of the application, for the PC program and for the tests that guard the screens' budget: the demo data
 * or the worst case the limits allow, a made-up clock, and the UI built with the memory the board will have.
 */

#include <stdbool.h>
#include <stdint.h>

#include "todo/app.h"
#include "todo/memrepo.h"

#define SCENE_NOW 1796230800LL /* 2026-12-02 17:00 UTC, 14:00 in UTC-3 */
#define SCENE_OFFSET (-180)

/* Fills the in-memory server: the demo data, or the worst case (60 quests and events with the longest texts). */
void scene_data(todo_memrepo *m, bool worst);

int scene_count(void);
/* The scenes that are also pinned with the worst-case data (the ones that stress the layout). */
int scene_worst_count(void);
const char *scene_worst_name(int i);
const char *scene_name(int i);
/* Puts the application in the state of the scene (after it has loaded). False for an unknown name. */
bool scene_apply(todo_app *a, const char *name);

#endif /* TODO_SCENES_H */
