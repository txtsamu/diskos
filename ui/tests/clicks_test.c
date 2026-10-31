/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <stdio.h>
#include <stdint.h>
#include "clicks.h"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)

static void test_single(void){
    clicks_t c; clicks_reset(&c);
    CHECK(clicks_press(&c, 1000) == 0, "single: press does not resolve");
    CHECK(clicks_poll(&c, 1000) == 0, "single: not resolved at 0 ms");
    CHECK(clicks_poll(&c, 1000 + CLICKS_WINDOW_MS - 1) == 0, "single: not resolved at 349 ms");
    CHECK(clicks_poll(&c, 1000 + CLICKS_WINDOW_MS) == 1, "single: resolved as 1 at exactly 350 ms");
    CHECK(clicks_poll(&c, 1000 + CLICKS_WINDOW_MS + 1) == 0, "single: nothing pending after resolve");
}

static void test_double(void){
    clicks_t c; clicks_reset(&c);
    CHECK(clicks_press(&c, 5000) == 0, "double: first press pending");
    CHECK(clicks_press(&c, 5200) == 0, "double: second press pending");
    CHECK(clicks_poll(&c, 5200 + CLICKS_WINDOW_MS - 1) == 0, "double: not resolved before window");
    CHECK(clicks_poll(&c, 5200 + CLICKS_WINDOW_MS) == 2, "double: resolved as 2");
}

static void test_window_from_last(void){
    clicks_t c; clicks_reset(&c);
    clicks_press(&c, 0);
    CHECK(clicks_press(&c, 349) == 0, "extend: second click at 349 ms pending");
    CHECK(clicks_poll(&c, 350) == 0, "extend: not resolved 350 ms after first click");
    CHECK(clicks_poll(&c, 349 + CLICKS_WINDOW_MS - 1) == 0, "extend: not resolved 349 ms after second click");
    CHECK(clicks_poll(&c, 349 + CLICKS_WINDOW_MS) == 2, "extend: resolved as 2 at 350 ms after second click");
}

static void test_triple(void){
    clicks_t c; clicks_reset(&c);
    CHECK(clicks_press(&c, 100) == 0, "triple: first pending");
    CHECK(clicks_press(&c, 200) == 0, "triple: second pending");
    CHECK(clicks_press(&c, 300) == CLICKS_MAX, "triple: third resolves immediately as 3");
    CHECK(c.count == 0, "triple: nothing pending in state");
    CHECK(clicks_poll(&c, 300) == 0, "triple: poll right after returns 0");
    CHECK(clicks_poll(&c, 300 + CLICKS_WINDOW_MS) == 0, "triple: poll after window returns 0");
}

static void test_fourth_starts_new(void){
    clicks_t c; clicks_reset(&c);
    clicks_press(&c, 100);
    clicks_press(&c, 200);
    clicks_press(&c, 300);
    CHECK(clicks_press(&c, 400) == 0, "fourth: starts a new sequence");
    CHECK(clicks_poll(&c, 400 + CLICKS_WINDOW_MS - 1) == 0, "fourth: not resolved early");
    CHECK(clicks_poll(&c, 400 + CLICKS_WINDOW_MS) == 1, "fourth: resolves as 1");
}

static void test_poll_idle(void){
    clicks_t c; clicks_reset(&c);
    CHECK(clicks_poll(&c, 0) == 0, "idle: poll with no clicks returns 0");
    CHECK(clicks_poll(&c, 100000) == 0, "idle: poll much later returns 0");
}

static void test_poll_after_resolve(void){
    clicks_t c; clicks_reset(&c);
    clicks_press(&c, 10);
    CHECK(clicks_poll(&c, 10 + CLICKS_WINDOW_MS) == 1, "resolve: first poll returns 1");
    CHECK(clicks_poll(&c, 10 + CLICKS_WINDOW_MS) == 0, "resolve: second poll returns 0");
    CHECK(clicks_poll(&c, 10 + 2 * CLICKS_WINDOW_MS) == 0, "resolve: later poll returns 0");
}

static void test_reset(void){
    clicks_t c; clicks_reset(&c);
    clicks_press(&c, 10);
    clicks_press(&c, 20);
    clicks_reset(&c);
    CHECK(c.count == 0 && c.last_ms == 0, "reset: state zeroed");
    CHECK(clicks_poll(&c, 20 + CLICKS_WINDOW_MS) == 0, "reset: pending clicks dropped");
    CHECK(clicks_press(&c, 1000) == 0, "reset: new press starts at 1");
    CHECK(clicks_poll(&c, 1000 + CLICKS_WINDOW_MS) == 1, "reset: new sequence resolves as 1");
}

static void test_wraparound(void){
    clicks_t c; clicks_reset(&c);
    clicks_press(&c, 0xFFFFFF00u);
    CHECK(clicks_poll(&c, 0xFFFFFF00u + 349u) == 0, "wrap: not resolved at 349 ms across wrap");
    CHECK(clicks_poll(&c, 0x00000060u) == 1, "wrap: resolved 352 ms later across wrap");

    clicks_reset(&c);
    clicks_press(&c, 0xFFFFFFF0u);
    CHECK(clicks_press(&c, 0x00000050u) == 0, "wrap: second click across wrap pending");
    CHECK(clicks_poll(&c, 0x00000050u + CLICKS_WINDOW_MS - 1) == 0, "wrap: window measured from last click across wrap");
    CHECK(clicks_poll(&c, 0x00000050u + CLICKS_WINDOW_MS) == 2, "wrap: resolved as 2 across wrap");
}

static void test_null(void){
    clicks_reset(NULL);
    CHECK(clicks_press(NULL, 0) == 0, "null: press returns 0");
    CHECK(clicks_poll(NULL, 0) == 0, "null: poll returns 0");
}

int main(void){
    test_single();
    test_double();
    test_window_from_last();
    test_triple();
    test_fourth_starts_new();
    test_poll_idle();
    test_poll_after_resolve();
    test_reset();
    test_wraparound();
    test_null();
    printf("%s (%d failure%s)\n", fails ? "FAIL" : "PASS", fails, fails==1?"":"s");
    return fails ? 1 : 0;
}
