/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "bootstate.h"
#include "ipc.h"
#include <stdio.h>
#include <string.h>

static const struct bootstate_env *E;
static int g_starting, g_changed;
static uint32_t g_t0;

void bootstate_start(const struct bootstate_env *env){
    E = env; g_t0 = E->now(); g_starting = 1; g_changed = 0;
}

static void finish(void){
    g_starting = 0; g_changed = 1;
}

void bootstate_tick(void){
    if(!g_starting) return;
    int up = E->player_up(), gate = E->gate_settled();
    uint32_t ms = E->now() - g_t0;
    if(up && gate){
        finish();
        fprintf(stderr, "boot: player ready after %u ms\n", (unsigned)ms); fflush(stderr);
    } else if(ms >= IPC_BOOT_HOLD_MS){
        finish();
        fprintf(stderr, "boot: gave up waiting after %u ms (player %s, SD gate %s)\n",
                (unsigned)ms, up ? "up" : "not up", gate ? "settled" : "pending"); fflush(stderr);
    }
}

int bootstate_starting(void){ return g_starting; }
int bootstate_take_change(void){ int c = g_changed; g_changed = 0; return c; }
int bootstate_is_reason(const char *msg){ return msg && !strcmp(msg, BOOTSTATE_REASON); }
const char *bootstate_reason(const char *msg){ return g_starting ? BOOTSTATE_REASON : msg; }
