/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_MODES_H
#define DISKOS_MODES_H

/* Output route (stock: Settings > SPDIF, Working Mode > USB AUDIO). The SPDIF row and the USB Audio picker row exist only
 * in builds made with -DDISKOS_TEST_OUTPUTS (device-unverified); the code is always built and is a no-op equivalent of
 * the old preamble while the route is the internal DAC. The switching itself (modes_output_*, ui_set_source_mode) lives
 * in srcswitch.c / srcswitch.h, which this header re-exports for the files that include it for those names; modes.c is
 * only the Working Mode picker screen. */
enum { OUT_INTERNAL = 0, OUT_SPDIF = 1, OUT_USB = 2 };

#include "srcswitch.h"

/* main.c: NULL when an output switch may run now, else a short reason for the toast */
const char *ui_output_blocked(void);

#endif
