/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_BOOTSEL_H
#define DISKOS_BOOTSEL_H

int boot_select_is_diskos(const char *path);

void boot_select_enforce(const char *path);

#endif
