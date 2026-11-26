#include "settings_store.h"

#include <string.h>

#include "nvs.h"

#define NS "cydtodo"
#define KEY "settings"

bool settings_store_load(todo_settings *out)
{
    nvs_handle_t h;
    uint8_t rec[TODO_SETTINGS_RECORD + 8]; /* room to notice a record that is too long */
    size_t n = sizeof(rec);
    bool ok = false;

    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    if (nvs_get_blob(h, KEY, rec, &n) == ESP_OK) {
        ok = todo_settings_decode(rec, n, out);
    }
    nvs_close(h);
    return ok;
}

bool settings_store_save(const todo_settings *s)
{
    nvs_handle_t h;
    uint8_t rec[TODO_SETTINGS_RECORD], old[TODO_SETTINGS_RECORD + 8];
    size_t n = sizeof(old);
    bool wrote = false;

    if (!todo_settings_encode(s, rec) || nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    if (nvs_get_blob(h, KEY, old, &n) == ESP_OK && n == sizeof(rec) && memcmp(old, rec, sizeof(rec)) == 0) {
        nvs_close(h); /* the flash already says this */
        return false;
    }
    if (nvs_set_blob(h, KEY, rec, sizeof(rec)) == ESP_OK && nvs_commit(h) == ESP_OK) {
        wrote = true;
    }
    nvs_close(h);
    return wrote;
}
