/* The setup form: what a phone (or somebody who is not a phone) can post, and the pages that go out. */

#include <stdio.h>
#include <string.h>

#include "prov/form.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define SESSION "0123456789abcdef"
#define TOKEN32 "tttttttttttttttttttttttttttttttt"

static prov_creds C;

static prov_status parse(const char *body)
{
    return prov_parse_form(body, strlen(body), SESSION, &C);
}

/* a body with each field replaceable, for the limits */
static prov_status post(const char *ssid, const char *pass, const char *server, const char *token, const char *session)
{
    static char body[PROV_BODY_MAX + 64];
    snprintf(body, sizeof(body), "ssid=%s&pass=%s&server=%s&token=%s&session=%s", ssid, pass, server, token, session);
    return prov_parse_form(body, strlen(body), SESSION, &C);
}

static void repeat(char *dst, char c, size_t n)
{
    memset(dst, c, n);
    dst[n] = '\0';
}

static void test_a_good_form_is_read_field_by_field(void)
{
    TEST_ASSERT_EQUAL_INT(PROV_OK, parse("ssid=home-net&pass=hunter2hunter2&server=192.168.1.50%3A8080&token=" TOKEN32 "&session=" SESSION));
    TEST_ASSERT_EQUAL_STRING("home-net", C.ssid);
    TEST_ASSERT_EQUAL_STRING("hunter2hunter2", C.pass);
    TEST_ASSERT_EQUAL_STRING("192.168.1.50:8080", C.server);
    TEST_ASSERT_EQUAL_STRING(TOKEN32, C.token);
}

static void test_fields_may_come_in_any_order_and_an_open_network_has_an_empty_password(void)
{
    TEST_ASSERT_EQUAL_INT(PROV_OK, parse("session=" SESSION "&token=" TOKEN32 "&server=nas.local&pass=&ssid=Cafe"));
    TEST_ASSERT_EQUAL_STRING("Cafe", C.ssid);
    TEST_ASSERT_EQUAL_STRING("", C.pass);
    TEST_ASSERT_EQUAL_STRING("nas.local", C.server);
}

static void test_percent_encoding_and_plus_are_decoded(void)
{
    TEST_ASSERT_EQUAL_INT(PROV_OK, post("My%20Net+2", "a%26b%3Dc%2Bd%25e12", "h.lan", TOKEN32, SESSION));
    TEST_ASSERT_EQUAL_STRING("My Net 2", C.ssid);
    TEST_ASSERT_EQUAL_STRING("a&b=c+d%e12", C.pass);
    TEST_ASSERT_EQUAL_INT(PROV_OK, post("caf%C3%A9", "", "h.lan", TOKEN32, SESSION));
    TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", C.ssid);
    TEST_ASSERT_EQUAL_INT(PROV_OK, post("lower%e9", "", "h.lan", TOKEN32, SESSION)); /* lowercase hex, a byte that is not UTF-8 */
}

static void test_the_size_of_every_field_is_checked_at_the_edge(void)
{
    char a[200], b[600];
    repeat(a, 'n', 32);
    TEST_ASSERT_EQUAL_INT(PROV_OK, post(a, "", "h.lan", TOKEN32, SESSION));
    repeat(a, 'n', 33);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SSID, post(a, "", "h.lan", TOKEN32, SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SSID, post("", "", "h.lan", TOKEN32, SESSION));
    repeat(a, 'p', 7);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_PASS, post("n", a, "h.lan", TOKEN32, SESSION));
    repeat(a, 'p', 8);
    TEST_ASSERT_EQUAL_INT(PROV_OK, post("n", a, "h.lan", TOKEN32, SESSION));
    repeat(a, 'p', 63);
    TEST_ASSERT_EQUAL_INT(PROV_OK, post("n", a, "h.lan", TOKEN32, SESSION));
    repeat(a, 'p', 64);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_PASS, post("n", a, "h.lan", TOKEN32, SESSION));
    repeat(a, 's', 2);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SERVER, post("n", "", a, TOKEN32, SESSION));
    repeat(a, 's', 3);
    TEST_ASSERT_EQUAL_INT(PROV_OK, post("n", "", a, TOKEN32, SESSION));
    repeat(a, 's', 32);
    TEST_ASSERT_EQUAL_INT(PROV_OK, post("n", "", a, TOKEN32, SESSION));
    repeat(a, 's', 33);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SERVER, post("n", "", a, TOKEN32, SESSION));
    repeat(b, 't', 31);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_TOKEN, post("n", "", "h.lan", b, SESSION));
    repeat(b, 't', 64);
    TEST_ASSERT_EQUAL_INT(PROV_OK, post("n", "", "h.lan", b, SESSION));
    repeat(b, 't', 65);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_TOKEN, post("n", "", "h.lan", b, SESSION));
    repeat(b, 't', 500);
    TEST_ASSERT_EQUAL_INT_MESSAGE(PROV_ERR_BODY, post("n", "", "h.lan", b, SESSION), "a body over 512 bytes is not looked at");
}

static void test_a_body_of_exactly_the_limit_is_read_and_one_byte_more_is_not(void)
{
    static char body[PROV_BODY_MAX + 8];
    size_t n = (size_t)snprintf(body, sizeof(body), "ssid=n&pass=&server=h.lan&session=" SESSION "&token=");
    memset(body + n, 't', PROV_BODY_MAX - n); /* ends up too long for a token, but the body itself is at the limit */
    TEST_ASSERT_EQUAL_INT(PROV_ERR_TOKEN, prov_parse_form(body, PROV_BODY_MAX, SESSION, &C));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, prov_parse_form(body, PROV_BODY_MAX + 1, SESSION, &C));
}

static void test_a_nul_a_bad_escape_or_a_control_character_never_gets_in(void)
{
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, post("a%00b", "", "h.lan", TOKEN32, SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, post("a%zzb", "", "h.lan", TOKEN32, SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, post("a%4", "", "h.lan", TOKEN32, SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, post("a%", "", "h.lan", TOKEN32, SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SSID, post("a%0Ab", "", "h.lan", TOKEN32, SESSION)); /* a newline in a name */
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SSID, post("a%7Fb", "", "h.lan", TOKEN32, SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_PASS, post("n", "pass%C3%A9word", "h.lan", TOKEN32, SESSION)); /* WPA wants ASCII */
    TEST_ASSERT_EQUAL_INT(PROV_ERR_PASS, post("n", "pass%09word", "h.lan", TOKEN32, SESSION));
    TEST_ASSERT_EQUAL_INT_MESSAGE(PROV_ERR_TOKEN, post("n", "", "h.lan", "tttttttttttttttttttttttttttttt+t", SESSION), "a plus is a space");
    TEST_ASSERT_EQUAL_INT(PROV_ERR_TOKEN, post("n", "", "h.lan", "tttttttttttttttttttttttttttttt%20t", SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_TOKEN, post("n", "", "h.lan", "tttttttttttttttttttttttttttttt%0Dt", SESSION));
}

static void test_the_server_address_is_a_host_and_a_port_and_nothing_else(void)
{
    static const char *const good[] = {"192.168.1.50", "192.168.1.50:8080", "nas.local", "my-server.home.lan:1", "a.b:65535", "Host1:80", "a:1"};
    static const char *const bad[] = {"http://nas", "nas/", "nas:", "nas:0", "nas:65536", "nas:99999", "nas:80:80", "nas:8o", "-nas", "nas-", ".nas", "nas.",
                                      "na..s", "na s", "na_s", "[::1]:80", "nas:+80", "nas?x", "nas#", "user@nas", "nas:000000"};
    size_t i;
    for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        TEST_ASSERT_TRUE_MESSAGE(prov_valid_server(good[i]), good[i]);
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(prov_valid_server(bad[i]), bad[i]);
    }
}

static void test_unknown_repeated_and_missing_fields_are_refused(void)
{
    TEST_ASSERT_EQUAL_INT(PROV_ERR_FIELD, parse("ssid=n&pass=&server=h.lan&token=" TOKEN32 "&session=" SESSION "&extra=1"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_FIELD, parse("ssid=n&ssid=m&pass=&server=h.lan&token=" TOKEN32 "&session=" SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_FIELD, parse("pass=&server=h.lan&token=" TOKEN32 "&session=" SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_FIELD, parse("ssid=n&server=h.lan&token=" TOKEN32 "&session=" SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_FIELD, parse("ssid=n&pass=&token=" TOKEN32 "&session=" SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_FIELD, parse("ssid=n&pass=&server=h.lan&session=" SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_FIELD, parse("ssid=n&pass=&server=h.lan&token=" TOKEN32));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_FIELD, parse("SSID=n&pass=&server=h.lan&token=" TOKEN32 "&session=" SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_FIELD, parse("ssid%3D=n&pass=&server=h.lan&token=" TOKEN32 "&session=" SESSION));
}

static void test_a_body_that_is_not_a_form_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, parse(""));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, parse("ssid"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, parse("&"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, parse("ssid=n&pass=&server=h.lan&token=" TOKEN32 "&session=" SESSION "&"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, parse("{\"ssid\":\"n\"}"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, prov_parse_form(NULL, 0, SESSION, &C));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, prov_parse_form("a=b", 3, NULL, &C));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, prov_parse_form("a=b", 3, "short", &C));
}

static void test_a_post_must_bring_back_the_value_of_the_page_it_came_from(void)
{
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SESSION, post("n", "", "h.lan", TOKEN32, "0123456789abcdee"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SESSION, post("n", "", "h.lan", TOKEN32, "0123456789abcde"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SESSION, post("n", "", "h.lan", TOKEN32, "0123456789abcdef0"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SESSION, post("n", "", "h.lan", TOKEN32, ""));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SESSION, post("n", "", "h.lan", TOKEN32, "0123456789ABCDEF"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BODY, post("n", "", "h.lan", TOKEN32, "%00123456789abcdef"));
}

static void test_the_first_wrong_field_is_the_one_reported_and_nothing_is_kept(void)
{
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SESSION, post("", "x", "", "", "wrong"));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SSID, post("", "x", "", "", SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_PASS, post("n", "x", "", "", SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SERVER, post("n", "", "", "", SESSION));
    TEST_ASSERT_EQUAL_INT(PROV_ERR_TOKEN, post("n", "", "h.lan", "", SESSION));
    TEST_ASSERT_EQUAL_STRING("", C.ssid);
    TEST_ASSERT_EQUAL_STRING("", C.token);
}

static void test_every_status_has_a_fixed_sentence_that_repeats_nothing_the_client_sent(void)
{
    int s;
    for (s = PROV_OK; s <= PROV_ERR_TOKEN; s++) {
        const char *t = prov_status_text((prov_status)s);
        TEST_ASSERT_TRUE(strlen(t) > 5);
        TEST_ASSERT_NULL(strchr(t, '%'));
    }
}

static void test_html_escaping_covers_the_five_characters_and_says_when_it_does_not_fit(void)
{
    char o[64];
    TEST_ASSERT_TRUE(prov_html_escape(o, sizeof(o), "<a href=\"x\">&'</a>"));
    TEST_ASSERT_EQUAL_STRING("&lt;a href=&quot;x&quot;&gt;&amp;&#39;&lt;/a&gt;", o);
    TEST_ASSERT_TRUE(prov_html_escape(o, sizeof(o), ""));
    TEST_ASSERT_EQUAL_STRING("", o);
    TEST_ASSERT_TRUE(prov_html_escape(o, 4, "abc"));
    TEST_ASSERT_FALSE(prov_html_escape(o, 3, "abc"));
    TEST_ASSERT_EQUAL_STRING("", o);
    TEST_ASSERT_FALSE(prov_html_escape(o, 5, "<<"));
    TEST_ASSERT_FALSE(prov_html_escape(NULL, 5, "a"));
    TEST_ASSERT_FALSE(prov_html_escape(o, 0, "a"));
}

static prov_net net(const char *ssid, int8_t rssi)
{
    prov_net n;
    memset(&n, 0, sizeof(n));
    snprintf(n.ssid, sizeof(n.ssid), "%s", ssid);
    n.rssi = rssi;
    n.secured = true;
    return n;
}

static void test_the_form_page_carries_the_session_the_fields_and_no_script(void)
{
    static char page[4096];
    prov_net nets[2] = {net("home-net", -40), net("Cafe", -70)};
    size_t n = prov_page_form(page, sizeof(page), nets, 2, SESSION, NULL);
    TEST_ASSERT_TRUE(n > 500 && n == strlen(page));
    TEST_ASSERT_NOT_NULL(strstr(page, "name=session value=\"" SESSION "\""));
    TEST_ASSERT_NOT_NULL(strstr(page, "name=ssid"));
    TEST_ASSERT_NOT_NULL(strstr(page, "name=pass type=password"));
    TEST_ASSERT_NOT_NULL(strstr(page, "name=server"));
    TEST_ASSERT_NOT_NULL(strstr(page, "name=token type=password"));
    TEST_ASSERT_NOT_NULL(strstr(page, "action=/save"));
    TEST_ASSERT_NOT_NULL(strstr(page, "<option value=\"home-net\">"));
    TEST_ASSERT_NULL(strstr(page, "<script"));
    TEST_ASSERT_NULL(strstr(page, "<p class=e>"));
}

static void test_a_hostile_network_name_and_a_reason_are_escaped_everywhere(void)
{
    static char page[4096];
    prov_net nets[1] = {net("\"><script>alert(1)</script>", -50)};
    size_t n = prov_page_form(page, sizeof(page), nets, 1, SESSION, "Wrong <b>password</b> for \"x\"");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(strstr(page, "<script"));
    TEST_ASSERT_NULL(strstr(page, "<b>"));
    TEST_ASSERT_NOT_NULL(strstr(page, "&lt;script&gt;alert(1)&lt;/script&gt;"));
    TEST_ASSERT_NOT_NULL(strstr(page, "<p class=e>Wrong &lt;b&gt;password&lt;/b&gt; for &quot;x&quot;</p>"));
}

static void test_a_page_that_does_not_fit_is_nothing_and_never_cut_in_the_middle(void)
{
    static char page[4096];
    prov_net nets[1] = {net("home", -40)};
    size_t full = prov_page_form(page, sizeof(page), nets, 1, SESSION, NULL), cap;
    for (cap = 1; cap < full + 1; cap += 17) {
        char small[4096];
        memset(small, 'x', sizeof(small));
        TEST_ASSERT_EQUAL_UINT(0, prov_page_form(small, cap, nets, 1, SESSION, NULL));
        TEST_ASSERT_EQUAL_CHAR('\0', small[0]);
    }
    TEST_ASSERT_EQUAL_UINT(full, prov_page_form(page, full + 1, nets, 1, SESSION, NULL));
    TEST_ASSERT_EQUAL_UINT(0, prov_page_form(page, full, nets, 1, SESSION, NULL));
    TEST_ASSERT_EQUAL_UINT(0, prov_page_form(NULL, 10, nets, 1, SESSION, NULL));
    TEST_ASSERT_EQUAL_UINT(0, prov_page_form(page, 0, nets, 1, SESSION, NULL));
}

static void test_sixteen_networks_fit_the_page_buffer_and_more_are_ignored(void)
{
    static char page[4096];
    prov_net nets[20];
    char name[40];
    int i, count = 0;
    const char *p;
    for (i = 0; i < 20; i++) {
        repeat(name, (char)('a' + i), 32); /* the longest names */
        nets[i] = net(name, -50);
    }
    TEST_ASSERT_TRUE(prov_page_form(page, sizeof(page), nets, 20, SESSION, "x") > 0);
    for (p = page; (p = strstr(p, "<option")) != NULL; p++) {
        count++;
    }
    TEST_ASSERT_EQUAL_INT(PROV_NETS_MAX, count);
    TEST_ASSERT_TRUE_MESSAGE(strlen(page) < 3000, "the page is small enough for a 4 KB buffer");
}

static void test_the_done_and_redirect_pages(void)
{
    char page[1024];
    TEST_ASSERT_TRUE(prov_page_done(page, sizeof(page), "<i>net</i>") > 0);
    TEST_ASSERT_NULL(strstr(page, "<i>"));
    TEST_ASSERT_NOT_NULL(strstr(page, "&lt;i&gt;net&lt;/i&gt;"));
    TEST_ASSERT_EQUAL_UINT(0, prov_page_done(page, 20, "n"));
    TEST_ASSERT_TRUE(prov_page_redirect(page, sizeof(page)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(page, "192.168.4.1"));
    TEST_ASSERT_EQUAL_UINT(0, prov_page_redirect(page, 10));
}

static void test_url_decoding_on_its_own(void)
{
    char o[16];
    TEST_ASSERT_TRUE(prov_url_decode(o, sizeof(o), "a%2Fb+c", 7));
    TEST_ASSERT_EQUAL_STRING("a/b c", o);
    TEST_ASSERT_TRUE(prov_url_decode(o, sizeof(o), "", 0));
    TEST_ASSERT_FALSE(prov_url_decode(o, 3, "abc", 3));
    TEST_ASSERT_TRUE(prov_url_decode(o, 4, "abc", 3));
    TEST_ASSERT_FALSE(prov_url_decode(o, sizeof(o), "%2", 2));
    TEST_ASSERT_FALSE(prov_url_decode(o, sizeof(o), "%2G", 3));
    TEST_ASSERT_FALSE(prov_url_decode(NULL, 5, "a", 1));
    TEST_ASSERT_FALSE(prov_url_decode(o, 0, "a", 1));
    TEST_ASSERT_TRUE_MESSAGE(prov_url_decode(o, sizeof(o), "a%25", 4) && strcmp(o, "a%") == 0, "%25 is a percent sign, not an escape again");
    TEST_ASSERT_TRUE(prov_url_decode(o, sizeof(o), "%2541", 5) && strcmp(o, "%41") == 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_good_form_is_read_field_by_field);
    RUN_TEST(test_fields_may_come_in_any_order_and_an_open_network_has_an_empty_password);
    RUN_TEST(test_percent_encoding_and_plus_are_decoded);
    RUN_TEST(test_the_size_of_every_field_is_checked_at_the_edge);
    RUN_TEST(test_a_body_of_exactly_the_limit_is_read_and_one_byte_more_is_not);
    RUN_TEST(test_a_nul_a_bad_escape_or_a_control_character_never_gets_in);
    RUN_TEST(test_the_server_address_is_a_host_and_a_port_and_nothing_else);
    RUN_TEST(test_unknown_repeated_and_missing_fields_are_refused);
    RUN_TEST(test_a_body_that_is_not_a_form_is_refused);
    RUN_TEST(test_a_post_must_bring_back_the_value_of_the_page_it_came_from);
    RUN_TEST(test_the_first_wrong_field_is_the_one_reported_and_nothing_is_kept);
    RUN_TEST(test_every_status_has_a_fixed_sentence_that_repeats_nothing_the_client_sent);
    RUN_TEST(test_html_escaping_covers_the_five_characters_and_says_when_it_does_not_fit);
    RUN_TEST(test_the_form_page_carries_the_session_the_fields_and_no_script);
    RUN_TEST(test_a_hostile_network_name_and_a_reason_are_escaped_everywhere);
    RUN_TEST(test_a_page_that_does_not_fit_is_nothing_and_never_cut_in_the_middle);
    RUN_TEST(test_sixteen_networks_fit_the_page_buffer_and_more_are_ignored);
    RUN_TEST(test_the_done_and_redirect_pages);
    RUN_TEST(test_url_decoding_on_its_own);
    return UNITY_END();
}
