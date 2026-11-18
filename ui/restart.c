/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "restart.h"
#include "cardclose.h"
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

enum { RS_IDLE = 0, RS_DRAINING, RS_SYNCING };

static const struct restart_env *E;
static int      g_state = RS_IDLE;
static int      g_kind;
static pid_t    g_sync, g_clock;
static struct cardclose_ops g_cc;

static void fail(const char *why);

void restart_set_env(const struct restart_env *env){ E = env; }
int  restart_busy(void){ return g_state != RS_IDLE; }
void restart_abort(const char *why){ if(g_state != RS_IDLE) fail(why); }

#ifdef DISKOS_SWB_FLASHMODE
#define SWB_REG_PAGE 0x10000000u
#define SWB_REG_OFF  0xCCu
#define SWB_USB_BOOT 0x42575302u
static int flash_arm_builtin(void){
    int mem = open("/dev/mem", O_RDWR | O_SYNC);
    if(mem < 0) return -1;
    void *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mem, SWB_REG_PAGE);
    close(mem);
    if(map == MAP_FAILED) return -1;
    volatile uint32_t *reg = (volatile uint32_t *)((char *)map + SWB_REG_OFF);
    *reg = SWB_USB_BOOT;
    int ok = *reg == SWB_USB_BOOT;
    munmap(map, 4096);
    return ok ? 0 : -1;
}
#else
static int flash_arm_builtin(void){ return -1; }
#endif

static void fail(const char *why){
    cardclose_abort();
    if(g_clock > 0){ E->kill(g_clock); g_clock = 0; }
    g_state = RS_IDLE;
    E->quiesce_end();
    E->toast(why);
}

static int cc_drained(void){ return E->card_drained(); }
static int cc_sync_start(void){
    g_sync = E->spawn("sync");
    if(g_sync <= 0){ g_sync = 0; return -1; }
    return 0;
}
static int cc_sync_poll(void){
    int st = 0;
    int w = E->wait(g_sync, &st);
    if(w != g_sync) return 0;
    g_sync = 0;
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 1 : -1;
}
static void cc_sync_kill(void){
    if(g_sync > 0){ E->kill(g_sync); g_sync = 0; }
}
static int cc_unmount(void){ return E->unmount_card ? E->unmount_card() : 0; }
static uint32_t cc_now(void){ return E->now(); }

int restart_begin(restart_kind_t kind){
    if(g_state != RS_IDLE) return -1;
    E->quiesce_begin();
    E->stop_readers();
    g_clock = E->save_clock();
    if(g_clock < 0) g_clock = 0;
    g_kind = kind;
    g_cc = (struct cardclose_ops){
        .drained = cc_drained, .sync_start = cc_sync_start, .sync_poll = cc_sync_poll,
        .sync_kill = cc_sync_kill, .unmount = cc_unmount, .now = cc_now,
    };
    if(cardclose_begin(&g_cc) != 0) return -1;
    g_state = RS_DRAINING;
    E->toast("Restarting...");
    return 0;
}

void restart_tick(void){
    if(g_state == RS_IDLE) return;
    int cc = cardclose_tick();
    if(cc == CARD_CLOSE_DRAINING){ g_state = RS_DRAINING; return; }
    if(cc == CARD_CLOSE_SYNCING){ g_state = RS_SYNCING; return; }
    if(cc == CARD_CLOSE_DONE){
        if(g_clock > 0){ E->kill(g_clock); g_clock = 0; }
        if(g_kind == RESTART_FLASH){
            int arm = E->flash_arm ? E->flash_arm() : flash_arm_builtin();
            if(arm != 0){ fail("Couldn't set flash mode - not restarted"); return; }
        }
        g_state = RS_IDLE;
        if(!E->reboot()){ fail("Couldn't restart - try again"); return; }
        E->quiesce_end();
        return;
    }
    if(cc == CARD_CLOSE_SYNC_FAILED){ fail("Couldn't restart - card flush failed"); return; }
    fail("Couldn't restart - card busy, try again");
}
