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

/* QUICK is the Settings SPDIF toggle: route frame + work mode, no settle, no gadget frame. */
typedef enum { OUTSEQ_FULL, OUTSEQ_QUICK } outseq_form_t;

/* Move the Player to a route (also the "restore Internal" recovery call). The caller pauses first and resumes after. */
outseq_rc outseq_route(outseq_route_t target, outseq_form_t form);
/* Boot-time Local init: the first send failing stops it (the boot timer retries the whole init next tick). */
outseq_rc outseq_local_boot(outseq_route_t current);
/* Play preamble: best effort, every frame is attempted whatever the one before returned. */
void      outseq_local_preamble(outseq_route_t current);

#endif
