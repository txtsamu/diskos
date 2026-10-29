/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Host test for radio.c: the bring-up watch (poll until up or timeout), the single off teardown, and the rollback of a
 * failed bring-up. The adapter is faked: every call it receives is appended to a log, the radio probe is a knob, and
 * time is just the number handed to the module. Two radios are built the way the real adapters differ: a Wi-Fi-like
 * one (reports a timeout, keeps its intent) and a Bluetooth-like one (a timeout clears the intent and powers down). */
#include "radio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- fakes ---- */
static char g_log[256];
static void L(const char *s){ assert(strlen(g_log) + strlen(s) + 2 < sizeof g_log); strcat(g_log, s); strcat(g_log, " "); }
static void clear(void){ g_log[0] = 0; }
static int  g_up, g_intent = -1, g_armed, g_arms, g_disarms, g_power_downs, g_cleanups;
static unsigned g_arm_ms;

static int  fake_up(void){ return g_up; }
static void fake_arm(unsigned ms){ assert(!g_armed); g_armed = 1; g_arms++; g_arm_ms = ms; L("arm"); }
static void fake_disarm(void){ assert(g_armed); g_armed = 0; g_disarms++; L("disarm"); }
static void fake_intent(int on){ g_intent = on; L(on ? "intent1" : "intent0"); }
static void fake_start(void){ L("start"); }
static void fake_cleanup(void){ g_cleanups++; L("cleanup"); }
static void fake_power_down(void){ g_power_downs++; L("down"); }

static struct radio_watch wifi_w = { 700, 8000, fake_up, fake_arm, fake_disarm, 0, 0 };
static struct radio wifi = { &wifi_w, fake_intent, fake_start, fake_cleanup, fake_power_down, 0 };
static struct radio_watch bt_w = { 1000, 28000, fake_up, fake_arm, fake_disarm, 0, 0 };
static struct radio bt = { &bt_w, fake_intent, fake_start, fake_cleanup, fake_power_down, 1 };

static void reset(void){
    clear(); g_up = 0; g_intent = -1; g_armed = 0; g_arms = g_disarms = g_power_downs = g_cleanups = 0; g_arm_ms = 0;
    wifi_w.active = bt_w.active = 0;
}
static void expect(const char *what, const char *want){
    if(strcmp(g_log, want)){ fprintf(stderr, "%s: log \"%s\", want \"%s\"\n", what, g_log, want); assert(0); }
}
/* no timer is left behind: every arm was matched by one disarm */
static void no_leak(void){ assert(!g_armed && g_arms == g_disarms && !wifi_w.active && !bt_w.active); }

static void check_watch(void){
    /* up on the first tick: verdict UP, disarmed before the caller acts, no timeout */
    reset();
    radio_watch_begin(&wifi_w, 1000);
    assert(g_armed && g_arm_ms == 700);                      /* the interval is the watch's own */
    g_up = 1;
    assert(radio_watch_tick(&wifi_w, 1001) == RADIO_UP);
    assert(!g_armed); expect("up", "arm disarm "); no_leak();
    /* a tick after the verdict is inert, not a second verdict */
    assert(radio_watch_tick(&wifi_w, 1002) == RADIO_IDLE && g_disarms == 1);

    /* pending, then the boundary: exactly timeout_ms is still pending, one more is the timeout */
    reset();
    radio_watch_begin(&wifi_w, 5000);
    assert(radio_watch_tick(&wifi_w, 5700) == RADIO_PENDING);
    assert(radio_watch_tick(&wifi_w, 5000 + 8000) == RADIO_PENDING && g_armed);
    assert(radio_watch_tick(&wifi_w, 5000 + 8001) == RADIO_TIMEOUT);
    no_leak();
    assert(radio_watch_tick(&wifi_w, 5000 + 9000) == RADIO_IDLE);

    /* coming up on the very tick that would have timed out is a success */
    reset();
    radio_watch_begin(&bt_w, 0);
    g_up = 1;
    assert(radio_watch_tick(&bt_w, 28001) == RADIO_UP); no_leak();

    /* the 32-bit clock wraps: elapsed time is still right across it */
    reset();
    radio_watch_begin(&bt_w, 0xFFFFFF00u);
    assert(radio_watch_tick(&bt_w, 0x00000100u) == RADIO_PENDING);              /* 512 ms later */
    assert(radio_watch_tick(&bt_w, 0xFFFFFF00u + 28001u) == RADIO_TIMEOUT); no_leak();

    /* begin again replaces the running watch: one disarm between, a fresh start time, still balanced */
    reset();
    radio_watch_begin(&bt_w, 0);
    radio_watch_begin(&bt_w, 20000);
    expect("replace", "arm disarm arm ");
    assert(radio_watch_tick(&bt_w, 20000 + 28000) == RADIO_PENDING);            /* counted from the second begin */
    assert(radio_watch_tick(&bt_w, 20000 + 28001) == RADIO_TIMEOUT); no_leak();

    /* cancel: no verdict, idempotent, and the tick that follows is inert */
    reset();
    radio_watch_cancel(&bt_w); assert(g_disarms == 0);                          /* idle: nothing to disarm */
    radio_watch_begin(&bt_w, 0);
    radio_watch_cancel(&bt_w); radio_watch_cancel(&bt_w);
    assert(g_disarms == 1); no_leak();
    g_up = 1; assert(radio_watch_tick(&bt_w, 10) == RADIO_IDLE);
}

static void check_on(void){
    /* intent is persisted before the radio is started, and the watch starts after both */
    reset();
    radio_on(&bt, 100, 1);
    expect("on observed", "intent1 start arm ");
    assert(g_intent == 1 && g_armed && bt_w.start == 100);
    /* without a screen to report to (the Quick Settings tile for Wi-Fi) nothing is watched */
    reset();
    radio_on(&wifi, 100, 0);
    expect("on unobserved", "intent1 start ");
    assert(g_arms == 0 && !wifi_w.active);
    /* turning on twice does not double the timer */
    reset();
    radio_on(&bt, 0, 1); radio_on(&bt, 50, 1);
    expect("on twice", "intent1 start arm intent1 start disarm arm ");
    g_up = 1; assert(radio_tick(&bt, 60) == RADIO_UP); no_leak();
}

static void check_off(void){
    /* off: intent first, then stop watching, then the radio's other work, then power down - each exactly once */
    reset();
    radio_on(&bt, 0, 1);
    clear();
    radio_off(&bt);
    expect("off", "intent0 disarm cleanup down ");
    assert(g_intent == 0 && g_cleanups == 1 && g_power_downs == 1); no_leak();
    /* an off during bring-up must not leave the watch to roll back or scan later */
    g_up = 0;
    assert(radio_tick(&bt, 40000) == RADIO_IDLE);
    assert(g_intent == 0 && g_power_downs == 1);
    /* off with no watch running: no disarm; the cleanup is optional */
    reset();
    radio_off(&wifi); expect("off idle", "intent0 cleanup down ");
    struct radio bare = { &wifi_w, fake_intent, fake_start, NULL, fake_power_down, 0 };
    clear(); radio_off(&bare); expect("off no cleanup", "intent0 down ");
    /* off after the bring-up ended: still one teardown */
    reset();
    radio_on(&wifi, 0, 1); g_up = 1; assert(radio_tick(&wifi, 10) == RADIO_UP);
    clear(); radio_off(&wifi);
    expect("off after up", "intent0 cleanup down "); no_leak();
}

static void check_rollback(void){
    /* Bluetooth-like: a timeout clears the intent and powers down - without the user-facing cleanup */
    reset();
    radio_on(&bt, 0, 1);
    clear();
    assert(radio_tick(&bt, 14000) == RADIO_PENDING && g_log[0] == 0);           /* nothing happens while pending */
    assert(radio_tick(&bt, 28001) == RADIO_TIMEOUT);
    expect("rollback", "disarm intent0 down ");
    assert(g_intent == 0 && g_power_downs == 1 && g_cleanups == 0); no_leak();
    assert(radio_tick(&bt, 99999) == RADIO_IDLE && g_power_downs == 1);         /* and only once */

    /* Wi-Fi-like: the timeout is reported but the intent stays on and nothing is powered down */
    reset();
    radio_on(&wifi, 0, 1);
    clear();
    assert(radio_tick(&wifi, 8001) == RADIO_TIMEOUT);
    expect("no rollback", "disarm ");
    assert(g_intent == 1 && g_power_downs == 0); no_leak();

    /* success never rolls back */
    reset();
    radio_on(&bt, 0, 1); g_up = 1; clear();
    assert(radio_tick(&bt, 1000) == RADIO_UP);
    expect("success", "disarm "); assert(g_intent == 1 && g_power_downs == 0); no_leak();

    /* a failed bring-up followed by a fresh attempt starts clean */
    reset();
    radio_on(&bt, 0, 1); assert(radio_tick(&bt, 28001) == RADIO_TIMEOUT);
    radio_on(&bt, 30000, 1);
    assert(g_intent == 1 && g_armed);
    assert(radio_tick(&bt, 30000 + 28000) == RADIO_PENDING);
    g_up = 1; assert(radio_tick(&bt, 31000) == RADIO_UP); no_leak();
}

int main(void){
    check_watch();
    check_on();
    check_off();
    check_rollback();
    printf("radio_test: watch, on, off, rollback ok\n");
    return 0;
}
