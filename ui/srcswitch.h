/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_SRCSWITCH_H
#define DISKOS_SRCSWITCH_H

#include <pthread.h>
#include <stdint.h>

struct srcswitch_env {
    uint32_t (*now)(void);
    int  (*media_mounted)(void);
    int  (*exported_to_host)(void);
    int  (*host_confirmed)(void);
    int  (*host_attached)(void);
    int  (*dac_gadget_present)(void);
    int  (*player_guarded)(void);
    int  (*playing)(void);
    int  (*scan_active)(void);
    void (*scan_if_library_empty)(void);
    void (*art_cancel)(void);
    void (*card_returned)(void);
    void (*restart)(void);
    void (*toast)(const char *msg);
    void (*set_volume)(int volume);
    int  (*seek_to)(long ms);
};

void srcswitch_set_env(const struct srcswitch_env *env);
void srcswitch_init(void);
void srcswitch_tick(void);

int  ui_set_source_mode(int mode);
int  ui_source_switch_pending(void);
int  ui_source_switch_failed(void);
int  ui_local_playback_allowed(void);
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

int  sd_write_begin(void);
void sd_write_end(void);
void ui_sd_quiesce_begin(void);
int  ui_sd_quiesce_drained(void);
void ui_sd_quiesce_end(void);

int  srcswitch_dac_level_settled(void);

void srcswitch_set_launch_verdict(int v);
void srcswitch_hold(void);
int  srcswitch_held(void);
void srcswitch_revoke(void);
void srcswitch_restart_deadline(void);
int  srcswitch_sd_writable(void);
pthread_mutex_t *srcswitch_mode_mutex(void);

#endif
