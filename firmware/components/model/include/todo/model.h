#ifndef TODO_MODEL_H
#define TODO_MODEL_H

/*
 * What the board knows: a small cache of quests and events, in fixed arrays (no heap), every string copied
 * with a limit. The sizes are the contract of docs/api.md; the server enforces the same numbers.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TODO_TITLE_MAX 64  /* bytes, without the NUL */
#define TODO_TEXT_MAX 256  /* notes and descriptions */
#define TODO_MAX_QUESTS 48
#define TODO_MAX_EVENTS 48

typedef struct {
    int32_t id;
    char title[TODO_TITLE_MAX + 1];
    char notes[TODO_TEXT_MAX + 1];
    uint8_t difficulty; /* 1..5 */
    int32_t due;        /* a calendar date packed as YYYYMMDD, or 0 for none */
    bool done;
    int64_t done_at;    /* UTC epoch seconds, 0 while open */
} todo_quest;

typedef struct {
    int32_t id;
    char title[TODO_TITLE_MAX + 1];
    char description[TODO_TEXT_MAX + 1];
    int64_t start, end; /* UTC epoch seconds, end after start */
} todo_event;

/* The cache. `rev` is the server's revision that it was read at. */
typedef struct {
    todo_quest quests[TODO_MAX_QUESTS];
    int n_quests;
    int32_t xp;          /* all-time, from the server (not the sum of what is cached) */
    int32_t open, done;  /* all-time counts */
    bool quests_truncated;

    todo_event events[TODO_MAX_EVENTS];
    int n_events;
    bool events_truncated;
    int64_t events_from, events_to; /* the window the events were read for, UTC epoch seconds */

    int64_t rev;
} todo_store;

/*
 * Copies `src` into `dst` (capacity `cap` including the NUL) and never cuts a UTF-8 character in two.
 * Returns true if something was left out.
 */
bool todo_copy(char *dst, size_t cap, const char *src);

/* Empties a store. */
void todo_store_clear(todo_store *s);

/* Finds a quest or an event by id in the cache, or NULL. */
todo_quest *todo_find_quest(todo_store *s, int32_t id);
todo_event *todo_find_event(todo_store *s, int32_t id);

#endif /* TODO_MODEL_H */
