/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_NOW_H
#define DISKOS_NOW_H
#include <stdint.h>
static inline uint32_t now_nonzero(uint32_t t){ return t ? t : 1; }
#endif
