#include "prov/bootflow.h"

#include <string.h>

void boot_init(boot_flow *f)
{
    memset(f, 0, sizeof(*f));
    f->state = BOOT_SETUP;
}

static int to_setup(boot_flow *f, boot_fail why)
{
    int actions = BOOT_ACT_START_AP;

    f->state = BOOT_SETUP;
    f->reason = why;
    f->expired = false;
    f->tries = 0;
    return actions;
}

static int to_joining(boot_flow *f, int max_tries)
{
    f->state = BOOT_JOINING;
    f->expired = false;
    f->tries = 1;
    f->max_tries = max_tries;
    return BOOT_ACT_JOIN;
}

int boot_step(boot_flow *f, boot_event ev, bool flag, boot_fail fail)
{
    switch (ev) {
    case BOOT_EV_BOOT:
        f->has_creds = flag;
        f->reason = BOOT_FAIL_NONE;
        return flag ? to_joining(f, BOOT_TRIES) : to_setup(f, BOOT_FAIL_NONE);

    case BOOT_EV_JOINED:
        if (f->state != BOOT_JOINING) {
            return BOOT_ACT_NONE;
        }
        f->state = BOOT_NORMAL;
        f->reason = BOOT_FAIL_NONE;
        f->has_creds = true;
        return BOOT_ACT_NONE;

    case BOOT_EV_JOIN_FAILED:
        if (f->state != BOOT_JOINING) {
            return BOOT_ACT_NONE;
        }
        if (fail == BOOT_FAIL_NONE) {
            fail = BOOT_FAIL_TIMEOUT;
        }
        if (fail == BOOT_FAIL_WRONG_PASSWORD || f->tries >= f->max_tries) { /* more tries would not help a wrong password */
            return to_setup(f, fail);
        }
        f->tries++;
        f->reason = fail;
        return BOOT_ACT_JOIN;

    case BOOT_EV_SETUP_REQUESTED:
        if (f->state == BOOT_SETUP && !f->expired) {
            return BOOT_ACT_NONE; /* already there */
        }
        return to_setup(f, BOOT_FAIL_NONE);

    case BOOT_EV_SETUP_CANCELLED:
        if (f->state != BOOT_SETUP || !f->has_creds) {
            return BOOT_ACT_NONE;
        }
        return BOOT_ACT_STOP_AP | to_joining(f, BOOT_TRIES);

    case BOOT_EV_SETUP_EXPIRED:
        if (f->state != BOOT_SETUP || f->expired) {
            return BOOT_ACT_NONE;
        }
        f->expired = true;
        return BOOT_ACT_STOP_AP;

    case BOOT_EV_CREDS_SAVED:
        if (f->state != BOOT_SETUP || f->expired) {
            return BOOT_ACT_NONE;
        }
        f->has_creds = true;
        return BOOT_ACT_STOP_AP | to_joining(f, 1); /* one try: whoever typed it is waiting for the answer */
    }
    return BOOT_ACT_NONE;
}

const char *boot_reason_format(boot_fail r)
{
    switch (r) {
    case BOOT_FAIL_WRONG_PASSWORD:
        return "Wrong password for %s";
    case BOOT_FAIL_NO_NETWORK:
        return "Could not find %s";
    case BOOT_FAIL_TIMEOUT:
        return "Could not join %s";
    case BOOT_FAIL_NONE:
        break;
    }
    return NULL;
}
