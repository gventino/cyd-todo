#include "schema.h"

#include <string.h>

#include "todo/datetime.h"

#define EPOCH_MIN 946684800LL /* 2000-01-01 */
#define EPOCH_MAX 4102444800LL /* 2100-01-01 */
#define BIG 2000000000LL

/* top-level fields */
enum { T_REV = 1, T_XP = 2, T_OPEN = 4, T_DONE = 8, T_TRUNC = 16, T_LIST = 32, T_CHANGED = 64, T_URL = 128, T_EXP = 256 };
/* item fields */
enum { I_ID = 1, I_TITLE = 2, I_NOTES = 4, I_DIFF = 8, I_DUE = 16, I_STATE = 32, I_DONE_AT = 64, I_UPD = 128, I_DESC = 256, I_START = 512, I_END = 1024 };

void schema_init(schema *s, schema_kind kind, net_quests *q, net_events *e)
{
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    s->quests = q;
    s->events = e;
    if (q) {
        memset(q, 0, sizeof(*q));
    }
    if (e) {
        memset(e, 0, sizeof(*e));
        e->from = e->to = 0;
    }
}

static bool once(unsigned *seen, unsigned bit)
{
    if (*seen & bit) {
        return false; /* a field twice */
    }
    *seen |= bit;
    return true;
}

static bool in_range(const jsonr_event *e, int64_t lo, int64_t hi)
{
    return e->kind == J_INT && e->i >= lo && e->i <= hi;
}

static bool text(const jsonr_event *e, char *dst, size_t cap, size_t min_len, size_t max_len)
{
    if (e->kind != J_STRING || e->len < min_len || e->len > max_len || e->len + 1 > cap) {
        return false;
    }
    memcpy(dst, e->str, e->len + 1);
    return true;
}

/* A field of an item of the list (depth 3: top object, list, item, field). */
static bool quest_field(schema *s, const jsonr_event *e, const char *key)
{
    todo_quest *q = &s->q;

    if (!strcmp(key, "id")) {
        if (!once(&s->item_seen, I_ID) || !in_range(e, 1, INT32_MAX)) {
            return false;
        }
        q->id = (int32_t)e->i;
    } else if (!strcmp(key, "title")) {
        return once(&s->item_seen, I_TITLE) && text(e, q->title, sizeof(q->title), 1, TODO_TITLE_MAX);
    } else if (!strcmp(key, "notes")) {
        return once(&s->item_seen, I_NOTES) && text(e, q->notes, sizeof(q->notes), 0, TODO_TEXT_MAX);
    } else if (!strcmp(key, "difficulty")) {
        if (!once(&s->item_seen, I_DIFF) || !in_range(e, 1, 5)) {
            return false;
        }
        q->difficulty = (uint8_t)e->i;
    } else if (!strcmp(key, "due")) {
        if (!once(&s->item_seen, I_DUE)) {
            return false;
        }
        if (e->kind == J_NULL) {
            q->due = 0;
        } else if (e->kind == J_STRING) {
            q->due = todo_due_parse(e->str);
            return q->due != 0;
        } else {
            return false;
        }
    } else if (!strcmp(key, "state")) {
        if (!once(&s->item_seen, I_STATE) || e->kind != J_STRING) {
            return false;
        }
        if (!strcmp(e->str, "done")) {
            q->done = true;
        } else if (!strcmp(e->str, "open")) {
            q->done = false;
        } else {
            return false;
        }
    } else if (!strcmp(key, "done_at")) {
        if (!once(&s->item_seen, I_DONE_AT)) {
            return false;
        }
        if (e->kind == J_NULL) {
            q->done_at = 0;
        } else if (in_range(e, EPOCH_MIN, EPOCH_MAX)) {
            q->done_at = e->i;
        } else {
            return false;
        }
    } else if (!strcmp(key, "updated_at")) {
        return once(&s->item_seen, I_UPD) && in_range(e, 0, INT64_MAX);
    } else {
        return e->kind != J_OBJECT_BEGIN && e->kind != J_ARRAY_BEGIN; /* an unknown field is skipped if it is a plain value */
    }
    return true;
}

static bool event_field(schema *s, const jsonr_event *e, const char *key)
{
    todo_event *v = &s->e;

    if (!strcmp(key, "id")) {
        if (!once(&s->item_seen, I_ID) || !in_range(e, 1, INT32_MAX)) {
            return false;
        }
        v->id = (int32_t)e->i;
    } else if (!strcmp(key, "title")) {
        return once(&s->item_seen, I_TITLE) && text(e, v->title, sizeof(v->title), 1, TODO_TITLE_MAX);
    } else if (!strcmp(key, "description")) {
        return once(&s->item_seen, I_DESC) && text(e, v->description, sizeof(v->description), 0, TODO_TEXT_MAX);
    } else if (!strcmp(key, "start")) {
        if (!once(&s->item_seen, I_START) || !in_range(e, EPOCH_MIN, EPOCH_MAX)) {
            return false;
        }
        v->start = e->i;
    } else if (!strcmp(key, "end")) {
        if (!once(&s->item_seen, I_END) || !in_range(e, EPOCH_MIN, EPOCH_MAX)) {
            return false;
        }
        v->end = e->i;
    } else if (!strcmp(key, "updated_at")) {
        return once(&s->item_seen, I_UPD) && in_range(e, 0, INT64_MAX);
    } else {
        return e->kind != J_OBJECT_BEGIN && e->kind != J_ARRAY_BEGIN;
    }
    return true;
}

static bool list_begin(schema *s, const jsonr_event *e)
{
    (void)e;
    return once(&s->seen, T_LIST);
}

bool schema_event(void *user, const jsonr_event *e)
{
    schema *s = user;
    const bool quests = s->kind == SCHEMA_QUESTS, events = s->kind == SCHEMA_EVENTS;
    const char *top = e->depth >= 1 ? e->key[0] : NULL;

    if (s->closed) {
        return false;
    }
    if (e->depth == 0) { /* the document is one object */
        if (e->kind == J_OBJECT_BEGIN) {
            return true;
        }
        if (e->kind == J_OBJECT_END) {
            s->closed = true;
            return true;
        }
        return false;
    }
    if (e->depth == 1) { /* a field of the top object */
        const char *k = e->key[0];
        switch (s->kind) {
        case SCHEMA_QUESTS:
        case SCHEMA_EVENTS:
            if (!strcmp(k, "rev")) {
                if (!once(&s->seen, T_REV) || !in_range(e, 0, INT64_MAX)) {
                    return false;
                }
                *(quests ? &s->quests->rev : &s->events->rev) = e->i;
            } else if (!strcmp(k, "truncated")) {
                if (!once(&s->seen, T_TRUNC) || e->kind != J_BOOL) {
                    return false;
                }
                *(quests ? &s->quests->truncated : &s->events->truncated) = e->b;
            } else if (quests && (!strcmp(k, "xp") || !strcmp(k, "open") || !strcmp(k, "done"))) {
                unsigned bit = !strcmp(k, "xp") ? T_XP : (!strcmp(k, "open") ? T_OPEN : T_DONE);
                int32_t *dst = bit == T_XP ? &s->quests->xp : (bit == T_OPEN ? &s->quests->open : &s->quests->done);
                if (!once(&s->seen, bit) || !in_range(e, 0, BIG)) {
                    return false;
                }
                *dst = (int32_t)e->i;
            } else if ((quests && !strcmp(k, "quests")) || (events && !strcmp(k, "events"))) {
                if (e->kind == J_ARRAY_END) {
                    return true;
                }
                return e->kind == J_ARRAY_BEGIN && list_begin(s, e);
            } else {
                return e->kind != J_OBJECT_BEGIN && e->kind != J_ARRAY_BEGIN;
            }
            return true;
        case SCHEMA_CHANGES:
            if (!strcmp(k, "rev")) {
                if (!once(&s->seen, T_REV) || !in_range(e, 0, INT64_MAX)) {
                    return false;
                }
                s->rev = e->i;
            } else if (!strcmp(k, "changed")) {
                if (!once(&s->seen, T_CHANGED) || e->kind != J_BOOL) {
                    return false;
                }
                s->changed = e->b;
            } else {
                return e->kind != J_OBJECT_BEGIN && e->kind != J_ARRAY_BEGIN;
            }
            return true;
        case SCHEMA_LINK:
            if (!strcmp(k, "url")) {
                size_t i;
                if (!once(&s->seen, T_URL) || e->kind != J_STRING || e->len < 8 || e->len > TODO_LINK_URL_MAX) {
                    return false;
                }
                for (i = 0; i < e->len; i++) { /* a URL: printable ASCII, no spaces */
                    if ((unsigned char)e->str[i] < 0x21 || (unsigned char)e->str[i] > 0x7E) {
                        return false;
                    }
                }
                if (strncmp(e->str, "http://", 7) != 0 && strncmp(e->str, "https://", 8) != 0) {
                    return false;
                }
                memcpy(s->url, e->str, e->len + 1);
            } else if (!strcmp(k, "expires_in")) {
                return once(&s->seen, T_EXP) && in_range(e, 0, BIG);
            } else {
                return e->kind != J_OBJECT_BEGIN && e->kind != J_ARRAY_BEGIN;
            }
            return true;
        }
        return false;
    }
    /* below the top: only the list and its items are allowed to be containers */
    if (!(quests || events) || !top || strcmp(top, quests ? "quests" : "events") != 0) {
        return false;
    }
    if (e->depth == 2) { /* an item begins or ends */
        if (e->kind == J_OBJECT_BEGIN) {
            if ((quests ? s->quests->n : s->events->n) >= (quests ? TODO_MAX_QUESTS : TODO_MAX_EVENTS)) {
                return false; /* more than the contract allows */
            }
            s->item_seen = 0;
            memset(&s->q, 0, sizeof(s->q));
            memset(&s->e, 0, sizeof(s->e));
            return true;
        }
        if (e->kind == J_OBJECT_END) {
            if (quests) {
                if ((s->item_seen & (I_ID | I_TITLE | I_DIFF | I_STATE)) != (I_ID | I_TITLE | I_DIFF | I_STATE)) {
                    return false;
                }
                s->quests->quests[s->quests->n++] = s->q;
            } else {
                if ((s->item_seen & (I_ID | I_TITLE | I_START | I_END)) != (I_ID | I_TITLE | I_START | I_END) || s->e.end <= s->e.start) {
                    return false;
                }
                s->events->events[s->events->n++] = s->e;
            }
            return true;
        }
        return false; /* a list of anything but objects */
    }
    if (e->depth == 3 && e->key[2]) { /* a field of an item */
        return quests ? quest_field(s, e, e->key[2]) : event_field(s, e, e->key[2]);
    }
    return false;
}

bool schema_complete(const schema *s)
{
    if (!s->closed) {
        return false;
    }
    switch (s->kind) {
    case SCHEMA_QUESTS:
        return (s->seen & (T_REV | T_XP | T_OPEN | T_DONE | T_LIST)) == (T_REV | T_XP | T_OPEN | T_DONE | T_LIST);
    case SCHEMA_EVENTS:
        return (s->seen & (T_REV | T_LIST)) == (T_REV | T_LIST);
    case SCHEMA_CHANGES:
        return (s->seen & (T_REV | T_CHANGED)) == (T_REV | T_CHANGED);
    case SCHEMA_LINK:
        return (s->seen & T_URL) != 0;
    }
    return false;
}
