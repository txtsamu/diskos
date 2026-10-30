/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_RADIO_H
#define DISKOS_RADIO_H
#include <stdint.h>

enum { RADIO_IDLE, RADIO_PENDING, RADIO_UP, RADIO_TIMEOUT };

struct radio_watch;

struct radio_env {
    uint32_t (*now)(void);
    void *(*timer_start)(unsigned interval_ms, struct radio_watch *w);
    void  (*timer_stop)(void *handle);
};
extern const struct radio_env radio_lv_env;

enum radio_timeout {
    RADIO_REPORT_ONLY,
    RADIO_ROLLBACK,
};

struct radio;

struct radio_watch {
    const struct radio_env *env;
    unsigned interval_ms, timeout_ms;
    int  (*is_up)(void);
    int  (*stay)(void);
    void (*on_up)(void);
    void (*on_timeout)(void);
    struct radio *owner;
    void *timer;
    int active;
    uint32_t start;
};

void radio_watch_begin(struct radio_watch *w);
void radio_watch_cancel(struct radio_watch *w);
int  radio_watch_fire(struct radio_watch *w);

struct radio {
    struct radio_watch watch;
    void (*set_intent)(int on);
    void (*start)(void);
    void (*cleanup)(void);
    void (*power_down)(void);
    int  (*is_on)(void);
    enum radio_timeout timeout;
    int  toggle_observes;
    struct radio_watch *excludes;
};

void radio_on(struct radio *r, int observe);
void radio_off(struct radio *r);
int  radio_toggle(struct radio *r);

#endif
