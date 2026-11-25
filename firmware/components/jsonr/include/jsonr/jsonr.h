#ifndef JSONR_H
#define JSONR_H

/*
 * A streaming JSON reader for bytes that come from a network: it is fed a piece at a time (as the socket gives them), keeps
 * a small fixed state, allocates nothing, and reports what it finds as events. Everything is bounded: the depth of the
 * document, the length of a key and of a string, the total number of bytes. Anything outside the rules is an error and the
 * reader stops for good: a caller that wants all-or-nothing keeps what it parsed apart until jsonr_end says OK.
 *
 * What it accepts is strict JSON (RFC 8259): no comments, no trailing commas, no single quotes, no leading zeros, no NUL in
 * a string, strings that are valid UTF-8, \u escapes with their surrogate pairs in order.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JSONR_MAX_DEPTH 6
#define JSONR_KEY_MAX 24
#define JSONR_STR_MAX 256
#define JSONR_NUM_MAX 28

typedef enum {
    JSONR_OK = 0,
    JSONR_ERR_SYNTAX,  /* not JSON (or not complete at the end) */
    JSONR_ERR_DEPTH,   /* nested deeper than JSONR_MAX_DEPTH */
    JSONR_ERR_TOO_LONG,/* a key or a string over its limit */
    JSONR_ERR_SIZE,    /* more bytes than the limit given to jsonr_init */
    JSONR_ERR_SCHEMA   /* the handler refused something */
} jsonr_status;

typedef enum { J_OBJECT_BEGIN, J_OBJECT_END, J_ARRAY_BEGIN, J_ARRAY_END, J_STRING, J_INT, J_NONINT, J_BOOL, J_NULL } jsonr_kind;

/*
 * One thing that was read. `depth` is how many containers enclose it (the top value is 0, a field of the top object 1).
 * For level L < depth: key[L] is the key under which the value (or its ancestor) sits when that container is an object, NULL
 * for an array, and idx[L] the position in it when it is an array. For J_STRING `str` is the decoded text (NUL-terminated,
 * valid UTF-8, `len` bytes); for J_INT `i`; for J_BOOL `b`. J_NONINT is a number that is not an integer or does not fit int64.
 */
typedef struct {
    jsonr_kind kind;
    int depth;
    const char *key[JSONR_MAX_DEPTH];
    int32_t idx[JSONR_MAX_DEPTH];
    const char *str;
    size_t len;
    int64_t i;
    bool b;
} jsonr_event;

/* Called for every event; return false to refuse the document (the reader then reports JSONR_ERR_SCHEMA). */
typedef bool (*jsonr_fn)(void *user, const jsonr_event *e);

typedef struct {
    jsonr_fn fn;
    void *user;
    size_t max_bytes, seen;
    uint8_t state, depth, lit_pos, num_len, u_n, flags;
    jsonr_kind lit_kind;
    const char *lit_word;
    bool lit_b;
    jsonr_status err;
    uint8_t is_arr[JSONR_MAX_DEPTH];
    int32_t index[JSONR_MAX_DEPTH];
    char keys[JSONR_MAX_DEPTH][JSONR_KEY_MAX + 1];
    char buf[JSONR_STR_MAX + 1];
    size_t len;
    uint32_t u, hi;
    char num[JSONR_NUM_MAX + 1];
} jsonr;

void jsonr_init(jsonr *p, jsonr_fn fn, void *user, size_t max_bytes);

/* Feeds `n` bytes. Returns JSONR_OK while all is well (the document may or may not be complete), else the error, for good. */
jsonr_status jsonr_feed(jsonr *p, const char *data, size_t n);

/* After the last piece: JSONR_OK only if a whole document was read and nothing but white space followed it. */
jsonr_status jsonr_end(jsonr *p);

#endif /* JSONR_H */
