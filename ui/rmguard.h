/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_RMGUARD_H
#define DISKOS_RMGUARD_H

#include <stddef.h>

/* The SD delete guard: a wrapper script that refuses any `rm` aimed at the card mountpoint, installed first in PATH so the
 * stock player's "umount /tmp/sdcard; rm -rf /tmp/sdcard" cannot empty a still-mounted card. This module verifies a guard
 * exists (rootfs copy, system copy, or a NAND fallback it publishes itself) and puts its directory first in PATH.
 * No LVGL. The three directories are macros (RMGUARD_ROOTFS, RMGUARD_SYSTEM, RMGUARD_FALLBACK, RMGUARD_FALLBACK_PARENT)
 * a host test overrides with temp paths. payload/diskos-rmguard is the same script; tests/rmguard_test.py compares them. */

/* Ensure an executable guard exists and PATH starts with its directory. Returns 1 if guarded, 0 if NO guard could be
 * established (the caller decides the fail policy). Idempotent; safe from the UI and from the launcher. Only mutates PATH
 * of THIS process (and, for the launcher, the exec'd player). */
int rmguard_ensure(void);

/* 1 if dir/rm is exactly the guard script: a regular, non-symlink, executable file with the complete content. */
int rmguard_dir_ok(const char *dir);

/* The guard script's bytes; *len is their count (without the terminating NUL). */
const char *rmguard_script(size_t *len);

#endif
