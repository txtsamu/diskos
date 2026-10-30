/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_ADMIT_H
#define DISKOS_ADMIT_H

#include <stdint.h>
#include "ipc.h"      /* track_state_t */
#include "outseq.h"   /* outseq_rc */

/* Player admission module (GLOSSARY.md): the one place that answers "may the UI touch the Player now?", and that runs
 * the pause -> Output sequence -> seek -> resume dance, so no caller re-derives either. Callers ask one question
 * (admit_check) or make one call (admit_switch, admit_play_start); the facts the rules read (Source mode, the
 * Bluetooth route, the post-restart settle window, the play-recent latch) and the order of the dance live here and
 * nowhere else. No LVGL. Seams: ipc_send_cmd() (through outseq and the dance) and ipc_get_state()/ipc_generation();
 * what only the UI process knows comes in through struct admit_env, so a host test drives it with fakes.
 *
 * Replaces playadmit.c (the play-start gate, now admit_play_start) and the per-caller compositions of
 * ui_local_playback_allowed / modes_output_busy / source mode / BT route / settle / play-recent in main.c, ui.c,
 * modes.c and srcswitch.c. */

struct admit_env {
    uint32_t (*now)(void);                 /* milliseconds, wrapping (lv_tick_get) */
    int  (*local_playback_allowed)(void);  /* the launch verdict qualified the Player and the card is local and healthy (srcswitch.c) */
    int  (*output_busy)(void);             /* an output switch is waiting for silence or recovering (modes.c) */
    int  (*play_pending)(void);            /* a play was sent and its track has not arrived yet */
    int  (*book_active)(void);             /* an audiobook is the active playback context */
    int  (*playing)(void);                 /* playstate's inference: position advancing. Paused and stopped both read 0 */
    int  (*fw_needs_local_init)(void);     /* this firmware's Player drifts its route/work mode when idle (V2.40/V2.57) */
};
void admit_set_env(const struct admit_env *env);   /* once at startup, before the first admit_* call */

/* ---- the question ---- */
typedef enum {
    ADMIT_OUTPUT_IDLE,      /* no output switch in flight: a source switch, a settings re-send, a pause the UI makes by itself */
    ADMIT_LOCAL_PLAYBACK,   /* card access alone: a seek, an audiobook tick, a library scan */
    ADMIT_PLAYER_COMMAND,   /* a transport, play or queue jump the user asked for: output idle and local playback allowed */
    ADMIT_OUTPUT_SWITCH,    /* an Output route switch (0666 re-inits the output): Local, no BT route, not starting, no book */
    ADMIT_ROUTE_BT,         /* route Local playback to a Bluetooth sink: output idle, local playback allowed, Local source */
    ADMIT_ROUTE_BT_SEND,    /* ...and past the post-restart settle window; asked after the already-routed shortcut */
    ADMIT_ROUTE_ANALOG,     /* leave a Bluetooth sink for the analog DAC: output idle and local playback allowed */
    ADMIT_LOCAL_INIT        /* the boot Local init (0666/0642/0657): local playback allowed, Local source, output idle */
} admit_what_t;

/* NULL = go. Otherwise a short reason, worded for a toast (callers that stay silent just ignore it). Rules, in the
 * order they are checked (the first that fails names the reason):
 *   OUTPUT_IDLE:    output switch in flight                    "Switching output - try again"
 *   LOCAL_PLAYBACK: not allowed                                "Return to local playback first"
 *   PLAYER_COMMAND: OUTPUT_IDLE, then LOCAL_PLAYBACK
 *   OUTPUT_SWITCH:  not allowed                                "SD card is not ready"
 *                   Source mode is not Local                   "Return to local playback first"
 *                   Bluetooth route active                     "Bluetooth output is active"
 *                   post-restart settle window                 "Player is starting - try again"
 *                   a play still starting                      "Player is busy - try again"
 *                   an audiobook is active                     "Stop the audiobook first"
 *   ROUTE_BT:       OUTPUT_IDLE, LOCAL_PLAYBACK, then Source mode is not Local ("Return to local playback first")
 *   ROUTE_BT_SEND:  post-restart settle window                 "Player is starting - try again"
 *   ROUTE_ANALOG:   same as PLAYER_COMMAND
 *   LOCAL_INIT:     LOCAL_PLAYBACK, Source mode, then OUTPUT_IDLE */
const char *admit_check(admit_what_t what);

/* ---- the dance ---- */
/* The Player as it was before the pause: handed to the step (its volume, position) and back to the caller (its log). */
typedef struct { int was_playing; track_state_t st; } admit_held_t;
typedef outseq_rc (*admit_step_fn)(const admit_held_t *held, void *arg);
#define ADMIT_SWITCH_OK            0
#define ADMIT_SWITCH_NOT_PAUSED  (-1)   /* the pause was refused: nothing else was sent */
#define ADMIT_SWITCH_STEP_FAILED (-2)   /* the step did not land: the Player is left paused (the caller keeps its recorded state) */
/* Run `step` (an Output sequence and whatever must follow it) with the Player held: snapshot it, pause it if it was
 * playing, run the step, then put the position back (seek, when a track was loaded past 0) and resume. A step that does
 * not return OUTSEQ_OK ends the dance without the seek and the resume. `held` may be NULL. The caller has asked
 * admit_check first. rc < 0 is a failure. */
int admit_switch(admit_step_fn step, void *arg, admit_held_t *held);

/* Seek the current track to `ms` (0103). -1 when local playback is not allowed or the send failed. */
int ui_seek_to(long ms);

/* ---- starting a play ---- */
/* LIST:          a library or queue play (0100 build, or a type-0 jump in the loaded queue).
 * RESERVED_SLOT: a play from the reserved custom-playlist slot (playlist, type-5 plan, audiobook): an idle Player can
 *                be left in NO_WORK_MODE by it, so the work mode is asserted on every firmware. */
typedef enum { ADMIT_PLAY_LIST, ADMIT_PLAY_RESERVED_SLOT } admit_play_t;
typedef enum { ADMIT_SENT_NONE = 0, ADMIT_SENT_WORKMODE = 1, ADMIT_SENT_PREAMBLE = 2 } admit_sent_t;
/* Send what this play start needs before its play frame, best effort (the result is not a verdict: the play goes on).
 *   LIST:          the route preamble (outseq_local_preamble) only on a firmware that needs the local init, Local
 *                  source, no Bluetooth route, not playing and no play still starting; 0666 under a stream would gap it.
 *                  "Not playing" covers paused and stopped alike on purpose: a LIST play while PAUSED gets the preamble
 *                  too (re-inits the USB output, restarts the song). Skipping it needs a signal playstate lacks: have_track
 *                  stays 1 for a Player that is idle after boot or stopped by a frame ipc.c does not recognise, and the
 *                  idle Player is exactly the one whose route/work mode have drifted.
 *   RESERVED_SLOT: work mode alone (outseq_assert_workmode) on Local source with no Bluetooth route; the route is
 *                  already right and a redundant 0666 would pause the stream. */
admit_sent_t admit_play_start(admit_play_t kind);
void admit_note_play_sent(void);   /* a play frame went out: a play is "recent" for the next 10 s of this Player generation */

/* ---- the post-restart settle window ---- */
/* A restarted Player runs a ~7 s late init that can overwrite an output route sent too early. */
void admit_note_player_restart(void);   /* arm the 9 s window (on a Player reconnect) */
int  admit_settling(void);              /* 1 inside the window; disarms itself once it has passed */
void admit_tick(void);                  /* every main-loop pass: service the expiry so a dormant armed flag can never wrap into a false "settling" */

#endif
