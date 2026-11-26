#ifndef TODO_SETTINGS_H
#define TODO_SETTINGS_H

/*
 * What the owner can change on the board and what must survive a reboot: the time zone, how long the backlight stays on, and the
 * palette. Pure C: the record that goes to the flash is built and read here, the flash itself is the board's (main/).
 *
 * The record is 10 bytes: version, zone (int16, little endian), backlight index, palette, three reserved zero bytes, and a
 * CRC-16 over the first eight. Anything that is not exactly such a record, with a good CRC and values the Settings tab can
 * produce, is refused whole: a corrupted, old, short or hostile record never turns into a half-read setting.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TODO_SETTINGS_RECORD 10
#define TODO_SETTINGS_VERSION 1
#define TODO_BACKLIGHT_COUNT 4 /* 30 s, 1 min, 5 min, never */
#define TODO_PALETTE_KINDS 2   /* day, night */

typedef struct {
    int16_t zone_min;  /* minutes east of UTC, one of the zones of the list */
    uint8_t backlight; /* 0 .. TODO_BACKLIGHT_COUNT - 1 */
    uint8_t palette;   /* 0 .. TODO_PALETTE_KINDS - 1 */
} todo_settings;

/* The zones the Settings tab walks through, west to east. */
int todo_zone_count(void);
int todo_zone_at(int index);
bool todo_zone_valid(int minutes);

/* The settings of a board that has saved nothing: a minute of backlight, the day palette, and `zone_min` (which must be valid, else UTC). */
void todo_settings_default(todo_settings *s, int zone_min);
bool todo_settings_valid(const todo_settings *s);

/* False (and `out` untouched) for settings that are not valid. */
bool todo_settings_encode(const todo_settings *s, uint8_t out[TODO_SETTINGS_RECORD]);
/* False (and `out` untouched) for anything that is not a good record: the caller then uses the defaults. */
bool todo_settings_decode(const uint8_t *rec, size_t len, todo_settings *out);

uint16_t todo_crc16(const uint8_t *data, size_t n);

/*
 * "Save when the changes have stopped": note each change, and ask whether it is time. The answer is true once, after `delay_ms`
 * without another change, so that an arrow held down is one write to the flash and not twenty. Wrap-safe 32-bit milliseconds.
 */
typedef struct {
    bool pending;
    uint32_t last;
} todo_debounce;

void todo_debounce_init(todo_debounce *d);
void todo_debounce_change(todo_debounce *d, uint32_t now_ms);
bool todo_debounce_due(todo_debounce *d, uint32_t now_ms, uint32_t delay_ms);

/*
 * The backlight policy: full brightness while the owner is around, dimmed to TODO_DIM_PERCENT after the timeout of the
 * Settings tab, back to full on the next touch. The brightness is the board's to set; this only says which one it should be.
 */
#define TODO_DIM_PERCENT 10

typedef struct {
    uint32_t last; /* milliseconds of the last touch (the boot counts as one) */
} todo_idle;

void todo_idle_init(todo_idle *i, uint32_t now_ms);
void todo_idle_touch(todo_idle *i, uint32_t now_ms);
/* 100 or TODO_DIM_PERCENT. The timeout is judged on the same last touch, so a changed setting takes effect at once. */
int todo_idle_level(const todo_idle *i, uint32_t now_ms, int backlight_index);
/* The timeout of a backlight index in milliseconds; 0 for "never". */
uint32_t todo_idle_timeout_ms(int backlight_index);

#endif /* TODO_SETTINGS_H */
