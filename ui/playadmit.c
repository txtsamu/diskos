/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "playadmit.h"
#include "outseq.h"

/* Local source on an analog/SPDIF/USB-audio route, never a Bluetooth A2DP path. */
static int local_route(const playadmit_situation_t *s){ return s->source_mode == 0 && !s->bt_route; }

playadmit_sent_t playadmit_start(playadmit_kind_t kind, const playadmit_situation_t *s){
    if(!s) return PLAYADMIT_NONE;
    if(kind == PLAYADMIT_RESERVED_SLOT){
        if(!local_route(s)) return PLAYADMIT_NONE;
        outseq_assert_workmode();                          /* 0657 8 LOCALPLAYER (fixes NO_WORK_MODE); NOT 0666: already on the local route */
        return PLAYADMIT_WORKMODE;
    }
    /* Only while STOPPED (an active track-jump already has the route and mode correct, and 0666 would gap the audio),
     * and not while a previous play is still starting (g_playing lags the stream by seconds; 0666 releases the local
     * output under it). V2.40/V2.57 only: V2.09/V2.28 don't drift, and 0666's close_player could disturb their path. */
    if(s->fw_needs_local_init && local_route(s) && !s->playing && !s->play_recent){
        outseq_local_preamble((outseq_route_t)s->output_route);   /* 0666 local route (6 internal, 4 SPDIF, 3 + 0642 5 USB audio), then 0657 8 */
        return PLAYADMIT_PREAMBLE;
    }
    return PLAYADMIT_NONE;
}
