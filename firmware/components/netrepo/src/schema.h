#ifndef NETREPO_SCHEMA_H
#define NETREPO_SCHEMA_H

/* What each answer of the server must look like (docs/api.md), as a handler for the JSON reader. A document that does not
 * match, in any detail, is refused as a whole. */

#include <stdbool.h>
#include <stdint.h>

#include "jsonr/jsonr.h"
#include "todo/model.h"
#include "todo/repo.h"

typedef struct {
    todo_quest quests[TODO_MAX_QUESTS];
    int n;
    int32_t xp, open, done;
    bool truncated;
    int64_t rev;
} net_quests;

typedef struct {
    todo_event events[TODO_MAX_EVENTS];
    int n;
    bool truncated;
    int64_t rev;
    int64_t from, to; /* the window it was asked for (filled by the caller) */
} net_events;

typedef enum { SCHEMA_QUESTS, SCHEMA_EVENTS, SCHEMA_CHANGES, SCHEMA_LINK } schema_kind;

typedef struct {
    schema_kind kind;
    net_quests *quests;
    net_events *events;
    /* /changes */
    int64_t rev;
    bool changed;
    /* /link */
    char url[TODO_LINK_URL_MAX + 1];
    /* progress */
    unsigned seen;      /* top-level fields met */
    unsigned item_seen; /* fields of the item being read */
    bool in_list, closed;
    todo_quest q;       /* the item being read */
    todo_event e;
    bool q_done_null;
} schema;

void schema_init(schema *s, schema_kind kind, net_quests *q, net_events *e);
bool schema_event(void *user, const jsonr_event *e); /* the jsonr_fn */
bool schema_complete(const schema *s);               /* a whole, valid document was read */

#endif /* NETREPO_SCHEMA_H */
