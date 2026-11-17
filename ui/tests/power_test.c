/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t g_now;
static int      g_poll;
static int      g_unmount_rc;
static int      g_unmounted;
static int      g_nsend;
static int      g_ntoast;
static char     g_order[8];
static int      g_norder;
static char     g_last_frame[16];

static void order(char c){
    assert(g_norder < 7);
    g_order[g_norder++] = c;
    g_order[g_norder] = 0;
}
static void fake_quiesce_begin(void *c){ (void)c; }
static int  fake_drained(void *c){ (void)c; return 1; }
static void fake_quiesce_end(void *c){ (void)c; }
static int  fake_sync_start(void *c){ (void)c; return 0; }
static int  fake_sync_poll(void *c){ (void)c; return g_poll; }
static void fake_sync_kill(void *c){ (void)c; }
static int  fake_send(void *c, const char *f){
    (void)c;
    snprintf(g_last_frame, sizeof g_last_frame, "%s", f);
    g_nsend++;
    order('S');
    return 0;
}
static uint32_t fake_now(void *c){ (void)c; return g_now; }
static void fake_toast(void *c, const char *m){ (void)c; (void)m; g_ntoast++; }
static int  fake_unmount(void *c){ (void)c; g_unmounted++; order('U'); return g_unmount_rc; }

static const power_ops_t ops_with_unmount = {
    .quiesce_begin = fake_quiesce_begin, .drained = fake_drained, .quiesce_end = fake_quiesce_end,
    .sync_start = fake_sync_start, .sync_poll = fake_sync_poll, .sync_kill = fake_sync_kill,
    .send = fake_send, .now_ms = fake_now, .toast = fake_toast,
    .unmount_card = fake_unmount,
};
static const power_ops_t ops_without_unmount = {
    .quiesce_begin = fake_quiesce_begin, .drained = fake_drained, .quiesce_end = fake_quiesce_end,
    .sync_start = fake_sync_start, .sync_poll = fake_sync_poll, .sync_kill = fake_sync_kill,
    .send = fake_send, .now_ms = fake_now, .toast = fake_toast,
};

static void reset(void){
    g_now = 1000;
    g_poll = 1;
    g_unmount_rc = 0;
    g_unmounted = g_nsend = g_ntoast = g_norder = 0;
    memset(g_order, 0, sizeof g_order);
    memset(g_last_frame, 0, sizeof g_last_frame);
}
static void to_off(void){
    if(power_shutdown_phase() == PW_HANDOFF){
        g_now += POWER_HANDOFF_MS;
        power_shutdown_tick();
    }
    if(power_shutdown_phase() == PW_PROTECTED) power_shutdown_new_generation();
    assert(power_shutdown_phase() == PW_OFF);
}

static void failed_sync_does_not_unmount(void){
    reset();
    g_poll = -1;
    assert(power_shutdown_begin(&ops_with_unmount) == 0);
    g_ntoast = 0;
    power_shutdown_tick();
    assert(power_shutdown_phase() == PW_SYNC);
    power_shutdown_tick();
    assert(power_shutdown_phase() == PW_OFF);
    assert(g_unmounted == 0 && g_nsend == 0 && g_ntoast == 1);
}

static void sync_success_unmounts_before_send(void){
    reset();
    assert(power_shutdown_begin(&ops_with_unmount) == 0);
    g_ntoast = 0;
    power_shutdown_tick();
    assert(power_shutdown_phase() == PW_SYNC && g_unmounted == 0);
    power_shutdown_tick();
    assert(power_shutdown_phase() == PW_HANDOFF);
    assert(g_unmounted == 1 && g_nsend == 1);
    assert(!strcmp(g_order, "US"));
    assert(!strcmp(g_last_frame, "0806000C0001"));
    assert(g_ntoast == 0);
    to_off();
}

static void unmount_failure_still_sends(void){
    reset();
    g_unmount_rc = -1;
    assert(power_shutdown_begin(&ops_with_unmount) == 0);
    g_ntoast = 0;
    power_shutdown_tick();
    power_shutdown_tick();
    assert(power_shutdown_phase() == PW_HANDOFF);
    assert(g_unmounted == 1 && g_nsend == 1);
    assert(!strcmp(g_order, "US"));
    assert(!strcmp(g_last_frame, "0806000C0001"));
    assert(g_ntoast == 0);
    to_off();
}

static void missing_unmount_still_sends(void){
    reset();
    assert(power_shutdown_begin(&ops_without_unmount) == 0);
    g_ntoast = 0;
    power_shutdown_tick();
    power_shutdown_tick();
    assert(power_shutdown_phase() == PW_HANDOFF);
    assert(g_unmounted == 0 && g_nsend == 1);
    assert(!strcmp(g_order, "S"));
    assert(!strcmp(g_last_frame, "0806000C0001"));
}

int main(void){
    failed_sync_does_not_unmount();
    sync_success_unmounts_before_send();
    unmount_failure_still_sends();
    missing_unmount_still_sends();
    puts("power_test: ok");
    return 0;
}
