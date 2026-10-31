/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_SESSION_H
#define DISKOS_SESSION_H

#include <stdint.h>

typedef enum { SESSION_COLD_BOOT = 0, SESSION_RELAUNCH = 1 } session_entry_t;

int  session_source_mode(void);
void session_set_source_mode(int mode);

void            session_init(void);
session_entry_t session_entry(void);
int  session_adopt(unsigned gen);
int  session_adopt_detected(unsigned gen, int detected);
void session_handoff_export(void);
void session_handoff_cancel(void);

int  session_saved_source(void);
void session_save_source(int mode);

typedef enum { SESSION_RESTORE_WAIT, SESSION_RESTORE_NONE, SESSION_RESTORE_SOURCE, SESSION_RESTORE_USB_AUDIO } session_restore_t;
void              session_restore_arm(uint32_t now_ms);
session_restore_t session_restore_poll(uint32_t now_ms, int busy, int blocked, int *src);

struct session_startup_env {
    int      (*admitted)(void);
    unsigned (*rx_frames)(void);
    int      (*play_starting)(void);
    int      (*playing)(void);
    int      (*have_track)(void);
    int      (*fw_needs_init)(void);
    int      (*player_old)(void);
    int      (*saved_bt_route)(void);
    int      (*connected_sink)(const char *mac);
    int      (*route_analog)(void);
};
void session_startup_set_env(const struct session_startup_env *env);

typedef enum { SESSION_STARTUP_WAIT, SESSION_STARTUP_PROBE, SESSION_STARTUP_DONE } session_startup_t;
session_startup_t session_startup_tick(uint32_t now_ms);

void session_startup_route_settled(void);

#endif
