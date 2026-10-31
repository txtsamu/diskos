/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "admit.h"
#include "session.h"
#include "outroute.h"
#include <stdio.h>
#include <string.h>

static const struct admit_env *E;
void admit_set_env(const struct admit_env *env){ E = env; }

#define REASON_SWITCHING   "Switching output - try again"
#define REASON_LOCAL_FIRST "Return to local playback first"

static int      g_settle_armed = 0;
static uint32_t g_settle_until = 0;
void admit_note_player_restart(void){ g_settle_armed = 1; g_settle_until = E->now() + 9000; }
int admit_settling(void){
    if(!g_settle_armed) return 0;
    if((int32_t)(E->now() - g_settle_until) >= 0){ g_settle_armed = 0; return 0; }
    return 1;
}
void admit_tick(void){ if(g_settle_armed) (void)admit_settling(); }

static unsigned g_play_last_gen = 0xFFFFFFFFu;
static uint32_t g_play_last_tick = 0;
void admit_note_play_sent(void){ g_play_last_gen = ipc_generation(); g_play_last_tick = E->now(); }
static int play_recent(void){
    return E->play_pending() || (g_play_last_gen == ipc_generation() && (uint32_t)(E->now() - g_play_last_tick) < 10000);
}

static int local_source(void){ return session_source_mode() == 0; }
static const char *idle_reason(void){ return E->output_busy() ? REASON_SWITCHING : NULL; }
static const char *card_reason(void){ return E->local_playback_allowed() ? NULL : REASON_LOCAL_FIRST; }

const char *admit_check(admit_what_t what){
    const char *why;
    switch(what){
    case ADMIT_OUTPUT_IDLE:    return idle_reason();
    case ADMIT_LOCAL_PLAYBACK: return card_reason();
    case ADMIT_PLAYER_COMMAND:
    case ADMIT_ROUTE_ANALOG:
        if((why = idle_reason())) return why;
        return card_reason();
    case ADMIT_OUTPUT_SWITCH:
        if(!E->local_playback_allowed()) return "SD card is not ready";
        if(!local_source()) return REASON_LOCAL_FIRST;
        if(outroute_bt_routed()) return "Bluetooth output is active";
        if(admit_settling()) return "Player is starting - try again";
        if(play_recent()) return "Player is busy - try again";
        if(E->book_active()) return "Stop the audiobook first";
        return NULL;
    case ADMIT_ROUTE_BT:
        if((why = idle_reason())) return why;
        if((why = card_reason())) return why;
        return local_source() ? NULL : REASON_LOCAL_FIRST;
    case ADMIT_ROUTE_BT_SEND:
        return admit_settling() ? "Player is starting - try again" : NULL;
    case ADMIT_LOCAL_INIT:
        if((why = card_reason())) return why;
        if(!local_source()) return REASON_LOCAL_FIRST;
        return idle_reason();
    }
    return REASON_LOCAL_FIRST;
}

int ui_seek_to(long ms){
    if(admit_check(ADMIT_LOCAL_PLAYBACK)) return -1;
    if(ms < 0) ms = 0;
    char f[32]; snprintf(f, sizeof f, "01030010%08lX", (unsigned long)ms);
    int rc = ipc_send_cmd(f);
    fprintf(stderr,"seek %ldms -> %s (rc=%d)\n", ms, f, rc); fflush(stderr);
    return rc;
}

typedef struct { int fw_needs_local_init, source_mode, bt_route, output_route, playing, play_recent; } situation_t;

static int local_route(const situation_t *s){ return s->source_mode == 0 && !s->bt_route; }

static admit_sent_t play_decide(admit_play_t kind, const situation_t *s){
    if(kind == ADMIT_PLAY_RESERVED_SLOT){
        if(!local_route(s)) return ADMIT_SENT_NONE;
        outseq_assert_workmode();
        return ADMIT_SENT_WORKMODE;
    }
    if(s->fw_needs_local_init && local_route(s) && !s->playing && !s->play_recent){
        outseq_local_preamble(s->output_route);
        return ADMIT_SENT_PREAMBLE;
    }
    return ADMIT_SENT_NONE;
}

admit_sent_t admit_play_start(admit_play_t kind){
    situation_t s = {
        .fw_needs_local_init = E->fw_needs_local_init(),
        .source_mode         = session_source_mode(),
        .bt_route            = outroute_bt_routed(),
        .output_route        = outroute_route(),
        .playing             = E->playing(),
        .play_recent         = play_recent(),
    };
    return play_decide(kind, &s);
}
