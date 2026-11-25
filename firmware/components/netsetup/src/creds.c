#include <string.h>

#include "internal.h"
#include "nvs.h"

#define NS "cydtodo"

bool creds_load(prov_creds *out)
{
    nvs_handle_t h;
    size_t n;
    bool ok = false;

    memset(out, 0, sizeof(*out));
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    n = sizeof(out->ssid);
    ok = nvs_get_str(h, "ssid", out->ssid, &n) == ESP_OK;
    n = sizeof(out->pass);
    if (nvs_get_str(h, "pass", out->pass, &n) != ESP_OK) {
        out->pass[0] = '\0';
    }
    n = sizeof(out->server);
    if (nvs_get_str(h, "server", out->server, &n) != ESP_OK) {
        out->server[0] = '\0';
    }
    n = sizeof(out->token);
    if (nvs_get_str(h, "token", out->token, &n) != ESP_OK) {
        out->token[0] = '\0';
    }
    nvs_close(h);
    /* what is read back is checked like what came in: a corrupted flash must not become a network name */
    return ok && prov_valid_ssid(out->ssid) && prov_valid_pass(out->pass);
}

esp_err_t creds_save(const prov_creds *c)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);

    if (e != ESP_OK) {
        return e;
    }
    e = nvs_set_str(h, "ssid", c->ssid);
    if (e == ESP_OK) {
        e = nvs_set_str(h, "pass", c->pass);
    }
    if (e == ESP_OK) {
        e = nvs_set_str(h, "server", c->server);
    }
    if (e == ESP_OK) {
        e = nvs_set_str(h, "token", c->token);
    }
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e;
}
