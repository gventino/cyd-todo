#ifndef PROV_BOOTFLOW_H
#define PROV_BOOTFLOW_H

/*
 * What the board does about the Wi-Fi, as a table: boot, join, set up, give up, start again. Pure: events in, actions out;
 * the timers (15 s a try, 10 minutes of setup) and the radio belong to the caller, which reports what happened.
 */

#include <stdbool.h>

#define BOOT_TRIES 3 /* attempts to join saved credentials before the setup page */

typedef enum { BOOT_JOINING, BOOT_NORMAL, BOOT_SETUP } boot_state;
typedef enum { BOOT_FAIL_NONE, BOOT_FAIL_WRONG_PASSWORD, BOOT_FAIL_NO_NETWORK, BOOT_FAIL_TIMEOUT } boot_fail;

typedef enum {
    BOOT_EV_BOOT,            /* the board started (a = has saved credentials) */
    BOOT_EV_JOINED,          /* got an address */
    BOOT_EV_JOIN_FAILED,     /* the attempt ended with a reason */
    BOOT_EV_SETUP_REQUESTED, /* the user chose Change Wi-Fi, or Try again after the window closed */
    BOOT_EV_SETUP_CANCELLED, /* the user left the setup page (only when there is something to go back to) */
    BOOT_EV_SETUP_EXPIRED,   /* the 10 minutes ran out */
    BOOT_EV_CREDS_SAVED      /* the form was accepted and stored */
} boot_event;

/* What the caller must do, as bits. */
enum { BOOT_ACT_NONE = 0, BOOT_ACT_JOIN = 1, BOOT_ACT_START_AP = 2, BOOT_ACT_STOP_AP = 4 };

typedef struct {
    boot_state state;
    boot_fail reason;    /* why the last attempt failed, for the setup page */
    int tries;           /* attempts made in this JOINING */
    int max_tries;       /* 3 for saved credentials, 1 for ones just typed */
    bool has_creds;      /* there is a network to go back to */
    bool expired;        /* SETUP: the window closed and the access point is off */
} boot_flow;

void boot_init(boot_flow *f);
/* Applies an event; returns the actions to carry out. `fail` is read for BOOT_EV_JOIN_FAILED. */
int boot_step(boot_flow *f, boot_event ev, bool flag, boot_fail fail);

/* The sentence for the setup page, e.g. "Wrong password for %s": the caller puts the network name in. NULL for no reason. */
const char *boot_reason_format(boot_fail r);

#endif /* PROV_BOOTFLOW_H */
