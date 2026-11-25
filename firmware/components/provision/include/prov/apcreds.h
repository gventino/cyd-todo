#ifndef PROV_APCREDS_H
#define PROV_APCREDS_H

/* The name and the password of the access point the board opens for the setup. Random bytes come from the caller. */

#include <stdint.h>

#define PROV_AP_SSID_LEN 13 /* "cyd-todo-4F2A" */
#define PROV_AP_PASS_LEN 12 /* 60 bits from a 32-character alphabet; it lives in the QR code only */
#define PROV_AP_RANDOM_BYTES 14

/* Fills `ssid` and `pass` (NUL-terminated) from 14 random bytes: 2 for the name, 12 for the password. */
void prov_ap_make(const uint8_t rnd[PROV_AP_RANDOM_BYTES], char ssid[PROV_AP_SSID_LEN + 1], char pass[PROV_AP_PASS_LEN + 1]);

#endif /* PROV_APCREDS_H */
