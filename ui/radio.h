/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_RADIO_H
#define DISKOS_RADIO_H
#include <stdint.h>

/* The lifecycle the Wi-Fi and Bluetooth radios share. No LVGL: the clock is passed in, and the periodic tick, the
 * radio probe and the power-down are adapter functions (wifi.c, bt.c), so a host test drives it with fakes.
 *
 *   bring-up watch  poll "is the radio really up?" every interval_ms until it is or timeout_ms has passed, then stop.
 *   radio           one persisted on/off intent + the single "off" teardown + the optional rollback of a failed
 *                   bring-up, on top of a watch.
 *
 * What stays in the adapters: what "up" means (probe), what starting and powering down are (the shell commands),
 * the screen-specific list rendering, and what to do once a watch ends (scan, switch state, message). */

enum { RADIO_IDLE, RADIO_PENDING, RADIO_UP, RADIO_TIMEOUT };   /* radio_watch_tick / radio_tick verdicts */

struct radio_watch {
    unsigned interval_ms, timeout_ms;
    int  (*is_up)(void);                 /* the actual radio state, not the intent */
    void (*arm)(unsigned interval_ms);   /* start the periodic tick that calls radio_watch_tick */
    void (*disarm)(void);                /* stop it; called exactly once per arm */
    int      active;                     /* private: armed and not yet decided */
    uint32_t start;
};

void radio_watch_begin(struct radio_watch *w, uint32_t now);   /* (re)start from `now`; replaces a running watch */
int  radio_watch_tick(struct radio_watch *w, uint32_t now);    /* RADIO_UP / RADIO_TIMEOUT end the watch (already disarmed) */
void radio_watch_cancel(struct radio_watch *w);                /* no verdict; no-op when idle */

struct radio {
    struct radio_watch *watch;
    void (*set_intent)(int on);          /* persist the on/off intent (the cfg key) */
    void (*start)(void);                 /* launch the bring-up; asynchronous */
    void (*cleanup)(void);               /* optional: cancel this radio's other pending work before it goes down */
    void (*power_down)(void);            /* the actual teardown */
    int rollback;                        /* a bring-up timeout clears the intent and powers down (else it only reports) */
};

void radio_on(struct radio *r, uint32_t now, int observe);     /* intent on, start, and watch it come up if `observe` */
void radio_off(struct radio *r);                               /* intent off, stop watching, cleanup, power down: once */
int  radio_tick(struct radio *r, uint32_t now);                /* a watch tick that also applies the rollback on RADIO_TIMEOUT */

#endif
