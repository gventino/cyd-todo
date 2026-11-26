/*
 * cyd-todo on the CYD. The same components that the PC program runs (the UI, the theme, the QR, the model), the panel and
 * the touch screen of the library's ESP32 backend, and the Wi-Fi setup (step A4.3). With CONFIG_TODO_SELFTEST it first
 * draws every scene of the PC program through the real panel and prints the hash of each ("SCENE name palette hash"), to
 * be compared with the goldens of firmware/pc/golden: the first proof that the board draws what the PC pinned.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cydui/ui.h"
#include "cydui_esp32.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "netsetup.h"
#include "netsync.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "scenes.h"
#include "settings_store.h"
#include "sdkconfig.h"
#include "prov/bootflow.h"
#include "todo/app.h"
#include "todo/memrepo.h"
#include "todo/theme_quest.h"

/* A Kconfig bool that is off is not defined at all: say 0, so that it can be used in an expression. */
#ifndef CONFIG_TODO_SELFTEST
#define CONFIG_TODO_SELFTEST 0
#endif

#define ROWS 20 /* rows per strip: two buffers of 12.8 KB */
#define W 320
#define H 240

static const char *TAG = "cyd-todo";

/* The UI's memory: what the PC's budget guard measured (320 commands, 1.5 KB of text, 64 state entries). */
CYDUI_STORAGE(storage, W, H, 1, 320, 1536, 64);
DMA_ATTR static cydui_color strips[2 * ROWS * W];
static uint64_t dirty[(H + ROWS - 1) / ROWS];

static cydui_esp32 dev;
static cydui_ui ui;
static cydui_theme theme;
static todo_app *app;
static todo_memrepo *repo; /* only in the self-test build */

/* The clock of the UI: the real one, except in the self-test, where it is the made-up one of the PC's scenes. */
static cydui_backend real_be, be;
static bool virt_clock;
static uint32_t virt_now;

static uint32_t clock_ms(void *user)
{
    (void)user;
    return virt_clock ? virt_now : real_be.now_ms(real_be.user);
}

static void heap_line(const char *where)
{
    printf("TODO_HEAP %s free=%u min=%u largest=%u\n", where, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static bool start_ui(const cydui_theme *th)
{
    cydui_ui_config cfg = CYDUI_CONFIG(storage, &be, th);

    cfg.core.strip_mem = strips;
    cfg.core.strip_mem_size = sizeof(strips);
    cfg.core.strip_buffers = 2;
    cfg.core.dirty_mem = dirty;
    cfg.core.dirty_mem_size = sizeof(dirty);
    cfg.frame_budget_ms = 0;
    memset(dirty, 0, sizeof(dirty));
    if (cydui_ui_init(&ui, &cfg) != 0) {
        ESP_LOGE(TAG, "cydui_ui_init failed");
        return false;
    }
    return true;
}

static bool alloc_app(void)
{
    app = heap_caps_calloc(1, sizeof(*app), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if CONFIG_TODO_SELFTEST /* the scenes of the PC program run on the pretend server; the real build has none */
    repo = heap_caps_calloc(1, sizeof(*repo), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    if (!app || (CONFIG_TODO_SELFTEST && !repo)) {
        ESP_LOGE(TAG, "no memory for the application (%u bytes)", (unsigned)sizeof(*app));
        return false;
    }
    printf("TODO_SIZES app=%u repo=%u ui=%u\n", (unsigned)sizeof(*app), repo ? (unsigned)sizeof(*repo) : 0u, (unsigned)sizeof(ui));
    return true;
}

#if CONFIG_TODO_SELFTEST
/* One scene exactly as pc/tests/test_scene_goldens.c draws it: 32 frames 16 ms apart, the scene applied at the 9th, then
 * the hash of everything the last one sends (the strips are invalidated first, so that all of them are sent). */
static void pin(const char *scene, todo_palette palette, bool worst)
{
    int i;

    todo_memrepo_init(repo, SCENE_NOW);
    scene_data(repo, worst);
    todo_app_init(app, &repo->repo, SCENE_OFFSET);
    app->palette = palette;
    todo_theme_quest_init(&theme, palette);
    virt_clock = true;
    virt_now = 0;
    if (!start_ui(&theme)) {
        virt_clock = false;
        return;
    }
    for (i = 0; i < 32; i++, virt_now += 16) {
        if (i == 8 && !scene_apply(app, scene)) {
            ESP_LOGE(TAG, "unknown scene %s", scene);
        }
        if (i == 31) {
            cydui_invalidate(&ui.ctx);
            cydui_esp32_hash_enable(&dev, true);
        }
        todo_app_set_time(app, SCENE_NOW + virt_now / 1000, true);
        cydui_ui_begin(&ui, 0);
        todo_app_frame(app, &ui);
        cydui_ui_end(&ui);
    }
    cydui_esp32_hash_enable(&dev, false);
    virt_clock = false;
    printf("SCENE %s %s%s %016" PRIx64 " errors=0x%x\n", scene, palette == TODO_PALETTE_PARCHMENT ? "day" : "night", worst ? "_worst" : "",
           cydui_esp32_hash(&dev), (unsigned)cydui_ui_errors(&ui));
}

static void selftest(void)
{
    int s, p;

    for (s = 0; s < scene_count(); s++) {
        for (p = 0; p < TODO_PALETTE_COUNT; p++) {
            pin(scene_name(s), (todo_palette)p, false);
        }
    }
    for (s = 0; s < scene_worst_count(); s++) {
        pin(scene_worst_name(s), TODO_PALETTE_PARCHMENT, true);
    }
    heap_line("after the self-test");
    printf("TODO_SELFTEST_DONE\n");
}
#endif

/* What the radio is doing is shown by the UI, and what the user asks of it is passed on: the two sides only meet here. */
static void sync_net(void)
{
    static uint32_t last_serial = 0xFFFFFFFFu;
    static boot_state last_state = BOOT_JOINING;
    static boot_fail shown_reason = BOOT_FAIL_NONE;
    static bool was_connected;
    netsetup_status n;
    todo_event_kind ev;

    netsetup_get(&n);
    if (n.serial != last_serial) {
        char reason[64] = "";
        const char *fmt = boot_reason_format(n.reason);

        printf("TODO_NET state=%d reason=%d joining=%d connected=%d ap_ready=%d expired=%d creds=%d\n", n.state, n.reason, n.joining, n.connected, n.ap_ready,
               n.expired, n.has_creds);
        if (n.ap_ready && !app->ssid[0]) {
            heap_line("setup open");
        }
        todo_app_set_join(app, n.joining, n.ssid);
        todo_app_set_wifi_name(app, n.has_creds ? n.ssid : "");
        todo_copy(app->server, sizeof(app->server), n.server);
        if (fmt) {
            snprintf(reason, sizeof(reason), fmt, n.ssid);
        }
        if (n.state == BOOT_SETUP && (app->overlay != TODO_OV_SETUP || n.reason != shown_reason)) {
            todo_app_enter_setup(app, NULL, NULL, reason, n.has_creds);
            shown_reason = n.reason;
        }
        if (n.state == BOOT_SETUP && n.ap_ready && strcmp(app->ssid, n.ap_ssid) != 0) {
            todo_app_set_ap(app, n.ap_ssid, n.ap_pass);
        }
        if (last_state == BOOT_SETUP && n.state != BOOT_SETUP) {
            todo_app_leave_setup(app); /* it joined (or went back to joining): the page has done its job */
        }
        last_state = n.state;
        last_serial = n.serial;
    }
    if (n.connected && !was_connected) { /* the network is up: tell the server's side, and read everything now */
        prov_creds c;
        if (netsetup_creds(&c) && c.server[0] && c.token[0]) {
            netsync_online(c.server, c.token);
            todo_app_refresh_now(app);
        }
        memset(&c, 0, sizeof(c));
        was_connected = true;
    } else if (!n.connected && was_connected) {
        netsync_offline();
        was_connected = false;
    }
    while ((ev = todo_app_take_event(app)) != TODO_EVT_NONE) {
        if (ev == TODO_EVT_SETUP_REQUESTED) {
            netsetup_request_setup();
        } else {
            netsetup_cancel_setup();
        }
    }
}

/* Settings are saved two seconds after the last change (an arrow held down is one write), and only if the flash says something else. */
static void save_settings_when_they_stop_changing(uint32_t now)
{
    static todo_debounce deb;
    static uint32_t seen_rev;
    static bool first = true;

    if (first) {
        todo_debounce_init(&deb);
        seen_rev = app->settings_rev;
        first = false;
    }
    if (app->settings_rev != seen_rev) {
        seen_rev = app->settings_rev;
        todo_debounce_change(&deb, now);
    }
    if (todo_debounce_due(&deb, now, 2000)) {
        todo_settings st;
        todo_app_get_settings(app, &st);
        printf("TODO_SETTINGS %s zone=%d backlight=%u palette=%u\n", settings_store_save(&st) ? "saved" : "unchanged", st.zone_min, (unsigned)st.backlight,
               (unsigned)st.palette);
    }
}

static void calibrate(void)
{
    esp_err_t e = cydui_esp32_calibrate(&dev, &ui.ctx, &todo_serif_16, 60000);

    printf("TODO_CALIBRATION result=%s\n", esp_err_to_name(e));
}

void app_main(void)
{
    cydui_esp32_config cfg = {.max_strip_bytes = ROWS * W * sizeof(cydui_color)};
    esp_err_t e = nvs_flash_init();
    int64_t last = esp_timer_get_time(), t_ui = 0, t_end = 0;
    uint32_t n_frames = 0;

    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    heap_line("at start");
    e = cydui_esp32_init(&dev, &cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "the display did not start: %s", esp_err_to_name(e));
        return;
    }
    real_be = *cydui_esp32_backend(&dev);
    be = real_be;
    be.now_ms = clock_ms;
    if (!alloc_app()) {
        return;
    }
    heap_line("after the backend and the application");
#if CONFIG_TODO_SELFTEST
    selftest();
#endif

    /* The application proper: the server's data, and the clock from SNTP. */
    if (netsync_start() != ESP_OK) {
        ESP_LOGE(TAG, "the network task did not start");
        return;
    }
    todo_app_init(app, netsync_repo(), CONFIG_TODO_DEFAULT_TZ_MIN);
    {   /* what the owner set last time (the zone of the build is only the default of a board that has saved nothing) */
        todo_settings st;
        todo_settings_default(&st, CONFIG_TODO_DEFAULT_TZ_MIN);
        if (settings_store_load(&st)) {
            printf("TODO_SETTINGS loaded zone=%d backlight=%u palette=%u\n", st.zone_min, (unsigned)st.backlight, (unsigned)st.palette);
        } else {
            printf("TODO_SETTINGS none saved: defaults zone=%d backlight=%u palette=%u\n", st.zone_min, (unsigned)st.backlight, (unsigned)st.palette);
        }
        todo_app_apply_settings(app, &st); /* before the theme is made, so that the first picture is in the right palette */
    }
    todo_theme_quest_init(&theme, app->palette);
    if (!start_ui(&theme)) {
        return;
    }
    cydui_ui_begin(&ui, 0);
    todo_app_frame(app, &ui);
    cydui_ui_end(&ui);
    cydui_esp32_set_backlight(&dev, 100); /* only now, so that no garbage is ever seen */
    if (cydui_esp32_touch_status(&dev) == ESP_OK) {
        if (!cydui_esp32_calibrated(&dev)) {
            calibrate();
        }
    } else {
        ESP_LOGW(TAG, "no touch: %s", esp_err_to_name(cydui_esp32_touch_status(&dev)));
    }
    todo_app_set_ap_on_demand(app, true);
    if (netsetup_start() != ESP_OK) {
        ESP_LOGE(TAG, "the Wi-Fi did not start");
    }
    heap_line("with the Wi-Fi started");
    sync_net(); /* with nothing saved, the first thing on screen is the setup page */
    cydui_invalidate(&ui.ctx);
    heap_line("running");

    for (;;) {
        int64_t a = esp_timer_get_time(), b, now;
        bool more;

        {
            int64_t epoch = 0;
            bool valid = netsync_time(&epoch);
            todo_app_set_time(app, valid ? epoch : 0, valid);
        }
        sync_net();
        save_settings_when_they_stop_changing((uint32_t)(esp_timer_get_time() / 1000));
        cydui_ui_begin(&ui, 0);
        todo_app_frame(app, &ui);
        b = esp_timer_get_time();
        more = cydui_ui_end(&ui);
        t_ui += b - a;
        t_end += esp_timer_get_time() - b;
        n_frames++;
        now = esp_timer_get_time();
        if (now - last > 5000000) {
            printf("TODO_STATS frames=%" PRIu32 " ui_us=%" PRId64 " end_us=%" PRId64 " strips_sent=%" PRIu32 " free_heap=%u stack_left=%u\n", n_frames,
                   n_frames ? t_ui / n_frames : 0, n_frames ? t_end / n_frames : 0, cydui_esp32_flushes(&dev),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)uxTaskGetStackHighWaterMark(NULL));
            t_ui = t_end = 0;
            n_frames = 0;
            last = now;
        }
        if (!more && !cydui_esp32_wait(&dev, 250)) {
            continue; /* a quarter of a second with nothing happening: a frame for the timers, no more */
        }
    }
}
