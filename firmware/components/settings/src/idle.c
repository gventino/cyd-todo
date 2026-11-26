#include "todo/settings.h"

/* In the order of the Settings tab: 30 s, 1 min, 5 min, never. */
static const uint32_t TIMEOUT_MS[TODO_BACKLIGHT_COUNT] = {30000u, 60000u, 300000u, 0u};

uint32_t todo_idle_timeout_ms(int backlight_index)
{
    return backlight_index >= 0 && backlight_index < TODO_BACKLIGHT_COUNT ? TIMEOUT_MS[backlight_index] : 0;
}

void todo_idle_init(todo_idle *i, uint32_t now_ms)
{
    i->last = now_ms;
}

void todo_idle_touch(todo_idle *i, uint32_t now_ms)
{
    i->last = now_ms;
}

int todo_idle_level(const todo_idle *i, uint32_t now_ms, int backlight_index)
{
    uint32_t t = todo_idle_timeout_ms(backlight_index);

    return t != 0 && (uint32_t)(now_ms - i->last) >= t ? TODO_DIM_PERCENT : 100;
}
