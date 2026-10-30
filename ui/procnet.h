/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_PROCNET_H
#define DISKOS_PROCNET_H

#include <stdio.h>

/* Readers of the kernel's /proc/net tables. No LVGL; host-tested with real table samples (tests/keyssh_test.c). */

/* Is something LISTENing on TCP `port` in this /proc/net/tcp or tcp6 table (f is open at its header line)? 1 = yes. */
int tcp_listen_in(FILE *f, int port);

#endif
