/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_RADIO_H
#define DISKOS_RADIO_H
#include <stdint.h>

/* The lifecycle the Wi-Fi and Bluetooth radios share. No LVGL: the clock and the periodic timer come in through a
 * struct radio_env (radio_lv_env is the real one, radio_lv.c), so a host test drives it with fakes.
 *
 *   bring-up watch  poll "is the radio really up?" every interval_ms until it is or timeout_ms has passed, then stop.
 *                   It owns its timer, so an adapter never arms, disarms or keeps a timer handle itself.
 *   radio           one persisted on/off intent + the single "off" teardown + the policy for a failed bring-up + what a
 *                   Quick Settings toggle flips, on top of a watch.
 *
 * An adapter (wifi.c, bt.c) states facts only: the probe, the shell commands behind start and power-down, the cfg key,
 * and what its screen does once a watch ends (on_up / on_timeout). Where the two radios deliberately differ, the
 * difference is a named field below (timeout, is_on, toggle_observes, excludes), not copied code. */

enum { RADIO_IDLE, RADIO_PENDING, RADIO_UP, RADIO_TIMEOUT };   /* radio_watch_fire verdicts */

struct radio_watch;

/* The platform seam: a clock and a periodic timer. A timer started with timer_start must call radio_watch_fire(w)
 * every interval_ms until timer_stop(handle); timer_stop may be called from inside that call. */
struct radio_env {
    uint32_t (*now)(void);
    void *(*timer_start)(unsigned interval_ms, struct radio_watch *w);   /* returns an opaque, non-NULL handle */
    void  (*timer_stop)(void *handle);
};
extern const struct radio_env radio_lv_env;   /* the LVGL timer + lv_tick_get adapter (radio_lv.c) */

/* The policy for a bring-up that times out. */
enum radio_timeout {
    RADIO_REPORT_ONLY,       /* only on_timeout runs: the intent stays on and something else retries (Wi-Fi keepalive) */
    RADIO_ROLLBACK,          /* the intent is cleared and the radio powered down first, so nothing keeps claiming it is on (Bluetooth) */
};

struct radio;

struct radio_watch {
    const struct radio_env *env;
    unsigned interval_ms, timeout_ms;
    int  (*is_up)(void);                 /* the actual radio state, not the intent */
    int  (*stay)(void);                  /* optional: returns 0 to drop the watch silently (e.g. the screen was left) */
    void (*on_up)(void);                 /* optional: the radio came up (the watch is already stopped) */
    void (*on_timeout)(void);            /* optional: gave up (already stopped, and rolled back if the radio says so) */
    struct radio *owner;                 /* private: set by radio_on; a standalone watch has none, so never rolls back */
    void *timer;                         /* private */
    int active;                          /* private: armed and not yet decided */
    uint32_t start;                      /* private */
};

void radio_watch_begin(struct radio_watch *w);   /* (re)start from now; replaces a running watch, never doubles it */
void radio_watch_cancel(struct radio_watch *w);  /* no verdict, no callback; no-op when idle */
/* One timer tick: stay, probe, timeout, rollback, then on_up / on_timeout. Called by the env's timer. */
int  radio_watch_fire(struct radio_watch *w);

struct radio {
    struct radio_watch watch;            /* the bring-up watch; set env, interval, timeout, probe and the screen reactions */
    void (*set_intent)(int on);          /* persist the on/off intent (the cfg key) */
    void (*start)(void);                 /* launch the bring-up; asynchronous */
    void (*cleanup)(void);               /* optional: cancel this radio's other pending work before it goes down */
    void (*power_down)(void);            /* the actual teardown */
    int  (*is_on)(void);                 /* what a toggle flips = what the Quick Settings tile shows: Wi-Fi the persisted
                                          * intent, Bluetooth the actual rfkill state (its intent lags the radio) */
    enum radio_timeout timeout;
    int  toggle_observes;                /* radio_toggle watches the bring-up (Bluetooth: for the rollback and the service
                                          * start; Wi-Fi: no, the keepalive enforces its intent and no screen is waiting) */
    struct radio_watch *excludes;        /* optional: another watch cancelled when this radio starts (BT scan-wait) */
};

void radio_on(struct radio *r, int observe);   /* intent on, start, cancel `excludes`, and watch it come up if `observe` */
void radio_off(struct radio *r);               /* intent off, stop watching, cleanup, power down: once */
int  radio_toggle(struct radio *r);            /* flip is_on(): radio_on(toggle_observes) or radio_off; returns the new state */

#endif
