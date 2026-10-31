/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "clicks.h"

void clicks_reset(clicks_t *c){
    if(!c) return;
    c->count = 0;
    c->last_ms = 0;
}

int clicks_press(clicks_t *c, uint32_t now){
    if(!c) return 0;
    c->count++;
    c->last_ms = now;
    if(c->count >= CLICKS_MAX){
        clicks_reset(c);
        return CLICKS_MAX;
    }
    return 0;
}

int clicks_poll(clicks_t *c, uint32_t now){
    int n;
    if(!c || c->count <= 0) return 0;
    if((uint32_t)(now - c->last_ms) < CLICKS_WINDOW_MS) return 0;
    n = c->count;
    clicks_reset(c);
    return n;
}
