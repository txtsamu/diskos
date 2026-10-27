/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_TIMEKEEP_H
#define DISKOS_TIMEKEEP_H
#include <time.h>

/* Persistent clock. The Disc's RTC can come up wrong (it lost power, or the boot init set a default), and a wrong
 * clock was then written back to it by the periodic `hwclock -w`. The UI keeps its own last-known-good time in a small
 * file and, at startup, restores it when the clock is clearly behind it. No LVGL; the glue (timers, boot) is in main.c. */

#define TIMEKEEP_FLOOR 1767312000LL   /* 2026-01-02 00:00:00 UTC: earlier is "unset" (the boot init defaults to 2026-01-01) */
#define TIMEKEEP_CEIL  4102444800LL   /* 2100-01-01: later is garbage */
#define TIMEKEEP_TOL_S 120            /* the clock may be this far behind the saved time before it counts as wrong */

time_t timekeep_load(const char *path);            /* the saved epoch, or 0 when missing, malformed or out of range */
int    timekeep_save(const char *path, time_t t);  /* atomic (temp file + fsync + rename); 0 = saved, -1 = refused or failed */
time_t timekeep_pick(time_t now, time_t saved);    /* the time to restore, or 0 to keep the current clock */
time_t timekeep_rtc_epoch(const char *dev);        /* the RTC's time read as local time, as an epoch; 0 when unreadable */

#endif
