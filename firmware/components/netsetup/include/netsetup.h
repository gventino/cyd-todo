#ifndef NETSETUP_H
#define NETSETUP_H

/*
 * The Wi-Fi of the board: joins the saved network, or opens an access point and a captive portal so that a phone can say
 * which one to join. The decisions are prov/bootflow.h's; this component only does what the table says (the radio, the
 * HTTP server, the DNS, the flash) on a task of its own. The UI never calls the radio: it reads a status that is copied
 * under a lock, and sends two requests.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "prov/apcreds.h"
#include "prov/bootflow.h"
#include "prov/form.h"

typedef struct {
    boot_state state;
    boot_fail reason;   /* why the last attempt failed (for the setup page) */
    bool joining;       /* trying to join `ssid` */
    bool connected;     /* has an address */
    bool has_creds;     /* a network is saved */
    bool ap_ready;      /* the access point is up and the credentials below are valid */
    bool expired;       /* the setup window closed */
    char ssid[PROV_SSID_MAX + 1];      /* the saved network */
    char server[PROV_SERVER_MAX + 1];  /* the saved server address */
    char ap_ssid[PROV_AP_SSID_LEN + 1];
    char ap_pass[PROV_AP_PASS_LEN + 1];
    uint32_t serial;    /* changes whenever anything above does */
} netsetup_status;

/* Starts the radio and the task. Reads the saved credentials and begins as the boot table says. */
esp_err_t netsetup_start(void);

/* A copy of the status. Cheap; call it every frame. */
void netsetup_get(netsetup_status *out);

/* The user chose Change Wi-Fi (or Try again), or left the setup page. */
void netsetup_request_setup(void);
void netsetup_cancel_setup(void);

#endif /* NETSETUP_H */
