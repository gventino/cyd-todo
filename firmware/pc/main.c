/*
 * The cyd-todo user interface on the PC, with the memory the board will have.
 *
 *   todo_pc --list                         the scenes
 *   todo_pc --scene NAME [--palette day|night] [--worst] [--png FILE] [--hash]
 *                                          renders a scene into memory (headless)
 *   todo_pc [--window] [--frames N]        a window; the mouse is the touch screen (built with -DTODO_SDL=ON)
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cydui_mem.h"
#include "scenes.h"
#include "todo/memrepo.h"
#ifdef TODO_HAVE_SDL
#include "cydui_sdl.h"
#endif

static cydui_color framebuffer[320 * 240];
static todo_memrepo mem_repo;
static todo_app app;
static cydui_ui ui;
static cydui_theme theme;

static void one_frame(uint32_t ms)
{
    todo_app_set_time(&app, SCENE_NOW + ms / 1000, true);
    cydui_ui_begin(&ui, 0);
    todo_app_frame(&app, &ui);
    cydui_ui_end(&ui);
}

static int run_scene(const char *scene, todo_palette palette, bool worst, bool print_hash, const char *png)
{
    cydui_mem mem;
    uint32_t t = 0;
    int i;

    cydui_mem_init(&mem, framebuffer, 320, 240);
    todo_memrepo_init(&mem_repo, SCENE_NOW);
    scene_data(&mem_repo, worst);
    todo_app_init(&app, &mem_repo.repo, SCENE_OFFSET);
    app.palette = palette;
    todo_theme_quest_init(&theme, palette);
    if (board_ui_init(&ui, &mem.backend, &theme)) {
        fprintf(stderr, "the UI did not start\n");
        return 1;
    }
    for (i = 0; i < 8; i++, t += 16, mem.now += 16) {
        one_frame(t);
    }
    if (!scene_apply(&app, scene)) {
        fprintf(stderr, "unknown scene '%s' (try --list)\n", scene);
        return 2;
    }
    for (i = 0; i < 24; i++, t += 16, mem.now += 16) {
        one_frame(t);
    }
    if (cydui_ui_errors(&ui) & ~(uint32_t)CYDUI_ERR_TEXT_TRUNCATED) {
        fprintf(stderr, "warning: cydui errors 0x%x\n", (unsigned)cydui_ui_errors(&ui));
    }
    if (print_hash) {
        printf("%016" PRIx64 "\n", cydui_mem_hash(&mem));
    }
    if (png && cydui_mem_write_png(&mem, png) != 0) {
        fprintf(stderr, "could not write %s\n", png);
        return 1;
    }
    return 0;
}

#ifdef TODO_HAVE_SDL
static int run_window(long max_frames, todo_palette palette)
{
    cydui_sdl *sdl = cydui_sdl_create("cyd-todo", 320, 240, 3);
    long frames = 0;

    if (!sdl) {
        fprintf(stderr, "could not open a window\n");
        return 1;
    }
    todo_memrepo_init(&mem_repo, SCENE_NOW);
    scene_data(&mem_repo, false);
    todo_app_init(&app, &mem_repo.repo, SCENE_OFFSET);
    app.palette = palette;
    todo_theme_quest_init(&theme, palette);
    if (board_ui_init(&ui, cydui_sdl_backend(sdl), &theme)) {
        cydui_sdl_destroy(sdl);
        return 1;
    }
    for (;;) {
        todo_app_set_time(&app, SCENE_NOW + cydui_now(&ui) / 1000, true);
        if (!cydui_ui_begin(&ui, 0) || (max_frames >= 0 && frames >= max_frames)) {
            break;
        }
        todo_app_frame(&app, &ui);
        cydui_ui_end(&ui);
        frames++;
    }
    cydui_sdl_destroy(sdl);
    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *scene = NULL, *png = NULL;
    todo_palette palette = TODO_PALETTE_PARCHMENT;
    bool worst = false, hash = false;
    long frames = -1;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list")) {
            for (int k = 0; k < scene_count(); k++) {
                puts(scene_name(k));
            }
            return 0;
        } else if (!strcmp(argv[i], "--scene") && i + 1 < argc) {
            scene = argv[++i];
        } else if (!strcmp(argv[i], "--palette") && i + 1 < argc) {
            palette = !strcmp(argv[++i], "night") ? TODO_PALETTE_CANDLELIGHT : TODO_PALETTE_PARCHMENT;
        } else if (!strcmp(argv[i], "--worst")) {
            worst = true;
        } else if (!strcmp(argv[i], "--hash")) {
            hash = true;
        } else if (!strcmp(argv[i], "--png") && i + 1 < argc) {
            png = argv[++i];
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = atol(argv[++i]);
        } else if (strcmp(argv[i], "--window") != 0) {
            fprintf(stderr, "usage: %s --list | --scene NAME [--palette day|night] [--worst] [--png FILE] [--hash] | [--window] [--frames N]\n", argv[0]);
            return 2;
        }
    }
    if (scene) {
        return run_scene(scene, palette, worst, hash, png);
    }
#ifdef TODO_HAVE_SDL
    return run_window(frames, palette);
#else
    (void)frames;
    fprintf(stderr, "built without SDL (-DTODO_SDL=ON); use --scene\n");
    return 2;
#endif
}
