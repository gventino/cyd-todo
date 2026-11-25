#include "rig.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#ifndef GOLDEN_DIR
#error "GOLDEN_DIR must point at pc/golden"
#endif
#ifndef GOLDEN_OUT_DIR
#error "GOLDEN_OUT_DIR must point at the build output directory for PNGs"
#endif

uint32_t rig_start(rig *r, const cydui_theme *theme)
{
    cydui_ui_config cfg;

    memset(r, 0, sizeof(*r));
    cydui_mem_init(&r->mem, r->fb, RIG_W, RIG_H);
    r->theme = *theme;
    memset(&cfg, 0, sizeof(cfg));
    cfg.core.backend = &r->mem.backend;
    cfg.core.cmd_mem = r->cmds;
    cfg.core.cmd_mem_size = sizeof(r->cmds);
    cfg.core.strip_mem = r->strip;
    cfg.core.strip_mem_size = sizeof(r->strip);
    cfg.core.text_mem = r->text;
    cfg.core.text_mem_size = sizeof(r->text);
    cfg.state_mem = r->state;
    cfg.state_mem_size = sizeof(r->state);
    cfg.idset_mem = r->idset;
    cfg.idset_mem_size = sizeof(r->idset);
    cfg.core.dirty_mem = r->dirty;
    cfg.core.dirty_mem_size = sizeof(r->dirty);
    cfg.theme = &r->theme;
    return cydui_ui_init(&r->ui, &cfg);
}

void golden_check_mem(const char *name, const cydui_mem *mem, uint32_t errors)
{
    char path[512], msg[256];
    uint64_t hash = cydui_mem_hash(mem);
    unsigned long long want = 0;
    FILE *f;

    snprintf(path, sizeof(path), "%s/%s.png", GOLDEN_OUT_DIR, name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, cydui_mem_write_png(mem, path), "could not write the PNG");
    /* "text was cut" is informative; any other flag means the scene did something wrong */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, errors & ~(uint32_t)CYDUI_ERR_TEXT_TRUNCATED, "the scene raised library errors");

    snprintf(path, sizeof(path), "%s/%s.hash", GOLDEN_DIR, name);
    if (getenv("CYDUI_UPDATE_GOLDEN")) {
        f = fopen(path, "w");
        TEST_ASSERT_NOT_NULL_MESSAGE(f, "could not write the golden hash");
        fprintf(f, "%016" PRIx64 "\n", hash);
        fclose(f);
        return;
    }
    f = fopen(path, "r");
    snprintf(msg, sizeof(msg), "no golden hash for '%s' (run with CYDUI_UPDATE_GOLDEN=1 after looking at the PNG)", name);
    TEST_ASSERT_NOT_NULL_MESSAGE(f, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, fscanf(f, "%llx", &want), "malformed golden hash file");
    fclose(f);
    snprintf(msg, sizeof(msg), "'%s': got %016" PRIx64 ", golden %016llx (see the PNG)", name, hash, want);
    TEST_ASSERT_TRUE_MESSAGE(hash == (uint64_t)want, msg);
}

void golden_check(const char *name, const rig *r)
{
    golden_check_mem(name, &r->mem, cydui_ui_errors(&r->ui));
}

void rig_down(rig *r, int x, int y) { cydui_mem_push_event(&r->mem, CYDUI_EVENT_DOWN, (int16_t)x, (int16_t)y); }
void rig_move(rig *r, int x, int y) { cydui_mem_push_event(&r->mem, CYDUI_EVENT_MOVE, (int16_t)x, (int16_t)y); }
void rig_up(rig *r, int x, int y) { cydui_mem_push_event(&r->mem, CYDUI_EVENT_UP, (int16_t)x, (int16_t)y); }
void rig_advance(rig *r, uint32_t ms) { r->mem.now += ms; }

void rig_snapshot(const rig *r, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", GOLDEN_OUT_DIR, name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, cydui_mem_write_png(&r->mem, path), "could not write the PNG");
}
