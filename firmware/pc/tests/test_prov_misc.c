/* The access point's credentials, the captive portal's DNS and the boot table. */

#include <string.h>

#include "prov/apcreds.h"
#include "prov/bootflow.h"
#include "prov/dnsans.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- the access point ---- */

static void test_the_name_is_the_prefix_and_four_hex_characters_of_two_bytes(void)
{
    uint8_t r[PROV_AP_RANDOM_BYTES] = {0x4F, 0x2A};
    char ssid[PROV_AP_SSID_LEN + 1], pass[PROV_AP_PASS_LEN + 1];
    prov_ap_make(r, ssid, pass);
    TEST_ASSERT_EQUAL_STRING("cyd-todo-4F2A", ssid);
    memset(r, 0, sizeof(r));
    prov_ap_make(r, ssid, pass);
    TEST_ASSERT_EQUAL_STRING("cyd-todo-0000", ssid);
    TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAA", pass);
    memset(r, 0xFF, sizeof(r));
    prov_ap_make(r, ssid, pass);
    TEST_ASSERT_EQUAL_STRING("cyd-todo-FFFF", ssid);
    TEST_ASSERT_EQUAL_STRING("999999999999", pass);
    TEST_ASSERT_EQUAL_UINT(PROV_AP_SSID_LEN, strlen(ssid));
}

static void test_every_byte_gives_a_character_of_the_alphabet_without_look_alikes_and_each_is_reachable(void)
{
    uint8_t r[PROV_AP_RANDOM_BYTES] = {0};
    char ssid[PROV_AP_SSID_LEN + 1], pass[PROV_AP_PASS_LEN + 1];
    int seen[256] = {0}, b, distinct = 0;
    for (b = 0; b < 256; b++) {
        r[2] = (uint8_t)b;
        prov_ap_make(r, ssid, pass);
        TEST_ASSERT_NULL(strchr("0O1Il", pass[0]));
        TEST_ASSERT_TRUE((pass[0] >= 'A' && pass[0] <= 'Z') || (pass[0] >= '2' && pass[0] <= '9'));
        seen[(unsigned char)pass[0]]++;
    }
    for (b = 0; b < 256; b++) {
        distinct += seen[b] > 0;
        TEST_ASSERT_TRUE_MESSAGE(seen[b] == 0 || seen[b] == 8, "every character is equally likely: 256 bytes onto 32 characters");
    }
    TEST_ASSERT_EQUAL_INT(32, distinct);
}

static void test_each_byte_of_the_password_matters_and_the_name_bytes_do_not_leak_into_it(void)
{
    uint8_t r[PROV_AP_RANDOM_BYTES] = {0}, s[PROV_AP_RANDOM_BYTES] = {0};
    char s1[PROV_AP_SSID_LEN + 1], p1[PROV_AP_PASS_LEN + 1], s2[PROV_AP_SSID_LEN + 1], p2[PROV_AP_PASS_LEN + 1];
    int i;
    prov_ap_make(r, s1, p1);
    for (i = 2; i < PROV_AP_RANDOM_BYTES; i++) {
        memcpy(s, r, sizeof(s));
        s[i] = 5;
        prov_ap_make(s, s2, p2);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, strcmp(p1, p2), "every byte feeds the password");
        TEST_ASSERT_EQUAL_STRING(s1, s2);
    }
    memcpy(s, r, sizeof(s));
    s[0] = 9;
    s[1] = 9;
    prov_ap_make(s, s2, p2);
    TEST_ASSERT_EQUAL_STRING(p1, p2);
}

/* ---- the DNS ---- */

static size_t query(uint8_t *q, const char *name, unsigned type, unsigned cls)
{
    size_t n = 12, i = 0;
    memset(q, 0, 12);
    q[0] = 0x12;
    q[1] = 0x34;
    q[2] = 0x01; /* recursion desired */
    q[5] = 1;
    while (name[i]) {
        size_t j = i;
        while (name[j] && name[j] != '.') {
            j++;
        }
        q[n++] = (uint8_t)(j - i);
        memcpy(q + n, name + i, j - i);
        n += j - i;
        i = name[j] ? j + 1 : j;
    }
    q[n++] = 0;
    q[n++] = (uint8_t)(type >> 8);
    q[n++] = (uint8_t)type;
    q[n++] = (uint8_t)(cls >> 8);
    q[n++] = (uint8_t)cls;
    return n;
}

static const uint8_t IP[4] = {192, 168, 4, 1};

static void test_an_a_question_is_answered_with_the_board_byte_for_byte(void)
{
    uint8_t q[128], a[160];
    size_t n = query(q, "example.com", 1, 1), r = prov_dns_answer(q, n, a, sizeof(a), IP);
    static const uint8_t tail[16] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04, 192, 168, 4, 1};
    TEST_ASSERT_EQUAL_UINT(n + 16, r);
    TEST_ASSERT_EQUAL_HEX8(0x12, a[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, a[1]);
    TEST_ASSERT_EQUAL_HEX8(0x85, a[2]); /* a response, authoritative, recursion desired copied */
    TEST_ASSERT_EQUAL_HEX8(0x80, a[3]);
    TEST_ASSERT_EQUAL_HEX8(0, a[4]);
    TEST_ASSERT_EQUAL_HEX8(1, a[5]);
    TEST_ASSERT_EQUAL_HEX8(0, a[6]);
    TEST_ASSERT_EQUAL_HEX8(1, a[7]);
    TEST_ASSERT_EQUAL_MEMORY(q + 12, a + 12, n - 12); /* the question is copied */
    TEST_ASSERT_EQUAL_MEMORY(tail, a + n, 16);
}

static void test_other_types_get_an_empty_answer_so_the_phone_falls_back_to_a(void)
{
    uint8_t q[128], a[160];
    size_t n = query(q, "example.com", 28, 1), r = prov_dns_answer(q, n, a, sizeof(a), IP); /* AAAA */
    TEST_ASSERT_EQUAL_UINT(n, r);
    TEST_ASSERT_EQUAL_HEX8(0, a[7]);
    TEST_ASSERT_EQUAL_HEX8(0x80, a[3]);
    n = query(q, "example.com", 1, 3); /* class CH */
    TEST_ASSERT_EQUAL_UINT(n, prov_dns_answer(q, n, a, sizeof(a), IP));
    n = query(q, "example.com", 255, 1);
    TEST_ASSERT_EQUAL_UINT(n, prov_dns_answer(q, n, a, sizeof(a), IP));
}

static void test_a_query_is_answered_whatever_the_name_up_to_the_limits(void)
{
    uint8_t q[600], a[700];
    char name[300];
    size_t n, i;
    n = query(q, "a", 1, 1);
    TEST_ASSERT_EQUAL_UINT(n + 16, prov_dns_answer(q, n, a, sizeof(a), IP));
    memset(name, 'x', 63);
    name[63] = '\0';
    n = query(q, name, 1, 1); /* a label of 63 */
    TEST_ASSERT_EQUAL_UINT(n + 16, prov_dns_answer(q, n, a, sizeof(a), IP));
    memset(name, 'x', 64);
    name[64] = '\0';
    n = query(q, name, 1, 1); /* a label of 64 is not a label (the top two bits are a type) */
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, sizeof(a), IP));
    for (i = 0; i < 4; i++) { /* four labels of 63: 4 * 64 = 256 > 254 */
        memset(name + i * 64, 'x', 63);
        name[i * 64 + 63] = (i == 3) ? '\0' : '.';
    }
    n = query(q, name, 1, 1);
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, sizeof(a), IP));
}

static void test_what_is_not_a_plain_query_is_ignored(void)
{
    uint8_t q[128], a[160];
    size_t n = query(q, "example.com", 1, 1);
    uint8_t good[128];
    size_t k;
    memcpy(good, q, n);
    TEST_ASSERT_TRUE(prov_dns_answer(q, n, a, sizeof(a), IP) > 0);
    q[2] |= 0x80; /* a response */
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, sizeof(a), IP));
    memcpy(q, good, n);
    q[2] |= 0x08; /* opcode 1 */
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, sizeof(a), IP));
    memcpy(q, good, n);
    q[5] = 2; /* two questions */
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, sizeof(a), IP));
    memcpy(q, good, n);
    q[4] = 1;
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, sizeof(a), IP));
    memcpy(q, good, n);
    q[12] = 0xC0; /* a compression pointer in a question */
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, sizeof(a), IP));
    memcpy(q, good, n);
    q[12] = 64; /* a label that is too long */
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, sizeof(a), IP));
    memcpy(q, good, n);
    q[12] = 0x40; /* a reserved label type */
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, sizeof(a), IP));
    for (k = 0; k < n; k++) { /* every truncation of a good query */
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, prov_dns_answer(good, k, a, sizeof(a), IP), "a cut packet is dropped");
    }
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(NULL, 20, a, sizeof(a), IP));
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(good, n, NULL, 20, IP));
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(good, n, a, sizeof(a), NULL));
}

static void test_an_answer_that_does_not_fit_is_not_written(void)
{
    uint8_t q[128], a[160];
    size_t n = query(q, "example.com", 1, 1), cap;
    for (cap = 0; cap < n + 16; cap++) {
        memset(a, 0xAA, sizeof(a));
        TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, n, a, cap, IP));
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xAA, a[cap < sizeof(a) ? cap : 0], "nothing past the capacity");
    }
    TEST_ASSERT_EQUAL_UINT(n + 16, prov_dns_answer(q, n, a, n + 16, IP));
}

static void test_a_query_over_the_udp_limit_is_ignored(void)
{
    static uint8_t q[PROV_DNS_MAX + 8], a[PROV_DNS_MAX + 64];
    memset(q, 0, sizeof(q));
    TEST_ASSERT_EQUAL_UINT(0, prov_dns_answer(q, PROV_DNS_MAX + 1, a, sizeof(a), IP));
}

/* ---- the boot table ---- */

static boot_flow F;

static int ev(boot_event e, bool flag, boot_fail why)
{
    return boot_step(&F, e, flag, why);
}

static void test_without_credentials_the_board_goes_straight_to_setup(void)
{
    boot_init(&F);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_START_AP, ev(BOOT_EV_BOOT, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_SETUP, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_NONE, F.reason);
    TEST_ASSERT_FALSE(F.has_creds);
}

static void test_with_credentials_it_tries_three_times_and_then_sets_up_with_the_reason(void)
{
    boot_init(&F);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_JOIN, ev(BOOT_EV_BOOT, true, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_JOINING, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_JOIN, ev(BOOT_EV_JOIN_FAILED, false, BOOT_FAIL_TIMEOUT));
    TEST_ASSERT_EQUAL_INT(2, F.tries);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_JOIN, ev(BOOT_EV_JOIN_FAILED, false, BOOT_FAIL_NO_NETWORK));
    TEST_ASSERT_EQUAL_INT(3, F.tries);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_START_AP, ev(BOOT_EV_JOIN_FAILED, false, BOOT_FAIL_NO_NETWORK));
    TEST_ASSERT_EQUAL_INT(BOOT_SETUP, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_NO_NETWORK, F.reason);
    TEST_ASSERT_TRUE_MESSAGE(F.has_creds, "there is a network to go back to");
}

static void test_a_wrong_password_is_not_retried(void)
{
    boot_init(&F);
    ev(BOOT_EV_BOOT, true, BOOT_FAIL_NONE);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_START_AP, ev(BOOT_EV_JOIN_FAILED, false, BOOT_FAIL_WRONG_PASSWORD));
    TEST_ASSERT_EQUAL_INT(BOOT_SETUP, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_WRONG_PASSWORD, F.reason);
}

static void test_joining_ends_in_normal_and_a_late_event_changes_nothing(void)
{
    boot_init(&F);
    ev(BOOT_EV_BOOT, true, BOOT_FAIL_NONE);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_NONE, ev(BOOT_EV_JOINED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_NORMAL, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_NONE, ev(BOOT_EV_JOIN_FAILED, false, BOOT_FAIL_TIMEOUT));
    TEST_ASSERT_EQUAL_INT(BOOT_NORMAL, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_NONE, ev(BOOT_EV_SETUP_EXPIRED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_NONE, ev(BOOT_EV_CREDS_SAVED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_NORMAL, F.state);
}

static void test_the_form_gives_one_attempt_and_its_failure_comes_back_to_setup(void)
{
    boot_init(&F);
    ev(BOOT_EV_BOOT, false, BOOT_FAIL_NONE);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_STOP_AP | BOOT_ACT_JOIN, ev(BOOT_EV_CREDS_SAVED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_JOINING, F.state);
    TEST_ASSERT_EQUAL_INT(1, F.max_tries);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_START_AP, ev(BOOT_EV_JOIN_FAILED, false, BOOT_FAIL_TIMEOUT));
    TEST_ASSERT_EQUAL_INT(BOOT_SETUP, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_TIMEOUT, F.reason);
    TEST_ASSERT_TRUE(F.has_creds);
}

static void test_the_form_that_works_ends_in_normal_with_no_access_point(void)
{
    boot_init(&F);
    ev(BOOT_EV_BOOT, false, BOOT_FAIL_NONE);
    ev(BOOT_EV_CREDS_SAVED, false, BOOT_FAIL_NONE);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_NONE, ev(BOOT_EV_JOINED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_NORMAL, F.state);
    TEST_ASSERT_TRUE(F.has_creds);
}

static void test_change_wifi_from_normal_starts_the_access_point_and_cancel_goes_back_to_joining(void)
{
    boot_init(&F);
    ev(BOOT_EV_BOOT, true, BOOT_FAIL_NONE);
    ev(BOOT_EV_JOINED, false, BOOT_FAIL_NONE);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_START_AP, ev(BOOT_EV_SETUP_REQUESTED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_SETUP, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_NONE, ev(BOOT_EV_SETUP_REQUESTED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_STOP_AP | BOOT_ACT_JOIN, ev(BOOT_EV_SETUP_CANCELLED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_JOINING, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_TRIES, F.max_tries);
}

static void test_there_is_nothing_to_cancel_back_to_without_a_network(void)
{
    boot_init(&F);
    ev(BOOT_EV_BOOT, false, BOOT_FAIL_NONE);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_NONE, ev(BOOT_EV_SETUP_CANCELLED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_SETUP, F.state);
}

static void test_the_window_closes_once_and_try_again_opens_it_again(void)
{
    boot_init(&F);
    ev(BOOT_EV_BOOT, false, BOOT_FAIL_NONE);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_STOP_AP, ev(BOOT_EV_SETUP_EXPIRED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_TRUE(F.expired);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_NONE, ev(BOOT_EV_SETUP_EXPIRED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_NONE, ev(BOOT_EV_CREDS_SAVED, false, BOOT_FAIL_NONE)); /* a late post is not honored */
    TEST_ASSERT_EQUAL_INT(BOOT_SETUP, F.state);
    TEST_ASSERT_EQUAL_INT(BOOT_ACT_START_AP, ev(BOOT_EV_SETUP_REQUESTED, false, BOOT_FAIL_NONE));
    TEST_ASSERT_FALSE(F.expired);
}

static void test_the_reasons_are_sentences_for_the_setup_page(void)
{
    TEST_ASSERT_EQUAL_STRING("Wrong password for %s", boot_reason_format(BOOT_FAIL_WRONG_PASSWORD));
    TEST_ASSERT_EQUAL_STRING("Could not find %s", boot_reason_format(BOOT_FAIL_NO_NETWORK));
    TEST_ASSERT_EQUAL_STRING("Could not join %s", boot_reason_format(BOOT_FAIL_TIMEOUT));
    TEST_ASSERT_NULL(boot_reason_format(BOOT_FAIL_NONE));
}

static void test_the_radio_s_reasons_become_the_user_s_reasons(void)
{
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_WRONG_PASSWORD, boot_fail_from_wifi_reason(15));
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_WRONG_PASSWORD, boot_fail_from_wifi_reason(202));
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_WRONG_PASSWORD, boot_fail_from_wifi_reason(204));
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_NO_NETWORK, boot_fail_from_wifi_reason(201));
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_TIMEOUT, boot_fail_from_wifi_reason(200));
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_TIMEOUT, boot_fail_from_wifi_reason(0));
    TEST_ASSERT_EQUAL_INT(BOOT_FAIL_TIMEOUT, boot_fail_from_wifi_reason(-1));
}

static void test_whatever_happens_the_table_keeps_its_invariants(void)
{
    uint32_t x = 12345;
    int i;
    boot_init(&F);
    for (i = 0; i < 200000; i++) {
        int acts;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        acts = boot_step(&F, (boot_event)(x % 7), (x >> 8) & 1, (boot_fail)((x >> 9) % 4));
        TEST_ASSERT_TRUE((acts & ~(BOOT_ACT_JOIN | BOOT_ACT_START_AP | BOOT_ACT_STOP_AP)) == 0);
        TEST_ASSERT_TRUE_MESSAGE(!(acts & BOOT_ACT_JOIN) || F.state == BOOT_JOINING, "joining only in JOINING");
        TEST_ASSERT_TRUE_MESSAGE(!(acts & BOOT_ACT_START_AP) || F.state == BOOT_SETUP, "an access point only in SETUP");
        TEST_ASSERT_TRUE(F.state == BOOT_JOINING || F.state == BOOT_NORMAL || F.state == BOOT_SETUP);
        TEST_ASSERT_TRUE(F.state != BOOT_JOINING || (F.tries >= 1 && F.tries <= F.max_tries));
        TEST_ASSERT_TRUE(!F.expired || F.state == BOOT_SETUP);
        TEST_ASSERT_TRUE_MESSAGE(F.state != BOOT_NORMAL || F.has_creds, "normal means a network");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_name_is_the_prefix_and_four_hex_characters_of_two_bytes);
    RUN_TEST(test_every_byte_gives_a_character_of_the_alphabet_without_look_alikes_and_each_is_reachable);
    RUN_TEST(test_each_byte_of_the_password_matters_and_the_name_bytes_do_not_leak_into_it);
    RUN_TEST(test_an_a_question_is_answered_with_the_board_byte_for_byte);
    RUN_TEST(test_other_types_get_an_empty_answer_so_the_phone_falls_back_to_a);
    RUN_TEST(test_a_query_is_answered_whatever_the_name_up_to_the_limits);
    RUN_TEST(test_what_is_not_a_plain_query_is_ignored);
    RUN_TEST(test_an_answer_that_does_not_fit_is_not_written);
    RUN_TEST(test_a_query_over_the_udp_limit_is_ignored);
    RUN_TEST(test_without_credentials_the_board_goes_straight_to_setup);
    RUN_TEST(test_with_credentials_it_tries_three_times_and_then_sets_up_with_the_reason);
    RUN_TEST(test_a_wrong_password_is_not_retried);
    RUN_TEST(test_joining_ends_in_normal_and_a_late_event_changes_nothing);
    RUN_TEST(test_the_form_gives_one_attempt_and_its_failure_comes_back_to_setup);
    RUN_TEST(test_the_form_that_works_ends_in_normal_with_no_access_point);
    RUN_TEST(test_change_wifi_from_normal_starts_the_access_point_and_cancel_goes_back_to_joining);
    RUN_TEST(test_there_is_nothing_to_cancel_back_to_without_a_network);
    RUN_TEST(test_the_window_closes_once_and_try_again_opens_it_again);
    RUN_TEST(test_the_reasons_are_sentences_for_the_setup_page);
    RUN_TEST(test_the_radio_s_reasons_become_the_user_s_reasons);
    RUN_TEST(test_whatever_happens_the_table_keeps_its_invariants);
    return UNITY_END();
}
