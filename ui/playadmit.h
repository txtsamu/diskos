/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_PLAYADMIT_H
#define DISKOS_PLAYADMIT_H

/* Play admission module (GLOSSARY.md): the one step that decides, from the situation, whether a play start must first
 * re-assert the Player's Local output route and work mode, and sends it through outseq.c. A caller says "I am starting
 * a play" and passes the situation; the gate (which situations get the 0666/0642/0657 preamble, which only the 0657 8
 * work mode, which nothing) lives here and nowhere else. No LVGL, no globals: the situation is a parameter, so the
 * accessors that fill it (source mode, Bluetooth route, play-recent latch, playstate) stay with the caller. The only
 * seam below is outseq.c (host tests link a recording fake of ipc_send_cmd). */

typedef struct {
    int fw_needs_local_init;   /* the firmware's Player drifts its route/work mode when idle (fw_needs_localplayer_init()) */
    int source_mode;           /* 0 = Local; any other source owns the Player (g_source_mode numbering) */
    int bt_route;              /* a Bluetooth sink route is active: never touch 0666 under it */
    int output_route;          /* outseq_route_t: the route Local playback currently uses */
    int playing;               /* playstate's inference: position advancing. NOT playing covers both paused and stopped:
                                * a LIST play while PAUSED therefore still gets the preamble (re-inits the USB output, restarts
                                * the song). Skipping it needs a signal playstate lacks: have_track stays 1 for a Player that is
                                * idle after boot (ipc_seed_state) or stopped by a frame ipc.c does not recognise as a stop,
                                * and the idle Player is exactly the one whose route/work mode have drifted. A true "paused on
                                * this route" input (e.g. the Player's own state, once a stop/pause frame is captured) would go
                                * here as a third value; until then paused == stopped on purpose. */
    int play_recent;           /* a previous play is still starting (it lags the stream by seconds) */
} playadmit_situation_t;

/* What kind of play is starting.
 * LIST:          a library or queue play (0100 build, or a type-0 jump in the loaded queue).
 * RESERVED_SLOT: a play from the reserved custom-playlist slot (playlist, type-5 plan, audiobook): an idle Player can
 *                be left in NO_WORK_MODE by it, so the work mode is asserted on every firmware. */
typedef enum { PLAYADMIT_LIST, PLAYADMIT_RESERVED_SLOT } playadmit_kind_t;

/* Which frames this admission sent. */
typedef enum { PLAYADMIT_NONE = 0, PLAYADMIT_WORKMODE = 1, PLAYADMIT_PREAMBLE = 2 } playadmit_sent_t;

/* Send what this play start needs before its play frame, best effort (the result is not a verdict: the play goes on).
 *   LIST:          route preamble (outseq_local_preamble) only on a firmware that needs the local init, Local source, no
 *                  Bluetooth route, not playing and no play still starting; 0666 under a stream would gap it.
 *   RESERVED_SLOT: work mode alone (outseq_assert_workmode) on Local source with no Bluetooth route; the route is
 *                  already right and a redundant 0666 would pause the stream.
 * A NULL situation sends nothing. */
playadmit_sent_t playadmit_start(playadmit_kind_t kind, const playadmit_situation_t *s);

#endif
