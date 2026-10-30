/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_LAUNCHER_H
#define DISKOS_LAUNCHER_H

struct launcher_env {
    int  (*sd_exported_to_host)(void);
    int  (*run_bounded)(char *const argv[], int timeout_ms);
    int  (*rmguard_ensure)(void);
    void (*kill_bounded)(void);
};
void launcher_set_env(const struct launcher_env *env);

void player_launch_prepare(void);

void exec_stock_player(void) __attribute__((noreturn));

int launch_status_get(void);

int player_local_mode_confirmed(void);

int player_config_absent_without_card(void);

#endif
