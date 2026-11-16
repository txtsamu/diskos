/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <stdio.h>
#include "../shuffle.h"

static int fails;

static void check(int got, int want, const char *what){
    if(got != want){
        printf("  FAIL: %s: got %d want %d\n", what, got, want);
        fails++;
    }
}

int main(void){
    check(shuffle_start(0, 1, 814, 814, 25), 1, "sequential starts at 1");
    check(shuffle_start(1, 2, 814, 814, 25), 1, "artist plan keeps its own position");
    check(shuffle_start(1, 1, 0, 814, 25), 1, "empty shown list starts at 1");
    check(shuffle_start(1, 1, 1, 814, 25), 1, "single shown row starts at 1");
    check(shuffle_start(1, 1, 814, 0, 25), 1, "unknown player list starts at 1");
    check(shuffle_start(1, 1, 814, -3, 25), 1, "negative player count starts at 1");
    check(shuffle_start(1, 1, 10, 5, 0), 1, "clamped range first");
    check(shuffle_start(1, 1, 10, 5, 4), 5, "clamped range last");
    check(shuffle_start(1, 1, 10, 5, 5), 1, "clamped range wraps");
    check(shuffle_start(1, 1, 5, 10, 4), 5, "shown range last");
    check(shuffle_start(1, 1, 5, 10, 5), 1, "shown range wraps");
    check(shuffle_start(1, 1, 814, 814, 25), 26, "full range keeps the offset");
    check(shuffle_start(1, 6, 10, 7, 6), 7, "favourites clamp");
    int huge = shuffle_start(1, 1, 5, 10, 0xFFFFFFFFu);
    if(huge < 1 || huge > 5){ printf("  FAIL: huge random out of range: %d\n", huge); fails++; }
    printf("%s (%d failures)\n", fails ? "FAIL" : "PASS shuffle", fails);
    return fails ? 1 : 0;
}
