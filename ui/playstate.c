/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "playstate.h"

void playstate_reset(playstate_t *ps){ ps->last_pos_ms = -1; ps->change_tick = 0; }

int playstate_playing(playstate_t *ps, int have_track, long position_ms,
                      unsigned now_tick, unsigned grace_ms, unsigned max_adv_ms){
    if(!have_track){ ps->last_pos_ms = -1; return 0; }
    if(ps->last_pos_ms < 0){
        ps->last_pos_ms = position_ms;
        ps->change_tick = now_tick;
    } else if(position_ms != ps->last_pos_ms){
        long delta = position_ms - ps->last_pos_ms;
        ps->last_pos_ms = position_ms;
        if(delta > 0 && (max_adv_ms == 0 || (unsigned long)delta <= max_adv_ms))
            ps->change_tick = now_tick;
    }
    return (position_ms > 0 && (unsigned)(now_tick - ps->change_tick) < grace_ms);
}
