/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_TIMEKEEP_H
#define DISKOS_TIMEKEEP_H
#include <time.h>

#define TIMEKEEP_FLOOR 1767312000LL
#define TIMEKEEP_CEIL  4102444800LL
#define TIMEKEEP_TOL_S 120

time_t timekeep_load(const char *path);
int    timekeep_save(const char *path, time_t t);
time_t timekeep_pick(time_t now, time_t saved);
time_t timekeep_rtc_epoch(const char *dev);

#endif
