/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "cardclose.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t g_now;
static int g_drained, g_start_rc, g_poll;
static int g_starts, g_polls, g_kills, g_unmounts;
static char g_order[16];
static int g_norder;

static void order(char c){
    assert(g_norder < 15);
    g_order[g_norder++] = c;
    g_order[g_norder] = 0;
}
static int fake_drained(void){ return g_drained; }
static int fake_sync_start(void){ order('a'); g_starts++; return g_start_rc; }
static int fake_sync_poll(void){
    g_polls++;
    if(g_poll != 0) order('p');
    return g_poll;
}
static void fake_sync_kill(void){ order('k'); g_kills++; }
static int fake_unmount(void){ order('u'); g_unmounts++; return 0; }
static uint32_t fake_now(void){ return g_now; }

static const struct cardclose_ops ops = {
    .drained = fake_drained, .sync_start = fake_sync_start, .sync_poll = fake_sync_poll,
    .sync_kill = fake_sync_kill, .unmount = fake_unmount, .now = fake_now,
};
static const struct cardclose_ops ops_no_unmount = {
    .drained = fake_drained, .sync_start = fake_sync_start, .sync_poll = fake_sync_poll,
    .sync_kill = fake_sync_kill, .unmount = NULL, .now = fake_now,
};

static void reset(void){
    g_now = 1000;
    g_drained = 1;
    g_start_rc = 0;
    g_poll = 0;
    g_starts = g_polls = g_kills = g_unmounts = g_norder = 0;
    memset(g_order, 0, sizeof g_order);
    assert(cardclose_begin(&ops) == 0);
}
static void run(int ms){ for(int t = 0; t < ms; t += 100){ g_now += 100; cardclose_tick(); } }

static void incomplete_ops_are_refused(void){
    struct cardclose_ops broken = ops;
    broken.sync_poll = NULL;
    assert(cardclose_begin(&broken) == -1);
    broken = ops;
    broken.now = NULL;
    assert(cardclose_begin(&broken) == -1);
    assert(cardclose_begin(NULL) == -1);
}

static void unmount_follows_a_successful_sync(void){
    reset();
    assert(cardclose_tick() == CARD_CLOSE_SYNCING);
    g_poll = 1;
    assert(cardclose_tick() == CARD_CLOSE_DONE);
    assert(!strcmp(g_order, "apu"));
    assert(g_unmounts == 1);
    assert(cardclose_tick() == CARD_CLOSE_DONE);
    assert(g_unmounts == 1 && g_polls == 1);
    cardclose_abort();
    assert(cardclose_tick() == CARD_CLOSE_OFF);
}

static void missing_unmount_still_completes(void){
    reset();
    assert(cardclose_begin(&ops_no_unmount) == 0);
    g_poll = 1;
    assert(cardclose_tick() == CARD_CLOSE_SYNCING);
    assert(cardclose_tick() == CARD_CLOSE_DONE && g_unmounts == 0);
}

static void drain_expires_at_the_unified_deadline(void){
    reset();
    g_drained = 0;
    run(8000);
    assert(cardclose_tick() == CARD_CLOSE_DRAINING && g_starts == 0);
    run(CARDCLOSE_DRAIN_MS - 8000 - 100);
    assert(cardclose_tick() == CARD_CLOSE_DRAINING && g_starts == 0);
    run(100);
    assert(cardclose_tick() == CARD_CLOSE_DRAIN_TIMEOUT);
    assert(g_starts == 0 && g_unmounts == 0 && g_kills == 0);
    cardclose_abort();
    assert(cardclose_tick() == CARD_CLOSE_OFF);
}

static void sync_failure_never_unmounts(void){
    reset();
    assert(cardclose_tick() == CARD_CLOSE_SYNCING);
    g_poll = -1;
    assert(cardclose_tick() == CARD_CLOSE_SYNC_FAILED);
    assert(g_unmounts == 0 && g_kills == 0);
    cardclose_abort();
    assert(cardclose_tick() == CARD_CLOSE_OFF);
}

static void sync_timeout_kills_the_child_once(void){
    reset();
    assert(cardclose_tick() == CARD_CLOSE_SYNCING);
    run(CARDCLOSE_SYNC_MS - 1000);
    assert(g_kills == 0);
    run(1000);
    assert(g_kills == 1 && g_unmounts == 0);
    assert(cardclose_tick() == CARD_CLOSE_SYNC_TIMEOUT);
    assert(g_kills == 1);
    cardclose_abort();
    assert(cardclose_tick() == CARD_CLOSE_OFF);
}

static void spawn_failure_fails_without_a_child(void){
    reset();
    g_start_rc = -1;
    assert(cardclose_tick() == CARD_CLOSE_SPAWN_FAILED);
    assert(g_unmounts == 0 && g_kills == 0 && g_polls == 0);
}

static void abort_kills_a_syncing_child(void){
    reset();
    assert(cardclose_tick() == CARD_CLOSE_SYNCING);
    cardclose_abort();
    assert(g_kills == 1);
    assert(cardclose_tick() == CARD_CLOSE_OFF);
    cardclose_abort();
    assert(g_kills == 1);
}

static void a_new_begin_reuses_the_transaction(void){
    reset();
    g_drained = 0;
    run(CARDCLOSE_DRAIN_MS);
    assert(cardclose_tick() == CARD_CLOSE_DRAIN_TIMEOUT);
    g_drained = 1;
    g_poll = 1;
    assert(cardclose_begin(&ops) == 0);
    assert(cardclose_tick() == CARD_CLOSE_SYNCING);
    assert(cardclose_tick() == CARD_CLOSE_DONE);
    assert(g_unmounts == 1);
}

int main(void){
    incomplete_ops_are_refused();
    unmount_follows_a_successful_sync();
    missing_unmount_still_completes();
    drain_expires_at_the_unified_deadline();
    sync_failure_never_unmounts();
    sync_timeout_kills_the_child_once();
    spawn_failure_fails_without_a_child();
    abort_kills_a_syncing_child();
    a_new_begin_reuses_the_transaction();
    puts("cardclose_test: ok");
    return 0;
}
