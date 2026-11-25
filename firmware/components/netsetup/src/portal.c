/* The captive portal: an HTTP server on port 80 and a DNS answerer on port 53 of the board's access point. Everything that
 * is decided about the bytes is prov/form.h's and prov/dnsans.h's; this file moves them in and out. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "internal.h"
#include "lwip/sockets.h"
#include "prov/dnsans.h"

static const char *TAG = "portal";

#define PAGE_MAX 4096

static httpd_handle_t server;
static char *page;                       /* one page at a time: the server runs its handlers on one task */
static prov_net nets[PROV_NETS_MAX];
static int n_nets;
static char session[PROV_SESSION_LEN + 1];
static portal_hooks hooks;
static char reason[96];                  /* what the next form page says, once */

static void headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Content-Security-Policy", PROV_CSP);
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t send_form(httpd_req_t *req, const char *status)
{
    size_t n = prov_page_form(page, PAGE_MAX, nets, n_nets, session, reason[0] ? reason : NULL);

    reason[0] = '\0';
    headers(req);
    if (status) {
        httpd_resp_set_status(req, status);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, n ? (ssize_t)n : 0);
}

static esp_err_t get_root(httpd_req_t *req)
{
    return send_form(req, NULL);
}

/* Every other path goes to the form: that is what makes the phone open it by itself. */
static esp_err_t redirect(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    headers(req);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, "", 0);
}

static esp_err_t refuse(httpd_req_t *req, const char *status, const char *why)
{
    headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, why, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t post_save(httpd_req_t *req)
{
    static char body[PROV_BODY_MAX + 1];
    static prov_creds creds;
    size_t got = 0, total = req->content_len;
    prov_status st;

    if (!hooks.accepting || !*hooks.accepting) {
        return refuse(req, "403 Forbidden", "The setup is closed.");
    }
    if (total == 0 || total > PROV_BODY_MAX) {
        return refuse(req, "413 Payload Too Large", "Too large.");
    }
    while (got < total) {
        int r = httpd_req_recv(req, body + got, total - got);
        if (r <= 0) { /* a timeout or a closed connection: nothing is half-read into a decision */
            return ESP_FAIL;
        }
        got += (size_t)r;
    }
    st = prov_parse_form(body, got, session, &creds);
    if (st != PROV_OK) {
        snprintf(reason, sizeof(reason), "%s", prov_status_text(st));
        return send_form(req, "400 Bad Request");
    }
    if (creds_save(&creds) != ESP_OK) {
        snprintf(reason, sizeof(reason), "The board could not save it. Try again.");
        return send_form(req, "500 Internal Server Error");
    }
    headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, page, (ssize_t)prov_page_done(page, PAGE_MAX, creds.ssid));
    ESP_LOGI(TAG, "credentials stored"); /* never what they are */
    hooks.on_saved(&creds);
    memset(&creds, 0, sizeof(creds));
    memset(body, 0, sizeof(body));
    return ESP_OK;
}

/* ---- DNS ---- */

static volatile bool dns_run;
static TaskHandle_t dns_waiter;

static void dns_task(void *arg)
{
    static const uint8_t ip[4] = {192, 168, 4, 1};
    uint8_t in[PROV_DNS_MAX + 8], out[PROV_DNS_MAX + 32];
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    struct timeval tv = {.tv_sec = 1};
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    (void)arg;
    if (s >= 0 && bind(s, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        while (dns_run) {
            struct sockaddr_in from;
            socklen_t fl = sizeof(from);
            int n = recvfrom(s, in, sizeof(in), 0, (struct sockaddr *)&from, &fl);
            if (n > 0) {
                size_t r = prov_dns_answer(in, (size_t)n, out, sizeof(out), ip);
                if (r) {
                    sendto(s, out, r, 0, (struct sockaddr *)&from, fl);
                }
            }
        }
    } else {
        ESP_LOGE(TAG, "no DNS socket");
    }
    if (s >= 0) {
        close(s);
    }
    xTaskNotifyGive(dns_waiter);
    vTaskDelete(NULL);
}

esp_err_t portal_start(const portal_hooks *h, const prov_net *list, int n, const char sess[PROV_SESSION_LEN + 1])
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    static const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = get_root};
    static const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = post_save};

    if (server) {
        return ESP_ERR_INVALID_STATE;
    }
    hooks = *h;
    n_nets = n > PROV_NETS_MAX ? PROV_NETS_MAX : n;
    memcpy(nets, list, (size_t)n_nets * sizeof(prov_net));
    memcpy(session, sess, PROV_SESSION_LEN + 1);
    reason[0] = '\0';
    if (!page) {
        page = malloc(PAGE_MAX);
        if (!page) {
            return ESP_ERR_NO_MEM;
        }
    }
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5; /* a client that stalls is let go */
    cfg.send_wait_timeout = 5;
    cfg.stack_size = 6144;
    cfg.max_uri_handlers = 4;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        server = NULL;
        return ESP_FAIL;
    }
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect);
    httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, redirect);
    dns_run = true;
    dns_waiter = xTaskGetCurrentTaskHandle();
    xTaskCreate(dns_task, "dns", 4096, NULL, 4, NULL);
    return ESP_OK;
}

void portal_stop(void)
{
    if (dns_run) {
        dns_run = false;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000)); /* the task ends within a second */
    }
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    free(page);
    page = NULL;
    memset(session, 0, sizeof(session));
}
