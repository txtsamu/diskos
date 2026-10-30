/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_TRANSPORT_H
#define DISKOS_TRANSPORT_H

#include <stdint.h>

/* Transport commands and their feedback, shared by Now Playing, the drawer, the quick settings and the hardware keys:
 * play/pause and next/prev (a book skips 30 s / 15 s instead of changing track), the optimistic play/pause icon, and the
 * library rescan request with its honest admission and feedback. No LVGL: everything else comes from the host process
 * through the prototypes below (main.c, ui.c, toast.c, admit.c, ipc.c, scanner.c, sdio.c, musicdb.c), which a host test
 * links fakes of. */

/* Send a transport command (PLAYER_CMD_PLAYPAUSE or a next/prev frame). 0 = sent, -1 = refused or the send failed
 * (the user is told); on failure nothing optimistic is shown. */
int  ui_transport_command(const char *cmd);

/* Optimistic play/pause icon. Play state is INFERRED from position advance with a grace period, so the glyph would lag ~1 s
 * when pausing; on a toggle tap we predict the new state, and the real inference reconciles a beat later. Main thread only. */
void ui_pp_tap_hint(void);                   /* a toggle tap: predict the opposite of the current state */
int  ui_pp_icon_playing(int real_playing);   /* the glyph state: the prediction while it is held, else real */

/* Start a library scan, or cancel the one running. Refuses with a toast when not on Local playback or the card is not ready. */
void ui_rescan_library(void);

/* ---- the host's side (not defined by transport.c) ---- */
uint32_t lv_tick_get(void);                  /* LVGL's millisecond tick (wraps) */
void ui_toast(const char *msg);
int  ui_is_playing(void);                    /* main.c: the normalised play state */
void ui_defer_sleep(void);                   /* hold the sleep pause off briefly after a tap */
void ui_note_transport_sent(void);           /* this ipc generation needs no local-init one-shot */
void ui_book_user_seeked(long target_ms);
void ui_cancel_book_resume(void);
void ui_disarm_book_eoc(void);
void ui_np_seek_hold(long target_ms);        /* ui.c: ignore stale position echoes around a seek to target_ms for ~2.5 s */
void ui_np_pp_refresh(int playing);          /* ui.c: set the Now Playing transport glyph */

#endif
