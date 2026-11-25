#include "prov/form.h"

#include <string.h>

static bool printable(unsigned char c)
{
    return c >= 0x21 && c <= 0x7E;
}

static int hexval(char c)
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

bool prov_url_decode(char *dst, size_t cap, const char *src, size_t len)
{
    size_t i = 0, n = 0;

    if (!dst || cap == 0) {
        return false;
    }
    dst[0] = '\0';
    while (i < len) {
        char c = src[i];
        if (c == '%') {
            int hi, lo;
            if (i + 3 > len) {
                return false;
            }
            hi = hexval(src[i + 1]);
            lo = hexval(src[i + 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            c = (char)(hi * 16 + lo);
            i += 3;
        } else {
            c = c == '+' ? ' ' : c;
            i++;
        }
        if (c == '\0' || n + 1 >= cap) { /* a NUL would end the string early: refuse it */
            dst[0] = '\0';
            return false;
        }
        dst[n++] = c;
    }
    dst[n] = '\0';
    return true;
}

bool prov_valid_ssid(const char *s)
{
    size_t n = strlen(s), i;

    if (n < 1 || n > PROV_SSID_MAX) {
        return false;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
    }
    return true;
}

bool prov_valid_pass(const char *s)
{
    size_t n = strlen(s), i;

    if (n == 0) {
        return true;
    }
    if (n < PROV_PASS_MIN || n > PROV_PASS_MAX) {
        return false;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7E) {
            return false;
        }
    }
    return true;
}

static bool alnum(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool prov_valid_server(const char *s)
{
    size_t n = strlen(s), host_end = 0, i;
    long port = 0;

    if (n < PROV_SERVER_MIN || n > PROV_SERVER_MAX) {
        return false;
    }
    while (host_end < n && s[host_end] != ':') {
        host_end++;
    }
    if (host_end == 0 || !alnum(s[0]) || !alnum(s[host_end - 1])) {
        return false;
    }
    for (i = 0; i < host_end; i++) {
        if (!alnum(s[i]) && s[i] != '.' && s[i] != '-') {
            return false;
        }
        if (s[i] == '.' && i + 1 < host_end && s[i + 1] == '.') {
            return false;
        }
    }
    if (host_end == n) {
        return true;
    }
    if (n - host_end < 2 || n - host_end > 6) { /* ":" and 1 to 5 digits */
        return false;
    }
    for (i = host_end + 1; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        port = port * 10 + (s[i] - '0');
    }
    return port >= 1 && port <= 65535;
}

bool prov_valid_token(const char *s)
{
    size_t n = strlen(s), i;

    if (n < PROV_TOKEN_MIN || n > PROV_TOKEN_MAX) {
        return false;
    }
    for (i = 0; i < n; i++) {
        if (!printable((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

/* ---- the form ---- */

enum { F_SSID, F_PASS, F_SERVER, F_TOKEN, F_SESSION, F_COUNT };

static int field_of(const char *name, size_t n)
{
    static const char *const names[F_COUNT] = {"ssid", "pass", "server", "token", "session"};
    int f;

    for (f = 0; f < F_COUNT; f++) {
        if (strlen(names[f]) == n && memcmp(names[f], name, n) == 0) {
            return f;
        }
    }
    return -1;
}

prov_status prov_parse_form(const char *body, size_t len, const char *session, prov_creds *out)
{
    /* one more byte than the largest value of each field, so that "too long" is told from "just fits" */
    char vals[F_COUNT][PROV_TOKEN_MAX + 2];
    bool seen[F_COUNT] = {false}, too_long[F_COUNT] = {false};
    size_t pos = 0;
    unsigned diff = 0;
    size_t i;
    int f;

    if (!body || !session || !out || len > PROV_BODY_MAX || strlen(session) != PROV_SESSION_LEN) {
        return PROV_ERR_BODY;
    }
    memset(out, 0, sizeof(*out));
    while (pos <= len) { /* "a=b&c=d": the last pair ends at len */
        size_t end = pos, eq;
        while (end < len && body[end] != '&') {
            end++;
        }
        eq = pos;
        while (eq < end && body[eq] != '=') {
            eq++;
        }
        if (eq == end) { /* no '=' */
            return PROV_ERR_BODY;
        }
        f = field_of(body + pos, eq - pos);
        if (f < 0 || seen[f]) {
            return PROV_ERR_FIELD;
        }
        seen[f] = true;
        if (!prov_url_decode(vals[f], sizeof(vals[f]), body + eq + 1, end - eq - 1)) {
            /* a value that does not fit is "too long" for its field; anything else is a bad body */
            char probe[PROV_BODY_MAX + 1];
            if (end - eq - 1 <= PROV_BODY_MAX && prov_url_decode(probe, sizeof(probe), body + eq + 1, end - eq - 1)) {
                too_long[f] = true;
                vals[f][0] = '\0';
            } else {
                return PROV_ERR_BODY;
            }
        }
        pos = end + 1;
    }
    for (f = 0; f < F_COUNT; f++) {
        if (!seen[f]) {
            return PROV_ERR_FIELD;
        }
    }
    for (i = 0; i < PROV_SESSION_LEN; i++) { /* constant time: the value is a secret of this page */
        diff |= (unsigned char)vals[F_SESSION][i] ^ (unsigned char)session[i];
    }
    if (too_long[F_SESSION] || strlen(vals[F_SESSION]) != PROV_SESSION_LEN || diff != 0) {
        return PROV_ERR_SESSION;
    }
    if (too_long[F_SSID] || !prov_valid_ssid(vals[F_SSID])) {
        return PROV_ERR_SSID;
    }
    if (too_long[F_PASS] || !prov_valid_pass(vals[F_PASS])) {
        return PROV_ERR_PASS;
    }
    if (too_long[F_SERVER] || !prov_valid_server(vals[F_SERVER])) {
        return PROV_ERR_SERVER;
    }
    if (too_long[F_TOKEN] || !prov_valid_token(vals[F_TOKEN])) {
        return PROV_ERR_TOKEN;
    }
    memcpy(out->ssid, vals[F_SSID], strlen(vals[F_SSID]) + 1);
    memcpy(out->pass, vals[F_PASS], strlen(vals[F_PASS]) + 1);
    memcpy(out->server, vals[F_SERVER], strlen(vals[F_SERVER]) + 1);
    memcpy(out->token, vals[F_TOKEN], strlen(vals[F_TOKEN]) + 1);
    return PROV_OK;
}

const char *prov_status_text(prov_status s)
{
    switch (s) {
    case PROV_OK:
        return "Saved.";
    case PROV_ERR_BODY:
        return "The form could not be read. Try again.";
    case PROV_ERR_FIELD:
        return "The form was not complete. Try again.";
    case PROV_ERR_SESSION:
        return "This page is out of date. Reload it and try again.";
    case PROV_ERR_SSID:
        return "The network name must be 1 to 32 characters.";
    case PROV_ERR_PASS:
        return "The password must be empty or 8 to 63 plain characters.";
    case PROV_ERR_SERVER:
        return "The server address must look like 192.168.1.50:8080.";
    case PROV_ERR_TOKEN:
        return "The device token must be 32 to 64 characters without spaces.";
    }
    return "Something went wrong.";
}
