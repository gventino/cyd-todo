#ifndef TODO_RIG_H
#define TODO_RIG_H

/* A complete cydui UI on a 320x240 in-memory screen, with all of its memory inside the struct (the pattern of the
 * library's own tests), plus the golden check of what is on screen. */

#include "cydui/ui.h"
#include "cydui_mem.h"

#define RIG_W 320
#define RIG_H 240

typedef struct {
    cydui_mem mem;
    cydui_ui ui;
    cydui_theme theme;
    cydui_color fb[RIG_W * RIG_H];
    cydui_cmd cmds[512];
    cydui_color strip[RIG_W * 8];
    char text[4096];
    cydui_state state[64];
    uint32_t idset[1024];
    uint64_t dirty[32]; /* one hash per strip: the screen sends only what changed, as the board does */
} rig;

/* Starts a rig with `theme` (copied). Returns 0 or a CYDUI_ERR_* flag. */
uint32_t rig_start(rig *r, const cydui_theme *theme);

/* Synthetic touches (read by the next cydui_ui_begin) and the clock of the in-memory backend. */
void rig_down(rig *r, int x, int y);
void rig_move(rig *r, int x, int y);
void rig_up(rig *r, int x, int y);
void rig_advance(rig *r, uint32_t ms);

/* Writes <build>/golden/<name>.png without comparing anything (to look at). */
void rig_snapshot(const rig *r, const char *name);

/* Compares the screen with pc/golden/<name>.hash and writes <build>/golden/<name>.png.
 * CYDUI_UPDATE_GOLDEN=1 writes the hash file instead. */
void golden_check(const char *name, const rig *r);

#endif /* TODO_RIG_H */
