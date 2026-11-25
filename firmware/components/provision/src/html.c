#include <stdio.h>
#include <string.h>

#include "prov/form.h"

/* A growing page in a caller's buffer: once it does not fit, everything after is ignored and the result is 0. */
typedef struct {
    char *buf;
    size_t cap, len;
    bool full;
} page;

static void put(page *p, const char *s)
{
    size_t n = strlen(s);

    if (p->full || p->len + n + 1 > p->cap) {
        p->full = true;
        return;
    }
    memcpy(p->buf + p->len, s, n + 1);
    p->len += n;
}

static void put_escaped(page *p, const char *s)
{
    size_t room;

    if (p->full) {
        return;
    }
    room = p->cap - p->len;
    if (!prov_html_escape(p->buf + p->len, room, s)) {
        p->full = true;
        return;
    }
    p->len += strlen(p->buf + p->len);
}

static size_t finish(page *p)
{
    if (p->full) {
        if (p->cap) {
            p->buf[0] = '\0';
        }
        return 0;
    }
    return p->len;
}

bool prov_html_escape(char *dst, size_t cap, const char *src)
{
    size_t n = 0;

    if (!dst || cap == 0) {
        return false;
    }
    for (; *src; src++) {
        const char *rep = NULL;
        switch (*src) {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        case '\'':
            rep = "&#39;";
            break;
        default:
            break;
        }
        if (rep) {
            size_t r = strlen(rep);
            if (n + r + 1 > cap) {
                dst[0] = '\0';
                return false;
            }
            memcpy(dst + n, rep, r);
            n += r;
        } else {
            if (n + 2 > cap) {
                dst[0] = '\0';
                return false;
            }
            dst[n++] = *src;
        }
    }
    dst[n] = '\0';
    return true;
}

#define HEAD                                                                                                                   \
    "<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"     \
    "<title>cyd-todo setup</title>"                                                                                            \
    "<style>body{font:16px sans-serif;margin:1em;max-width:30em}input{width:100%;box-sizing:border-box;font-size:1em;"         \
    "padding:.5em;margin:.2em 0 1em}button{font-size:1em;padding:.6em 1.2em}.e{color:#b00}</style><h1>cyd-todo</h1>"

size_t prov_page_form(char *out, size_t cap, const prov_net *nets, int n_nets, const char *session, const char *reason)
{
    page p = {out, cap, 0, false};
    int i;

    if (!out || cap == 0 || !session) {
        return 0;
    }
    out[0] = '\0';
    put(&p, HEAD);
    if (reason && reason[0]) {
        put(&p, "<p class=e>");
        put_escaped(&p, reason);
        put(&p, "</p>");
    }
    put(&p, "<form method=post action=/save><input type=hidden name=session value=\"");
    put_escaped(&p, session);
    put(&p, "\"><label>Wi-Fi network</label><input name=ssid list=nets maxlength=32 required autocomplete=off><datalist id=nets>");
    for (i = 0; i < n_nets && i < PROV_NETS_MAX; i++) {
        put(&p, "<option value=\"");
        put_escaped(&p, nets[i].ssid);
        put(&p, "\">");
    }
    put(&p, "</datalist><label>Password</label><input name=pass type=password maxlength=63 autocomplete=off>"
            "<label>Server address</label><input name=server maxlength=32 required placeholder=\"192.168.1.50:8080\" autocomplete=off>"
            "<label>Device token</label><input name=token type=password minlength=32 maxlength=64 required autocomplete=off>"
            "<button>Save and connect</button></form>");
    return finish(&p);
}

size_t prov_page_done(char *out, size_t cap, const char *ssid)
{
    page p = {out, cap, 0, false};

    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    put(&p, HEAD "<p>Saved. The board is joining <b>");
    put_escaped(&p, ssid ? ssid : "");
    put(&p, "</b> now. You can close this page; the screen of the board tells you if it worked.</p>");
    return finish(&p);
}

size_t prov_page_redirect(char *out, size_t cap)
{
    page p = {out, cap, 0, false};

    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    put(&p, "<!doctype html><title>cyd-todo</title><a href=\"http://192.168.4.1/\">cyd-todo setup</a>");
    return finish(&p);
}
