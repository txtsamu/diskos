/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_RESTART_H
#define DISKOS_RESTART_H

#include <stdint.h>
#include <sys/types.h>

typedef enum { RESTART_UI = 0, RESTART_FLASH = 1 } restart_kind_t;

struct restart_env {
    uint32_t (*now)(void);
    int  (*card_drained)(void);
    void (*quiesce_begin)(void);
    void (*quiesce_end)(void);
    void (*stop_readers)(void);
    pid_t (*save_clock)(void);
    pid_t (*spawn)(const char *cmd);
    int  (*wait)(pid_t pid, int *status);
    void (*kill)(pid_t pid);
    int  (*flash_arm)(void);
    int  (*reboot)(void);
    int  (*unmount_card)(void);
    void (*toast)(const char *msg);
};

void restart_set_env(const struct restart_env *env);
int  restart_begin(restart_kind_t kind);
void restart_tick(void);
int  restart_busy(void);
void restart_abort(const char *why);

#endif
