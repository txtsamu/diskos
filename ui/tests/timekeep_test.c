/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Host test for timekeep.c: save/load round trip, rejects, and the restore rule. */
#include "timekeep.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define T0 1793000000LL   /* 2026-10-26: a sane "now" */

static char dir[] = "/tmp/timekeep_test_XXXXXX";
static char path[128], tmp[136];

static void put(const char *s){ FILE *f = fopen(path, "w"); assert(f); fputs(s, f); fclose(f); }

int main(void){
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/time", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    /* missing file */
    assert(timekeep_load(path) == 0);
    /* round trip, atomic: no temp file left behind */
    assert(timekeep_save(path, T0) == 0);
    assert(timekeep_load(path) == T0);
    assert(access(tmp, F_OK) != 0);
    assert(timekeep_save(path, T0 + 600) == 0 && timekeep_load(path) == T0 + 600);
    /* an unset or garbage clock is never persisted, and does not clobber the good value */
    assert(timekeep_save(path, 0) == -1);
    assert(timekeep_save(path, 86400) == -1);                       /* 1970 */
    assert(timekeep_save(path, TIMEKEEP_FLOOR - 1) == -1);          /* just under the floor (the boot default 2026-01-01) */
    assert(timekeep_save(path, TIMEKEEP_CEIL) == -1);
    assert(timekeep_load(path) == T0 + 600);
    assert(timekeep_save(path, TIMEKEEP_FLOOR) == 0);               /* the floor itself is valid */
    /* a save into a missing directory fails cleanly */
    assert(timekeep_save("/nonexistent-dir/time", T0) == -1);
    /* malformed files read as unset */
    put("");                       assert(timekeep_load(path) == 0);
    put("garbage\n");              assert(timekeep_load(path) == 0);
    put("1793000000abc\n");        assert(timekeep_load(path) == 0);
    put("-5\n");                   assert(timekeep_load(path) == 0);
    put("99999999999999999999\n"); assert(timekeep_load(path) == 0);
    put("100\n");                  assert(timekeep_load(path) == 0);   /* below the floor */
    put("1793000000");             assert(timekeep_load(path) == T0);  /* no newline is fine */

    /* the restore rule */
    assert(timekeep_pick(T0, T0) == 0);                               /* equal: keep */
    assert(timekeep_pick(T0 + 5000, T0) == 0);                        /* clock ahead of the saved time: keep */
    assert(timekeep_pick(T0 - 60, T0) == 0);                          /* within the tolerance: keep */
    assert(timekeep_pick(T0 - TIMEKEEP_TOL_S, T0) == 0);              /* exactly at the tolerance: keep */
    assert(timekeep_pick(T0 - TIMEKEEP_TOL_S - 1, T0) == T0);         /* just beyond: restore */
    assert(timekeep_pick(1767225600LL, T0) == T0);                    /* the boot default 2026-01-01 00:00: restore */
    assert(timekeep_pick(0, T0) == T0);                               /* a zeroed/1970 clock: restore */
    assert(timekeep_pick(T0 - 7 * 3600, T0) == T0);                   /* the 7 h local/UTC mix-up (behind): restore */
    assert(timekeep_pick(T0, 0) == 0);                                /* nothing saved: keep */
    assert(timekeep_pick(0, 5) == 0);                                 /* a saved value out of range is ignored */

    /* an unreadable RTC device reads as 0 */
    assert(timekeep_rtc_epoch("/nonexistent-rtc") == 0);

    unlink(path); unlink(tmp); rmdir(dir);
    printf("timekeep_test: ok\n");
    return 0;
}
