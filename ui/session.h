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
/* a relaunch is not a boot: what is playing must keep playing. Nor is it the Player's own resume autoplay when Resume
 * Playback was explicitly set Off (cfg memory_play 0, which the UI sends to the Player at startup): then the Player starts
 * nothing, and anything playing in the first minute (a Bluetooth headset's play, a hardware key) is the user's and must
 * not be paused. Pass cfg_get_int("memory_play", -1): UNSET (-1) is NOT Off. The UI never sent 0684 then, so the Player keeps
 * its own Resume Playback setting, which autoplays at boot on the development Disc, and the pause must stay. */
int  session_autoplay_pause_allowed(int resume_playback);
/* The charging screen belongs to the power-on a charger triggers. A relaunch is the user mid-session (a theme change):
 * showing it there, with a charger plugged in, would power the Disc off if the charger is then pulled. A cold boot
 * cannot tell a power-on from a crash restart or a hot-load, which also carry no handoff: those keep the screen. */
int  session_chargemode_boot_allowed(void);

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

/* Startup route recovery (Bluetooth sink left by a previous UI): looked at once per process. */
int  session_startup_route_pending(void);
void session_startup_route_settled(void);

/* The Local init (outseq_local_boot) runs once per Player generation, and never for the generation a relaunch adopted
 * (its 0642/0666/0657 would pull the Player off a USB DAC). allowed() is false for the adopted generation; done()/mark()
 * track the one-shot; send() sends it on the current route and marks it. send(): 0 sent, 1 nothing to do, -1 a frame was
 * refused (not marked: the caller retries on its next tick). */
int  session_local_init_allowed(unsigned gen);
int  session_local_init_done(unsigned gen);
void session_local_init_mark(unsigned gen);
int  session_local_init_send(unsigned gen);

#endif
