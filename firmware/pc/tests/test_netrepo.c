/* The repository that talks to the server, against a scripted transport: every request, every outcome, every way a server
 * can break the contract. The store of the UI is never touched by an answer that was refused. */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "todo/netrepo.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- a transport that plays a script ---- */

typedef struct {
    const char *method, *path;
    const char *body_expected; /* NULL: not checked */
    int status;
    const char *body;
    long clen;        /* -2: the length of body; -1: none (chunked); else as given */
    bool offline, incomplete;
    bool offline_mid; /* the connection dies (reported as offline) after half of the body */
    size_t chunk;     /* 0: one piece */
} step_t;

static step_t script[16];
static int n_script, at_script;
static int performed;
static char last_body[128];

static todo_http perform(void *user, const char *method, const char *path, const char *body, size_t body_len, const todo_sink *sink)
{
    const step_t *s;
    todo_http h = {0, false, false, false};
    char msg[160];
    size_t len, i, c;
    long clen;

    (void)user;
    (void)body_len;
    performed++;
    snprintf(last_body, sizeof(last_body), "%s", body ? body : "");
    snprintf(msg, sizeof(msg), "unexpected request #%d: %s %s", at_script, method, path);
    TEST_ASSERT_TRUE_MESSAGE(at_script < n_script, msg);
    s = &script[at_script++];
    snprintf(msg, sizeof(msg), "request #%d: expected %s %s, got %s %s", at_script - 1, s->method, s->path, method, path);
    TEST_ASSERT_TRUE_MESSAGE(strcmp(s->method, method) == 0 && strcmp(s->path, path) == 0, msg);
    if (s->body_expected) {
        TEST_ASSERT_EQUAL_STRING(s->body_expected, body ? body : "");
    }
    if (s->offline) {
        h.offline = true;
        return h;
    }
    len = s->body ? strlen(s->body) : 0;
    clen = s->clen == -2 ? (long)len : s->clen;
    h.status = s->status;
    if (!sink->head(sink->ctx, s->status, clen)) {
        h.aborted = true;
        return h;
    }
    c = s->chunk ? s->chunk : (len ? len : 1);
    for (i = 0; i < len; i += c) {
        size_t piece = len - i < c ? len - i : c;
        if (s->incomplete && i >= len / 2) {
            h.incomplete = true;
            return h;
        }
        if (s->offline_mid && i >= len / 2) {
            h.offline = true;
            return h;
        }
        if (!sink->body(sink->ctx, s->body + i, piece)) {
            h.aborted = true;
            return h;
        }
    }
    return h;
}

static uint32_t now_ms;
static uint32_t clock_fn(void *u)
{
    (void)u;
    return now_ms;
}

static int live, peak_live;
static size_t live_bytes, peak_bytes;
typedef struct {
    size_t n;
    union {
        long long l;
        long double d;
        void *p;
    } pad; /* alignment, as C99 has no max_align_t */
} hdr;
static void *alloc_fn(size_t n)
{
    hdr *h = malloc(sizeof(hdr) + n);
    h->n = n;
    live++;
    live_bytes += n;
    peak_live = live > peak_live ? live : peak_live;
    peak_bytes = live_bytes > peak_bytes ? live_bytes : peak_bytes;
    return h + 1;
}
static void release_fn(void *p)
{
    hdr *h = (hdr *)p - 1;
    live--;
    live_bytes -= h->n;
    free(h);
}
static void noop(void *u) { (void)u; }

static todo_netrepo N;
static todo_store S;

static void fresh(void)
{
    todo_netrepo_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.transport.perform = perform;
    cfg.lock.take = noop;
    cfg.lock.give = noop;
    cfg.now_ms = clock_fn;
    cfg.alloc = alloc_fn;
    cfg.release = release_fn;
    memset(script, 0, sizeof(script));
    n_script = at_script = performed = 0;
    now_ms = 100000;
    live = peak_live = 0;
    live_bytes = peak_bytes = 0;
    todo_netrepo_init(&N, &cfg);
    memset(&S, 0, sizeof(S));
}

static void add(const char *method, const char *path, int status, const char *body)
{
    step_t *s = &script[n_script++];
    s->method = method;
    s->path = path;
    s->status = status;
    s->body = body;
    s->clen = -2;
}

static todo_result ask(todo_request q)
{
    todo_result r;
    TEST_ASSERT_TRUE(todo_repo_submit(&N.repo, &q));
    while (todo_netrepo_step(&N)) {
    }
    TEST_ASSERT_TRUE_MESSAGE(todo_repo_poll(&N.repo, &S, &r), "an answer was queued");
    return r;
}

static todo_request req(todo_req_kind k, int32_t id, int64_t a, int64_t b)
{
    todo_request q = todo_req(k);
    q.id = id;
    q.a = a;
    q.b = b;
    return q;
}

/* ---- bodies of the contract ---- */

#define Q1 "{\"id\":7,\"title\":\"Slay the inbox\",\"notes\":\"line 1\\nline 2\",\"difficulty\":3,\"due\":\"2026-12-04\",\"state\":\"open\",\"done_at\":null,\"updated_at\":1795600000}"
#define Q2 "{\"id\":8,\"title\":\"caf\\u00e9\",\"notes\":\"\",\"difficulty\":1,\"due\":null,\"state\":\"done\",\"done_at\":1795600001,\"updated_at\":1795600002}"
#define QUESTS "{\"rev\":41,\"xp\":270,\"open\":1,\"done\":1,\"truncated\":false,\"quests\":[" Q1 "," Q2 "]}"
#define E1 "{\"id\":3,\"title\":\"Dentist\",\"description\":\"Bring the X-rays\",\"start\":1796230800,\"end\":1796234400,\"updated_at\":1795600000}"
#define EVENTS "{\"rev\":40,\"truncated\":false,\"events\":[" E1 "]}"
#define CHANGES "{\"rev\":42,\"changed\":true}"
#define LINK "{\"url\":\"http://192.168.1.50:8080/m/K3H9Q7ZB2VN4X8D5WMT6RYE1GA\",\"expires_in\":600}"

static void test_the_quests_are_read_into_the_store_field_by_field(void)
{
    todo_result r;
    fresh();
    add("GET", "/quests", 200, QUESTS);
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.offline);
    TEST_ASSERT_EQUAL_INT64(41, r.rev);
    TEST_ASSERT_EQUAL_INT(2, S.n_quests);
    TEST_ASSERT_EQUAL_INT32(7, S.quests[0].id);
    TEST_ASSERT_EQUAL_STRING("Slay the inbox", S.quests[0].title);
    TEST_ASSERT_EQUAL_STRING("line 1\nline 2", S.quests[0].notes);
    TEST_ASSERT_EQUAL_INT(3, S.quests[0].difficulty);
    TEST_ASSERT_EQUAL_INT32(20261204, S.quests[0].due);
    TEST_ASSERT_FALSE(S.quests[0].done);
    TEST_ASSERT_EQUAL_INT64(0, S.quests[0].done_at);
    TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", S.quests[1].title);
    TEST_ASSERT_EQUAL_INT32(0, S.quests[1].due);
    TEST_ASSERT_TRUE(S.quests[1].done);
    TEST_ASSERT_EQUAL_INT64(1795600001, S.quests[1].done_at);
    TEST_ASSERT_EQUAL_INT32(270, S.xp);
    TEST_ASSERT_EQUAL_INT32(1, S.open);
    TEST_ASSERT_EQUAL_INT32(1, S.done);
    TEST_ASSERT_FALSE(S.quests_truncated);
    TEST_ASSERT_EQUAL_INT64(41, S.rev);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, live, "every staging block is given back");
}

static void test_the_events_of_a_window_are_read_and_the_window_is_remembered(void)
{
    todo_result r;
    fresh();
    add("GET", "/events?from=1795000000&to=1797000000", 200, EVENTS);
    r = ask(req(TODO_REQ_REFRESH_EVENTS, 0, 1795000000, 1797000000));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(1, S.n_events);
    TEST_ASSERT_EQUAL_STRING("Dentist", S.events[0].title);
    TEST_ASSERT_EQUAL_STRING("Bring the X-rays", S.events[0].description);
    TEST_ASSERT_EQUAL_INT64(1796230800, S.events[0].start);
    TEST_ASSERT_EQUAL_INT64(1796234400, S.events[0].end);
    TEST_ASSERT_EQUAL_INT64(1795000000, S.events_from);
    TEST_ASSERT_EQUAL_INT64(1797000000, S.events_to);
    TEST_ASSERT_EQUAL_INT(0, live);
}

static void test_the_cache_revision_is_the_older_of_the_two_lists(void)
{
    fresh();
    add("GET", "/quests", 200, QUESTS);
    add("GET", "/events?from=1&to=2", 200, EVENTS);
    ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_EQUAL_INT64(41, S.rev);
    ask(req(TODO_REQ_REFRESH_EVENTS, 0, 1, 2));
    TEST_ASSERT_EQUAL_INT64_MESSAGE(40, S.rev, "the events were read at 40: a poll must see what happened since");
}

static void test_changes_are_asked_since_the_revision_and_say_so(void)
{
    todo_result r;
    fresh();
    add("GET", "/changes?since=41", 200, CHANGES);
    r = ask(req(TODO_REQ_POLL_CHANGES, 0, 41, 0));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.changed);
    TEST_ASSERT_EQUAL_INT64(42, r.rev);
    fresh();
    add("GET", "/changes?since=42", 200, "{\"rev\":42,\"changed\":false}");
    r = ask(req(TODO_REQ_POLL_CHANGES, 0, 42, 0));
    TEST_ASSERT_FALSE(r.changed);
}

static void test_completing_a_quest_patches_it_and_brings_back_the_list(void)
{
    todo_request q = req(TODO_REQ_SET_QUEST_DONE, 7, 0, 0);
    todo_result r;
    fresh();
    q.flag = true;
    add("PATCH", "/quests/7", 200, Q1);
    script[0].body_expected = "{\"state\":\"done\"}";
    add("GET", "/quests", 200, QUESTS);
    r = ask(q);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT32(7, r.id);
    TEST_ASSERT_EQUAL_INT(2, S.n_quests);
    TEST_ASSERT_EQUAL_INT32(270, S.xp);
    fresh();
    q.flag = false;
    add("PATCH", "/quests/7", 200, Q1);
    script[0].body_expected = "{\"state\":\"open\"}";
    add("GET", "/quests", 200, QUESTS);
    TEST_ASSERT_TRUE(ask(q).ok);
}

static void test_deleting_a_quest_and_an_event(void)
{
    todo_result r;
    fresh();
    add("DELETE", "/quests/7", 204, "");
    add("GET", "/quests", 200, QUESTS);
    r = ask(req(TODO_REQ_DELETE_QUEST, 7, 0, 0));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(2, S.n_quests);
    /* an event: the window that was read is read again */
    fresh();
    add("GET", "/events?from=10&to=20", 200, EVENTS);
    ask(req(TODO_REQ_REFRESH_EVENTS, 0, 10, 20));
    add("DELETE", "/events/3", 204, "");
    add("GET", "/events?from=10&to=20", 200, "{\"rev\":43,\"truncated\":false,\"events\":[]}");
    r = ask(req(TODO_REQ_DELETE_EVENT, 3, 0, 0));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(0, S.n_events);
    /* with no window known there is nothing to read again */
    fresh();
    add("DELETE", "/events/3", 204, "");
    r = ask(req(TODO_REQ_DELETE_EVENT, 3, 0, 0));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(1, performed);
}

static void test_a_write_that_worked_is_ok_even_if_reading_the_list_back_failed(void)
{
    todo_request q = req(TODO_REQ_SET_QUEST_DONE, 7, 0, 0);
    todo_result r;
    fresh();
    S.n_quests = 1;
    snprintf(S.quests[0].title, sizeof(S.quests[0].title), "kept");
    add("PATCH", "/quests/7", 200, Q1);
    add("GET", "/quests", 500, "{\"error\":\"internal\",\"message\":\"x\"}");
    r = ask(q);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, "the server did write it");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("kept", S.quests[0].title, "and the cache is as it was");
}

static void test_a_link_is_asked_for_by_kind_and_comes_with_the_revision_the_phone_will_move(void)
{
    static const char *const bodies[] = {"{\"kind\":\"quest\"}", "{\"kind\":\"event\"}", "{\"kind\":\"home\"}"};
    todo_result r;
    int k;
    for (k = 0; k < 3; k++) {
        todo_request q = todo_req(TODO_REQ_NEW_LINK);
        fresh();
        q.link = (todo_link_kind)k;
        add("POST", "/link", 201, LINK);
        script[0].body_expected = bodies[k];
        add("GET", "/changes", 200, "{\"rev\":77,\"changed\":true}");
        r = ask(q);
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL_STRING("http://192.168.1.50:8080/m/K3H9Q7ZB2VN4X8D5WMT6RYE1GA", r.url);
        TEST_ASSERT_EQUAL_INT64(77, r.rev);
    }
}

static void test_a_server_that_cannot_be_reached_is_offline_and_not_an_error(void)
{
    todo_result r;
    fresh();
    n_script = 1;
    script[0].method = "GET";
    script[0].path = "/quests";
    script[0].offline = true;
    S.n_quests = 3;
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.offline);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, S.n_quests, "the cache is left alone");
}

static void test_a_refusal_by_the_server_is_an_error(void)
{
    static const int codes[] = {400, 401, 403, 404, 413, 422, 429, 500, 503, 301, 302};
    size_t i;
    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        todo_result r;
        fresh();
        add("GET", "/quests", codes[i], "{\"error\":\"nope\",\"message\":\"no\"}");
        r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
        TEST_ASSERT_FALSE(r.ok);
        TEST_ASSERT_FALSE_MESSAGE(r.offline, "the server answered");
        TEST_ASSERT_EQUAL_INT(0, S.n_quests);
        TEST_ASSERT_EQUAL_INT(0, live);
    }
}

/* ---- the contract broken, in every way: refused whole, the cache untouched ---- */

static void refused(const char *what, const char *body)
{
    todo_result r;
    fresh();
    S.n_quests = 1;
    snprintf(S.quests[0].title, sizeof(S.quests[0].title), "before");
    S.xp = 55;
    add("GET", "/quests", 200, body);
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_FALSE_MESSAGE(r.ok, what);
    TEST_ASSERT_FALSE_MESSAGE(r.offline, what);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("before", S.quests[0].title, what);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(55, S.xp, what);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, live, what);
}

static const char *item(char *dst, size_t cap, const char *field_replaced_by)
{
    snprintf(dst, cap, "{\"id\":1,\"title\":\"t\",\"notes\":\"\",\"difficulty\":2,\"due\":null,\"state\":\"open\",\"done_at\":null,\"updated_at\":5%s}", field_replaced_by);
    return dst;
}

static void doc(char *dst, size_t cap, const char *items)
{
    snprintf(dst, cap, "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"truncated\":false,\"quests\":[%s]}", items);
}

static void test_every_way_of_breaking_the_contract_is_refused_whole(void)
{
    static char b[4096], it[700];
    refused("not json", "<html>nope</html>");
    refused("empty", "");
    refused("an array", "[]");
    refused("a string", "\"quests\"");
    refused("no quests", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0}");
    refused("no rev", "{\"xp\":0,\"open\":0,\"done\":0,\"quests\":[]}");
    refused("rev as a string", "{\"rev\":\"1\",\"xp\":0,\"open\":0,\"done\":0,\"quests\":[]}");
    refused("negative xp", "{\"rev\":1,\"xp\":-1,\"open\":0,\"done\":0,\"quests\":[]}");
    refused("float xp", "{\"rev\":1,\"xp\":1.5,\"open\":0,\"done\":0,\"quests\":[]}");
    refused("quests is an object", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"quests\":{}}");
    refused("quests is a string", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"quests\":\"none\"}");
    refused("truncated is a string", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"truncated\":\"no\",\"quests\":[]}");
    refused("a field twice", "{\"rev\":1,\"rev\":2,\"xp\":0,\"open\":0,\"done\":0,\"quests\":[]}");
    refused("the list twice", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"quests\":[],\"quests\":[]}");
    refused("a nested object where a plain value belongs", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"extra\":{\"a\":1},\"quests\":[]}");
    refused("an item that is not an object", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"quests\":[1]}");
    refused("trailing garbage", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"quests\":[]} x");
    refused("cut short", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"quests\":[");

    doc(b, sizeof(b), item(it, sizeof(it), ",\"title\":\"again\""));
    refused("a field twice in an item", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":2}");
    refused("an item without a state", b);
    doc(b, sizeof(b), "{\"id\":1,\"difficulty\":2,\"state\":\"open\"}");
    refused("an item without a title", b);
    doc(b, sizeof(b), "{\"title\":\"t\",\"difficulty\":2,\"state\":\"open\"}");
    refused("an item without an id", b);
    doc(b, sizeof(b), "{\"id\":0,\"title\":\"t\",\"difficulty\":2,\"state\":\"open\"}");
    refused("id 0", b);
    doc(b, sizeof(b), "{\"id\":2147483648,\"title\":\"t\",\"difficulty\":2,\"state\":\"open\"}");
    refused("id over int32", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"\",\"difficulty\":2,\"state\":\"open\"}");
    refused("an empty title", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":0,\"state\":\"open\"}");
    refused("difficulty 0", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":6,\"state\":\"open\"}");
    refused("difficulty 6", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":2,\"state\":\"finished\"}");
    refused("an unknown state", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":2,\"state\":\"open\",\"due\":\"2026-02-30\"}");
    refused("a date that does not exist", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":2,\"state\":\"open\",\"due\":\"2026-1-3\"}");
    refused("a date in the wrong shape", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":2,\"state\":\"open\",\"due\":20261204}");
    refused("a date as a number", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":2,\"state\":\"done\",\"done_at\":5}");
    refused("done_at before 2000", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":2,\"state\":\"done\",\"done_at\":4102444801}");
    refused("done_at after 2100", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"t\",\"difficulty\":2,\"state\":\"open\",\"extra\":[1]}");
    refused("an array inside an item", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"bad \xC0\x80 utf8\",\"difficulty\":2,\"state\":\"open\"}");
    refused("bad UTF-8 in a title", b);
    doc(b, sizeof(b), "{\"id\":1,\"title\":\"nul \\u0000 inside\",\"difficulty\":2,\"state\":\"open\"}");
    refused("a NUL in a title", b);
}

static void test_the_limits_of_the_contract_are_accepted_exactly_and_refused_one_over(void)
{
    static char b[16384], t[100], n[300], it[16000];
    todo_result r;
    int i;

    memset(t, 'T', 64);
    t[64] = '\0';
    memset(n, 'N', 256);
    n[256] = '\0';
    snprintf(it, sizeof(it), "{\"id\":1,\"title\":\"%s\",\"notes\":\"%s\",\"difficulty\":5,\"state\":\"open\"}", t, n);
    doc(b, sizeof(b), it);
    fresh();
    add("GET", "/quests", 200, b);
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_TRUE_MESSAGE(r.ok, "a title of 64 and notes of 256 bytes");
    TEST_ASSERT_EQUAL_UINT(64, strlen(S.quests[0].title));
    TEST_ASSERT_EQUAL_UINT(256, strlen(S.quests[0].notes));

    memset(t, 'T', 65);
    t[65] = '\0';
    snprintf(it, sizeof(it), "{\"id\":1,\"title\":\"%s\",\"difficulty\":5,\"state\":\"open\"}", t);
    doc(b, sizeof(b), it);
    refused("a title of 65 bytes", b);
    memset(n, 'N', 257);
    n[257] = '\0';
    snprintf(it, sizeof(it), "{\"id\":1,\"title\":\"t\",\"notes\":\"%s\",\"difficulty\":5,\"state\":\"open\"}", n);
    doc(b, sizeof(b), it);
    refused("notes of 257 bytes", b);

    it[0] = '\0';
    for (i = 0; i < 48; i++) {
        char one[80];
        snprintf(one, sizeof(one), "%s{\"id\":%d,\"title\":\"q\",\"difficulty\":1,\"state\":\"open\"}", i ? "," : "", i + 1);
        strcat(it, one);
    }
    doc(b, sizeof(b), it);
    fresh();
    add("GET", "/quests", 200, b);
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_TRUE_MESSAGE(r.ok, "48 quests");
    TEST_ASSERT_EQUAL_INT(48, S.n_quests);
    strcat(it, ",{\"id\":49,\"title\":\"q\",\"difficulty\":1,\"state\":\"open\"}");
    doc(b, sizeof(b), it);
    refused("49 quests", b);
}

static void test_the_size_limit_is_exact_and_what_is_not_bounded_is_not_read(void)
{
    static char big[NET_RESPONSE_MAX + 64];
    todo_result r;
    size_t len;
    fresh();
    snprintf(big, sizeof(big), "%s", "{\"rev\":1,\"xp\":0,\"open\":0,\"done\":0,\"quests\":[]}");
    len = strlen(big);
    memset(big + len, ' ', NET_RESPONSE_MAX - len);
    big[NET_RESPONSE_MAX] = '\0';
    add("GET", "/quests", 200, big);
    TEST_ASSERT_TRUE_MESSAGE(ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0)).ok, "exactly 16384 bytes");
    memset(big + len, ' ', NET_RESPONSE_MAX + 1 - len);
    big[NET_RESPONSE_MAX + 1] = '\0';
    refused("16385 bytes", big);

    fresh();
    add("GET", "/quests", 200, QUESTS);
    script[0].clen = -1; /* chunked, or no length at all */
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_INT(0, S.n_quests);

    fresh();
    add("GET", "/quests", 200, QUESTS);
    script[0].clen = 200000;
    TEST_ASSERT_FALSE(ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0)).ok);

    fresh();
    add("GET", "/quests", 200, QUESTS);
    script[0].clen = (long)strlen(QUESTS) - 10; /* more bytes than it promised */
    TEST_ASSERT_FALSE(ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0)).ok);
    TEST_ASSERT_EQUAL_INT(0, S.n_quests);

    fresh();
    add("GET", "/quests", 200, QUESTS);
    script[0].clen = (long)strlen(QUESTS) + 10; /* fewer bytes than it promised: the transport sees the end early */
    script[0].incomplete = true;
    TEST_ASSERT_FALSE(ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0)).ok);
    TEST_ASSERT_EQUAL_INT(0, S.n_quests);
    TEST_ASSERT_EQUAL_INT(0, live);
}

static void test_a_connection_that_dies_after_the_head_is_an_error_not_offline(void)
{
    todo_result r;
    fresh();
    add("GET", "/quests", 200, QUESTS);
    script[0].offline_mid = true;
    script[0].chunk = 20;
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE_MESSAGE(r.offline, "the server was there: it answered, and then stopped");
}

static void test_a_body_cut_half_way_is_an_error_and_not_offline(void)
{
    todo_result r;
    fresh();
    add("GET", "/quests", 200, QUESTS);
    script[0].incomplete = true;
    script[0].chunk = 20;
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.offline);
}

static void test_the_answer_does_not_depend_on_how_the_bytes_arrive(void)
{
    static const size_t chunks[] = {1, 2, 7, 64, 511, 4096};
    size_t i;
    for (i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        fresh();
        add("GET", "/quests", 200, QUESTS);
        script[0].chunk = chunks[i];
        TEST_ASSERT_TRUE(ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0)).ok);
        TEST_ASSERT_EQUAL_INT(2, S.n_quests);
        TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", S.quests[1].title);
    }
}

static void test_events_have_their_own_rules(void)
{
    static const char *const bad[] = {
        "{\"rev\":1,\"events\":[{\"id\":1,\"title\":\"e\",\"start\":1796230800,\"end\":1796230800}]}",       /* end = start */
        "{\"rev\":1,\"events\":[{\"id\":1,\"title\":\"e\",\"start\":1796230800,\"end\":1796230700}]}",       /* end < start */
        "{\"rev\":1,\"events\":[{\"id\":1,\"title\":\"e\",\"start\":5,\"end\":1796230800}]}",                /* before 2000 */
        "{\"rev\":1,\"events\":[{\"id\":1,\"title\":\"e\",\"start\":1796230800}]}",                          /* no end */
        "{\"rev\":1,\"events\":[{\"id\":1,\"start\":1796230800,\"end\":1796234400}]}",                       /* no title */
        "{\"rev\":1,\"events\":[{\"id\":1,\"title\":\"e\",\"description\":5,\"start\":1796230800,\"end\":1796234400}]}",
        "{\"rev\":1,\"events\":[{\"id\":1,\"title\":\"e\",\"start\":\"1796230800\",\"end\":1796234400}]}",   /* a string */
        "{\"rev\":1,\"events\":[{\"id\":1,\"title\":\"e\",\"start\":1796230800.5,\"end\":1796234400}]}",     /* a float */
        "{\"events\":[]}",
        "{\"rev\":1}",
    };
    size_t i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        todo_result r;
        fresh();
        add("GET", "/events?from=1&to=2", 200, bad[i]);
        S.n_events = 1;
        r = ask(req(TODO_REQ_REFRESH_EVENTS, 0, 1, 2));
        TEST_ASSERT_FALSE_MESSAGE(r.ok, bad[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, S.n_events, "the cache is as it was");
    }
}

static void test_changes_and_a_link_have_their_own_rules(void)
{
    static const char *const bad_changes[] = {"{\"rev\":1}", "{\"changed\":true}", "{\"rev\":\"1\",\"changed\":true}", "{\"rev\":1,\"changed\":\"yes\"}", "{\"rev\":-1,\"changed\":true}", "[]"};
    static const char *const bad_links[] = {
        "{\"expires_in\":600}", "{\"url\":5}", "{\"url\":\"\"}", "{\"url\":\"ftp://x.y/m/AAAAAAAAAAAAAAAAAAAAAAAAAA\"}", "{\"url\":\"http://a b/m/x\"}",
        "{\"url\":\"http://192.168.1.50:8080/m/K3H9Q7ZB2VN4X8D5WMT6RYE1GAxxxxxxxxxxxxxx\"}" /* over 61 bytes */, "{\"url\":\"http://h/\\u00e9\"}",
        "{\"url\":\"http://192.168.1.50:8080/m/K3H9Q7ZB2VN4X8D5WMT6RYE1GAxxxxxxxxx\"}" /* 62 bytes */};
    size_t i;
    for (i = 0; i < sizeof(bad_changes) / sizeof(bad_changes[0]); i++) {
        fresh();
        add("GET", "/changes?since=1", 200, bad_changes[i]);
        TEST_ASSERT_FALSE_MESSAGE(ask(req(TODO_REQ_POLL_CHANGES, 0, 1, 0)).ok, bad_changes[i]);
    }
    for (i = 0; i < sizeof(bad_links) / sizeof(bad_links[0]); i++) {
        todo_request q = todo_req(TODO_REQ_NEW_LINK);
        todo_result r;
        fresh();
        add("POST", "/link", 201, bad_links[i]);
        r = ask(q);
        TEST_ASSERT_FALSE_MESSAGE(r.ok, bad_links[i]);
        TEST_ASSERT_EQUAL_STRING("", r.url);
        TEST_ASSERT_EQUAL_INT(1, performed);
    }
}

/* ---- failing, waiting, and the queues ---- */

static void test_after_a_failure_requests_are_answered_at_once_until_the_wait_is_over(void)
{
    todo_result r;
    fresh();
    n_script = 1;
    script[0].method = "GET";
    script[0].path = "/quests";
    script[0].offline = true;
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_TRUE(r.offline);
    TEST_ASSERT_EQUAL_INT(1, performed);
    now_ms += 500; /* inside the first second */
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_TRUE_MESSAGE(r.offline, "answered at once, and as it was: offline");
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, performed, "the network was not touched");
    now_ms += 600; /* past a second */
    at_script = 0;
    n_script = 1;
    script[0].offline = false;
    script[0].status = 200;
    script[0].body = QUESTS;
    script[0].clen = -2;
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(2, performed);
    /* a success wipes the memory of the failures: the next failure waits one second again, not two */
    at_script = 0;
    n_script = 1;
    script[0].offline = true;
    ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_EQUAL_INT(3, performed);
    now_ms += 1000;
    at_script = 0;
    ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, performed, "one second after the first failure of a new series");
}

static void test_the_wait_doubles_and_stops_at_fifteen_seconds(void)
{
    static const uint32_t waits[] = {1000, 2000, 4000, 8000, 15000, 15000};
    size_t i;
    fresh();
    for (i = 0; i < sizeof(waits) / sizeof(waits[0]); i++) {
        todo_result r;
        at_script = 0;
        n_script = 1;
        script[0].method = "GET";
        script[0].path = "/quests";
        script[0].offline = true;
        r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
        TEST_ASSERT_TRUE(r.offline);
        {
            int before = performed;
            now_ms += waits[i] - 1;
            ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
            TEST_ASSERT_EQUAL_INT_MESSAGE(before, performed, "still waiting one millisecond short");
            now_ms += 1;
        }
    }
}

static void test_an_error_also_makes_the_next_request_wait_but_says_error_not_offline(void)
{
    todo_result r;
    fresh();
    add("GET", "/quests", 500, "{}");
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_FALSE(r.ok);
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.offline);
    TEST_ASSERT_EQUAL_INT(1, performed);
}

static void test_the_clock_wrapping_round_does_not_freeze_the_wait(void)
{
    todo_result r;
    fresh();
    now_ms = 0xFFFFFF00u;
    add("GET", "/quests", 500, "{}");
    ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    now_ms += 2000; /* wraps past zero */
    at_script = 0;
    n_script = 0;
    add("GET", "/quests", 200, QUESTS);
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_TRUE(r.ok);
}

static void test_the_request_queue_holds_eight_and_the_ninth_is_refused(void)
{
    todo_request q = req(TODO_REQ_POLL_CHANGES, 0, 1, 0);
    int i;
    fresh();
    for (i = 0; i < NET_QUEUE; i++) {
        TEST_ASSERT_TRUE(todo_repo_submit(&N.repo, &q));
    }
    TEST_ASSERT_FALSE(todo_repo_submit(&N.repo, &q));
}

static void test_the_network_task_waits_when_the_answers_are_not_collected_and_when_a_list_is_waiting(void)
{
    todo_request q = req(TODO_REQ_POLL_CHANGES, 0, 1, 0);
    todo_result r;
    int i, did = 0;
    fresh();
    for (i = 0; i < 6; i++) {
        add("GET", "/changes?since=1", 200, CHANGES);
        TEST_ASSERT_TRUE(todo_repo_submit(&N.repo, &q));
    }
    while (todo_netrepo_step(&N)) {
        did++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(NET_RESULTS, did, "four answers wait for the UI; the network task does not run ahead");
    TEST_ASSERT_TRUE(todo_repo_poll(&N.repo, &S, &r));
    TEST_ASSERT_TRUE(todo_netrepo_step(&N));

    /* only one list is staged at a time */
    fresh();
    add("GET", "/quests", 200, QUESTS);
    add("GET", "/quests", 200, QUESTS);
    TEST_ASSERT_TRUE(todo_repo_submit(&N.repo, &(todo_request){TODO_REQ_REFRESH_QUESTS, 0, 0, 0, false, TODO_LINK_HOME}));
    TEST_ASSERT_TRUE(todo_repo_submit(&N.repo, &(todo_request){TODO_REQ_REFRESH_QUESTS, 0, 0, 0, false, TODO_LINK_HOME}));
    TEST_ASSERT_TRUE(todo_netrepo_step(&N));
    TEST_ASSERT_FALSE_MESSAGE(todo_netrepo_step(&N), "a list is waiting for the UI: no second block is made");
    TEST_ASSERT_EQUAL_INT(1, live);
    TEST_ASSERT_TRUE(todo_repo_poll(&N.repo, &S, &r));
    TEST_ASSERT_EQUAL_INT(0, live);
    TEST_ASSERT_TRUE(todo_netrepo_step(&N));
    TEST_ASSERT_TRUE(todo_repo_poll(&N.repo, &S, &r));
    TEST_ASSERT_EQUAL_INT(0, live);
}

static void test_the_staging_memory_is_what_the_board_can_afford(void)
{
    todo_result r;
    fresh();
    add("GET", "/quests", 200, QUESTS);
    r = ask(req(TODO_REQ_REFRESH_QUESTS, 0, 0, 0));
    TEST_ASSERT_TRUE(r.ok);
    printf("netrepo: the quest list stages in %zu bytes in all (at most %d blocks live)\n", peak_bytes, peak_live);
    TEST_ASSERT_TRUE_MESSAGE(peak_bytes < 20000, "a list and the working state of one exchange stay under 20 KB");
    TEST_ASSERT_TRUE(peak_live <= 2);
}

static void test_an_unready_or_empty_repository_is_safe(void)
{
    todo_result r;
    fresh();
    TEST_ASSERT_FALSE(todo_netrepo_step(&N));
    TEST_ASSERT_FALSE(todo_repo_poll(&N.repo, &S, &r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_quests_are_read_into_the_store_field_by_field);
    RUN_TEST(test_the_events_of_a_window_are_read_and_the_window_is_remembered);
    RUN_TEST(test_the_cache_revision_is_the_older_of_the_two_lists);
    RUN_TEST(test_changes_are_asked_since_the_revision_and_say_so);
    RUN_TEST(test_completing_a_quest_patches_it_and_brings_back_the_list);
    RUN_TEST(test_deleting_a_quest_and_an_event);
    RUN_TEST(test_a_write_that_worked_is_ok_even_if_reading_the_list_back_failed);
    RUN_TEST(test_a_link_is_asked_for_by_kind_and_comes_with_the_revision_the_phone_will_move);
    RUN_TEST(test_a_server_that_cannot_be_reached_is_offline_and_not_an_error);
    RUN_TEST(test_a_refusal_by_the_server_is_an_error);
    RUN_TEST(test_every_way_of_breaking_the_contract_is_refused_whole);
    RUN_TEST(test_the_limits_of_the_contract_are_accepted_exactly_and_refused_one_over);
    RUN_TEST(test_the_size_limit_is_exact_and_what_is_not_bounded_is_not_read);
    RUN_TEST(test_a_connection_that_dies_after_the_head_is_an_error_not_offline);
    RUN_TEST(test_a_body_cut_half_way_is_an_error_and_not_offline);
    RUN_TEST(test_the_answer_does_not_depend_on_how_the_bytes_arrive);
    RUN_TEST(test_events_have_their_own_rules);
    RUN_TEST(test_changes_and_a_link_have_their_own_rules);
    RUN_TEST(test_after_a_failure_requests_are_answered_at_once_until_the_wait_is_over);
    RUN_TEST(test_the_wait_doubles_and_stops_at_fifteen_seconds);
    RUN_TEST(test_an_error_also_makes_the_next_request_wait_but_says_error_not_offline);
    RUN_TEST(test_the_clock_wrapping_round_does_not_freeze_the_wait);
    RUN_TEST(test_the_request_queue_holds_eight_and_the_ninth_is_refused);
    RUN_TEST(test_the_network_task_waits_when_the_answers_are_not_collected_and_when_a_list_is_waiting);
    RUN_TEST(test_the_staging_memory_is_what_the_board_can_afford);
    RUN_TEST(test_an_unready_or_empty_repository_is_safe);
    return UNITY_END();
}
