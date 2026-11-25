#include "netsync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_transport.h"
#include "todo/netrepo.h"

static const char *TAG = "netsync";

static todo_netrepo net;
static http_target target;
static SemaphoreHandle_t mutex;
static TaskHandle_t task_handle;
static bool sntp_started;

static void lock_take(void *u)
{
    (void)u;
    xSemaphoreTake(mutex, portMAX_DELAY);
}

static void lock_give(void *u)
{
    (void)u;
    xSemaphoreGive(mutex);
}

static uint32_t now_ms(void *u)
{
    (void)u;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void wake(void *u)
{
    (void)u;
    if (task_handle) {
        xTaskNotifyGive(task_handle);
    }
}

static void *alloc_fn(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void release_fn(void *p)
{
    heap_caps_free(p);
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        while (todo_netrepo_step(&net)) {
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250)); /* a submit wakes it; otherwise it looks again a few times a second */
    }
}

esp_err_t netsync_start(void)
{
    todo_netrepo_config cfg;

    memset(&cfg, 0, sizeof(cfg));
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        return ESP_ERR_NO_MEM;
    }
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE); /* its errors would name the host; ours say what matters */
    cfg.transport.perform = http_perform;
    cfg.transport.user = &target;
    cfg.lock.take = lock_take;
    cfg.lock.give = lock_give;
    cfg.now_ms = now_ms;
    cfg.wake = wake;
    cfg.alloc = alloc_fn;
    cfg.release = release_fn;
    todo_netrepo_init(&net, &cfg);
    if (xTaskCreate(task, "netsync", 6144, NULL, 5, &task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

todo_repo *netsync_repo(void)
{
    return &net.repo;
}

void netsync_online(const char *server, const char *token)
{
    const char *colon = strchr(server, ':');
    size_t hl = colon ? (size_t)(colon - server) : strlen(server);

    if (hl >= sizeof(target.host)) {
        ESP_LOGE(TAG, "the server address is too long");
        return;
    }
    memcpy(target.host, server, hl);
    target.host[hl] = '\0';
    target.port = colon ? atoi(colon + 1) : 80;
    snprintf(target.token, sizeof(target.token), "%s", token);
    target.online = true;
    if (!sntp_started) {
        sntp_started = true;
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        sntp_set_sync_interval(3600 * 1000);
        esp_sntp_init();
    }
    ESP_LOGI(TAG, "online: a server is set (port %d)", target.port); /* neither the host nor the token is logged */
}

void netsync_offline(void)
{
    target.online = false;
}

bool netsync_time(int64_t *epoch)
{
    time_t t = time(NULL);

    if (t < 1700000000) { /* November 2023: before that the clock has not been set */
        return false;
    }
    *epoch = (int64_t)t;
    return true;
}
