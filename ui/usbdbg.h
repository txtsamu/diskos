/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_USBDBG_H
#define DISKOS_USBDBG_H

#include <stdint.h>

typedef enum { USBDBG_OFF, USBDBG_STARTING, USBDBG_ON, USBDBG_STOPPING } usbdbg_state_t;
typedef enum { USBDBG_EV_CHANGED, USBDBG_EV_STOPPED, USBDBG_EV_STOP_FAILED } usbdbg_event_t;

#define USBDBG_RUNNING (-1)

struct usbdbg_env {
    uint32_t (*now)(void);
    int  (*spawn)(const char *verb);
    int  (*reap)(int pid);
    void (*abort)(int pid);
    int  (*bound)(void);
    int  (*present)(void);
    int  (*stock_gadget)(void);
    int  (*host_connected)(void);
    void (*toast)(const char *msg);
};

void usbdbg_set_env(const struct usbdbg_env *env);
void usbdbg_set_listener(void (*cb)(usbdbg_event_t));
void usbdbg_init(void);
void usbdbg_tick(void);

int  usbdbg_start(void);
#define USBDBG_STOP_STARTED 0
#define USBDBG_STOP_NOOP    1
int  usbdbg_stop(void);

usbdbg_state_t usbdbg_state(void);
int  usbdbg_active(void);
int  usbdbg_busy(void);
int  usbdbg_host(void);

#endif
