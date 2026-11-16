/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "restart.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

static uint32_t g_now;
static int g_drained, g_quiesced, g_unqueued, g_stopped, g_killed;
static int g_flash_rc, g_rebooted;
static pid_t g_clock = 900;
static char g_spawned[8][16];
static int g_nspawn, g_ntoast;
static char g_toast[8][96];
static struct { pid_t pid; int running, status; } g_child;

static uint32_t env_now(void){ return g_now; }
static int env_drained(void){ return g_drained; }
static void env_quiesce(void){ g_quiesced++; }
static void env_unquiesce(void){ g_unqueued++; }
static void env_stop(void){ g_stopped++; }
static pid_t env_clock(void){ return g_clock; }
static pid_t env_spawn(const char *cmd){
    assert(g_nspawn < 8);
    snprintf(g_spawned[g_nspawn++], sizeof g_spawned[0], "%s", cmd);
    g_child.pid = 901; g_child.running = 1; g_child.status = 0;
    return g_child.pid;
}
static int env_wait(pid_t pid, int *status){
    assert(pid == g_child.pid);
    if(g_child.running) return 0;
    *status = g_child.status;
    return (int)pid;
}
static void env_kill(pid_t pid){ (void)pid; g_killed++; }
static int env_flash(void){ return g_flash_rc; }
static int env_reboot(void){ g_rebooted++; return 1; }
static void env_toast(const char *s){
    assert(g_ntoast < 8);
    snprintf(g_toast[g_ntoast++], sizeof g_toast[0], "%s", s);
}

static const struct restart_env env = {
    .now = env_now, .card_drained = env_drained,
    .quiesce_begin = env_quiesce, .quiesce_end = env_unquiesce,
    .stop_readers = env_stop, .save_clock = env_clock,
    .spawn = env_spawn, .wait = env_wait, .kill = env_kill,
    .flash_arm = env_flash, .reboot = env_reboot, .toast = env_toast,
};

static void setup(void){
    g_now = 1000; g_drained = 0; g_quiesced = g_unqueued = g_stopped = g_killed = 0;
    g_flash_rc = 0; g_rebooted = 0; g_clock = 900;
    g_nspawn = g_ntoast = 0; memset(&g_child, 0, sizeof g_child);
    restart_set_env(&env);
}
static void run(int ms){ for(int t = 0; t < ms; t += 100){ g_now += 100; restart_tick(); } }
static int toasted(const char *s){ for(int i = 0; i < g_ntoast; i++) if(!strcmp(g_toast[i], s)) return 1; return 0; }

static void card_never_drains(void){
    setup();
    assert(restart_begin(RESTART_UI) == 0 && restart_busy());
    assert(g_quiesced == 1 && g_stopped == 1 && toasted("Restarting..."));
    assert(restart_begin(RESTART_UI) < 0);
    run(9900);
    assert(restart_busy());
    run(100);
    assert(!restart_busy() && g_unqueued == 1 && g_nspawn == 0 && !g_rebooted);
    assert(toasted("Couldn't restart - card busy, try again") && g_killed == 1);
}

static void drain_sync_and_reboot(void){
    setup(); g_drained = 1;
    assert(restart_begin(RESTART_UI) == 0);
    restart_tick();
    assert(g_nspawn == 1 && !strcmp(g_spawned[0], "sync") && restart_busy());
    run(1000);
    assert(restart_busy() && !g_rebooted);
    g_child.running = 0; g_child.status = 0;
    restart_tick();
    assert(!restart_busy() && g_rebooted == 1 && g_unqueued == 1 && g_killed == 1);
    assert(!toasted("Couldn't restart - card busy, try again"));
}

static void sync_failure_stops_the_restart(void){
    setup(); g_drained = 1;
    assert(restart_begin(RESTART_UI) == 0);
    restart_tick();
    g_child.running = 0; g_child.status = 1 << 8;
    restart_tick();
    assert(!restart_busy() && !g_rebooted && g_unqueued == 1 && g_killed == 1);
    assert(toasted("Couldn't restart - card flush failed"));
}

static void sync_timeout_stops_the_restart(void){
    setup(); g_drained = 1;
    assert(restart_begin(RESTART_UI) == 0);
    restart_tick();
    run(14900);
    assert(restart_busy());
    run(100);
    assert(!restart_busy() && !g_rebooted && g_unqueued == 1);
    assert(toasted("Couldn't restart - card busy, try again"));
}

static void flash_arm_failure_stops_the_restart(void){
    setup(); g_drained = 1; g_flash_rc = -1;
    assert(restart_begin(RESTART_FLASH) == 0);
    restart_tick();
    g_child.running = 0; g_child.status = 0;
    restart_tick();
    assert(!restart_busy() && !g_rebooted && g_unqueued == 1 && g_killed == 1);
    assert(toasted("Couldn't set flash mode - not restarted"));
}

static void abort_cleans_up_a_started_restart(void){
    setup(); g_drained = 0;
    assert(restart_begin(RESTART_UI) == 0);
    restart_abort("Couldn't restart - try again");
    assert(!restart_busy() && g_unqueued == 1 && g_killed == 1 && !g_rebooted);
    assert(toasted("Couldn't restart - try again"));
}
static void flash_arm_then_reboot(void){
    setup(); g_drained = 1; g_flash_rc = 0;
    assert(restart_begin(RESTART_FLASH) == 0);
    restart_tick();
    g_child.running = 0; g_child.status = 0;
    restart_tick();
    assert(!restart_busy() && g_rebooted == 1 && g_unqueued == 1);
}

int main(void){
    card_never_drains();
    drain_sync_and_reboot();
    sync_failure_stops_the_restart();
    sync_timeout_stops_the_restart();
    flash_arm_failure_stops_the_restart();
    abort_cleans_up_a_started_restart();
    flash_arm_then_reboot();
    puts("restart_test: ok");
    return 0;
}
