/* Random and mutated input for everything the network can reach: the form, the DNS, the pages. Deterministic (a fixed
 * seed), run under the sanitizers: what is asserted is that nothing reads or writes out of bounds and that every
 * accepted input really satisfies the rules. */

#include <stdio.h>
#include <string.h>

#include "prov/dnsans.h"
#include "prov/form.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t rng_state = 0x9E3779B9u;

static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

#define SESSION "0123456789abcdef"
static const char VALID[] = "ssid=home-net&pass=hunter2hunter2&server=192.168.1.50%3A8080&token=tttttttttttttttttttttttttttttttt&session=" SESSION;

/* the bytes that make forms interesting */
static const char NASTY[] = "&=%+\0\r\n\"'<>\\/ :;0123456789abcdefABCDEF";

static size_t mutate(char *buf, size_t len, size_t cap)
{
    int k, n = 1 + (int)(rnd() % 6);
    for (k = 0; k < n; k++) {
        size_t at = len ? rnd() % len : 0;
        switch (rnd() % 5) {
        case 0: /* flip a byte */
            if (len) {
                buf[at] = (char)rnd();
            }
            break;
        case 1: /* a nasty byte */
            if (len) {
                buf[at] = NASTY[rnd() % sizeof(NASTY)];
            }
            break;
        case 2: /* delete */
            if (len) {
                memmove(buf + at, buf + at + 1, len - at - 1);
                len--;
            }
            break;
        case 3: /* insert an escape or a byte */
            if (len + 3 < cap) {
                memmove(buf + at + 3, buf + at, len - at);
                buf[at] = '%';
                buf[at + 1] = "0123456789ABCDEFxz"[rnd() % 18];
                buf[at + 2] = "0123456789ABCDEFxz"[rnd() % 18];
                len += 3;
            }
            break;
        default: /* duplicate a chunk */
            if (len && len + 16 < cap) {
                size_t c = 1 + rnd() % 15;
                if (at + c > len) {
                    c = len - at;
                }
                memmove(buf + at + c, buf + at, len - at);
                len += c;
            }
            break;
        }
    }
    return len;
}

static void test_a_quarter_of_a_million_mutated_forms_never_break_the_parser_and_accepted_ones_are_valid(void)
{
    static char buf[PROV_BODY_MAX + 64];
    prov_creds c;
    long accepted = 0, i;

    for (i = 0; i < 250000; i++) {
        size_t len = sizeof(VALID) - 1;
        prov_status st;
        memcpy(buf, VALID, len);
        len = mutate(buf, len, sizeof(buf));
        st = prov_parse_form(buf, len, SESSION, &c);
        if (st == PROV_OK) {
            accepted++;
            TEST_ASSERT_TRUE(prov_valid_ssid(c.ssid));
            TEST_ASSERT_TRUE(prov_valid_pass(c.pass));
            TEST_ASSERT_TRUE(prov_valid_server(c.server));
            TEST_ASSERT_TRUE(prov_valid_token(c.token));
        } else {
            TEST_ASSERT_TRUE_MESSAGE(c.ssid[0] == '\0' && c.token[0] == '\0', "nothing of a refused form is kept");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(accepted > 1000, "the fuzzer reaches the accepting path too");
    TEST_ASSERT_TRUE_MESSAGE(accepted < 240000, "and the refusing ones");
}

static void test_completely_random_bodies_of_every_length_are_refused_without_trouble(void)
{
    static char buf[PROV_BODY_MAX + 64];
    prov_creds c;
    long i;
    for (i = 0; i < 60000; i++) {
        size_t len = rnd() % (PROV_BODY_MAX + 40), k;
        for (k = 0; k < len; k++) {
            buf[k] = (i & 1) ? (char)rnd() : NASTY[rnd() % sizeof(NASTY)];
        }
        TEST_ASSERT_NOT_EQUAL(PROV_OK, prov_parse_form(buf, len, SESSION, &c));
    }
}

static void test_the_decoder_never_writes_past_what_it_was_given(void)
{
    char buf[40];
    long i;
    for (i = 0; i < 100000; i++) {
        char src[64];
        size_t len = rnd() % 60, cap = 1 + rnd() % 30, k;
        for (k = 0; k < len; k++) {
            src[k] = (rnd() & 3) ? NASTY[rnd() % sizeof(NASTY)] : (char)rnd();
        }
        memset(buf, 0x55, sizeof(buf));
        if (prov_url_decode(buf, cap, src, len)) {
            TEST_ASSERT_TRUE(strlen(buf) < cap);
        }
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x55, buf[cap < sizeof(buf) ? cap : sizeof(buf) - 1], "past the capacity");
    }
}

static size_t valid_query(uint8_t *q)
{
    static const uint8_t base[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
                                   3,    'c',  'o',  'm',  0,    0x00, 0x01, 0x00, 0x01};
    memcpy(q, base, sizeof(base));
    return sizeof(base);
}

static void test_random_and_mutated_dns_packets_are_dropped_or_answered_in_bounds(void)
{
    static const uint8_t ip[4] = {192, 168, 4, 1};
    uint8_t q[PROV_DNS_MAX + 40], a[PROV_DNS_MAX + 64];
    long answered = 0, i;

    for (i = 0; i < 300000; i++) {
        size_t len, k, cap = (i % 5 == 0) ? rnd() % 64 : sizeof(a) - 8, r;
        if (i & 1) {
            len = valid_query(q);
            for (k = 0; k < 1 + rnd() % 4; k++) {
                q[rnd() % len] = (uint8_t)rnd();
            }
            if (rnd() % 4 == 0) {
                len = rnd() % (len + 1);
            }
        } else {
            len = rnd() % (PROV_DNS_MAX + 30);
            for (k = 0; k < len; k++) {
                q[k] = (uint8_t)rnd();
            }
        }
        memset(a, 0xA5, sizeof(a));
        r = prov_dns_answer(q, len, a, cap, ip);
        if (r) {
            answered++;
            TEST_ASSERT_TRUE(r <= cap);
            TEST_ASSERT_TRUE_MESSAGE(r >= 17, "a header and a question at least");
            TEST_ASSERT_EQUAL_HEX8(q[0], a[0]);
            TEST_ASSERT_EQUAL_HEX8(q[1], a[1]);
            TEST_ASSERT_TRUE((a[2] & 0x80) != 0);
            TEST_ASSERT_TRUE(r + 1 > sizeof(a) || a[r] == 0xA5);
        }
    }
    TEST_ASSERT_TRUE(answered > 10000);
}

static void test_pages_built_from_hostile_networks_stay_in_their_buffer_and_never_carry_markup(void)
{
    static char page[4200];
    prov_net nets[PROV_NETS_MAX];
    long i;

    for (i = 0; i < 4000; i++) {
        int n = (int)(rnd() % 20), k;
        size_t cap = (i % 3) ? sizeof(page) - 100 : rnd() % 3000, r;
        char reason[80];
        for (k = 0; k < n && k < PROV_NETS_MAX; k++) {
            size_t l = rnd() % 33, j;
            for (j = 0; j < l; j++) {
                nets[k].ssid[j] = (rnd() & 1) ? "<>\"'&"[rnd() % 5] : (char)(1 + rnd() % 254);
                if (nets[k].ssid[j] == '\0') {
                    nets[k].ssid[j] = 'x';
                }
            }
            nets[k].ssid[l] = '\0';
        }
        for (k = 0; k < 79; k++) {
            reason[k] = "<>\"'& ab"[rnd() % 8];
        }
        reason[79] = '\0';
        memset(page, 0xA5, sizeof(page));
        r = prov_page_form(page, cap, nets, n < PROV_NETS_MAX ? n : PROV_NETS_MAX, SESSION, (i & 1) ? reason : NULL);
        if (cap == 0) {
            TEST_ASSERT_EQUAL_UINT(0, r);
            continue;
        }
        TEST_ASSERT_TRUE(r < cap);
        TEST_ASSERT_EQUAL_UINT(r, strlen(page));
        if (r) {
            const char *p = page;
            int tags = 0;
            for (; *p; p++) {
                tags += *p == '<';
            }
            /* the template has a fixed number of tags; the hostile text adds none */
            TEST_ASSERT_TRUE_MESSAGE(tags < 40 + PROV_NETS_MAX * 0 + 20, "no markup came from the data");
            TEST_ASSERT_NULL(strstr(page, "<script"));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_quarter_of_a_million_mutated_forms_never_break_the_parser_and_accepted_ones_are_valid);
    RUN_TEST(test_completely_random_bodies_of_every_length_are_refused_without_trouble);
    RUN_TEST(test_the_decoder_never_writes_past_what_it_was_given);
    RUN_TEST(test_random_and_mutated_dns_packets_are_dropped_or_answered_in_bounds);
    RUN_TEST(test_pages_built_from_hostile_networks_stay_in_their_buffer_and_never_carry_markup);
    return UNITY_END();
}
