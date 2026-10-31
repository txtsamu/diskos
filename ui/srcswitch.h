/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_SRCSWITCH_H
#define DISKOS_SRCSWITCH_H

#include <stdint.h>

struct srcswitch_env {
    uint32_t (*now)(void);
    int  (*host_attached)(void);
    int  (*dac_gadget_present)(void);
    int  (*playing)(void);
    void (*toast)(const char *msg);
    void (*set_volume)(int volume);
    int  (*seek_to)(long ms);
};

void srcswitch_set_env(const struct srcswitch_env *env);
void srcswitch_init(void);
void srcswitch_tick(void);

int  ui_set_source_mode(int mode);
int  modes_source_switch_busy(void);
int  srcswitch_via_local_pending(void);
const char *srcswitch_switch_msg(int mode);

void modes_output_boot_restore(void);

typedef enum {
    SRCSWITCH_EV_CHANGED,
    SRCSWITCH_EV_SETTLE_RESTART,
    SRCSWITCH_EV_ABANDONED,
} srcswitch_event_t;
void srcswitch_set_listener(void (*cb)(srcswitch_event_t));

int  srcswitch_dac_level_settled(void);

int  srcswitch_send_source(int mode);

#endif
