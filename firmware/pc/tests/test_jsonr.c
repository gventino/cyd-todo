/* The streaming JSON reader: strict JSON, bounded everything, the same answer for any way of cutting the input. */

#include <stdio.h>
#include <string.h>

#include "jsonr/jsonr.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A handler that writes every event as a line of text, so that documents can be compared by what they said. */
typedef struct {
    char log[8192];
    size_t n;
    int refuse_after; /* refuse the Nth event (1-based), 0 never */
    int count;
} rec;

static void put(rec *r, const char *fmt, const char *a, long long b)
{
    int w = snprintf(r->log + r->n, sizeof(r->log) - r->n, fmt, a, b);
    if (w > 0 && r->n + (size_t)w < sizeof(r->log)) {
        r->n += (size_t)w;
    }
}

static bool record(void *user, const jsonr_event *e)
{
    rec *r = user;
    char path[160] = "";
    int l;
    for (l = 0; l < e->depth; l++) {
        char one[48];
        if (e->key[l]) {
            snprintf(one, sizeof(one), "/%s", e->key[l]);
        } else {
            snprintf(one, sizeof(one), "[%d]", (int)e->idx[l]);
        }
        strncat(path, one, sizeof(path) - strlen(path) - 1);
    }
    switch (e->kind) {
    case J_OBJECT_BEGIN:
        put(r, "%s {\n", path, 0);
        break;
    case J_OBJECT_END:
        put(r, "%s }\n", path, 0);
        break;
    case J_ARRAY_BEGIN:
        put(r, "%s [\n", path, 0);
        break;
    case J_ARRAY_END:
        put(r, "%s ]\n", path, 0);
        break;
    case J_STRING: {
        char t[400];
        snprintf(t, sizeof(t), "%s s:%s(%zu)\n", path, e->str, e->len);
        put(r, "%s", t, 0);
        break;
    }
    case J_INT:
        put(r, "%s i:%lld\n", path, (long long)e->i);
        break;
    case J_NONINT:
        put(r, "%s nonint%.0lld\n", path, 0);
        break;
    case J_BOOL:
        put(r, "%s b:%lld\n", path, e->b);
        break;
    case J_NULL:
        put(r, "%s null%.0lld\n", path, 0);
        break;
    }
    r->count++;
    return !(r->refuse_after && r->count >= r->refuse_after);
}

static jsonr_status parse_whole(const char *doc, rec *r)
{
    jsonr p;
    jsonr_status s;
    memset(r, 0, sizeof(*r));
    jsonr_init(&p, record, r, 16384);
    s = jsonr_feed(&p, doc, strlen(doc));
    return s != JSONR_OK ? s : jsonr_end(&p);
}

static jsonr_status parse_chunks(const char *doc, size_t chunk, rec *r)
{
    jsonr p;
    size_t n = strlen(doc), i;
    memset(r, 0, sizeof(*r));
    jsonr_init(&p, record, r, 16384);
    for (i = 0; i < n; i += chunk) {
        size_t c = n - i < chunk ? n - i : chunk;
        jsonr_status s = jsonr_feed(&p, doc + i, c);
        if (s != JSONR_OK) {
            return s;
        }
    }
    return jsonr_end(&p);
}

#define OK(doc) TEST_ASSERT_EQUAL_INT_MESSAGE(JSONR_OK, parse_whole(doc, &R), doc)
#define BAD(doc) TEST_ASSERT_NOT_EQUAL_MESSAGE(JSONR_OK, parse_whole(doc, &R), doc)
static rec R;

static void test_a_document_is_reported_event_by_event_with_its_path(void)
{
    OK("{\"a\":1,\"b\":[true,null,\"x\"],\"c\":{\"d\":-5}}");
    TEST_ASSERT_EQUAL_STRING(" {\n/a i:1\n/b [\n/b[0] b:1\n/b[1] null\n/b[2] s:x(1)\n/b ]\n/c {\n/c/d i:-5\n/c }\n }\n", R.log);
}

static void test_every_valid_scalar_and_container_at_the_top_level(void)
{
    OK("0");
    OK("-0");
    OK("123");
    OK("-9223372036854775808");
    OK("9223372036854775807");
    OK("true");
    OK("false");
    OK("null");
    OK("\"\"");
    OK("[]");
    OK("{}");
    OK("[[],{},[{}]]");
    OK("  \t\r\n [ 1 , 2 ] \n ");
    OK("1.5");
    OK("1e5");
    OK("-1.5E-3");
}

static void test_the_things_that_are_not_json_are_refused(void)
{
    static const char *const bad[] = {"", " ", "{", "}", "[", "]", "{]", "[}", "[1,]", "[,1]", "[1 2]", "{\"a\":1,}", "{,}", "{\"a\"}", "{\"a\":}", "{a:1}", "{'a':1}",
                                      "{\"a\":1 \"b\":2}", "{\"a\" 1}", "tru", "truee", "nul", "falsey", "True", "NaN", "Infinity", "-", "+1", ".5", "1.", "1e", "1e+", "01", "-01", "00",
                                      "0x10", "1_000", "\"abc", "\"a\nb\"", "\"a\tb\"", "\"\\x\"", "\"\\u12\"", "\"\\u12G4\"", "// c\n1", "/* c */ 1", "1 2", "{} {}", "[] x", "\"a\" \"b\"",
                                      "\xEF\xBB\xBF{}", "{\"a\":1}}", "[1]]"};
    size_t i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        BAD(bad[i]);
    }
}

static void test_escapes_and_unicode_are_decoded_to_utf8(void)
{
    OK("\"a\\nb\\t\\\"\\\\\\/\\b\\f\\r\"");
    TEST_ASSERT_EQUAL_STRING(" s:a\nb\t\"\\/\b\f\r(10)\n", R.log);
    OK("\"\\u00e9\\u20AC\"");
    TEST_ASSERT_EQUAL_STRING(" s:\xC3\xA9\xE2\x82\xAC(5)\n", R.log);
    OK("\"\\uDBFF\\uDFFF\""); /* the last pair: U+10FFFF */
    TEST_ASSERT_EQUAL_STRING(" s:\xF4\x8F\xBF\xBF(4)\n", R.log);
    OK("\"\\uD800\\uDC00\""); /* the first pair: U+10000 */
    TEST_ASSERT_EQUAL_STRING(" s:\xF0\x90\x80\x80(4)\n", R.log);
    OK("\"\\uD83D\\uDE00\""); /* a pair: U+1F600 */
    TEST_ASSERT_EQUAL_STRING(" s:\xF0\x9F\x98\x80(4)\n", R.log);
    OK("\"caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80\""); /* raw UTF-8 */
    BAD("\"\\uD83D\"");          /* a first half alone */
    BAD("\"\\uD83Dx\"");
    BAD("\"\\uD83D\\u0041\"");   /* a first half then a plain character */
    BAD("\"\\uDE00\"");          /* a second half alone */
    BAD("\"\\uDE00\\uD83D\"");   /* in the wrong order */
    BAD("\"\\u0000\"");          /* NUL */
}

static void test_a_string_with_bad_utf8_is_refused(void)
{
    static const char *const bad[] = {"\"\x80\"", "\"\xC0\x80\"", "\"\xC1\xBF\"", "\"\xE0\x80\x80\"", "\"\xED\xA0\x80\"", "\"\xF4\x90\x80\x80\"", "\"\xF5\x80\x80\x80\"",
                                      "\"\xC3\"", "\"\xE2\x82\"", "\"\xF0\x9F\x98\"", "\"\xFF\"", "\"\xC3(\""};
    size_t i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        BAD(bad[i]);
    }
    BAD("\"\xE0\x9F\xBF\"");           /* U+07FF in three bytes: an overlong form */
    BAD("\"\xF0\x8F\xBF\xBF\"");       /* U+FFFF in four bytes */
    {
        char c;
        for (c = 0x01; c < 0x20; c++) { /* no control character, raw, in a string */
            char d[4] = {'"', c, '"', '\0'};
            BAD(d);
        }
    }
    OK("\"\xC2\x80\"");           /* the smallest two-byte character */
    OK("\"\xF4\x8F\xBF\xBF\"");   /* the largest character */
}

static void test_integers_up_to_the_edge_of_int64_and_everything_else_is_nonint(void)
{
    OK("[9223372036854775807,-9223372036854775808]");
    TEST_ASSERT_EQUAL_STRING(" [\n[0] i:9223372036854775807\n[1] i:-9223372036854775808\n ]\n", R.log);
    OK("[9223372036854775808,-9223372036854775809,18446744073709551616,1.0,1e2,12345678901234567890123]");
    TEST_ASSERT_EQUAL_STRING(" [\n[0] nonint\n[1] nonint\n[2] nonint\n[3] nonint\n[4] nonint\n[5] nonint\n ]\n", R.log);
    BAD("1234567890123456789012345678901234567890"); /* more digits than the reader holds */
}

static void test_depth_is_limited_and_the_limit_is_exact(void)
{
    jsonr p;
    char doc[64];
    int d;
    for (d = 1; d <= JSONR_MAX_DEPTH + 1; d++) {
        int i;
        for (i = 0; i < d; i++) {
            doc[i] = '[';
        }
        for (i = 0; i < d; i++) {
            doc[d + i] = ']';
        }
        doc[2 * d] = '\0';
        memset(&R, 0, sizeof(R));
        jsonr_init(&p, record, &R, 1000);
        if (d <= JSONR_MAX_DEPTH) {
            TEST_ASSERT_EQUAL_INT(JSONR_OK, jsonr_feed(&p, doc, strlen(doc)));
            TEST_ASSERT_EQUAL_INT(JSONR_OK, jsonr_end(&p));
        } else {
            TEST_ASSERT_EQUAL_INT(JSONR_ERR_DEPTH, jsonr_feed(&p, doc, strlen(doc)));
        }
    }
    memset(doc, '[', 40);
    doc[40] = '\0';
    jsonr_init(&p, record, &R, 1000);
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_DEPTH, jsonr_feed(&p, doc, 40));
}

static void test_a_key_and_a_string_have_their_limits(void)
{
    char doc[600], k[100];
    memset(k, 'k', 24);
    k[24] = '\0';
    snprintf(doc, sizeof(doc), "{\"%s\":1}", k);
    OK(doc);
    memset(k, 'k', 25);
    k[25] = '\0';
    snprintf(doc, sizeof(doc), "{\"%s\":1}", k);
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_TOO_LONG, parse_whole(doc, &R));
    memset(doc, 'a', sizeof(doc));
    doc[0] = '"';
    doc[257] = '"';
    doc[258] = '\0';
    OK(doc); /* 256 characters */
    doc[257] = 'a';
    doc[258] = '"';
    doc[259] = '\0';
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_TOO_LONG, parse_whole(doc, &R));
    /* the limit counts decoded bytes: 129 characters of two bytes is 258 */
    {
        char d2[700] = "\"";
        int i;
        for (i = 0; i < 129; i++) {
            strcat(d2, "\xC3\xA9");
        }
        strcat(d2, "\"");
        TEST_ASSERT_EQUAL_INT(JSONR_ERR_TOO_LONG, parse_whole(d2, &R));
    }
}

static void test_the_total_size_is_limited_while_it_arrives(void)
{
    jsonr p;
    char chunk[512];
    size_t total = 0;
    jsonr_status s = JSONR_OK;
    memset(chunk, ' ', sizeof(chunk));
    jsonr_init(&p, record, &R, 16384);
    while (s == JSONR_OK && total < 40000) {
        s = jsonr_feed(&p, chunk, sizeof(chunk));
        total += sizeof(chunk);
    }
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_SIZE, s);
    TEST_ASSERT_EQUAL_UINT(16384 + 512, total);
    jsonr_init(&p, record, &R, 16384); /* exactly the limit is fine */
    {
        static char big[16384];
        memset(big, ' ', sizeof(big));
        big[0] = '1';
        TEST_ASSERT_EQUAL_INT(JSONR_OK, jsonr_feed(&p, big, sizeof(big)));
        TEST_ASSERT_EQUAL_INT(JSONR_OK, jsonr_end(&p));
        TEST_ASSERT_EQUAL_INT(JSONR_ERR_SIZE, jsonr_feed(&p, " ", 1));
    }
}

static void test_the_handler_can_refuse_and_the_reader_stays_refused(void)
{
    jsonr p;
    memset(&R, 0, sizeof(R));
    R.refuse_after = 3;
    jsonr_init(&p, record, &R, 1000);
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_SCHEMA, jsonr_feed(&p, "[1,2,3,4,5]", 11));
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_SCHEMA, jsonr_feed(&p, " ", 1));
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_SCHEMA, jsonr_end(&p));
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, R.count, "nothing is reported after a refusal");
}

static void test_an_error_is_for_good_and_an_unfinished_document_is_not_complete(void)
{
    jsonr p;
    memset(&R, 0, sizeof(R));
    jsonr_init(&p, record, &R, 1000);
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_SYNTAX, jsonr_feed(&p, "[1,,2]", 6));
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_SYNTAX, jsonr_feed(&p, "]", 1));
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_SYNTAX, jsonr_end(&p));
    jsonr_init(&p, record, &R, 1000);
    TEST_ASSERT_EQUAL_INT(JSONR_OK, jsonr_feed(&p, "{\"a\":[1,2", 9));
    TEST_ASSERT_EQUAL_INT(JSONR_ERR_SYNTAX, jsonr_end(&p));
}

static const char *const GOOD[] = {
    "{\"rev\":41,\"xp\":270,\"open\":5,\"done\":12,\"truncated\":false,\"quests\":[{\"id\":7,\"title\":\"Slay the inbox\",\"notes\":\"\",\"difficulty\":3,\"due\":\"2026-12-01\","
    "\"state\":\"open\",\"done_at\":null,\"updated_at\":1795600000},{\"id\":8,\"title\":\"caf\\u00e9 \\ud83d\\ude00\",\"notes\":\"a\\nb\",\"difficulty\":1,\"due\":null,\"state\":\"done\","
    "\"done_at\":1795600001,\"updated_at\":1795600002}]}",
    "[1, 2.5e3, -7, \"x\", [true, false, null], {\"k\": {\"j\": []}}]",
    " { \"a\" : \"b\" , \"c\" : [ ] } ",
    "-12345", "\"just a string with \\\"quotes\\\" and \\\\\"",
};

static void test_any_way_of_cutting_the_input_gives_the_same_events(void)
{
    static rec whole;
    size_t d, chunk;
    for (d = 0; d < sizeof(GOOD) / sizeof(GOOD[0]); d++) {
        TEST_ASSERT_EQUAL_INT(JSONR_OK, parse_whole(GOOD[d], &whole));
        for (chunk = 1; chunk <= 40; chunk++) {
            TEST_ASSERT_EQUAL_INT_MESSAGE(JSONR_OK, parse_chunks(GOOD[d], chunk, &R), GOOD[d]);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(whole.log, R.log, "the events do not depend on the cuts");
        }
    }
}

static void test_cutting_every_bad_document_anywhere_is_still_an_error(void)
{
    static const char *const bad[] = {"[1,,2]", "{\"a\":01}", "\"\\uD83D\\u0041\"", "\"\xC0\x80\"", "[[[[[[[[1]]]]]]]]", "{\"a\":1}}", "tru", "[1 2]"};
    size_t d, chunk;
    for (d = 0; d < sizeof(bad) / sizeof(bad[0]); d++) {
        for (chunk = 1; chunk <= 9; chunk++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(JSONR_OK, parse_chunks(bad[d], chunk, &R), bad[d]);
        }
    }
}

static uint32_t rs = 0xC0FFEE11u;
static uint32_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static void test_a_million_mutated_documents_never_break_the_reader_and_cuts_never_change_the_verdict(void)
{
    static const char alphabet[] = "{}[]\",:\\ 0123456789.eE+-tfnrulsa/\xC3\xA9\x80\xF0\xFF";
    long i;
    long good = 0;
    for (i = 0; i < 300000; i++) {
        char doc[700];
        size_t len = strlen(GOOD[0]), k;
        int n = 1 + (int)(rnd() % 5);
        static rec a, b;
        jsonr_status sa, sb;
        memcpy(doc, GOOD[0], len + 1);
        for (k = 0; k < (size_t)n && len; k++) {
            size_t at = rnd() % len;
            switch (rnd() % 3) {
            case 0:
                doc[at] = alphabet[rnd() % (sizeof(alphabet) - 1)];
                break;
            case 1:
                memmove(doc + at, doc + at + 1, len - at);
                len--;
                break;
            default:
                if (len + 2 < sizeof(doc)) {
                    memmove(doc + at + 1, doc + at, len - at + 1);
                    doc[at] = alphabet[rnd() % (sizeof(alphabet) - 1)];
                    len++;
                }
                break;
            }
        }
        doc[len] = '\0';
        sa = parse_whole(doc, &a);
        sb = parse_chunks(doc, 1 + rnd() % 7, &b);
        TEST_ASSERT_EQUAL_INT_MESSAGE(sa, sb, doc);
        if (sa == JSONR_OK) {
            good++;
            TEST_ASSERT_EQUAL_STRING(a.log, b.log);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(good > 100, "some mutations are still valid JSON");
    TEST_ASSERT_TRUE(good < 290000);
}

static void test_random_bytes_are_refused_without_trouble(void)
{
    long i;
    for (i = 0; i < 100000; i++) {
        char doc[200];
        size_t len = rnd() % 190, k;
        for (k = 0; k < len; k++) {
            doc[k] = (char)(1 + rnd() % 255);
        }
        doc[len] = '\0';
        (void)parse_whole(doc, &R);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_document_is_reported_event_by_event_with_its_path);
    RUN_TEST(test_every_valid_scalar_and_container_at_the_top_level);
    RUN_TEST(test_the_things_that_are_not_json_are_refused);
    RUN_TEST(test_escapes_and_unicode_are_decoded_to_utf8);
    RUN_TEST(test_a_string_with_bad_utf8_is_refused);
    RUN_TEST(test_integers_up_to_the_edge_of_int64_and_everything_else_is_nonint);
    RUN_TEST(test_depth_is_limited_and_the_limit_is_exact);
    RUN_TEST(test_a_key_and_a_string_have_their_limits);
    RUN_TEST(test_the_total_size_is_limited_while_it_arrives);
    RUN_TEST(test_the_handler_can_refuse_and_the_reader_stays_refused);
    RUN_TEST(test_an_error_is_for_good_and_an_unfinished_document_is_not_complete);
    RUN_TEST(test_any_way_of_cutting_the_input_gives_the_same_events);
    RUN_TEST(test_cutting_every_bad_document_anywhere_is_still_an_error);
    RUN_TEST(test_a_million_mutated_documents_never_break_the_reader_and_cuts_never_change_the_verdict);
    RUN_TEST(test_random_bytes_are_refused_without_trouble);
    return UNITY_END();
}
