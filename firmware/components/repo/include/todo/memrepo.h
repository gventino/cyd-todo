#ifndef TODO_MEMREPO_H
#define TODO_MEMREPO_H

/*
 * The repository that lives in memory: a tiny pretend server. Used by the PC build, the tests and the demo mode.
 * It can be slow, fail, go offline and change by itself ("the phone saved something"), so that the screens'
 * waiting, error and refresh paths are exercised without a network.
 */

#include "todo/repo.h"

#define TODO_MEM_QUEUE 8

typedef struct {
    todo_repo repo; /* hand &mem.repo to the UI */

    /* the pretend server */
    todo_quest quests[TODO_MAX_QUESTS * 2];
    int n_quests;
    todo_event events[TODO_MAX_EVENTS * 2];
    int n_events;
    int32_t next_id;
    int64_t rev;
    int link_counter;

    /* behavior */
    int latency;   /* polls that pass before an answer is ready (0: the next poll) */
    int fail_next; /* how many of the next requests are answered with an error */
    bool offline;  /* every request is answered "offline" */

    /* the queue */
    todo_request queue[TODO_MEM_QUEUE];
    int wait[TODO_MEM_QUEUE];
    int head, len;

    int64_t now;  /* the clock the demo data is relative to */
} todo_memrepo;

/* An empty server. */
void todo_memrepo_init(todo_memrepo *m, int64_t now);
/* The demo data: the same six quests and four events as `cyd-todo-server seed`, relative to `now`. */
void todo_memrepo_seed_demo(todo_memrepo *m);

/* Direct changes of the pretend server, as if somebody else (the phone) had made them. They move the revision. */
int32_t todo_memrepo_add_quest(todo_memrepo *m, const char *title, const char *notes, int difficulty, int32_t due);
int32_t todo_memrepo_add_event(todo_memrepo *m, const char *title, const char *description, int64_t start, int64_t end);

#endif /* TODO_MEMREPO_H */
