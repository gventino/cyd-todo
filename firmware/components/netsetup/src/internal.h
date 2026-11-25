#ifndef NETSETUP_INTERNAL_H
#define NETSETUP_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "prov/form.h"

/* creds.c: the saved network and server (NVS). */
bool creds_load(prov_creds *out);
esp_err_t creds_save(const prov_creds *c);

/* portal.c: the HTTP server and the DNS of the setup. `on_saved` is called from the server's task once a form was accepted
 * and stored. `accepting` is read to refuse a post when no setup is open. */
typedef struct {
    void (*on_saved)(const prov_creds *c);
    volatile bool *accepting;
} portal_hooks;

esp_err_t portal_start(const portal_hooks *hooks, const prov_net *nets, int n_nets, const char session[PROV_SESSION_LEN + 1]);
void portal_stop(void);

#endif /* NETSETUP_INTERNAL_H */
