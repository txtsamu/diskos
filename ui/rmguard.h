/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_RMGUARD_H
#define DISKOS_RMGUARD_H

#include <stddef.h>

int rmguard_ensure(void);

int rmguard_dir_ok(const char *dir);

const char *rmguard_script(size_t *len);

#endif
