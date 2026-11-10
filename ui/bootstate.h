/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_BOOTSTATE_H
#define DISKOS_BOOTSTATE_H

#include <stdint.h>

#define BOOTSTATE_REASON "Starting up..."

struct bootstate_env {
    uint32_t (*now)(void);
    int  (*player_up)(void);
    int  (*gate_settled)(void);
};

void bootstate_start(const struct bootstate_env *env);
void bootstate_tick(void);
int  bootstate_starting(void);
int  bootstate_take_change(void);
int  bootstate_is_reason(const char *msg);
const char *bootstate_reason(const char *msg);

#endif
