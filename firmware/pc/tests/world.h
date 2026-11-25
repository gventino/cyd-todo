#ifndef TODO_WORLD_H
#define TODO_WORLD_H

/* The application on the test rig: the real UI, the in-memory repository with the demo data, a fixed clock. */

#include "rig.h"
#include "todo/app.h"
#include "todo/memrepo.h"

#define WORLD_NOW 1796230800LL /* 2026-12-02 17:00 UTC = 14:00 in UTC-3 */
#define WORLD_OFFSET (-180)

typedef struct {
    rig r;
    todo_memrepo mem;
    todo_app app;
    int64_t skew; /* seconds added to the fixed clock */
    uint32_t cmds; /* commands in the last frame, before it was rendered */
    uint32_t errors; /* errors flagged in the last frame */
} world;

/* Starts the app, with the demo data when `demo`. */
void world_start(world *w, todo_palette p, bool demo);
/* One frame, 16 ms after the previous one, with the clock moving along. */
void world_frame(world *w);
void world_frames(world *w, int n);
void world_tap(world *w, int x, int y);
void world_snap(const world *w, const char *name);

#endif /* TODO_WORLD_H */
