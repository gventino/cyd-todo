#include "jsonr/jsonr.h"

#include <string.h>

enum {
    S_VALUE,       /* a value is expected */
    S_ARR_FIRST,   /* the first value of an array, or its end */
    S_OBJ_FIRST,   /* the first key of an object, or its end */
    S_OBJ_KEY,     /* a key (after a comma) */
    S_COLON,
    S_AFTER_VALUE, /* a comma or the end of the container */
    S_STRING,
    S_ESC,
    S_HI1,         /* after a high surrogate: the backslash of the next \u */
    S_HI2,         /* ... and its u */
    S_UNI,
    S_NUMBER,
    S_LITERAL,
    S_DONE,
    S_ERROR
};
enum { F_IS_KEY = 1 };

void jsonr_init(jsonr *p, jsonr_fn fn, void *user, size_t max_bytes)
{
    memset(p, 0, sizeof(*p));
    p->fn = fn;
    p->user = user;
    p->max_bytes = max_bytes;
    p->state = S_VALUE;
}

static jsonr_status fail(jsonr *p, jsonr_status e)
{
    p->state = S_ERROR;
    p->err = e;
    return e;
}

/* ---- events ---- */

static bool emit(jsonr *p, jsonr_kind kind, const char *str, size_t len, int64_t i, bool b)
{
    jsonr_event e;
    int l;

    memset(&e, 0, sizeof(e));
    e.kind = kind;
    e.depth = p->depth;
    for (l = 0; l < p->depth; l++) {
        e.key[l] = p->is_arr[l] ? NULL : p->keys[l];
        e.idx[l] = p->is_arr[l] ? p->index[l] : -1;
    }
    e.str = str;
    e.len = len;
    e.i = i;
    e.b = b;
    return p->fn ? p->fn(p->user, &e) : true;
}

/* A value is complete: the container it is in moves on (an array counts it), or the document is done. */
static void value_done(jsonr *p)
{
    if (p->depth == 0) {
        p->state = S_DONE;
        return;
    }
    if (p->is_arr[p->depth - 1]) {
        p->index[p->depth - 1]++;
    }
    p->state = S_AFTER_VALUE;
}

#define EMIT(p, ...)                                       \
    do {                                                   \
        if (!emit((p), __VA_ARGS__)) {                     \
            return fail((p), JSONR_ERR_SCHEMA);            \
        }                                                  \
    } while (0)

/* ---- strings ---- */

static bool append(jsonr *p, const char *s, size_t n)
{
    size_t cap = (p->flags & F_IS_KEY) ? JSONR_KEY_MAX : JSONR_STR_MAX;

    if (p->len + n > cap) {
        return false;
    }
    memcpy(p->buf + p->len, s, n);
    p->len += n;
    return true;
}

static size_t utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF. */
static bool utf8_valid(const char *s, size_t n)
{
    size_t i = 0;

    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        size_t need;
        uint32_t cp, min;
        if (c < 0x80) {
            i++;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF) {
            need = 1, cp = c & 0x1Fu, min = 0x80;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2, cp = c & 0x0Fu, min = 0x800;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3, cp = c & 0x07u, min = 0x10000;
        } else {
            return false;
        }
        if (i + need >= n) { /* the sequence runs past the end */
            return false;
        }
        for (size_t k = 1; k <= need; k++) {
            unsigned char d = (unsigned char)s[i + k];
            if ((d & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (d & 0x3Fu);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += need + 1;
    }
    return true;
}

static jsonr_status string_end(jsonr *p)
{
    p->buf[p->len] = '\0';
    if (!utf8_valid(p->buf, p->len)) {
        return fail(p, JSONR_ERR_SYNTAX);
    }
    if (p->flags & F_IS_KEY) {
        memcpy(p->keys[p->depth - 1], p->buf, p->len + 1);
        p->state = S_COLON;
        return JSONR_OK;
    }
    EMIT(p, J_STRING, p->buf, p->len, 0, false);
    value_done(p);
    return JSONR_OK;
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* ---- numbers ---- */

static bool num_char(char c)
{
    return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
}

static jsonr_status number_end(jsonr *p)
{
    const char *s = p->num;
    size_t n = p->num_len, i = 0;
    bool integer = true, neg = false;
    uint64_t v = 0;
    bool overflow = false;

    p->num[n] = '\0';
    if (i < n && s[i] == '-') {
        neg = true;
        i++;
    }
    if (i >= n || s[i] < '0' || s[i] > '9') {
        return fail(p, JSONR_ERR_SYNTAX);
    }
    if (s[i] == '0') {
        i++; /* a leading zero ends the integer part: "01" leaves a digit over and is refused below */
    } else {
        for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) {
            if (v > (UINT64_MAX - 9) / 10) {
                overflow = true;
            } else {
                v = v * 10 + (uint64_t)(s[i] - '0');
            }
        }
    }
    if (i < n && s[i] == '.') {
        size_t start = ++i;
        integer = false;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
        }
        if (i == start) {
            return fail(p, JSONR_ERR_SYNTAX);
        }
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        size_t start;
        integer = false;
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        start = i;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
        }
        if (i == start) {
            return fail(p, JSONR_ERR_SYNTAX);
        }
    }
    if (i != n) {
        return fail(p, JSONR_ERR_SYNTAX);
    }
    if (integer && !overflow && (neg ? v <= (uint64_t)INT64_MAX + 1 : v <= (uint64_t)INT64_MAX)) {
        EMIT(p, J_INT, NULL, 0, neg ? (int64_t)(0 - v) : (int64_t)v, false);
    } else {
        EMIT(p, J_NONINT, NULL, 0, 0, false);
    }
    value_done(p);
    return JSONR_OK;
}

/* ---- containers ---- */

static jsonr_status open_container(jsonr *p, bool arr)
{
    if (p->depth >= JSONR_MAX_DEPTH) {
        return fail(p, JSONR_ERR_DEPTH);
    }
    EMIT(p, arr ? J_ARRAY_BEGIN : J_OBJECT_BEGIN, NULL, 0, 0, false);
    p->is_arr[p->depth] = arr;
    p->index[p->depth] = 0;
    p->keys[p->depth][0] = '\0';
    p->depth++;
    p->state = arr ? S_ARR_FIRST : S_OBJ_FIRST;
    return JSONR_OK;
}

static jsonr_status close_container(jsonr *p, bool arr)
{
    p->depth--;
    EMIT(p, arr ? J_ARRAY_END : J_OBJECT_END, NULL, 0, 0, false);
    value_done(p);
    return JSONR_OK;
}

static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static jsonr_status start_value(jsonr *p, char c)
{
    switch (c) {
    case '{':
        return open_container(p, false);
    case '[':
        return open_container(p, true);
    case '"':
        p->flags &= (uint8_t)~F_IS_KEY;
        p->len = 0;
        p->state = S_STRING;
        return JSONR_OK;
    case 't':
        p->lit_word = "true", p->lit_kind = J_BOOL, p->lit_b = true;
        break;
    case 'f':
        p->lit_word = "false", p->lit_kind = J_BOOL, p->lit_b = false;
        break;
    case 'n':
        p->lit_word = "null", p->lit_kind = J_NULL, p->lit_b = false;
        break;
    default:
        if (c == '-' || (c >= '0' && c <= '9')) {
            p->num[0] = c;
            p->num_len = 1;
            p->state = S_NUMBER;
            return JSONR_OK;
        }
        return fail(p, JSONR_ERR_SYNTAX);
    }
    p->lit_pos = 1;
    p->state = S_LITERAL;
    return JSONR_OK;
}

static jsonr_status step(jsonr *p, char c)
{
    switch (p->state) {
    case S_VALUE:
    case S_ARR_FIRST:
        if (is_ws(c)) {
            return JSONR_OK;
        }
        if (c == ']' && p->state == S_ARR_FIRST) {
            return close_container(p, true);
        }
        return start_value(p, c);

    case S_OBJ_FIRST:
    case S_OBJ_KEY:
        if (is_ws(c)) {
            return JSONR_OK;
        }
        if (c == '}' && p->state == S_OBJ_FIRST) {
            return close_container(p, false);
        }
        if (c != '"') {
            return fail(p, JSONR_ERR_SYNTAX);
        }
        p->flags |= F_IS_KEY;
        p->len = 0;
        p->state = S_STRING;
        return JSONR_OK;

    case S_COLON:
        if (is_ws(c)) {
            return JSONR_OK;
        }
        if (c != ':') {
            return fail(p, JSONR_ERR_SYNTAX);
        }
        p->state = S_VALUE;
        return JSONR_OK;

    case S_AFTER_VALUE:
        if (is_ws(c)) {
            return JSONR_OK;
        }
        if (c == ',') {
            p->state = p->is_arr[p->depth - 1] ? S_VALUE : S_OBJ_KEY;
            return JSONR_OK;
        }
        if ((c == '}' && !p->is_arr[p->depth - 1]) || (c == ']' && p->is_arr[p->depth - 1])) {
            return close_container(p, c == ']');
        }
        return fail(p, JSONR_ERR_SYNTAX);

    case S_STRING:
        if (c == '"') {
            return string_end(p);
        }
        if (c == '\\') {
            p->state = S_ESC;
            return JSONR_OK;
        }
        if ((unsigned char)c < 0x20) {
            return fail(p, JSONR_ERR_SYNTAX);
        }
        return append(p, &c, 1) ? JSONR_OK : fail(p, JSONR_ERR_TOO_LONG);

    case S_ESC: {
        char out;
        switch (c) {
        case '"':
            out = '"';
            break;
        case '\\':
            out = '\\';
            break;
        case '/':
            out = '/';
            break;
        case 'b':
            out = '\b';
            break;
        case 'f':
            out = '\f';
            break;
        case 'n':
            out = '\n';
            break;
        case 'r':
            out = '\r';
            break;
        case 't':
            out = '\t';
            break;
        case 'u':
            p->u = 0;
            p->u_n = 0;
            p->state = S_UNI;
            return JSONR_OK;
        default:
            return fail(p, JSONR_ERR_SYNTAX);
        }
        p->state = S_STRING;
        return append(p, &out, 1) ? JSONR_OK : fail(p, JSONR_ERR_TOO_LONG);
    }

    case S_HI1:
        if (c != '\\') {
            return fail(p, JSONR_ERR_SYNTAX);
        }
        p->state = S_HI2;
        return JSONR_OK;

    case S_HI2:
        if (c != 'u') {
            return fail(p, JSONR_ERR_SYNTAX);
        }
        p->u = 0;
        p->u_n = 0;
        p->state = S_UNI;
        return JSONR_OK;

    case S_UNI: {
        int h = hex(c);
        char out[4];
        uint32_t cp;
        if (h < 0) {
            return fail(p, JSONR_ERR_SYNTAX);
        }
        p->u = (p->u << 4) | (uint32_t)h;
        if (++p->u_n < 4) {
            return JSONR_OK;
        }
        cp = p->u;
        if (p->hi) { /* the second half of a pair */
            if (cp < 0xDC00 || cp > 0xDFFF) {
                return fail(p, JSONR_ERR_SYNTAX);
            }
            cp = 0x10000 + ((p->hi - 0xD800) << 10) + (cp - 0xDC00);
            p->hi = 0;
        } else if (cp >= 0xD800 && cp <= 0xDBFF) { /* the first half: the second must follow at once */
            p->hi = cp;
            p->state = S_HI1;
            return JSONR_OK;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) { /* a second half alone */
            return fail(p, JSONR_ERR_SYNTAX);
        }
        if (cp == 0) { /* strings are C strings */
            return fail(p, JSONR_ERR_SYNTAX);
        }
        p->state = S_STRING;
        return append(p, out, utf8_encode(cp, out)) ? JSONR_OK : fail(p, JSONR_ERR_TOO_LONG);
    }

    case S_NUMBER:
        if (num_char(c)) {
            if (p->num_len >= JSONR_NUM_MAX) {
                return fail(p, JSONR_ERR_SYNTAX);
            }
            p->num[p->num_len++] = c;
            return JSONR_OK;
        } else {
            jsonr_status s = number_end(p);
            return s != JSONR_OK ? s : step(p, c); /* the character that ended the number is read again */
        }

    case S_LITERAL:
        if (c != p->lit_word[p->lit_pos]) {
            return fail(p, JSONR_ERR_SYNTAX);
        }
        if (p->lit_word[++p->lit_pos] == '\0') {
            EMIT(p, p->lit_kind, NULL, 0, 0, p->lit_b);
            value_done(p);
        }
        return JSONR_OK;

    case S_DONE:
        return is_ws(c) ? JSONR_OK : fail(p, JSONR_ERR_SYNTAX);

    default:
        return p->err ? p->err : JSONR_ERR_SYNTAX;
    }
}

jsonr_status jsonr_feed(jsonr *p, const char *data, size_t n)
{
    size_t i;

    if (p->state == S_ERROR) {
        return p->err;
    }
    if (p->seen > p->max_bytes || n > p->max_bytes - p->seen) {
        return fail(p, JSONR_ERR_SIZE);
    }
    p->seen += n;
    for (i = 0; i < n; i++) {
        jsonr_status s = step(p, data[i]);
        if (s != JSONR_OK) {
            return s;
        }
    }
    return JSONR_OK;
}

jsonr_status jsonr_end(jsonr *p)
{
    if (p->state == S_ERROR) {
        return p->err;
    }
    if (p->state == S_NUMBER && p->depth == 0) { /* a number at the top level ends with the input */
        jsonr_status s = number_end(p);
        if (s != JSONR_OK) {
            return s;
        }
    }
    return p->state == S_DONE ? JSONR_OK : fail(p, JSONR_ERR_SYNTAX);
}
