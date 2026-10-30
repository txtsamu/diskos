/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "admit.h"
#include "session.h"
#include <stdio.h>
#include <string.h>

/* See admit.h. The rules are the ones main.c, ui.c, modes.c and srcswitch.c each composed by hand before this module;
 * the reason strings are theirs, word for word. */

static const struct admit_env *E;
void admit_set_env(const struct admit_env *env){ E = env; }

#define REASON_SWITCHING   "Switching output - try again"
#define REASON_LOCAL_FIRST "Return to local playback first"

/* ---- the facts this module owns ---- */
/* The post-restart settle window. An EXPLICIT armed flag (not a bare deadline) avoids a false "settling" when the
 * absolute tick sits in the upper half of the 32-bit range; the window disarms itself once it has passed. */
static int      g_settle_armed = 0;
static uint32_t g_settle_until = 0;
void admit_note_player_restart(void){ g_settle_armed = 1; g_settle_until = E->now() + 9000; }
int admit_settling(void){
    if(!g_settle_armed) return 0;
    if((int32_t)(E->now() - g_settle_until) >= 0){ g_settle_armed = 0; return 0; }   /* window passed -> disarm */
    return 1;
}
void admit_tick(void){ if(g_settle_armed) (void)admit_settling(); }

/* Generation + tick of the last play command sent. g_playing lags the stream by seconds, and 0666 under a play that
 * is still starting releases the local output (seen live). */
static unsigned g_play_last_gen = 0xFFFFFFFFu;
static uint32_t g_play_last_tick = 0;
void admit_note_play_sent(void){ g_play_last_gen = ipc_generation(); g_play_last_tick = E->now(); }
static int play_recent(void){
    return E->play_pending() || (g_play_last_gen == ipc_generation() && (uint32_t)(E->now() - g_play_last_tick) < 10000);
}

/* ---- the question ---- */
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
        if(session_bt_routed()) return "Bluetooth output is active";
        if(admit_settling()) return "Player is starting - try again";
        if(play_recent()) return "Player is busy - try again";
        if(E->book_active()) return "Stop the audiobook first";
        return NULL;
    case ADMIT_ROUTE_BT:   /* BT routing re-sends 0657 8: in USB DAC / BT-receive it would drop the Player out of that work mode */
        if((why = idle_reason())) return why;
        if((why = card_reason())) return why;
        return local_source() ? NULL : REASON_LOCAL_FIRST;
    case ADMIT_ROUTE_BT_SEND:   /* a route sent in the settle window could be reverted by the Player's late init */
        return admit_settling() ? "Player is starting - try again" : NULL;
    case ADMIT_LOCAL_INIT:
        if((why = card_reason())) return why;
        if(!local_source()) return REASON_LOCAL_FIRST;
        return idle_reason();
    }
    return REASON_LOCAL_FIRST;   /* an unknown question is never a yes */
}

/* ---- the dance ---- */
int ui_seek_to(long ms){
    /* A seek drives the player's current (card-backed) track, so it is card access like any other: never
     * before this boot's launch verdict qualified the player, and never while SD access is held. */
    if(admit_check(ADMIT_LOCAL_PLAYBACK)) return -1;
    if(ms < 0) ms = 0;
    char f[32]; snprintf(f, sizeof f, "01030010%08lX", (unsigned long)ms);
    int rc = ipc_send_cmd(f);   /* 0 = queued OK, -1 = send failed */
    fprintf(stderr,"seek %ldms -> %s (rc=%d)\n", ms, f, rc); fflush(stderr);
    return rc;
}

int admit_switch(admit_step_fn step, void *arg, admit_held_t *held){
    admit_held_t h;
    ipc_get_state(&h.st);
    h.was_playing = E->playing();   /* real play state: the raw st.state reports 0 while playing, so the pause would be skipped */
    if(held) *held = h;
    if(h.was_playing && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0) return ADMIT_SWITCH_NOT_PAUSED;   /* don't switch the output unpaused */
    if(step(&h, arg) != OUTSEQ_OK) return ADMIT_SWITCH_STEP_FAILED;
    /* Safe minimal resume; deterministic load/play remains a future improvement. */
    if(h.st.have_track && h.st.position_ms > 0) ui_seek_to(h.st.position_ms);   /* resume position, not restart */
    if(h.was_playing) ipc_send_cmd(PLAYER_CMD_PLAYPAUSE);                        /* play */
    return ADMIT_SWITCH_OK;
}

/* ---- starting a play ---- */
typedef struct { int fw_needs_local_init, source_mode, bt_route, output_route, playing, play_recent; } situation_t;

/* Local source on an analog/SPDIF/USB-audio route, never a Bluetooth A2DP path. */
static int local_route(const situation_t *s){ return s->source_mode == 0 && !s->bt_route; }

static admit_sent_t play_decide(admit_play_t kind, const situation_t *s){
    if(kind == ADMIT_PLAY_RESERVED_SLOT){
        if(!local_route(s)) return ADMIT_SENT_NONE;
        outseq_assert_workmode();                          /* 0657 8 LOCALPLAYER (fixes NO_WORK_MODE); NOT 0666: already on the local route */
        return ADMIT_SENT_WORKMODE;
    }
    /* Only while STOPPED (an active track-jump already has the route and mode correct, and 0666 would gap the audio),
     * and not while a previous play is still starting (g_playing lags the stream by seconds; 0666 releases the local
     * output under it). V2.40/V2.57 only: V2.09/V2.28 don't drift, and 0666's close_player could disturb their path. */
    if(s->fw_needs_local_init && local_route(s) && !s->playing && !s->play_recent){
        outseq_local_preamble((outseq_route_t)s->output_route);   /* 0666 local route (6 internal, 4 SPDIF, 3 + 0642 5 USB audio), then 0657 8 */
        return ADMIT_SENT_PREAMBLE;
    }
    return ADMIT_SENT_NONE;
}

admit_sent_t admit_play_start(admit_play_t kind){
    situation_t s = {
        .fw_needs_local_init = E->fw_needs_local_init(),
        .source_mode         = session_source_mode(),
        .bt_route            = session_bt_routed(),
        .output_route        = session_route(),
        .playing             = E->playing(),
        .play_recent         = play_recent(),
    };
    return play_decide(kind, &s);
}
