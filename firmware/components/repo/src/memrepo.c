#include "todo/memrepo.h"

#include <stdio.h>
#include <string.h>

#include "todo/datetime.h"
#include "todo/levels.h"

#define WINDOW_CAP TODO_MAX_QUESTS

/* ---- the pretend server ---- */

int32_t todo_memrepo_add_quest(todo_memrepo *m, const char *title, const char *notes, int difficulty, int32_t due)
{
    todo_quest *q;

    if (m->n_quests >= (int)(sizeof(m->quests) / sizeof(m->quests[0]))) {
        return 0;
    }
    q = &m->quests[m->n_quests++];
    memset(q, 0, sizeof(*q));
    q->id = m->next_id++;
    todo_copy(q->title, sizeof(q->title), title);
    todo_copy(q->notes, sizeof(q->notes), notes ? notes : "");
    q->difficulty = (uint8_t)(difficulty < 1 ? 1 : (difficulty > 5 ? 5 : difficulty));
    q->due = due;
    m->rev++;
    return q->id;
}

int32_t todo_memrepo_add_event(todo_memrepo *m, const char *title, const char *description, int64_t start, int64_t end)
{
    todo_event *e;

    if (m->n_events >= (int)(sizeof(m->events) / sizeof(m->events[0])) || end <= start) {
        return 0;
    }
    e = &m->events[m->n_events++];
    memset(e, 0, sizeof(*e));
    e->id = m->next_id++;
    todo_copy(e->title, sizeof(e->title), title);
    todo_copy(e->description, sizeof(e->description), description ? description : "");
    e->start = start;
    e->end = end;
    m->rev++;
    return e->id;
}

static todo_quest *server_quest(todo_memrepo *m, int32_t id)
{
    int i;
    for (i = 0; i < m->n_quests; i++) {
        if (m->quests[i].id == id) {
            return &m->quests[i];
        }
    }
    return NULL;
}

/* Open quests first (by due date, undated last, then id), then done ones, most recently done first. */
static bool quest_before(const todo_quest *a, const todo_quest *b)
{
    if (a->done != b->done) {
        return !a->done;
    }
    if (!a->done) {
        int32_t da = a->due ? a->due : INT32_MAX, db = b->due ? b->due : INT32_MAX;
        if (da != db) {
            return da < db;
        }
        return a->id < b->id;
    }
    if (a->done_at != b->done_at) {
        return a->done_at > b->done_at;
    }
    return a->id < b->id;
}

static void read_quests(todo_memrepo *m, todo_store *s)
{
    int i, j, n = 0;
    static int order[TODO_MAX_QUESTS * 2];

    s->xp = s->open = s->done = 0;
    for (i = 0; i < m->n_quests; i++) {
        order[n++] = i;
        if (m->quests[i].done) {
            s->done++;
            s->xp += todo_xp_for_quest(m->quests[i].difficulty);
        } else {
            s->open++;
        }
    }
    for (i = 1; i < n; i++) { /* insertion sort: a few dozen items */
        int k = order[i];
        for (j = i - 1; j >= 0 && quest_before(&m->quests[k], &m->quests[order[j]]); j--) {
            order[j + 1] = order[j];
        }
        order[j + 1] = k;
    }
    s->n_quests = n > TODO_MAX_QUESTS ? TODO_MAX_QUESTS : n;
    s->quests_truncated = n > TODO_MAX_QUESTS;
    for (i = 0; i < s->n_quests; i++) {
        s->quests[i] = m->quests[order[i]];
    }
    s->rev = m->rev;
}

static void read_events(todo_memrepo *m, todo_store *s, int64_t from, int64_t to)
{
    int i, j, n = 0;
    static int order[TODO_MAX_EVENTS * 2];

    for (i = 0; i < m->n_events; i++) {
        if (m->events[i].start < to && m->events[i].end > from) {
            order[n++] = i;
        }
    }
    for (i = 1; i < n; i++) {
        int k = order[i];
        for (j = i - 1; j >= 0 && (m->events[k].start < m->events[order[j]].start ||
                                   (m->events[k].start == m->events[order[j]].start && m->events[k].id < m->events[order[j]].id));
             j--) {
            order[j + 1] = order[j];
        }
        order[j + 1] = k;
    }
    s->n_events = n > WINDOW_CAP ? WINDOW_CAP : n;
    s->events_truncated = n > WINDOW_CAP;
    for (i = 0; i < s->n_events; i++) {
        s->events[i] = m->events[order[i]];
    }
    s->events_from = from;
    s->events_to = to;
    s->rev = m->rev;
}

/* ---- the repository ---- */

static bool submit(todo_repo *r, const todo_request *req)
{
    todo_memrepo *m = (todo_memrepo *)r->impl;
    int at;

    if (m->len >= TODO_MEM_QUEUE) {
        return false;
    }
    at = (m->head + m->len) % TODO_MEM_QUEUE;
    m->queue[at] = *req;
    m->wait[at] = m->latency;
    m->len++;
    return true;
}

static bool poll(todo_repo *r, todo_store *store, todo_result *out)
{
    todo_memrepo *m = (todo_memrepo *)r->impl;
    todo_request q;
    todo_quest *sq;
    int i;

    if (m->len == 0) {
        return false;
    }
    if (m->wait[m->head] > 0) {
        m->wait[m->head]--; /* the answer is not ready yet */
        return false;
    }
    q = m->queue[m->head];
    m->head = (m->head + 1) % TODO_MEM_QUEUE;
    m->len--;

    memset(out, 0, sizeof(*out));
    out->kind = q.kind;
    out->id = q.id;
    out->rev = m->rev;
    if (m->offline) {
        out->offline = true;
        return true;
    }
    if (m->fail_next > 0) {
        m->fail_next--;
        return true; /* ok stays false: the server said no */
    }
    out->ok = true;
    switch (q.kind) {
    case TODO_REQ_REFRESH_QUESTS:
        read_quests(m, store);
        break;
    case TODO_REQ_REFRESH_EVENTS:
        read_events(m, store, q.a, q.b);
        break;
    case TODO_REQ_SET_QUEST_DONE:
        sq = server_quest(m, q.id);
        if (!sq) {
            out->ok = false;
            break;
        }
        if (sq->done != q.flag) {
            sq->done = q.flag;
            sq->done_at = q.flag ? m->now : 0;
            m->rev++;
        }
        read_quests(m, store);
        break;
    case TODO_REQ_DELETE_QUEST:
        sq = server_quest(m, q.id);
        if (!sq) {
            out->ok = false;
            break;
        }
        *sq = m->quests[--m->n_quests];
        m->rev++;
        read_quests(m, store);
        break;
    case TODO_REQ_DELETE_EVENT:
        out->ok = false;
        for (i = 0; i < m->n_events; i++) {
            if (m->events[i].id == q.id) {
                m->events[i] = m->events[--m->n_events];
                m->rev++;
                out->ok = true;
                break;
            }
        }
        if (out->ok) {
            read_events(m, store, store->events_from, store->events_to);
        }
        break;
    case TODO_REQ_POLL_CHANGES:
        out->changed = q.a != m->rev;
        break;
    case TODO_REQ_NEW_LINK:
        m->link_counter++;
        snprintf(out->url, sizeof(out->url), "http://192.168.1.50:8080/m/%026d", m->link_counter);
        break;
    }
    out->rev = m->rev;
    return true;
}

static const todo_repo_ops OPS = {submit, poll};

void todo_memrepo_init(todo_memrepo *m, int64_t now)
{
    memset(m, 0, sizeof(*m));
    m->repo.ops = &OPS;
    m->repo.impl = m;
    m->next_id = 1;
    m->now = now;
}

void todo_memrepo_seed_demo(todo_memrepo *m)
{
    static const struct {
        const char *title, *notes;
        int difficulty, due_in; /* days from today, -1: none */
        bool done;
    } quests[] = {
        {"Slay the inbox", "Zero unread. Coffee is allowed.", 3, 2, false},
        {"Visit the dentist", "Ask about the crown.", 2, 5, false},
        {"Pay the guild fees", "", 1, 1, true},
        {"Clean the armory", "Garage, second shelf.", 4, -1, false},
        {"Write the dragon report", "Two pages, with a map.", 5, 9, false},
        {"Water the mandrake", "", 1, -1, true},
    };
    static const struct {
        const char *title, *description;
        int day, at, length;
    } events[] = {
        {"Dentist", "Bring the X-rays.", 1, 10 * 3600, 45 * 60},
        {"Guild meeting", "Quarterly. Bring the ledger.", 2, 18 * 3600, 2 * 3600},
        {"Moonlight market", "", 3, 20 * 3600, 3 * 3600},
        {"Night watch", "Until the small hours.", 4, 22 * 3600, 6 * 3600},
    };
    int64_t day_start = m->now - (m->now % TODO_DAY);
    size_t i;

    for (i = 0; i < sizeof(quests) / sizeof(quests[0]); i++) {
        int32_t due = 0;
        int32_t id;
        if (quests[i].due_in >= 0) {
            int y, mo, d;
            todo_civil_from_days(m->now / TODO_DAY + quests[i].due_in, &y, &mo, &d);
            due = todo_due_pack(y, mo, d);
        }
        id = todo_memrepo_add_quest(m, quests[i].title, quests[i].notes, quests[i].difficulty, due);
        if (quests[i].done && id) {
            todo_quest *q = &m->quests[m->n_quests - 1];
            q->done = true;
            q->done_at = m->now - (int64_t)(i + 1) * 3600;
        }
    }
    for (i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        int64_t start = day_start + events[i].day * TODO_DAY + events[i].at;
        todo_memrepo_add_event(m, events[i].title, events[i].description, start, start + events[i].length);
    }
}
