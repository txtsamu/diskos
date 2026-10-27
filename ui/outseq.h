/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_OUTSEQ_H
#define DISKOS_OUTSEQ_H

/* Output sequence module (GLOSSARY.md): the ordered Player commands that move the Player to an output route and leave
 * it with a valid Player work mode. The frame order, the settle times and which failures stop the sequence live here
 * and nowhere else. No LVGL; the only seam is ipc_send_cmd() (host tests link a recording fake of it).
 * Stateless: the route the UI believes it is on stays in modes.c; a caller reads the result and decides. */

typedef enum { OUTSEQ_INTERNAL = 0, OUTSEQ_SPDIF = 1, OUTSEQ_USB = 2 } outseq_route_t;   /* == OUT_* in modes.h */

/* OUTSEQ_REFUSED: the caller keeps its recorded state (the first frame that matters was refused, or a best-effort
 *                 sequence had a send fail). It does not mean nothing was sent.
 * OUTSEQ_MOVED:   the route moved but a later frame failed: the Player may be on a route the UI does not know. */
typedef enum { OUTSEQ_OK = 0, OUTSEQ_REFUSED = -1, OUTSEQ_MOVED = -2 } outseq_rc;   /* rc < 0 is still a failure */

/* Source modes, numbered as g_source_mode in main.c. */
typedef enum { OUTSEQ_SRC_LOCAL = 0, OUTSEQ_SRC_USB_DAC = 1, OUTSEQ_SRC_BT_RECEIVING = 2, OUTSEQ_SRC_USB_STORAGE = 3 } outseq_src_t;

/* QUICK is the Settings SPDIF toggle: route frame + work mode, no settle, no gadget frame. */
typedef enum { OUTSEQ_FULL, OUTSEQ_QUICK } outseq_form_t;

/* Move the Player to a route (also the "restore Internal" recovery call). The caller pauses first and resumes after. */
outseq_rc outseq_route(outseq_route_t target, outseq_form_t form);
/* Boot-time Local init: the first send failing stops it (the boot timer retries the whole init next tick). */
outseq_rc outseq_local_boot(outseq_route_t current);
/* Play preamble: best effort, every frame is attempted whatever the one before returned. */
void      outseq_local_preamble(outseq_route_t current);

/* Move the Player to a source mode, always starting from the internal DAC route (0666 6). The route moves with the
 * first frame, so the caller resets its recorded route whenever the result is not OUTSEQ_REFUSED. An invalid mode
 * sends nothing. */
outseq_rc outseq_source(outseq_src_t mode);

/* Leave a Bluetooth sink for the analog DAC: 0666 6 then 0657 8, both must land. There is no route-moved step, so any
 * failure is OUTSEQ_REFUSED and the caller keeps its recorded Bluetooth route (a retry re-runs it). */
outseq_rc outseq_analog(void);

/* Route Local playback to a Bluetooth sink (the caller pauses first and builds the 06b3 codec frame, with the MAC,
 * before calling, so nothing can fail on it after the route has moved). Only the two route frames and the codec frame
 * must land; the others are sent best effort. The route moves with the 0666 2 frame. A NULL or empty frame sends
 * nothing. The caller resets its recorded route when the result is not OUTSEQ_REFUSED. */
outseq_rc outseq_bt_sink(const char *codec_frame);

/* Re-assert the Player work mode LOCALPLAYER (0657 8) alone, best effort: a play from an idle Player needs it, and a
 * route that is already right must not be re-sent (a redundant 0666 pauses the stream). The result is not observable. */
void      outseq_assert_workmode(void);

/* Drop the USB gadget role alone (0642 0). The USB DAC ordered exit sends it first, while a host is still attached, and
 * waits for the gadget to disappear before the rest of the switch. Must land: REFUSED when it did not. */
outseq_rc outseq_gadget_none(void);

#endif
