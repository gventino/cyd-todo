#include "netsetup.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "internal.h"

static const char *TAG = "netsetup";

#define JOIN_ATTEMPT_MS 15000
#define SETUP_WINDOW_MS 600000

typedef enum { CMD_SETUP, CMD_CANCEL, CMD_SAVED, CMD_FAILED, CMD_GOT_IP } cmd_kind;
typedef struct {
    cmd_kind kind;
    int reason; /* CMD_FAILED: the WIFI_REASON_* */
} cmd;

static QueueHandle_t queue;
static SemaphoreHandle_t lock;
static netsetup_status st; /* published, under `lock` */
static boot_flow flow;     /* the task's own */
static prov_creds creds;   /* the saved network, the task's own */
static prov_creds posted;  /* what the portal accepted, handed over under `lock` */
static volatile bool accepting;
static bool wifi_started;
static int64_t deadline_us; /* the end of the current attempt or setup window, 0 for none */
static int first_actions;   /* what the boot table asked for at the start, done by the task */

/* ---- the status the UI reads ---- */

static void publish(void (*fn)(netsetup_status *))
{
    xSemaphoreTake(lock, portMAX_DELAY);
    fn(&st);
    st.serial++;
    xSemaphoreGive(lock);
}

static void pub_flow(netsetup_status *s)
{
    s->state = flow.state;
    s->reason = flow.reason;
    s->has_creds = flow.has_creds;
    s->expired = flow.expired;
    s->joining = flow.state == BOOT_JOINING;
    if (flow.state != BOOT_NORMAL) {
        s->connected = false;
    }
    if (flow.state != BOOT_SETUP) {
        s->ap_ready = false;
    }
    snprintf(s->ssid, sizeof(s->ssid), "%s", creds.ssid);
    snprintf(s->server, sizeof(s->server), "%s", creds.server);
}

static void pub_connected(netsetup_status *s)
{
    s->connected = true;
}

static void pub_disconnected(netsetup_status *s)
{
    s->connected = false;
}

static char ap_ssid_now[PROV_AP_SSID_LEN + 1], ap_pass_now[PROV_AP_PASS_LEN + 1];

static void pub_ap(netsetup_status *s)
{
    s->ap_ready = true;
    snprintf(s->ap_ssid, sizeof(s->ap_ssid), "%s", ap_ssid_now);
    snprintf(s->ap_pass, sizeof(s->ap_pass), "%s", ap_pass_now);
}

void netsetup_get(netsetup_status *out)
{
    if (!lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = st;
    xSemaphoreGive(lock);
}

static void send(cmd_kind k, int reason)
{
    cmd c = {k, reason};
    if (queue) {
        xQueueSend(queue, &c, 0);
    }
}

void netsetup_request_setup(void)
{
    send(CMD_SETUP, 0);
}

void netsetup_cancel_setup(void)
{
    send(CMD_CANCEL, 0);
}

/* ---- the radio ---- */

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        send(CMD_FAILED, d ? d->reason : 0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        send(CMD_GOT_IP, 0);
    }
}

static esp_err_t radio_mode(wifi_mode_t mode)
{
    esp_err_t e;

    if (wifi_started) {
        esp_wifi_stop();
        wifi_started = false;
    }
    e = esp_wifi_set_mode(mode);
    if (e == ESP_OK) {
        e = esp_wifi_start();
    }
    wifi_started = e == ESP_OK;
    return e;
}

static void start_join(void)
{
    wifi_config_t c;

    portal_stop();
    accepting = false;
    memset(&c, 0, sizeof(c));
    memcpy(c.sta.ssid, creds.ssid, strlen(creds.ssid)); /* at most 32 bytes, which need no terminator */
    snprintf((char *)c.sta.password, sizeof(c.sta.password), "%s", creds.pass);
    c.sta.threshold.authmode = creds.pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN; /* no downgrade to an open network of that name */
    c.sta.pmf_cfg.capable = true;
    ESP_LOGI(TAG, "joining a saved network (%u characters)", (unsigned)strlen(creds.ssid));
    if (radio_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &c) != ESP_OK || esp_wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "the radio did not start");
        send(CMD_FAILED, 0);
        return;
    }
    deadline_us = esp_timer_get_time() + (int64_t)JOIN_ATTEMPT_MS * 1000;
}

/* The networks around, strongest first, one line per name (hidden ones and doubles are left out). */
static int scan(prov_net *out)
{
    static wifi_ap_record_t recs[24];
    uint16_t n = 24, i;
    int count = 0, j;

    if (esp_wifi_scan_start(NULL, true) != ESP_OK || esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        return 0;
    }
    for (i = 0; i < n && count < PROV_NETS_MAX; i++) { /* the driver returns them strongest first */
        const char *name = (const char *)recs[i].ssid;
        bool dup = false;
        if (name[0] == '\0' || !prov_valid_ssid(name)) {
            continue;
        }
        for (j = 0; j < count; j++) {
            dup = dup || strcmp(out[j].ssid, name) == 0;
        }
        if (!dup) {
            snprintf(out[count].ssid, sizeof(out[count].ssid), "%s", name);
            out[count].rssi = recs[i].rssi;
            out[count].secured = recs[i].authmode != WIFI_AUTH_OPEN;
            count++;
        }
    }
    esp_wifi_clear_ap_list();
    return count;
}

static void on_saved(const prov_creds *c)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    posted = *c;
    xSemaphoreGive(lock);
    accepting = false; /* one form is enough */
    send(CMD_SAVED, 0);
}

static void start_setup(void)
{
    static prov_net nets[PROV_NETS_MAX];
    uint8_t rnd[PROV_AP_RANDOM_BYTES], sess_raw[PROV_SESSION_LEN / 2];
    char session[PROV_SESSION_LEN + 1];
    wifi_config_t c;
    portal_hooks hooks = {on_saved, &accepting};
    int n, i;

    portal_stop();
    accepting = false;
    deadline_us = 0;
    if (radio_mode(WIFI_MODE_STA) != ESP_OK) { /* the scan needs the station, and has to happen before the access point exists */
        ESP_LOGE(TAG, "the radio did not start");
        return;
    }
    n = scan(nets);
    esp_fill_random(rnd, sizeof(rnd));
    prov_ap_make(rnd, ap_ssid_now, ap_pass_now);
    esp_fill_random(sess_raw, sizeof(sess_raw));
    for (i = 0; i < (int)sizeof(sess_raw); i++) {
        static const char HEX[] = "0123456789abcdef";
        session[2 * i] = HEX[sess_raw[i] >> 4];
        session[2 * i + 1] = HEX[sess_raw[i] & 15];
    }
    session[PROV_SESSION_LEN] = '\0';

    memset(&c, 0, sizeof(c));
    memcpy(c.ap.ssid, ap_ssid_now, strlen(ap_ssid_now));
    snprintf((char *)c.ap.password, sizeof(c.ap.password), "%s", ap_pass_now);
    c.ap.ssid_len = (uint8_t)strlen(ap_ssid_now);
    c.ap.channel = 6;
    c.ap.authmode = WIFI_AUTH_WPA2_PSK;
    c.ap.max_connection = 1; /* the phone that scanned the code, and nobody else */
    c.ap.pmf_cfg.required = false;
    if (radio_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &c) != ESP_OK) {
        ESP_LOGE(TAG, "the access point did not start");
        return;
    }
    if (portal_start(&hooks, nets, n, session) != ESP_OK) {
        ESP_LOGE(TAG, "the portal did not start");
        return;
    }
    accepting = true;
    deadline_us = esp_timer_get_time() + (int64_t)SETUP_WINDOW_MS * 1000;
    ESP_LOGI(TAG, "setup open: %d networks seen, access point up", n); /* the name and the password are not logged */
    publish(pub_ap);
}

static void apply(int acts)
{
    if (acts & BOOT_ACT_STOP_AP) {
        portal_stop();
        accepting = false;
    }
    if (acts & BOOT_ACT_START_AP) {
        start_setup();
    }
    if (acts & BOOT_ACT_JOIN) {
        start_join();
    }
    publish(pub_flow);
}

/* ---- the task ---- */

static void task(void *arg)
{
    cmd c;

    (void)arg;
    apply(first_actions);
    for (;;) {
        bool got = xQueueReceive(queue, &c, pdMS_TO_TICKS(250)) == pdTRUE;
        int64_t now = esp_timer_get_time();

        if (got) {
            switch (c.kind) {
            case CMD_SETUP:
                apply(boot_step(&flow, BOOT_EV_SETUP_REQUESTED, false, BOOT_FAIL_NONE));
                break;
            case CMD_CANCEL:
                apply(boot_step(&flow, BOOT_EV_SETUP_CANCELLED, false, BOOT_FAIL_NONE));
                break;
            case CMD_SAVED:
                vTaskDelay(pdMS_TO_TICKS(1500)); /* the phone is still reading "Saved": let the page finish before the access point goes */
                xSemaphoreTake(lock, portMAX_DELAY);
                creds = posted;
                memset(&posted, 0, sizeof(posted));
                xSemaphoreGive(lock);
                apply(boot_step(&flow, BOOT_EV_CREDS_SAVED, false, BOOT_FAIL_NONE));
                break;
            case CMD_GOT_IP:
                apply(boot_step(&flow, BOOT_EV_JOINED, false, BOOT_FAIL_NONE));
                publish(pub_connected);
                deadline_us = 0;
                break;
            case CMD_FAILED:
                if (flow.state == BOOT_JOINING) {
                    esp_wifi_disconnect();
                    apply(boot_step(&flow, BOOT_EV_JOIN_FAILED, false, boot_fail_from_wifi_reason(c.reason)));
                } else if (flow.state == BOOT_NORMAL) { /* the network went away: say so, and keep trying */
                    publish(pub_disconnected);
                    esp_wifi_connect();
                }
                break;
            }
        }
        if (deadline_us && now > deadline_us) {
            deadline_us = 0;
            if (flow.state == BOOT_JOINING) {
                esp_wifi_disconnect();
                apply(boot_step(&flow, BOOT_EV_JOIN_FAILED, false, BOOT_FAIL_TIMEOUT));
            } else if (flow.state == BOOT_SETUP) {
                ESP_LOGI(TAG, "setup window closed");
                apply(boot_step(&flow, BOOT_EV_SETUP_EXPIRED, false, BOOT_FAIL_NONE));
            }
        }
    }
}

esp_err_t netsetup_start(void)
{
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e;

    lock = xSemaphoreCreateMutex();
    queue = xQueueCreate(8, sizeof(cmd));
    if (!lock || !queue) {
        return ESP_ERR_NO_MEM;
    }
    memset(&flow, 0, sizeof(flow));
    boot_init(&flow);
    flow.has_creds = creds_load(&creds);
    first_actions = boot_step(&flow, BOOT_EV_BOOT, flow.has_creds, BOOT_FAIL_NONE); /* the state is known before the radio is */
    e = esp_netif_init();
    if (e == ESP_OK) {
        e = esp_event_loop_create_default();
    }
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return e;
    }
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    e = esp_wifi_init(&wcfg);
    if (e != ESP_OK) {
        return e;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM); /* the credentials are ours, in NVS, and nobody else's */
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
    publish(pub_flow);
    if (xTaskCreate(task, "netsetup", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
