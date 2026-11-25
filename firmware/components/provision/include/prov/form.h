#ifndef PROV_FORM_H
#define PROV_FORM_H

/*
 * The setup form: what the phone posts, how it is checked, and the HTML that is served. Pure C, no heap, every buffer
 * bounded: the board's HTTP handler only moves bytes in and out. Everything that arrives is hostile until it passed here.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROV_BODY_MAX 512 /* the largest POST body that is looked at */
#define PROV_SSID_MAX 32
#define PROV_PASS_MIN 8
#define PROV_PASS_MAX 63
#define PROV_SERVER_MIN 3
#define PROV_SERVER_MAX 32 /* "host" or "host:port", no scheme (it ends up in a QR code on the server's side) */
#define PROV_TOKEN_MIN 32
#define PROV_TOKEN_MAX 64
#define PROV_SESSION_LEN 16 /* hex characters of the per-page field that a post must return */
#define PROV_NETS_MAX 16

typedef struct {
    char ssid[PROV_SSID_MAX + 1];
    char pass[PROV_PASS_MAX + 1];
    char server[PROV_SERVER_MAX + 1];
    char token[PROV_TOKEN_MAX + 1];
} prov_creds;

typedef struct {
    char ssid[PROV_SSID_MAX + 1];
    int8_t rssi;
    bool secured;
} prov_net;

typedef enum {
    PROV_OK = 0,
    PROV_ERR_BODY,    /* too long, not a form, bad percent-encoding, a NUL */
    PROV_ERR_FIELD,   /* an unknown, repeated or missing field */
    PROV_ERR_SESSION, /* the page it came from is not the one served */
    PROV_ERR_SSID,
    PROV_ERR_PASS,
    PROV_ERR_SERVER,
    PROV_ERR_TOKEN
} prov_status;

/* Reads `body` (application/x-www-form-urlencoded) into `out`. `session` is the value that was put in the page. */
prov_status prov_parse_form(const char *body, size_t len, const char *session, prov_creds *out);

/* A fixed sentence for a status, never containing what the client sent. */
const char *prov_status_text(prov_status s);

/* Percent-decodes src[0..len) into dst (NUL-terminated); '+' is a space. False on a bad escape, a NUL, or no room. */
bool prov_url_decode(char *dst, size_t cap, const char *src, size_t len);

bool prov_valid_ssid(const char *s);
bool prov_valid_pass(const char *s);   /* empty (an open network) or 8..63 printable ASCII */
bool prov_valid_server(const char *s); /* host[:port], letters, digits, dots and hyphens */
bool prov_valid_token(const char *s);

/* Escapes & < > " ' for HTML text and attribute values. False (and an empty string) if it does not fit. */
bool prov_html_escape(char *dst, size_t cap, const char *src);

/* The pages. Each returns the length, or 0 if the page does not fit `cap`. `reason` may be NULL. */
size_t prov_page_form(char *out, size_t cap, const prov_net *nets, int n_nets, const char *session, const char *reason);
size_t prov_page_done(char *out, size_t cap, const char *ssid);
size_t prov_page_redirect(char *out, size_t cap); /* the body of the redirect every other path gets */

/* The Content-Security-Policy of every answer of the portal: no script, nothing loaded, the form posts to itself. */
#define PROV_CSP "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'"

#endif /* PROV_FORM_H */
