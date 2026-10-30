/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_SESSION_H
#define DISKOS_SESSION_H

#include <stdint.h>
#include "modes.h"   /* OUT_INTERNAL / OUT_SPDIF / OUT_USB */

/* Session module (GLOSSARY.md): what the Player is currently on, as the UI believes it (Output route + Source mode),
 * how this UI process came to be (cold boot, or a relaunch such as a theme change under a Player that kept running),
 * and what a startup restore may therefore still do. Before this module the answer was spread over a modes.c static (now srcswitch.c),
 * main.c flags and the DISKOS_RELOAD_* environment, and every startup timer re-derived it, so each bug got another guard.
 * No LVGL. Seams: ipc_send_cmd() (through outseq), ipc_generation() and cfg_*(); time is passed in, never read.
 * The Bluetooth sink route is here too (session_bt_mac); the SD/DAC state machines are in srcswitch.c and the rules for
 * when the UI may touch the Player are in admit.c. */

typedef enum { SESSION_COLD_BOOT = 0, SESSION_RELAUNCH = 1 } session_entry_t;

/* ---- what the Player is on ---- */
/* A route is tied to the Player generation it was set in: a restarted Player is back on the internal DAC, so a stale
 * route reads OUT_INTERNAL. Setting it mirrors the cfg "spdif" key (the Settings toggle shows the real route). */
int  session_route(void);
void session_set_route(int route);
int  session_source_mode(void);            /* best-effort, atomic: the coldplug thread reads it */
void session_set_source_mode(int mode);
/* The Bluetooth sink Local playback is routed to ("" = none, the analog route). Beside the Output route above because
 * the two together say where Local sound goes; admit.c reads it, main.c sets it when a route lands. */
const char *session_bt_mac(void);
int  session_bt_routed(void);
void session_set_bt_mac(const char *mac);   /* the normalised AA:BB:CC:DD:EE:FF of the sink */
void session_clear_bt_mac(void);

/* ---- how this process was entered ---- */
void            session_init(void);        /* once at startup: read and consume the relaunch handoff (the DISKOS_* env) */
session_entry_t session_entry(void);
/* A relaunch whose Player was off the default (internal DAC, Local) takes that state over for `gen`, the current Player
 * generation. 1 = adopted; the caller then refreshes whatever shows the route. */
int  session_adopt(unsigned gen);
/* A cold-boot-entry process (no handoff: a hot-load, a crash restart) may find the Player already on the saved Source
 * mode. `detected` is the Source mode read from the real USB gadget state (ui_detect_source_mode), `gen` the current
 * Player generation. Only USB DAC is gadget-visible, so only USB DAC is adopted (BT Receiving cannot be told from Local).
 * 1 = adopted (source mode set, no Local init for `gen`, the startup restore stands down); 0 = not: a real cold boot, where
 * the Player has not switched yet, reads Local and the restore must still run. The caller then refreshes whatever shows it. */
int  session_adopt_detected(unsigned gen, int detected);
/* Before exec of the new image: export route and source mode. cancel() undoes it when the exec failed. */
void session_handoff_export(void);
void session_handoff_cancel(void);

/* ---- what may a startup restore still do ---- */
/* The two startup actions that depend only on the entry (a relaunch is not a boot) are decided by their one caller each
 * in main.c, from session_entry(): the Resume Playback autoplay pause and the charging screen. */

/* Saved Working Mode (cfg src_mode: 0 Local, 1 USB DAC, 2 BT Receiving; usb_audio_on: the output route was USB Audio). */
int  session_saved_source(void);
int  session_saved_usb_audio(void);
void session_save_source(int mode);        /* a user pick (also the one that leaves USB Storage): ends a remembered USB Audio output; 3 (Storage) is ignored */
void session_save_usb_audio(int on);

/* Boot restore of the saved Working Mode, once the Player has settled (~15 s). poll() returns WAIT while it is too early
 * or the caller reports `busy` / `blocked` (an output switch is running / may not start), NONE when there is nothing
 * (or nothing left) to restore, else SOURCE (with *src) or USB_AUDIO: each is returned once, then the restore is spent. */
typedef enum { SESSION_RESTORE_WAIT, SESSION_RESTORE_NONE, SESSION_RESTORE_SOURCE, SESSION_RESTORE_USB_AUDIO } session_restore_t;
void              session_restore_arm(uint32_t now_ms);
session_restore_t session_restore_poll(uint32_t now_ms, int busy, int blocked, int *src);

/* ---- the startup Local init: when does it go out ---- */
/* The Local init (outseq_local_boot: 0642/0666/0657) runs once per Player generation, and never for the generation a
 * relaunch adopted (it would pull the Player off a USB DAC). Whether it may go out NOW is decided here, together with the
 * Bluetooth sink recovery that must precede it (a sink left by a previous UI is looked at once per process). main.c's
 * 500 ms timer passes session_startup_tick() the time and acts on the answer; the facts and the slow system probes come
 * through struct session_startup_env, so a host test drives the whole decision with fakes. */
struct session_startup_env {
    /* facts, read when the decision needs them and not earlier: the order of the reads is part of the behaviour */
    int      (*admitted)(void);          /* admit_check(ADMIT_LOCAL_INIT) == NULL: card access allowed, Local source, no output switch */
    unsigned (*rx_frames)(void);         /* ipc_rx_frames(): CUMULATIVE across Player generations */
    int      (*play_starting)(void);     /* a play was sent and its track has not arrived yet, or an audiobook is active */
    int      (*playing)(void);           /* playstate's inference: position advancing */
    int      (*have_track)(void);        /* the Player has reported a loaded track (paused or not) */
    int      (*fw_needs_init)(void);     /* this firmware's Player needs the Local init (V2.40/V2.57) */
    /* slow system probes, and the one route change the recovery makes (main.c adapters: shell, /proc, an Output sequence) */
    int      (*player_old)(void);        /* the live Player has been up long enough that an old idle one need not answer first */
    int      (*saved_bt_route)(void);    /* 1 the Player's saved output is a Bluetooth sink, 0 it is not, -1 unknown */
    int      (*connected_sink)(const char *mac);   /* 1 an A2DP sink is connected (the auto-route poll owns it), 0 none, -1 inconclusive */
    int      (*route_analog)(void);      /* leave the Bluetooth sink for the analog DAC: 0 done, < 0 refused or failed */
};
void session_startup_set_env(const struct session_startup_env *env);   /* once at startup, before the first tick */

/* A play/pause/next/prev went to the Player, or playback was observed, in Player generation `gen` at `now_ms`: that
 * generation's Local init is moot (the play path sends its own preamble) and the startup recovery waits for it to settle. */
void session_note_transport_sent(unsigned gen, uint32_t now_ms);

/* One pass of the startup timer. WAIT: nothing to do yet, tick again. PROBE: the caller sends a silent 02020008 (an idle
 * Player emits nothing until asked). DONE: the Local init is not needed on this firmware, or the recovery has handled the
 * generation: stop ticking. The Local init frames and the recovery's route change go out from inside, through outseq;
 * a refused frame is not marked done and the next tick retries it. */
typedef enum { SESSION_STARTUP_WAIT, SESSION_STARTUP_PROBE, SESSION_STARTUP_DONE } session_startup_t;
session_startup_t session_startup_tick(uint32_t now_ms);

/* An intentional Bluetooth route supersedes the startup recovery (ui_route_bt). */
void session_startup_route_settled(void);

#endif
