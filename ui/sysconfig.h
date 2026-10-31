/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_SYSCONFIG_H
#define DISKOS_SYSCONFIG_H

#ifndef PLAYER_SYSCONFIG_DB
#define PLAYER_SYSCONFIG_DB "/usr/data/fiio/db/sysconfig.db"
#endif

typedef enum {
    SC_OUT_DEV, SC_WIFI_STATUS, SC_BT_STATUS, SC_WORK_MODE,
    SC_SCREEN_ROT, SC_KEY_SINGLE, SC_KEY_DOUBLE, SC_KEY_LONG,
    SC_POWER_SAVE, SC_CHARGE_PROTECT, SC_PLAY_MODE, SC_LANGUAGE,
    SC_AUTO_TIME, SC_FOLDER_JUMP, SC_ARTIST_CLASS,
    SC_N
} sc_col_t;

#define SC_MAX_READ 4

typedef int (*sysconfig_cap_t)(const char *cmd, char *out, int cap, int timeout_ms);
typedef int (*sysconfig_run_t)(char *const argv[], int timeout_ms);

const char *sysconfig_path(void);
void sysconfig_set_path(const char *path);

int sysconfig_get(sc_col_t col);
int sysconfig_get_many(const sc_col_t *cols, int n, int *out);
int sysconfig_set(sc_col_t col, int v);

int sysconfig_get_via(sysconfig_cap_t cap, sc_col_t col, int timeout_ms);
int sysconfig_get_many_via(sysconfig_cap_t cap, const sc_col_t *cols, int n, int *out, int timeout_ms);

int sysconfig_work_mode_reset_via(sysconfig_run_t run, int timeout_ms);
int sysconfig_work_mode_confirm_via(sysconfig_run_t run, int timeout_ms);
int sysconfig_work_mode_reset_confirm_via(sysconfig_run_t run, int timeout_ms);

#endif
