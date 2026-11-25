/* The pretend server behind the repository interface: what the screens will be able to rely on. */

#include <stdio.h>
#include <string.h>

#include "todo/datetime.h"
#include "todo/memrepo.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define NOW 1796230800LL /* 2026-12-02 17:00 UTC */

static todo_memrepo mem;
static todo_store store;

static void fresh(void)
{
    todo_memrepo_init(&mem, NOW);
    todo_memrepo_seed_demo(&mem);
    todo_store_clear(&store);
}

/* Submits a request and polls until its answer comes (at most 20 polls). */
static todo_result ask(todo_request q)
{
    todo_result r;
    int i;
    TEST_ASSERT_TRUE(todo_repo_submit(&mem.repo, &q));
    for (i = 0; i < 20; i++) {
        if (todo_repo_poll(&mem.repo, &store, &r)) {
            return r;
        }
    }
    TEST_FAIL_MESSAGE("no answer");
    return r;
}

static void test_the_demo_quests_come_open_first_by_due_date_then_done_latest_first(void)
{
    todo_result r;
    fresh();
    r = ask(todo_req(TODO_REQ_REFRESH_QUESTS));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(6, store.n_quests);
    TEST_ASSERT_EQUAL_STRING("Slay the inbox", store.quests[0].title);        /* due in 2 days */
    TEST_ASSERT_EQUAL_STRING("Visit the dentist", store.quests[1].title);     /* 5 */
    TEST_ASSERT_EQUAL_STRING("Write the dragon report", store.quests[2].title); /* 9 */
    TEST_ASSERT_EQUAL_STRING("Clean the armory", store.quests[3].title);      /* undated last among the open */
    TEST_ASSERT_TRUE(store.quests[4].done && store.quests[5].done);
    TEST_ASSERT_EQUAL_STRING("Pay the guild fees", store.quests[4].title);    /* done most recently (an hour before) */
    TEST_ASSERT_EQUAL_INT32(20260204 - 20260204 + 20261204, store.quests[0].due);
    TEST_ASSERT_EQUAL_INT32(0, store.quests[3].due);
    TEST_ASSERT_EQUAL_INT32(20, store.xp);
    TEST_ASSERT_EQUAL_INT32(4, store.open);
    TEST_ASSERT_EQUAL_INT32(2, store.done);
    TEST_ASSERT_FALSE(store.quests_truncated);
    TEST_ASSERT_EQUAL_INT64(mem.rev, store.rev);
}

static void test_events_come_for_a_window_in_order(void)
{
    todo_request q = todo_req(TODO_REQ_REFRESH_EVENTS);
    int64_t from, to;
    fresh();
    todo_month_window(2026, 12, -180, &from, &to);
    q.a = from;
    q.b = to;
    TEST_ASSERT_TRUE(ask(q).ok);
    TEST_ASSERT_EQUAL_INT(4, store.n_events);
    TEST_ASSERT_EQUAL_STRING("Dentist", store.events[0].title);
    TEST_ASSERT_EQUAL_STRING("Night watch", store.events[3].title);
    TEST_ASSERT_EQUAL_INT64(from, store.events_from);
    /* an empty window */
    q.a = NOW + 100 * TODO_DAY;
    q.b = q.a + TODO_DAY;
    TEST_ASSERT_TRUE(ask(q).ok);
    TEST_ASSERT_EQUAL_INT(0, store.n_events);
}

static void test_an_event_that_crosses_the_window_edge_is_in_and_one_that_touches_it_is_out(void)
{
    todo_request q = todo_req(TODO_REQ_REFRESH_EVENTS);
    fresh();
    todo_memrepo_add_event(&mem, "ends at from", "", NOW - 100, NOW);
    todo_memrepo_add_event(&mem, "starts at to", "", NOW + 1000, NOW + 1100);
    todo_memrepo_add_event(&mem, "crosses", "", NOW - 5, NOW + 5);
    q.a = NOW;
    q.b = NOW + 1000;
    TEST_ASSERT_TRUE(ask(q).ok);
    TEST_ASSERT_EQUAL_INT(1, store.n_events);
    TEST_ASSERT_EQUAL_STRING("crosses", store.events[0].title);
}

static void test_one_answer_per_poll_and_a_slow_server_makes_the_ui_wait(void)
{
    todo_request q = todo_req(TODO_REQ_REFRESH_QUESTS);
    todo_result r;
    fresh();
    mem.latency = 2;
    TEST_ASSERT_TRUE(todo_repo_submit(&mem.repo, &q));
    TEST_ASSERT_TRUE(todo_repo_submit(&mem.repo, &q));
    TEST_ASSERT_FALSE(todo_repo_poll(&mem.repo, &store, &r));
    TEST_ASSERT_FALSE(todo_repo_poll(&mem.repo, &store, &r));
    TEST_ASSERT_EQUAL_INT(0, store.n_quests);
    TEST_ASSERT_TRUE(todo_repo_poll(&mem.repo, &store, &r));
    TEST_ASSERT_EQUAL_INT(6, store.n_quests);
    TEST_ASSERT_FALSE_MESSAGE(todo_repo_poll(&mem.repo, &store, &r) && false, "only one answer per poll");
}

static void test_the_queue_has_room_for_eight_and_then_says_no(void)
{
    todo_request q = todo_req(TODO_REQ_POLL_CHANGES);
    todo_result r;
    int i;
    fresh();
    for (i = 0; i < TODO_MEM_QUEUE; i++) {
        TEST_ASSERT_TRUE(todo_repo_submit(&mem.repo, &q));
    }
    TEST_ASSERT_FALSE(todo_repo_submit(&mem.repo, &q));
    TEST_ASSERT_TRUE(todo_repo_poll(&mem.repo, &store, &r));
    TEST_ASSERT_TRUE(todo_repo_submit(&mem.repo, &q));
}

static void test_completing_a_quest_moves_the_xp_and_reopening_takes_it_back(void)
{
    todo_request q = todo_req(TODO_REQ_SET_QUEST_DONE);
    todo_result r;
    fresh();
    ask(todo_req(TODO_REQ_REFRESH_QUESTS));
    q.id = store.quests[0].id; /* Slay the inbox, difficulty 3 */
    q.flag = true;
    r = ask(q);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT32(50, store.xp);
    TEST_ASSERT_EQUAL_INT32(3, store.open);
    TEST_ASSERT_EQUAL_INT32(3, store.done);
    TEST_ASSERT_TRUE(todo_find_quest(&store, q.id)->done);
    TEST_ASSERT_EQUAL_INT64(NOW, todo_find_quest(&store, q.id)->done_at);
    q.flag = false;
    r = ask(q);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT32(20, store.xp);
    TEST_ASSERT_FALSE(todo_find_quest(&store, q.id)->done);
    TEST_ASSERT_EQUAL_INT64(0, todo_find_quest(&store, q.id)->done_at);
}

static void test_completing_twice_does_not_move_the_revision_again(void)
{
    todo_request q = todo_req(TODO_REQ_SET_QUEST_DONE);
    int64_t rev;
    fresh();
    q.id = 1;
    q.flag = true;
    ask(q);
    rev = mem.rev;
    ask(q);
    TEST_ASSERT_EQUAL_INT64(rev, mem.rev);
}

static void test_a_quest_or_event_that_is_not_there_is_an_error_not_a_crash(void)
{
    todo_request q = todo_req(TODO_REQ_SET_QUEST_DONE);
    fresh();
    q.id = 999;
    TEST_ASSERT_FALSE(ask(q).ok);
    q.kind = TODO_REQ_DELETE_QUEST;
    TEST_ASSERT_FALSE(ask(q).ok);
    q.kind = TODO_REQ_DELETE_EVENT;
    TEST_ASSERT_FALSE(ask(q).ok);
}

static void test_deleting_a_quest_and_an_event_updates_the_cache(void)
{
    todo_request q = todo_req(TODO_REQ_DELETE_QUEST);
    todo_request e = todo_req(TODO_REQ_REFRESH_EVENTS);
    fresh();
    ask(todo_req(TODO_REQ_REFRESH_QUESTS));
    q.id = store.quests[1].id;
    TEST_ASSERT_TRUE(ask(q).ok);
    TEST_ASSERT_EQUAL_INT(5, store.n_quests);
    TEST_ASSERT_NULL(todo_find_quest(&store, q.id));
    e.a = NOW - TODO_DAY;
    e.b = NOW + 10 * TODO_DAY;
    ask(e);
    TEST_ASSERT_EQUAL_INT(4, store.n_events);
    e.kind = TODO_REQ_DELETE_EVENT;
    e.id = store.events[0].id;
    TEST_ASSERT_TRUE(ask(e).ok);
    TEST_ASSERT_EQUAL_INT(3, store.n_events);
    TEST_ASSERT_NULL(todo_find_event(&store, e.id));
}

static void test_a_failed_request_changes_nothing_and_says_so(void)
{
    todo_request q = todo_req(TODO_REQ_SET_QUEST_DONE);
    todo_result r;
    int64_t rev;
    fresh();
    ask(todo_req(TODO_REQ_REFRESH_QUESTS));
    rev = mem.rev;
    mem.fail_next = 1;
    q.id = store.quests[0].id;
    q.flag = true;
    r = ask(q);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.offline);
    TEST_ASSERT_EQUAL_INT64(rev, mem.rev);
    TEST_ASSERT_FALSE(todo_find_quest(&store, q.id)->done);
    TEST_ASSERT_TRUE_MESSAGE(ask(q).ok, "only one request failed");
}

static void test_offline_is_told_apart_from_an_error(void)
{
    todo_result r;
    fresh();
    mem.offline = true;
    r = ask(todo_req(TODO_REQ_REFRESH_QUESTS));
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.offline);
    TEST_ASSERT_EQUAL_INT(0, store.n_quests);
    mem.offline = false;
    TEST_ASSERT_TRUE(ask(todo_req(TODO_REQ_REFRESH_QUESTS)).ok);
}

static void test_a_change_made_elsewhere_is_seen_by_the_revision_poll(void)
{
    todo_request q = todo_req(TODO_REQ_POLL_CHANGES);
    todo_result r;
    fresh();
    ask(todo_req(TODO_REQ_REFRESH_QUESTS));
    q.a = store.rev;
    r = ask(q);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.changed);
    todo_memrepo_add_quest(&mem, "Written on the phone", "", 2, 0);
    r = ask(q);
    TEST_ASSERT_TRUE(r.changed);
    TEST_ASSERT_EQUAL_INT64(mem.rev, r.rev);
    ask(todo_req(TODO_REQ_REFRESH_QUESTS));
    TEST_ASSERT_EQUAL_INT(7, store.n_quests);
    q.a = store.rev;
    TEST_ASSERT_FALSE(ask(q).changed);
}

static void test_a_link_is_a_url_of_the_length_a_qr_code_can_take_and_a_new_one_each_time(void)
{
    todo_request q = todo_req(TODO_REQ_NEW_LINK);
    todo_result a, b;
    fresh();
    q.link = TODO_LINK_EVENT;
    a = ask(q);
    b = ask(q);
    TEST_ASSERT_TRUE(a.ok && b.ok);
    TEST_ASSERT_TRUE(strlen(a.url) <= TODO_LINK_URL_MAX);
    TEST_ASSERT_EQUAL_STRING_LEN("http://192.168.1.50:8080/m/", a.url, 27);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a.url, b.url));
}

static void test_more_than_48_quests_are_cut_and_the_totals_stay_whole(void)
{
    int i;
    char t[16];
    fresh();
    for (i = 0; i < 60; i++) {
        snprintf(t, sizeof(t), "q%02d", i);
        todo_memrepo_add_quest(&mem, t, "", 2, 0);
    }
    ask(todo_req(TODO_REQ_REFRESH_QUESTS));
    TEST_ASSERT_EQUAL_INT(48, store.n_quests);
    TEST_ASSERT_TRUE(store.quests_truncated);
    TEST_ASSERT_EQUAL_INT32(64, store.open);
    TEST_ASSERT_EQUAL_INT32(2, store.done);
}

static void test_an_unready_repository_is_safe_to_call(void)
{
    todo_request q = todo_req(TODO_REQ_REFRESH_QUESTS);
    todo_result r;
    todo_repo none = {0, 0};
    TEST_ASSERT_FALSE(todo_repo_submit(NULL, &q));
    TEST_ASSERT_FALSE(todo_repo_submit(&none, &q));
    TEST_ASSERT_FALSE(todo_repo_poll(&none, &store, &r));
    TEST_ASSERT_FALSE(todo_repo_poll(NULL, &store, &r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_demo_quests_come_open_first_by_due_date_then_done_latest_first);
    RUN_TEST(test_events_come_for_a_window_in_order);
    RUN_TEST(test_an_event_that_crosses_the_window_edge_is_in_and_one_that_touches_it_is_out);
    RUN_TEST(test_one_answer_per_poll_and_a_slow_server_makes_the_ui_wait);
    RUN_TEST(test_the_queue_has_room_for_eight_and_then_says_no);
    RUN_TEST(test_completing_a_quest_moves_the_xp_and_reopening_takes_it_back);
    RUN_TEST(test_completing_twice_does_not_move_the_revision_again);
    RUN_TEST(test_a_quest_or_event_that_is_not_there_is_an_error_not_a_crash);
    RUN_TEST(test_deleting_a_quest_and_an_event_updates_the_cache);
    RUN_TEST(test_a_failed_request_changes_nothing_and_says_so);
    RUN_TEST(test_offline_is_told_apart_from_an_error);
    RUN_TEST(test_a_change_made_elsewhere_is_seen_by_the_revision_poll);
    RUN_TEST(test_a_link_is_a_url_of_the_length_a_qr_code_can_take_and_a_new_one_each_time);
    RUN_TEST(test_more_than_48_quests_are_cut_and_the_totals_stay_whole);
    RUN_TEST(test_an_unready_repository_is_safe_to_call);
    return UNITY_END();
}
