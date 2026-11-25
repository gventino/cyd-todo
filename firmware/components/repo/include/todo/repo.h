#ifndef TODO_REPO_H
#define TODO_REPO_H

/*
 * Where the data comes from, as the UI sees it: requests that return at once and answers that arrive later,
 * one per frame, through todo_repo_poll(). The screen never waits for the network and never knows if the
 * answer came from the server (ESP32, A5) or from memory (the PC, the tests, the demo).
 *
 * The UI owns the cache (todo_store). An answer is applied to it inside todo_repo_poll(), on the caller's
 * task: a repository that works on another task keeps its own staging area and copies at that moment.
 */

#include <stdbool.h>
#include <stdint.h>

#include "todo/model.h"

#define TODO_LINK_URL_MAX 61 /* PUBLIC_URL (32) + "/m/" + 26 characters: what fits a QR code of version 4 */

typedef enum {
    TODO_REQ_REFRESH_QUESTS,
    TODO_REQ_REFRESH_EVENTS, /* a = from, b = to (UTC epoch seconds) */
    TODO_REQ_SET_QUEST_DONE, /* id, flag */
    TODO_REQ_DELETE_QUEST,   /* id */
    TODO_REQ_DELETE_EVENT,   /* id */
    TODO_REQ_POLL_CHANGES,   /* a = the revision the cache was read at */
    TODO_REQ_NEW_LINK        /* link: which page the phone should land on */
} todo_req_kind;

typedef enum { TODO_LINK_QUEST, TODO_LINK_EVENT, TODO_LINK_HOME } todo_link_kind;

typedef struct {
    todo_req_kind kind;
    int32_t id;
    int64_t a, b;
    bool flag;
    todo_link_kind link;
} todo_request;

typedef struct {
    todo_req_kind kind; /* what this answers */
    int32_t id;         /* the id it was about, if any */
    bool ok;
    bool offline;       /* not ok because there is no server to talk to (as opposed to an error from it) */
    bool changed;       /* POLL_CHANGES: the server's revision is not the one asked about */
    int64_t rev;        /* the server's revision */
    char url[TODO_LINK_URL_MAX + 1]; /* NEW_LINK */
} todo_result;

typedef struct todo_repo todo_repo;

typedef struct {
    /* Queues a request. False if there is no room: the caller tries again later. */
    bool (*submit)(todo_repo *r, const todo_request *req);
    /* Gives the next answer, if one is ready, applying it to `store`. */
    bool (*poll)(todo_repo *r, todo_store *store, todo_result *out);
} todo_repo_ops;

struct todo_repo {
    const todo_repo_ops *ops;
    void *impl;
};

static inline bool todo_repo_submit(todo_repo *r, const todo_request *req)
{
    return r && r->ops && r->ops->submit(r, req);
}

static inline bool todo_repo_poll(todo_repo *r, todo_store *store, todo_result *out)
{
    return r && r->ops && r->ops->poll(r, store, out);
}

/* Shorthands for the requests that take few arguments. */
static inline todo_request todo_req(todo_req_kind kind)
{
    todo_request q = {kind, 0, 0, 0, false, TODO_LINK_HOME};
    return q;
}

#endif /* TODO_REPO_H */
