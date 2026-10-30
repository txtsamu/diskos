/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "radio.h"
#include <stddef.h>

void radio_watch_cancel(struct radio_watch *w){
    if(!w->active) return;
    w->active = 0;
    w->env->timer_stop(w->timer);
    w->timer = NULL;
}

void radio_watch_begin(struct radio_watch *w){
    radio_watch_cancel(w);                 /* a running watch is replaced, never doubled */
    w->start = w->env->now();
    w->active = 1;
    w->timer = w->env->timer_start(w->interval_ms, w);
}

int radio_watch_fire(struct radio_watch *w){
    if(!w->active) return RADIO_IDLE;
    if(w->stay && !w->stay()){ radio_watch_cancel(w); return RADIO_IDLE; }
    if(w->is_up()){
        radio_watch_cancel(w);
        if(w->on_up) w->on_up();
        return RADIO_UP;
    }
    if((uint32_t)(w->env->now() - w->start) > w->timeout_ms){
        radio_watch_cancel(w);
        struct radio *r = w->owner;
        if(r && r->timeout == RADIO_ROLLBACK){
            r->set_intent(0);              /* never came up: do not keep claiming it is on */
            r->power_down();               /* and make the cleared intent true, so a late half-start cannot linger */
        }
        if(w->on_timeout) w->on_timeout();
        return RADIO_TIMEOUT;
    }
    return RADIO_PENDING;
}

void radio_on(struct radio *r, int observe){
    r->watch.owner = r;
    r->set_intent(1);                      /* intent first: whatever keeps the radio up (keepalive, boot restore) sees it */
    r->start();
    if(r->excludes) radio_watch_cancel(r->excludes);   /* this bring-up owns the radio now */
    if(observe) radio_watch_begin(&r->watch);
}

void radio_off(struct radio *r){
    r->set_intent(0);                      /* OFF before the teardown, so nothing resurrects the radio mid-way */
    radio_watch_cancel(&r->watch);
    if(r->cleanup) r->cleanup();
    r->power_down();
}

int radio_toggle(struct radio *r){
    int on = !r->is_on();
    if(on) radio_on(r, r->toggle_observes);
    else   radio_off(r);
    return on;
}
