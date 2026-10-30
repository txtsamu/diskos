/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_BOOTSEL_H
#define DISKOS_BOOTSEL_H

/* The boot-selection record: S96diskos_select writes exactly "diskos\n" or "stock\n" to /tmp/.diskos_boot_select. This is
 * the C reader and the enforcement of it; payload/diskos-selected (shell) reads the same record by the same rule, and
 * tests/boot_select_test.py compares the two. No LVGL. */

/* 1 only for a regular, non-symlink file holding exactly the 7 bytes "diskos\n". Missing, unreadable, FIFO/device, wrong
 * size, NUL-containing or any other content is 0 = stock. Non-blocking open, so a FIFO planted at the path cannot stall. */
int boot_select_is_diskos(const char *path);

/* Returns only for a positive diskOS record. Anything else hands this process to the stock UI (handoff_exec), and that is
 * TERMINAL: if the stock exec fails it exits 127 rather than run diskOS against the boot's decision. */
void boot_select_enforce(const char *path);

#endif
