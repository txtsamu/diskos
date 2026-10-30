/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_OUTSEQ_H
#define DISKOS_OUTSEQ_H

typedef enum { OUTSEQ_INTERNAL = 0, OUTSEQ_SPDIF = 1, OUTSEQ_USB = 2 } outseq_route_t;

typedef enum { OUTSEQ_OK = 0, OUTSEQ_REFUSED = -1, OUTSEQ_MOVED = -2 } outseq_rc;

typedef enum { OUTSEQ_SRC_LOCAL = 0, OUTSEQ_SRC_USB_DAC = 1, OUTSEQ_SRC_BT_RECEIVING = 2, OUTSEQ_SRC_USB_STORAGE = 3 } outseq_src_t;

typedef enum { OUTSEQ_FULL, OUTSEQ_QUICK } outseq_form_t;

outseq_rc outseq_route(outseq_route_t target, outseq_form_t form);
outseq_rc outseq_local_boot(outseq_route_t current);
void      outseq_local_preamble(outseq_route_t current);

outseq_rc outseq_source(outseq_src_t mode);

outseq_rc outseq_analog(void);

outseq_rc outseq_bt_sink(const char *codec_frame);

void      outseq_assert_workmode(void);

outseq_rc outseq_gadget_none(void);

#endif
