#define _POSIX_C_SOURCE 200809L

/* The repository against the real server: the contract as the server really keeps it, and the firmware's defences against the
 * server that misbehaves on purpose (FAULT=...). The server is the binary of ../server, started on a free port with a temporary
 * database; the test is skipped (exit 77) if it has not been built. */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "http_posix.h"
#include "todo/netrepo.h"
#include "unity.h"

#ifndef SERVER_BIN
#error "SERVER_BIN must name the server binary"
#endif

void setUp(void) {}
void tearDown(void) {}

#define TOKEN "0123456789abcdef0123456789abcdef"

typedef struct {
    pid_t pid;
    int port;
    char dir[64];
} server;

static server SRV;
static posix_conn CONN;
static todo_netrepo N;
static todo_store S;
static int live;
static uint32_t clock_ms_value;

static int free_port(void)
{
    struct sockaddr_in a;
    socklen_t l = sizeof(a);
    int fd = socket(AF_INET, SOCK_STREAM, 0), port;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TEST_ASSERT_EQUAL_INT(0, bind(fd, (struct sockaddr *)&a, sizeof(a)));
    getsockname(fd, (struct sockaddr *)&a, &l);
    port = ntohs(a.sin_port);
    close(fd);
    return port;
}

static void run_server_command(const char *arg, const char *fault)
{
    pid_t pid;
    fflush(NULL); /* the child would otherwise print what the parent has buffered, again */
    pid = fork();
    if (pid == 0) {
        char db[128], bind_[48], url[48];
        snprintf(db, sizeof(db), "sqlite://%s/t.db?mode=rwc", SRV.dir);
        snprintf(bind_, sizeof(bind_), "127.0.0.1:%d", SRV.port);
        snprintf(url, sizeof(url), "http://127.0.0.1:%d", SRV.port);
        setenv("CYD_TOKEN", TOKEN, 1);
        setenv("PUBLIC_URL", url, 1);
        setenv("BIND", bind_, 1);
        setenv("DATABASE_URL", db, 1);
        if (fault) {
            setenv("FAULT", fault, 1);
            setenv("ALLOW_FAULTS", "1", 1);
        }
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execl(SERVER_BIN, SERVER_BIN, arg, (char *)NULL);
        _exit(127);
    }
    TEST_ASSERT_TRUE(pid > 0);
    if (strcmp(arg, "serve") == 0) {
        SRV.pid = pid;
    } else {
        int st = 0;
        waitpid(pid, &st, 0);
        TEST_ASSERT_TRUE_MESSAGE(WIFEXITED(st) && WEXITSTATUS(st) == 0, "the server's seed command");
    }
}

static void stop_server(void)
{
    if (SRV.pid > 0) {
        int st;
        kill(SRV.pid, SIGKILL);
        waitpid(SRV.pid, &st, 0);
        SRV.pid = 0;
    }
    if (SRV.dir[0]) { /* the temporary database */
        static const char *const files[] = {"t.db", "t.db-shm", "t.db-wal"};
        char path[128];
        size_t i;
        for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
            snprintf(path, sizeof(path), "%s/%s", SRV.dir, files[i]);
            unlink(path);
        }
        rmdir(SRV.dir);
        SRV.dir[0] = '\0';
    }
}

static void *alloc_fn(size_t n)
{
    live++;
    return malloc(n);
}
static void release_fn(void *p)
{
    live--;
    free(p);
}
static void noop(void *u) { (void)u; }
static uint32_t clock_fn(void *u)
{
    (void)u;
    return clock_ms_value;
}

static int link_status;
static bool link_head(void *ctx, int status, long clen)
{
    (void)ctx;
    (void)clen;
    link_status = status;
    return true;
}
static bool link_body(void *ctx, const char *d, size_t n)
{
    (void)ctx;
    (void)d;
    (void)n;
    return true;
}

/* A server on a new port with a seeded database, a transport to it, and a repository. */
static void start(const char *fault, int timeout_ms)
{
    todo_netrepo_config cfg;
    long i;
    char *dir;

    stop_server();
    snprintf(SRV.dir, sizeof(SRV.dir), "/tmp/todo-srv-XXXXXX");
    dir = mkdtemp(SRV.dir);
    TEST_ASSERT_NOT_NULL(dir);
    SRV.port = free_port();
    run_server_command("seed", NULL); /* the seed is written before any fault exists */
    run_server_command("serve", fault);
    memset(&CONN, 0, sizeof(CONN));
    snprintf(CONN.host, sizeof(CONN.host), "127.0.0.1");
    CONN.port = SRV.port;
    snprintf(CONN.token, sizeof(CONN.token), "%s", TOKEN);
    CONN.timeout_ms = 5000;
    for (i = 0; i < 100; i++) { /* wait for /health */
        todo_sink none = {link_head, link_body, NULL};
        CONN.timeout_ms = 200;
        if (posix_perform(&CONN, "GET", "/health", NULL, 0, &none).status == 0) {
            struct timespec t = {0, 50 * 1000 * 1000};
            nanosleep(&t, NULL);
            continue;
        }
        break;
    }
    CONN.timeout_ms = timeout_ms;
    CONN.delivered = 0;
    memset(&cfg, 0, sizeof(cfg));
    cfg.transport.perform = posix_perform;
    cfg.transport.user = &CONN;
    cfg.lock.take = noop;
    cfg.lock.give = noop;
    cfg.now_ms = clock_fn;
    cfg.alloc = alloc_fn;
    cfg.release = release_fn;
    clock_ms_value = 1000000;
    live = 0;
    todo_netrepo_init(&N, &cfg);
    memset(&S, 0, sizeof(S));
}

static todo_result ask(todo_request q)
{
    todo_result r;
    TEST_ASSERT_TRUE(todo_repo_submit(&N.repo, &q));
    while (todo_netrepo_step(&N)) {
    }
    TEST_ASSERT_TRUE(todo_repo_poll(&N.repo, &S, &r));
    return r;
}

static todo_request req(todo_req_kind k, int32_t id)
{
    todo_request q = todo_req(k);
    q.id = id;
    return q;
}

static todo_request events_window(void)
{
    todo_request q = todo_req(TODO_REQ_REFRESH_EVENTS);
    int64_t now = (int64_t)time(NULL);
    q.a = now - 7 * 86400;
    q.b = now + 30 * 86400;
    return q;
}

/* ---- the contract, as the real server keeps it ---- */

static void test_the_demo_data_of_the_server_arrives_complete_and_in_order(void)
{
    todo_result r;
    int i;
    start(NULL, 5000);
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(6, S.n_quests);
    TEST_ASSERT_FALSE(S.quests_truncated);
    TEST_ASSERT_EQUAL_INT32(4, S.open);
    TEST_ASSERT_EQUAL_INT32(2, S.done);
    TEST_ASSERT_TRUE(S.rev > 0);
    for (i = 0; i < 4; i++) {
        TEST_ASSERT_FALSE_MESSAGE(S.quests[i].done, "open quests first");
    }
    TEST_ASSERT_TRUE(S.quests[4].done && S.quests[5].done);
    TEST_ASSERT_EQUAL_STRING("Slay the inbox", S.quests[0].title);
    TEST_ASSERT_NOT_EQUAL(0, S.quests[0].due);
    r = ask(events_window());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(4, S.n_events);
    TEST_ASSERT_TRUE(S.events[0].start < S.events[1].start);
    TEST_ASSERT_EQUAL_INT(0, live);
    stop_server();
}

static void test_completing_and_reopening_a_quest_changes_the_servers_xp_by_ten_times_its_difficulty(void)
{
    todo_request q;
    todo_result r;
    int32_t id, xp, diff;
    start(NULL, 5000);
    ask(req(TODO_REQ_REFRESH_QUESTS, 0));
    id = S.quests[0].id;
    diff = S.quests[0].difficulty;
    xp = S.xp;
    q = req(TODO_REQ_SET_QUEST_DONE, id);
    q.flag = true;
    r = ask(q);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT32(xp + 10 * diff, S.xp);
    TEST_ASSERT_TRUE(todo_find_quest(&S, id)->done);
    TEST_ASSERT_EQUAL_INT32(3, S.open);
    q.flag = false;
    r = ask(q);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT32(xp, S.xp);
    TEST_ASSERT_FALSE(todo_find_quest(&S, id)->done);
    stop_server();
}

static void test_deleting_a_quest_and_an_event_and_deleting_what_is_not_there(void)
{
    todo_result r;
    int32_t qid, eid;
    start(NULL, 5000);
    ask(req(TODO_REQ_REFRESH_QUESTS, 0));
    ask(events_window());
    qid = S.quests[1].id;
    eid = S.events[0].id;
    r = ask(req(TODO_REQ_DELETE_QUEST, qid));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_NULL(todo_find_quest(&S, qid));
    TEST_ASSERT_EQUAL_INT(5, S.n_quests);
    r = ask(req(TODO_REQ_DELETE_EVENT, eid));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_NULL(todo_find_event(&S, eid));
    TEST_ASSERT_EQUAL_INT(3, S.n_events);
    clock_ms_value += 20000;
    r = ask(req(TODO_REQ_DELETE_QUEST, qid)); /* again: a 404 */
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.offline);
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, S.n_quests, "the cache is as it was");
    stop_server();
}

static void test_a_write_by_somebody_else_is_seen_by_the_revision_poll(void)
{
    todo_request q = todo_req(TODO_REQ_POLL_CHANGES);
    todo_result r;
    posix_conn other = CONN;
    todo_sink none = {link_head, link_body, NULL};
    start(NULL, 5000);
    ask(req(TODO_REQ_REFRESH_QUESTS, 0));
    q.a = S.rev;
    r = ask(q);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.changed);
    other = CONN; /* the phone page, or another device, writing */
    TEST_ASSERT_EQUAL_INT(201, posix_perform(&other, "POST", "/quests", "{\"title\":\"from elsewhere\"}", 26, &none).status);
    r = ask(q);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.changed);
    TEST_ASSERT_TRUE(r.rev > S.rev);
    ask(req(TODO_REQ_REFRESH_QUESTS, 0));
    TEST_ASSERT_EQUAL_INT(7, S.n_quests);
    stop_server();
}

static void test_a_one_time_link_is_made_with_the_servers_revision_and_works_once(void)
{
    todo_request q = todo_req(TODO_REQ_NEW_LINK);
    todo_result r;
    char path[96];
    todo_sink sink = {link_head, link_body, NULL};
    start(NULL, 5000);
    ask(req(TODO_REQ_REFRESH_QUESTS, 0));
    q.link = TODO_LINK_QUEST;
    r = ask(q);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(strlen(r.url) <= TODO_LINK_URL_MAX);
    TEST_ASSERT_EQUAL_STRING_LEN("http://127.0.0.1:", r.url, 17);
    TEST_ASSERT_NOT_NULL(strstr(r.url, "/m/"));
    TEST_ASSERT_EQUAL_INT64(S.rev, r.rev);
    snprintf(path, sizeof(path), "%s", strstr(r.url, "/m/"));
    posix_perform(&CONN, "GET", path, NULL, 0, &sink);
    TEST_ASSERT_EQUAL_INT_MESSAGE(303, link_status, "the phone is let in");
    posix_perform(&CONN, "GET", path, NULL, 0, &sink);
    TEST_ASSERT_EQUAL_INT_MESSAGE(404, link_status, "and only once");
    stop_server();
}

static void test_a_wrong_token_is_an_error_and_not_offline_and_the_wait_follows(void)
{
    todo_result r;
    start(NULL, 5000);
    snprintf(CONN.token, sizeof(CONN.token), "a-token-that-is-not-the-servers-0000");
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0));
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.offline);
    TEST_ASSERT_EQUAL_INT(0, S.n_quests);
    {
        int before = CONN.connections;
        r = ask(req(TODO_REQ_REFRESH_QUESTS, 0));
        TEST_ASSERT_FALSE(r.ok);
        TEST_ASSERT_EQUAL_INT_MESSAGE(before, CONN.connections, "the second try is not even sent");
    }
    stop_server();
}

static void test_a_server_that_goes_away_is_offline_and_comes_back_when_it_does(void)
{
    todo_result r;
    int port;
    start(NULL, 2000);
    TEST_ASSERT_TRUE(ask(req(TODO_REQ_REFRESH_QUESTS, 0)).ok);
    port = SRV.port;
    kill(SRV.pid, SIGKILL); /* the process only: its database stays for the restart */
    waitpid(SRV.pid, NULL, 0);
    SRV.pid = 0;
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0));
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.offline);
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, S.n_quests, "what it had is kept");
    SRV.port = port;
    run_server_command("serve", NULL);
    {
        struct timespec t = {0, 700 * 1000 * 1000};
        nanosleep(&t, NULL);
    }
    clock_ms_value += 2000;
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0));
    TEST_ASSERT_TRUE_MESSAGE(r.ok, "after the wait it tries again, and the server is back");
    stop_server();
}

/* ---- a server that lies, on purpose ---- */

static void fault_run(const char *fault, int timeout_ms, bool expect_offline)
{
    todo_request q = todo_req(TODO_REQ_POLL_CHANGES);
    todo_result r;
    char msg[96];
    int i;

    start(fault, timeout_ms);
    S.n_quests = 1;
    snprintf(S.quests[0].title, sizeof(S.quests[0].title), "kept");
    S.n_events = 1;
    snprintf(S.events[0].title, sizeof(S.events[0].title), "kept too");
    snprintf(msg, sizeof(msg), "fault %s: quests", fault);
    for (i = 0; i < 4; i++) { /* every kind of request, with the wait between them taken away */
        clock_ms_value += 20000;
        switch (i) {
        case 0:
            r = ask(req(TODO_REQ_REFRESH_QUESTS, 0));
            break;
        case 1:
            r = ask(events_window());
            break;
        case 2:
            q.a = 1;
            r = ask(q);
            break;
        default: {
            todo_request l = todo_req(TODO_REQ_NEW_LINK);
            r = ask(l);
            break;
        }
        }
        snprintf(msg, sizeof(msg), "fault %s, request %d", fault, i);
        TEST_ASSERT_FALSE_MESSAGE(r.ok, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(expect_offline ? 1 : 0, r.offline ? 1 : 0, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.url, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("kept", S.quests[0].title, "the cache is never touched by a lie");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("kept too", S.events[0].title, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, live, "no staging block is left behind");
    }
}

static void test_a_slow_server_is_offline_after_the_timeout_and_the_wait_is_not_sat_through(void)
{
    time_t t0 = time(NULL);
    fault_run("slow", 700, true);
    TEST_ASSERT_TRUE_MESSAGE(time(NULL) - t0 < 20, "four requests of 0.7 s, not four of 20 s");
    stop_server();
}

static void test_a_body_cut_half_way_is_an_error(void)
{
    fault_run("truncate", 3000, false);
    stop_server();
}

static void test_a_huge_body_is_not_read_past_the_limit(void)
{
    fault_run("huge", 3000, false);
    TEST_ASSERT_TRUE_MESSAGE(CONN.delivered < 4 * (NET_RESPONSE_MAX + 4096), "at most the limit and one piece per request, of 200 KB offered");
    stop_server();
}

static void test_garbage_chunked_deep_and_wrongtype_and_500_are_errors(void)
{
    fault_run("garbage", 3000, false);
    stop_server();
    fault_run("chunked", 3000, false);
    stop_server();
    fault_run("deep", 3000, false);
    stop_server();
    fault_run("wrongtype", 3000, false);
    stop_server();
    fault_run("error500", 3000, false);
    stop_server();
}

static void cleanup(void)
{
    stop_server();
}

int main(void)
{
    struct stat st;

    if (stat(SERVER_BIN, &st) != 0) {
        printf("test_netrepo_server: %s has not been built (cargo build in server/): SKIPPED\n", SERVER_BIN);
        return 77;
    }
    atexit(cleanup);
    UNITY_BEGIN();
    RUN_TEST(test_the_demo_data_of_the_server_arrives_complete_and_in_order);
    RUN_TEST(test_completing_and_reopening_a_quest_changes_the_servers_xp_by_ten_times_its_difficulty);
    RUN_TEST(test_deleting_a_quest_and_an_event_and_deleting_what_is_not_there);
    RUN_TEST(test_a_write_by_somebody_else_is_seen_by_the_revision_poll);
    RUN_TEST(test_a_one_time_link_is_made_with_the_servers_revision_and_works_once);
    RUN_TEST(test_a_wrong_token_is_an_error_and_not_offline_and_the_wait_follows);
    RUN_TEST(test_a_server_that_goes_away_is_offline_and_comes_back_when_it_does);
    RUN_TEST(test_a_slow_server_is_offline_after_the_timeout_and_the_wait_is_not_sat_through);
    RUN_TEST(test_a_body_cut_half_way_is_an_error);
    RUN_TEST(test_a_huge_body_is_not_read_past_the_limit);
    RUN_TEST(test_garbage_chunked_deep_and_wrongtype_and_500_are_errors);
    return UNITY_END();
}
