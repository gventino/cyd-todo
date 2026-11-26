#include "todo/settings.h"

#include <string.h>

/* The zones the list walks through, minutes east of UTC. */
static const int16_t ZONES[] = {-720, -660, -600, -570, -540, -480, -420, -360, -300, -240, -210, -180, -120, -60, 0, 60, 120, 180, 210, 240,
                                270,  300,  330,  345,  360,  390,  420,  480,  525,  540,  570,  600,  630,  660, 720, 765, 780, 840};

int todo_zone_count(void)
{
    return (int)(sizeof(ZONES) / sizeof(ZONES[0]));
}

int todo_zone_at(int index)
{
    return index >= 0 && index < todo_zone_count() ? ZONES[index] : 0;
}

bool todo_zone_valid(int minutes)
{
    int i;

    for (i = 0; i < todo_zone_count(); i++) {
        if (ZONES[i] == minutes) {
            return true;
        }
    }
    return false;
}

void todo_settings_default(todo_settings *s, int zone_min)
{
    s->zone_min = (int16_t)(todo_zone_valid(zone_min) ? zone_min : 0);
    s->backlight = 1;
    s->palette = 0;
}

bool todo_settings_valid(const todo_settings *s)
{
    return todo_zone_valid(s->zone_min) && s->backlight < TODO_BACKLIGHT_COUNT && s->palette < TODO_PALETTE_KINDS;
}

uint16_t todo_crc16(const uint8_t *data, size_t n)
{
    uint16_t crc = 0xFFFF; /* CRC-16/CCITT-FALSE */
    size_t i;
    int b;

    for (i = 0; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (b = 0; b < 8; b++) {
            crc = (uint16_t)((crc & 0x8000u) ? ((unsigned)crc << 1) ^ 0x1021u : (unsigned)crc << 1);
        }
    }
    return crc;
}

bool todo_settings_encode(const todo_settings *s, uint8_t out[TODO_SETTINGS_RECORD])
{
    uint16_t crc;
    uint16_t zone = (uint16_t)s->zone_min;

    if (!todo_settings_valid(s)) {
        return false;
    }
    out[0] = TODO_SETTINGS_VERSION;
    out[1] = (uint8_t)(zone & 0xFF);
    out[2] = (uint8_t)(zone >> 8);
    out[3] = s->backlight;
    out[4] = s->palette;
    out[5] = out[6] = out[7] = 0;
    crc = todo_crc16(out, 8);
    out[8] = (uint8_t)(crc & 0xFF);
    out[9] = (uint8_t)(crc >> 8);
    return true;
}

bool todo_settings_decode(const uint8_t *rec, size_t len, todo_settings *out)
{
    todo_settings s;
    uint16_t crc;

    if (!rec || !out || len != TODO_SETTINGS_RECORD || rec[0] != TODO_SETTINGS_VERSION) {
        return false;
    }
    crc = (uint16_t)(rec[8] | (rec[9] << 8));
    if (crc != todo_crc16(rec, 8) || rec[5] != 0 || rec[6] != 0 || rec[7] != 0) {
        return false;
    }
    s.zone_min = (int16_t)(uint16_t)(rec[1] | (rec[2] << 8));
    s.backlight = rec[3];
    s.palette = rec[4];
    if (!todo_settings_valid(&s)) {
        return false;
    }
    *out = s;
    return true;
}

void todo_debounce_init(todo_debounce *d)
{
    d->pending = false;
    d->last = 0;
}

void todo_debounce_change(todo_debounce *d, uint32_t now_ms)
{
    d->pending = true;
    d->last = now_ms;
}

bool todo_debounce_due(todo_debounce *d, uint32_t now_ms, uint32_t delay_ms)
{
    if (!d->pending || (uint32_t)(now_ms - d->last) < delay_ms) {
        return false;
    }
    d->pending = false;
    return true;
}
