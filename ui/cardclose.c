/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "cardclose.h"

static const struct cardclose_ops *E;
static cardclose_phase_t g_cc;
static uint32_t g_t0;

static uint32_t cc_elapsed(void){ return E->now() - g_t0; }

int cardclose_begin(const struct cardclose_ops *ops){
    if(!ops || !ops->drained || !ops->sync_start || !ops->sync_poll || !ops->sync_kill || !ops->now) return -1;
    E = ops;
    g_cc = CARD_CLOSE_DRAINING;
    g_t0 = E->now();
    return 0;
}

int cardclose_tick(void){
    switch(g_cc){
    case CARD_CLOSE_OFF:
    case CARD_CLOSE_DONE:
    case CARD_CLOSE_DRAIN_TIMEOUT:
    case CARD_CLOSE_SYNC_TIMEOUT:
    case CARD_CLOSE_SYNC_FAILED:
    case CARD_CLOSE_SPAWN_FAILED:
        break;
    case CARD_CLOSE_DRAINING:
        if(E->drained()){
            if(E->sync_start() != 0){ g_cc = CARD_CLOSE_SPAWN_FAILED; break; }
            g_cc = CARD_CLOSE_SYNCING;
            g_t0 = E->now();
        } else if(cc_elapsed() >= CARDCLOSE_DRAIN_MS) g_cc = CARD_CLOSE_DRAIN_TIMEOUT;
        break;
    case CARD_CLOSE_SYNCING: {
        int r = E->sync_poll();
        if(r < 0){ g_cc = CARD_CLOSE_SYNC_FAILED; break; }
        if(r == 1){
            if(E->unmount) E->unmount();
            g_cc = CARD_CLOSE_DONE;
            break;
        }
        if(cc_elapsed() >= CARDCLOSE_SYNC_MS){ E->sync_kill(); g_cc = CARD_CLOSE_SYNC_TIMEOUT; }
        break; }
    }
    return (int)g_cc;
}

void cardclose_abort(void){
    if(g_cc == CARD_CLOSE_SYNCING) E->sync_kill();
    g_cc = CARD_CLOSE_OFF;
}
