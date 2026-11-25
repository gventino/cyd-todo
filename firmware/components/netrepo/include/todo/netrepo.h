#ifndef TODO_NETREPO_H
#define TODO_NETREPO_H

/*
 * The repository that talks to the server (docs/api.md): the second implementation of todo_repo, next to the in-memory one.
 *
 * Two sides meet in it and nothing else: the UI task calls todo_repo_submit() and todo_repo_poll() (they never wait for the
 * network), and a network task calls todo_netrepo_step() in a loop. Between them a short queue of requests and a short queue
 * of answers, guarded by a lock the platform supplies. A list that was read is **parsed into a staging block while it
 * arrives** (the bytes are never held: one JSON reader, fed piece by piece) and moved into the UI's cache only inside
 * todo_repo_poll(), on the UI task: the UI never sees half an answer and the network task never touches its memory.
 *
 * Nothing the server sends is trusted. A response that has no Content-Length, promises or delivers more than 16 KB, stops
 * short, is not JSON of the shape of the contract, or breaks a limit of the contract, is refused whole and reported as an
 * error. A server that cannot be reached is reported as offline. After a failure requests are answered at once, without
 * touching the network, until a back-off time has passed (1, 2, 4, 8, then 15 seconds).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "todo/repo.h"

#define NET_RESPONSE_MAX 16384
#define NET_QUEUE 8
#define NET_RESULTS 4

/* What a transport reports of an HTTP response while it arrives. A callback that returns false stops the transfer. */
typedef struct {
    bool (*head)(void *ctx, int status, long content_length); /* content_length is -1 when the server did not say */
    bool (*body)(void *ctx, const char *data, size_t n);
    void *ctx;
} todo_sink;

typedef struct {
    int status;       /* 0 when no response was read */
    bool offline;     /* could not connect, or no answer at all within the time */
    bool aborted;     /* a callback of the sink stopped the transfer */
    bool incomplete;  /* the connection ended, or time ran out, before the whole body arrived */
} todo_http;

/*
 * One HTTP/1.1 request to the server (the transport knows its address and its token; the token never passes through here).
 * It enforces the total time (15 s), sends `body` (may be NULL) as JSON, and calls the sink with the status line's status and the
 * Content-Length of the head, then with the body in pieces.
 */
typedef struct {
    todo_http (*perform)(void *user, const char *method, const char *path, const char *body, size_t body_len, const todo_sink *sink);
    void *user;
} todo_transport;

typedef struct {
    void (*take)(void *user);
    void (*give)(void *user);
    void *user;
} todo_lock;

typedef struct {
    todo_transport transport;
    todo_lock lock;                       /* guards the two queues: the platform's mutex (a no-op where one task does both) */
    uint32_t (*now_ms)(void *user);       /* for the back-off */
    void *now_user;
    void (*wake)(void *user);             /* a request was queued: the network task should run (may be NULL) */
    void *wake_user;
    void *(*alloc)(size_t n);             /* the staging blocks (~17 KB, one at a time) */
    void (*release)(void *p);
} todo_netrepo_config;

typedef struct {
    todo_result res;
    void *staged;          /* a list that goes into the store at poll, or NULL */
    int staged_kind;       /* 0 none, 1 quests, 2 events */
} net_slot;

typedef struct {
    todo_repo repo; /* hand &n->repo to the UI */
    todo_netrepo_config cfg;

    todo_request queue[NET_QUEUE];
    int q_head, q_len;
    net_slot results[NET_RESULTS];
    int r_head, r_len;
    int staged_pending; /* answers that carry a staging block (at most one is allowed at a time) */

    int failures;       /* in a row */
    uint32_t blocked_until;
    bool last_offline;
    bool have_window;
    int64_t win_from, win_to;
    int64_t last_rev;
    int64_t quests_rev, events_rev; /* the revision of what the cache holds (-1: not read yet) */
} todo_netrepo;

void todo_netrepo_init(todo_netrepo *n, const todo_netrepo_config *cfg);

/*
 * The network task: takes the next request, if there is room for its answer, does it, and queues the answer. Returns true if
 * it did something (call again), false if there was nothing to do.
 */
bool todo_netrepo_step(todo_netrepo *n);

#endif /* TODO_NETREPO_H */
