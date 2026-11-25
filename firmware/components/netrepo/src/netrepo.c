#include "todo/netrepo.h"

#include <stdio.h>
#include <string.h>

#include "schema.h"

/* ---- one HTTP exchange, parsed as it arrives ---- */

typedef struct {
    int status;
    long clen;
    size_t bytes;
    bool head_seen, refused, parsing;
    jsonr json;
    schema sch;
} call;

static bool on_head(void *ctx, int status, long content_length)
{
    call *c = ctx;

    c->status = status;
    c->clen = content_length;
    c->head_seen = true;
    if (content_length < 0 || content_length > NET_RESPONSE_MAX) { /* chunked, unbounded, or too big: not read at all */
        c->refused = true;
        return false;
    }
    return true;
}

static bool on_body(void *ctx, const char *data, size_t n)
{
    call *c = ctx;

    c->bytes += n;
    if (c->bytes > (size_t)c->clen || c->bytes > NET_RESPONSE_MAX) { /* more than it promised */
        c->refused = true;
        return false;
    }
    if (c->parsing && jsonr_feed(&c->json, data, n) != JSONR_OK) {
        c->refused = true;
        return false;
    }
    return true;
}

typedef enum { OUT_OK, OUT_OFFLINE, OUT_ERROR } outcome;

/*
 * Does a request. With `kind` >= 0 a 2xx body is parsed by that schema (into `q` or `e`) and must be a whole, valid
 * document; with kind < 0 the body is read and thrown away. Returns how it went.
 */
static outcome exchange(todo_netrepo *n, const char *method, const char *path, const char *body, int kind, net_quests *q, net_events *e, call *c, bool want_body)
{
    todo_sink sink = {on_head, on_body, c};
    todo_http h;

    memset(c, 0, sizeof(*c));
    c->clen = -1;
    c->parsing = kind >= 0 && want_body;
    if (c->parsing) {
        schema_init(&c->sch, (schema_kind)kind, q, e);
        jsonr_init(&c->json, schema_event, &c->sch, NET_RESPONSE_MAX);
    }
    /* the parser must only run on a good status: decide at the head. A wrapper keeps that out of the transport. */
    h = n->cfg.transport.perform(n->cfg.transport.user, method, path, body, body ? strlen(body) : 0, &sink);
    if (h.offline && !c->head_seen) {
        return OUT_OFFLINE;
    }
    if (h.aborted || h.incomplete || c->refused || !c->head_seen) {
        return OUT_ERROR;
    }
    if (c->status < 200 || c->status > 299) {
        return OUT_ERROR;
    }
    if (c->parsing) {
        if (c->bytes != (size_t)c->clen || jsonr_end(&c->json) != JSONR_OK || !schema_complete(&c->sch)) {
            return OUT_ERROR;
        }
    }
    return OUT_OK;
}

/* ---- the answers ---- */

static void fill_result(todo_result *r, const todo_request *req, todo_netrepo *n)
{
    memset(r, 0, sizeof(*r));
    r->kind = req->kind;
    r->id = req->id;
    r->rev = n->last_rev;
}

static void finish(todo_netrepo *n, todo_result *r, outcome o)
{
    uint32_t now = n->cfg.now_ms ? n->cfg.now_ms(n->cfg.now_user) : 0;

    r->ok = o == OUT_OK;
    r->offline = o == OUT_OFFLINE;
    if (o == OUT_OK) {
        n->failures = 0;
        n->blocked_until = 0;
        return;
    }
    n->failures++;
    n->last_offline = o == OUT_OFFLINE;
    {
        int shift = n->failures - 1 > 4 ? 4 : n->failures - 1; /* 1, 2, 4, 8, 16 -> capped */
        uint32_t wait = 1000u << shift;
        n->blocked_until = now + (wait > 15000u ? 15000u : wait);
    }
}

static void note_rev(todo_netrepo *n, int64_t rev)
{
    n->last_rev = rev;
}

/* Reads the quests into a new staging block. */
static outcome read_quests(todo_netrepo *n, net_slot *slot)
{
    net_quests *q = n->cfg.alloc(sizeof(*q));
    call *c = n->cfg.alloc(sizeof(*c));
    outcome o = OUT_ERROR;

    if (q && c) {
        o = exchange(n, "GET", "/quests", NULL, SCHEMA_QUESTS, q, NULL, c, true);
    }
    if (o == OUT_OK) {
        slot->staged = q;
        slot->staged_kind = 1;
        n->quests_rev = q->rev;
        note_rev(n, q->rev);
        q = NULL;
    }
    if (q) {
        n->cfg.release(q);
    }
    if (c) {
        n->cfg.release(c);
    }
    return o;
}

static outcome read_events(todo_netrepo *n, net_slot *slot, int64_t from, int64_t to)
{
    net_events *e = n->cfg.alloc(sizeof(*e));
    call *c = n->cfg.alloc(sizeof(*c));
    char path[96];
    outcome o = OUT_ERROR;

    snprintf(path, sizeof(path), "/events?from=%lld&to=%lld", (long long)from, (long long)to);
    if (e && c) {
        o = exchange(n, "GET", path, NULL, SCHEMA_EVENTS, NULL, e, c, true);
    }
    if (o == OUT_OK) {
        e->from = from;
        e->to = to;
        slot->staged = e;
        slot->staged_kind = 2;
        n->events_rev = e->rev;
        n->win_from = from;
        n->win_to = to;
        n->have_window = true;
        note_rev(n, e->rev);
        e = NULL;
    }
    if (e) {
        n->cfg.release(e);
    }
    if (c) {
        n->cfg.release(c);
    }
    return o;
}

/* GET /changes: the revision, and whether it is not `since` (a negative `since` asks for the revision only). */
static outcome read_changes(todo_netrepo *n, int64_t since, int64_t *rev, bool *changed)
{
    call *c = n->cfg.alloc(sizeof(*c));
    char path[64];
    outcome o = OUT_ERROR;

    if (since >= 0) {
        snprintf(path, sizeof(path), "/changes?since=%lld", (long long)since);
    } else {
        snprintf(path, sizeof(path), "/changes");
    }
    if (c) {
        o = exchange(n, "GET", path, NULL, SCHEMA_CHANGES, NULL, NULL, c, true);
        if (o == OUT_OK) {
            *rev = c->sch.rev;
            *changed = c->sch.changed;
            note_rev(n, c->sch.rev);
        }
        n->cfg.release(c);
    }
    return o;
}

/* Does one request and fills `slot` with its answer. */
static void execute(todo_netrepo *n, const todo_request *req, net_slot *slot)
{
    todo_result *r = &slot->res;
    call *c;
    char path[64], body[40];
    outcome o;

    fill_result(r, req, n);
    switch (req->kind) {
    case TODO_REQ_REFRESH_QUESTS:
        o = read_quests(n, slot);
        break;
    case TODO_REQ_REFRESH_EVENTS:
        o = read_events(n, slot, req->a, req->b);
        break;
    case TODO_REQ_POLL_CHANGES: {
        int64_t rev = 0;
        bool changed = false;
        o = read_changes(n, req->a, &rev, &changed);
        if (o == OUT_OK) {
            r->changed = changed;
            r->rev = rev;
        }
        break;
    }
    case TODO_REQ_SET_QUEST_DONE:
    case TODO_REQ_DELETE_QUEST:
    case TODO_REQ_DELETE_EVENT: {
        const bool quest = req->kind != TODO_REQ_DELETE_EVENT;
        snprintf(path, sizeof(path), "/%s/%ld", quest ? "quests" : "events", (long)req->id);
        snprintf(body, sizeof(body), "{\"state\":\"%s\"}", req->flag ? "done" : "open");
        c = n->cfg.alloc(sizeof(*c));
        o = OUT_ERROR;
        if (c) {
            o = exchange(n, req->kind == TODO_REQ_SET_QUEST_DONE ? "PATCH" : "DELETE", path, req->kind == TODO_REQ_SET_QUEST_DONE ? body : NULL, -1, NULL, NULL, c, false);
            n->cfg.release(c);
        }
        if (o == OUT_OK) { /* what the server holds now comes back with the answer, as the in-memory server does */
            if (quest) {
                (void)read_quests(n, slot);
            } else if (n->have_window) {
                (void)read_events(n, slot, n->win_from, n->win_to);
            }
        }
        break;
    }
    case TODO_REQ_NEW_LINK: {
        static const char *const kinds[] = {"quest", "event", "home"};
        int k = req->link >= 0 && req->link <= 2 ? (int)req->link : 2;
        schema s;
        int64_t rev = 0;
        bool changed;

        snprintf(body, sizeof(body), "{\"kind\":\"%s\"}", kinds[k]);
        c = n->cfg.alloc(sizeof(*c));
        o = OUT_ERROR;
        if (c) {
            o = exchange(n, "POST", "/link", body, SCHEMA_LINK, NULL, NULL, c, true);
            if (o == OUT_OK) {
                s = c->sch;
                memcpy(r->url, s.url, sizeof(r->url));
            }
            n->cfg.release(c);
        }
        if (o == OUT_OK && read_changes(n, -1, &rev, &changed) == OUT_OK) { /* the revision the phone will move */
            r->rev = rev;
        }
        break;
    }
    default:
        o = OUT_ERROR;
        break;
    }
    finish(n, r, o);
    if (o == OUT_OK) {
        r->rev = n->last_rev;
    }
}

/* ---- the todo_repo side (the UI task) ---- */

static bool net_submit(todo_repo *r, const todo_request *req)
{
    todo_netrepo *n = r->impl;
    bool ok = false;

    n->cfg.lock.take(n->cfg.lock.user);
    if (n->q_len < NET_QUEUE) {
        n->queue[(n->q_head + n->q_len) % NET_QUEUE] = *req;
        n->q_len++;
        ok = true;
    }
    n->cfg.lock.give(n->cfg.lock.user);
    if (ok && n->cfg.wake) {
        n->cfg.wake(n->cfg.wake_user);
    }
    return ok;
}

static int64_t min_rev(const todo_netrepo *n)
{
    if (n->quests_rev >= 0 && n->events_rev >= 0) {
        return n->quests_rev < n->events_rev ? n->quests_rev : n->events_rev;
    }
    return n->quests_rev >= 0 ? n->quests_rev : n->events_rev;
}

static bool net_poll(todo_repo *r, todo_store *store, todo_result *out)
{
    todo_netrepo *n = r->impl;
    net_slot slot;
    bool got = false;

    n->cfg.lock.take(n->cfg.lock.user);
    if (n->r_len > 0) {
        slot = n->results[n->r_head];
        n->r_head = (n->r_head + 1) % NET_RESULTS;
        n->r_len--;
        if (slot.staged) {
            n->staged_pending--;
        }
        got = true;
    }
    n->cfg.lock.give(n->cfg.lock.user);
    if (!got) {
        return false;
    }
    if (slot.staged_kind == 1) {
        const net_quests *q = slot.staged;
        int i;
        for (i = 0; i < q->n; i++) {
            store->quests[i] = q->quests[i];
        }
        store->n_quests = q->n;
        store->xp = q->xp;
        store->open = q->open;
        store->done = q->done;
        store->quests_truncated = q->truncated;
        store->rev = min_rev(n);
    } else if (slot.staged_kind == 2) {
        const net_events *e = slot.staged;
        int i;
        for (i = 0; i < e->n; i++) {
            store->events[i] = e->events[i];
        }
        store->n_events = e->n;
        store->events_truncated = e->truncated;
        store->events_from = e->from;
        store->events_to = e->to;
        store->rev = min_rev(n);
    }
    if (slot.staged) {
        n->cfg.release(slot.staged);
    }
    *out = slot.res;
    return true;
}

static const todo_repo_ops OPS = {net_submit, net_poll};

void todo_netrepo_init(todo_netrepo *n, const todo_netrepo_config *cfg)
{
    memset(n, 0, sizeof(*n));
    n->cfg = *cfg;
    n->repo.ops = &OPS;
    n->repo.impl = n;
    n->quests_rev = n->events_rev = -1;
}

/* ---- the network task's side ---- */

bool todo_netrepo_step(todo_netrepo *n)
{
    todo_request req;
    net_slot slot;
    uint32_t now = n->cfg.now_ms ? n->cfg.now_ms(n->cfg.now_user) : 0;
    bool blocked;

    n->cfg.lock.take(n->cfg.lock.user);
    if (n->q_len == 0 || n->r_len >= NET_RESULTS || n->staged_pending > 0) { /* nothing to do, or no room for the answer */
        n->cfg.lock.give(n->cfg.lock.user);
        return false;
    }
    req = n->queue[n->q_head];
    n->q_head = (n->q_head + 1) % NET_QUEUE;
    n->q_len--;
    n->cfg.lock.give(n->cfg.lock.user);

    memset(&slot, 0, sizeof(slot));
    blocked = n->failures > 0 && (int32_t)(now - n->blocked_until) < 0;
    if (blocked) { /* the last attempt failed a moment ago: say so at once, do not wait for the network again */
        fill_result(&slot.res, &req, n);
        slot.res.offline = n->last_offline;
    } else {
        execute(n, &req, &slot);
    }
    n->cfg.lock.take(n->cfg.lock.user);
    n->results[(n->r_head + n->r_len) % NET_RESULTS] = slot;
    n->r_len++;
    if (slot.staged) {
        n->staged_pending++;
    }
    n->cfg.lock.give(n->cfg.lock.user);
    return true;
}
