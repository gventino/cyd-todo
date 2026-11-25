#include "todo/model.h"

#include <string.h>

bool todo_copy(char *dst, size_t cap, const char *src)
{
    size_t n = 0, end;

    if (!dst || cap == 0) {
        return src && *src;
    }
    if (!src) {
        dst[0] = '\0';
        return false;
    }
    while (src[n]) {
        n++;
    }
    if (n < cap) {
        memcpy(dst, src, n + 1);
        return false;
    }
    end = cap - 1; /* room for the NUL: do not end in the middle of a character */
    while (end > 0 && ((unsigned char)src[end] & 0xC0u) == 0x80u) {
        end--; /* src[end] is a continuation byte: the character starts earlier */
    }
    memcpy(dst, src, end);
    dst[end] = '\0';
    return true;
}

void todo_store_clear(todo_store *s)
{
    memset(s, 0, sizeof(*s));
}

todo_quest *todo_find_quest(todo_store *s, int32_t id)
{
    int i;

    for (i = 0; i < s->n_quests; i++) {
        if (s->quests[i].id == id) {
            return &s->quests[i];
        }
    }
    return NULL;
}

todo_event *todo_find_event(todo_store *s, int32_t id)
{
    int i;

    for (i = 0; i < s->n_events; i++) {
        if (s->events[i].id == id) {
            return &s->events[i];
        }
    }
    return NULL;
}
