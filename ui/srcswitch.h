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
    int  (*external_power)(void);
    int  (*detected_mode)(void);
    void (*reapply_audio)(void);
};

#define SRCSWITCH_MODE_LOCAL        0
#define SRCSWITCH_MODE_USB_DAC      1
#define SRCSWITCH_MODE_BT_RECEIVING 2
#define SRCSWITCH_MODE_USB_STORAGE  3
#define SRCSWITCH_MODE_USB_DEBUG    4
#define SRCSWITCH_MODE_USB_AUDIO    5

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

typedef enum {
    SRCSWITCH_STATE_IDLE,
    SRCSWITCH_STATE_LEAVING,
    SRCSWITCH_STATE_WAITING,
    SRCSWITCH_STATE_ENTERING,
    SRCSWITCH_STATE_SETTLING,
} srcswitch_state_t;

typedef struct {
    srcswitch_state_t state;
    int from;
    int to;
    int mode;
    int failed;
    const char *reason;
} srcswitch_status_t;

srcswitch_status_t srcswitch_status(void);

int  srcswitch_dac_level_settled(void);

int  srcswitch_send_source(int mode);

#endif
