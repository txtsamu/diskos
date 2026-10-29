/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "radio.h"
#include <stddef.h>

void radio_watch_cancel(struct radio_watch *w){
    if(!w->active) return;
    w->active = 0;
    w->disarm();
}

void radio_watch_begin(struct radio_watch *w, uint32_t now){
    radio_watch_cancel(w);                 /* a running watch is replaced, never doubled */
    w->start = now;
    w->active = 1;
    w->arm(w->interval_ms);
}

int radio_watch_tick(struct radio_watch *w, uint32_t now){
    if(!w->active) return RADIO_IDLE;
    if(w->is_up()){ radio_watch_cancel(w); return RADIO_UP; }
    if((uint32_t)(now - w->start) > w->timeout_ms){ radio_watch_cancel(w); return RADIO_TIMEOUT; }
    return RADIO_PENDING;
}

void radio_on(struct radio *r, uint32_t now, int observe){
    r->set_intent(1);                      /* intent first: whatever keeps the radio up (keepalive, boot restore) sees it */
    r->start();
    if(observe) radio_watch_begin(r->watch, now);
}

void radio_off(struct radio *r){
    r->set_intent(0);                      /* OFF before the teardown, so nothing resurrects the radio mid-way */
    radio_watch_cancel(r->watch);
    if(r->cleanup) r->cleanup();
    r->power_down();
}

int radio_tick(struct radio *r, uint32_t now){
    int v = radio_watch_tick(r->watch, now);
    if(v == RADIO_TIMEOUT && r->rollback){
        r->set_intent(0);                  /* never came up: do not keep claiming it is on */
        r->power_down();                   /* and make the cleared intent true, so a late half-start cannot linger */
    }
    return v;
}
