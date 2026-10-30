/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "timekeep.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define T0 1793000000LL

static char dir[] = "/tmp/timekeep_test_XXXXXX";
static char path[128], tmp[136];

static void put(const char *s){ FILE *f = fopen(path, "w"); assert(f); fputs(s, f); fclose(f); }

int main(void){
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/time", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    assert(timekeep_load(path) == 0);
    assert(timekeep_save(path, T0) == 0);
    assert(timekeep_load(path) == T0);
    assert(access(tmp, F_OK) != 0);
    assert(timekeep_save(path, T0 + 600) == 0 && timekeep_load(path) == T0 + 600);
    assert(timekeep_save(path, 0) == -1);
    assert(timekeep_save(path, 86400) == -1);
    assert(timekeep_save(path, TIMEKEEP_FLOOR - 1) == -1);
    assert(timekeep_save(path, TIMEKEEP_CEIL) == -1);
    assert(timekeep_load(path) == T0 + 600);
    assert(timekeep_save(path, TIMEKEEP_FLOOR) == 0);
    assert(timekeep_save("/nonexistent-dir/time", T0) == -1);
    put("");                       assert(timekeep_load(path) == 0);
    put("garbage\n");              assert(timekeep_load(path) == 0);
    put("1793000000abc\n");        assert(timekeep_load(path) == 0);
    put("-5\n");                   assert(timekeep_load(path) == 0);
    put("99999999999999999999\n"); assert(timekeep_load(path) == 0);
    put("100\n");                  assert(timekeep_load(path) == 0);
    put("1793000000");             assert(timekeep_load(path) == T0);

    assert(timekeep_pick(T0, T0) == 0);
    assert(timekeep_pick(T0 + 5000, T0) == 0);
    assert(timekeep_pick(T0 - 60, T0) == 0);
    assert(timekeep_pick(T0 - TIMEKEEP_TOL_S, T0) == 0);
    assert(timekeep_pick(T0 - TIMEKEEP_TOL_S - 1, T0) == T0);
    assert(timekeep_pick(1767225600LL, T0) == T0);
    assert(timekeep_pick(0, T0) == T0);
    assert(timekeep_pick(T0 - 7 * 3600, T0) == T0);
    assert(timekeep_pick(T0, 0) == 0);
    assert(timekeep_pick(0, 5) == 0);

    assert(timekeep_rtc_epoch("/nonexistent-rtc") == 0);

    unlink(path); unlink(tmp); rmdir(dir);
    printf("timekeep_test: ok\n");
    return 0;
}
