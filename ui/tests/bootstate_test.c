/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "bootstate.h"
#include "ipc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static uint32_t k_now;
static int k_up, k_gate;
static uint32_t env_now(void){ return k_now; }
static int env_up(void){ return k_up; }
static int env_gate(void){ return k_gate; }
static const struct bootstate_env env = { env_now, env_up, env_gate };

static void reset(uint32_t t0){ k_now = t0; k_up = 0; k_gate = 0; bootstate_start(&env); }

static char g_cap[512];
static int g_save = -1, g_pipe[2];
static void cap_begin(void){
    fflush(stderr);
    assert(pipe(g_pipe) == 0);
    g_save = dup(2); dup2(g_pipe[1], 2); close(g_pipe[1]);
    fcntl(g_pipe[0], F_SETFL, O_NONBLOCK);
}
static const char *cap_end(void){
    fflush(stderr);
    dup2(g_save, 2); close(g_save);
    ssize_t n = read(g_pipe[0], g_cap, sizeof g_cap - 1);
    close(g_pipe[0]);
    g_cap[n > 0 ? n : 0] = 0;
    return g_cap;
}

static void starts_true(void){
    reset(5000);
    assert(bootstate_starting());
    assert(!bootstate_take_change());
    assert(!strcmp(bootstate_reason("x"), BOOTSTATE_REASON));
}

static void waits_for_both_signals(void){
    reset(100);
    k_now = 600; bootstate_tick(); assert(bootstate_starting());
    k_up = 1; k_now = 900; bootstate_tick(); assert(bootstate_starting());
    k_up = 0; k_gate = 1; bootstate_tick(); assert(bootstate_starting());
    k_up = 1; k_now = 2100;
    cap_begin(); bootstate_tick(); const char *log = cap_end();
    assert(!bootstate_starting());
    assert(!strcmp(log, "boot: player ready after 2000 ms\n"));
    assert(bootstate_take_change());
    assert(!bootstate_take_change());
}

static void ready_is_latched(void){
    reset(0);
    k_up = 1; k_gate = 1; k_now = 10; bootstate_tick(); assert(!bootstate_starting());
    k_up = 0; k_gate = 0; k_now = 20; bootstate_tick();
    assert(!bootstate_starting());
    assert(!strcmp(bootstate_reason("x"), "x"));
}

static void gives_up_at_the_ipc_hold(void){
    reset(UINT32_MAX - 99);
    k_now = UINT32_MAX - 99 + IPC_BOOT_HOLD_MS - 1; bootstate_tick(); assert(bootstate_starting());
    k_now += 1;
    cap_begin(); bootstate_tick(); const char *log = cap_end();
    assert(!bootstate_starting());
    assert(strstr(log, "gave up waiting after 30000 ms (player not up, SD gate pending)"));
    assert(bootstate_take_change());
}

static void gives_up_names_the_missing_signal(void){
    reset(0); k_up = 1;
    k_now = IPC_BOOT_HOLD_MS;
    cap_begin(); bootstate_tick(); const char *log = cap_end();
    assert(strstr(log, "player up, SD gate pending"));
    reset(0); k_gate = 1;
    k_now = IPC_BOOT_HOLD_MS;
    cap_begin(); bootstate_tick(); log = cap_end();
    assert(strstr(log, "player not up, SD gate settled"));
}

static void reason_helpers(void){
    reset(0);
    assert(bootstate_is_reason(BOOTSTATE_REASON));
    assert(!bootstate_is_reason("Player is busy - try again"));
    assert(!bootstate_is_reason(NULL));
    assert(!strcmp(bootstate_reason("SD card is not ready"), BOOTSTATE_REASON));
    k_up = k_gate = 1; bootstate_tick();
    assert(!strcmp(bootstate_reason("SD card is not ready"), "SD card is not ready"));
}

static void restart_rearms(void){
    reset(0); k_up = k_gate = 1; bootstate_tick(); assert(!bootstate_starting());
    reset(500);
    assert(bootstate_starting() && !bootstate_take_change());
}

int main(void){
    starts_true();
    waits_for_both_signals();
    ready_is_latched();
    gives_up_at_the_ipc_hold();
    gives_up_names_the_missing_signal();
    reason_helpers();
    restart_rearms();
    puts("bootstate_test: ok");
    return 0;
}
