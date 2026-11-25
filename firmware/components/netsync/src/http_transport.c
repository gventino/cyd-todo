#include "http_transport.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "http";

#define TOTAL_MS 15000
#define PIECE 512

todo_http http_perform(void *user, const char *method, const char *path, const char *body, size_t body_len, const todo_sink *sink)
{
    http_target *t = user;
    todo_http h = {0, false, false, false};
    esp_http_client_config_t cfg;
    esp_http_client_handle_t c;
    char auth[96];
    char buf[PIECE];
    int64_t started = esp_timer_get_time(), deadline = started + (int64_t)TOTAL_MS * 1000;
    int64_t clen;
    size_t got = 0;

    if (!t->online || t->host[0] == '\0') {
        h.offline = true;
        return h;
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.host = t->host;
    cfg.port = t->port;
    cfg.path = path;
    cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;
    cfg.timeout_ms = 8000; /* one wait; the whole exchange is bounded below */
    cfg.disable_auto_redirect = true;
    cfg.keep_alive_enable = false;
    cfg.buffer_size = 1024;
    cfg.buffer_size_tx = 1024;
    cfg.method = strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : (strcmp(method, "PATCH") == 0 ? HTTP_METHOD_PATCH : (strcmp(method, "DELETE") == 0 ? HTTP_METHOD_DELETE : HTTP_METHOD_GET));
    c = esp_http_client_init(&cfg);
    if (!c) {
        h.offline = true;
        return h;
    }
    snprintf(auth, sizeof(auth), "Bearer %s", t->token); /* never logged */
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "Connection", "close");
    if (esp_http_client_open(c, (int)body_len) != ESP_OK) { /* could not connect */
        memset(auth, 0, sizeof(auth));
        esp_http_client_cleanup(c);
        h.offline = true;
        return h;
    }
    memset(auth, 0, sizeof(auth));
    if (body_len && esp_http_client_write(c, body, (int)body_len) != (int)body_len) {
        h.offline = true;
        goto done;
    }
    clen = esp_http_client_fetch_headers(c);
    if (clen < 0) { /* nothing that was an answer */
        h.offline = true;
        goto done;
    }
    h.status = esp_http_client_get_status_code(c);
    if (esp_http_client_is_chunked_response(c)) {
        clen = -1;
    } else if (h.status == 204 || h.status == 304) {
        clen = 0;
    }
    if (!sink->head(sink->ctx, h.status, (long)clen)) {
        h.aborted = true;
        goto done;
    }
    while (clen >= 0 && (int64_t)got < clen) {
        int want = clen - (int64_t)got < PIECE ? (int)(clen - (int64_t)got) : PIECE, r;
        if (esp_timer_get_time() > deadline) { /* the exchange has taken too long */
            h.incomplete = true;
            break;
        }
        r = esp_http_client_read(c, buf, want);
        if (r <= 0) { /* closed, or silent for a whole wait */
            h.incomplete = true;
            break;
        }
        got += (size_t)r;
        if (!sink->body(sink->ctx, buf, (size_t)r)) {
            h.aborted = true;
            break;
        }
    }
done:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    ESP_LOGI(TAG, "%s %.*s -> %d, %u bytes, %lld ms", method, 40, path, h.status, (unsigned)got, (long long)((esp_timer_get_time() - started) / 1000));
    return h;
}
