#include "prov/apcreds.h"

/* 32 characters without the look-alikes (0 O 1 I l): a byte & 31 picks one with no bias. */
static const char ALPHABET[33] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const char HEX[17] = "0123456789ABCDEF";

void prov_ap_make(const uint8_t rnd[PROV_AP_RANDOM_BYTES], char ssid[PROV_AP_SSID_LEN + 1], char pass[PROV_AP_PASS_LEN + 1])
{
    static const char PREFIX[10] = "cyd-todo-";
    int i;

    for (i = 0; i < 9; i++) {
        ssid[i] = PREFIX[i];
    }
    ssid[9] = HEX[rnd[0] >> 4];
    ssid[10] = HEX[rnd[0] & 15];
    ssid[11] = HEX[rnd[1] >> 4];
    ssid[12] = HEX[rnd[1] & 15];
    ssid[13] = '\0';
    for (i = 0; i < PROV_AP_PASS_LEN; i++) {
        pass[i] = ALPHABET[rnd[2 + i] & 31];
    }
    pass[PROV_AP_PASS_LEN] = '\0';
}
