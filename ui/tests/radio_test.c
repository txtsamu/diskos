/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "radio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char g_log[256];
static void L(const char *s){ assert(strlen(g_log) + strlen(s) + 2 < sizeof g_log); strcat(g_log, s); strcat(g_log, " "); }
static void clear(void){ g_log[0] = 0; }
static uint32_t g_now;
static int  g_up, g_actual, g_intent = -1, g_stay = 1;
static int  g_power_downs, g_cleanups, g_arms, g_disarms;
static unsigned g_arm_ms;
#define MAXW 8
static struct radio_watch *g_armed[MAXW];
static int armed_n(void){ int n = 0; for(int i = 0; i < MAXW; i++) if(g_armed[i]) n++; return n; }
static int is_armed(struct radio_watch *w){ for(int i = 0; i < MAXW; i++) if(g_armed[i] == w) return 1; return 0; }

static uint32_t fake_now(void){ return g_now; }
static void *fake_timer_start(unsigned ms, struct radio_watch *w){
    assert(!is_armed(w));
    for(int i = 0; i < MAXW; i++) if(!g_armed[i]){ g_armed[i] = w; g_arms++; g_arm_ms = ms; L("arm"); return w; }
    assert(0); return NULL;
}
static void fake_timer_stop(void *h){
    for(int i = 0; i < MAXW; i++) if(g_armed[i] == h){ g_armed[i] = NULL; g_disarms++; L("disarm"); return; }
    assert(0);
}
static const struct radio_env env = { fake_now, fake_timer_start, fake_timer_stop };

static int  fake_up(void){ return g_up; }
static int  fake_actual(void){ return g_actual; }
static int  fake_stay(void){ return g_stay; }
static void fake_intent(int on){ g_intent = on; L(on ? "intent1" : "intent0"); }
static void fake_start(void){ L("start"); }
static void fake_cleanup(void){ g_cleanups++; L("cleanup"); }
static void fake_power_down(void){ g_power_downs++; L("down"); }
static int  fake_intent_on(void){ return g_intent; }

static struct radio_watch *g_cb_watch;
static void on_up_cb(void){ assert(!is_armed(g_cb_watch)); L("up!"); }
static void on_timeout_cb(void){ assert(!is_armed(g_cb_watch)); L("timeout!"); }

static struct radio_watch scanwait = { .env = &env, .interval_ms = 1000, .timeout_ms = 28000, .is_up = fake_up,
                                       .stay = fake_stay, .on_up = on_up_cb, .on_timeout = on_timeout_cb };
static struct radio_watch bootrestore = { .env = &env, .interval_ms = 1000, .timeout_ms = 28000, .is_up = fake_up,
                                          .on_up = on_up_cb };
static struct radio wifi = {
    .watch = { .env = &env, .interval_ms = 700, .timeout_ms = 8000, .is_up = fake_up, .on_up = on_up_cb, .on_timeout = on_timeout_cb },
    .set_intent = fake_intent, .start = fake_start, .cleanup = fake_cleanup, .power_down = fake_power_down,
    .is_on = fake_intent_on, .timeout = RADIO_REPORT_ONLY, .toggle_observes = 0,
};
static struct radio bt = {
    .watch = { .env = &env, .interval_ms = 1000, .timeout_ms = 28000, .is_up = fake_up, .on_up = on_up_cb, .on_timeout = on_timeout_cb },
    .set_intent = fake_intent, .start = fake_start, .cleanup = fake_cleanup, .power_down = fake_power_down,
    .is_on = fake_actual, .timeout = RADIO_ROLLBACK, .toggle_observes = 1, .excludes = &scanwait,
};
static struct radio_watch *const watches[] = { &scanwait, &bootrestore, &wifi.watch, &bt.watch };

static void reset(void){
    clear(); g_now = 0; g_up = g_actual = 0; g_intent = -1; g_stay = 1;
    g_power_downs = g_cleanups = g_arms = g_disarms = 0; g_arm_ms = 0;
    for(int i = 0; i < MAXW; i++) g_armed[i] = NULL;
    for(unsigned i = 0; i < sizeof watches / sizeof *watches; i++){ watches[i]->active = 0; watches[i]->timer = NULL; watches[i]->owner = NULL; }
    g_cb_watch = NULL;
}
static void expect(const char *what, const char *want){
    if(strcmp(g_log, want)){ fprintf(stderr, "%s: log \"%s\", want \"%s\"\n", what, g_log, want); assert(0); }
}
static void no_leak(void){
    assert(armed_n() == 0 && g_arms == g_disarms);
    for(unsigned i = 0; i < sizeof watches / sizeof *watches; i++) assert(!watches[i]->active);
}
static int fire(struct radio_watch *w, uint32_t at){ g_now = at; g_cb_watch = w; return radio_watch_fire(w); }

static void check_watch(void){
    reset();
    g_now = 1000; radio_watch_begin(&bootrestore);
    assert(is_armed(&bootrestore) && g_arm_ms == 1000);
    g_up = 1;
    assert(fire(&bootrestore, 1001) == RADIO_UP);
    expect("up", "arm disarm up! "); no_leak();
    assert(fire(&bootrestore, 1002) == RADIO_IDLE && g_disarms == 1);

    reset();
    g_now = 5000; radio_watch_begin(&scanwait);
    assert(fire(&scanwait, 5700) == RADIO_PENDING);
    assert(fire(&scanwait, 5000 + 28000) == RADIO_PENDING && is_armed(&scanwait));
    clear();
    assert(fire(&scanwait, 5000 + 28001) == RADIO_TIMEOUT);
    expect("timeout", "disarm timeout! "); no_leak();
    assert(fire(&scanwait, 5000 + 29000) == RADIO_IDLE);

    reset();
    radio_watch_begin(&bootrestore);
    assert(fire(&bootrestore, 28001) == RADIO_TIMEOUT);
    expect("quiet timeout", "arm disarm "); assert(g_power_downs == 0 && g_intent == -1); no_leak();

    reset();
    radio_watch_begin(&scanwait);
    g_up = 1;
    assert(fire(&scanwait, 28001) == RADIO_UP); no_leak();

    reset();
    g_now = 0xFFFFFF00u; radio_watch_begin(&scanwait);
    assert(fire(&scanwait, 0x00000100u) == RADIO_PENDING);
    assert(fire(&scanwait, 0xFFFFFF00u + 28001u) == RADIO_TIMEOUT); no_leak();

    reset();
    radio_watch_begin(&scanwait);
    g_now = 20000; radio_watch_begin(&scanwait);
    expect("replace", "arm disarm arm ");
    assert(fire(&scanwait, 20000 + 28000) == RADIO_PENDING);
    assert(fire(&scanwait, 20000 + 28001) == RADIO_TIMEOUT); no_leak();

    reset();
    radio_watch_cancel(&scanwait); assert(g_disarms == 0);
    radio_watch_begin(&scanwait);
    radio_watch_cancel(&scanwait); radio_watch_cancel(&scanwait);
    assert(g_disarms == 1); no_leak(); expect("cancel", "arm disarm ");
    g_up = 1; assert(fire(&scanwait, 10) == RADIO_IDLE);

    reset();
    radio_watch_begin(&scanwait);
    assert(fire(&scanwait, 500) == RADIO_PENDING);
    g_stay = 0; g_up = 1;
    assert(fire(&scanwait, 1500) == RADIO_IDLE);
    expect("stay", "arm disarm "); no_leak();
    g_stay = 1; assert(fire(&scanwait, 2500) == RADIO_IDLE);

    reset();
    radio_watch_begin(&scanwait); radio_watch_begin(&bootrestore);
    assert(armed_n() == 2);
    radio_watch_cancel(&scanwait);
    assert(armed_n() == 1 && is_armed(&bootrestore));
    radio_watch_cancel(&bootrestore); no_leak();
}

static void check_on(void){
    reset();
    g_now = 100; radio_on(&bt, 1);
    expect("on observed", "intent1 start arm ");
    assert(g_intent == 1 && is_armed(&bt.watch) && bt.watch.start == 100);
    reset();
    radio_on(&wifi, 0);
    expect("on unobserved", "intent1 start ");
    assert(g_arms == 0 && !wifi.watch.active);
    reset();
    radio_on(&bt, 1); g_now = 50; radio_on(&bt, 1);
    expect("on twice", "intent1 start arm intent1 start disarm arm ");
    g_up = 1; assert(fire(&bt.watch, 60) == RADIO_UP); no_leak();
    reset();
    radio_on(&wifi, 1); g_up = 1; clear();
    assert(fire(&wifi.watch, 100) == RADIO_UP);
    expect("on up", "disarm up! "); no_leak();
}

static void check_off(void){
    reset();
    radio_on(&bt, 1);
    clear();
    radio_off(&bt);
    expect("off", "intent0 disarm cleanup down ");
    assert(g_intent == 0 && g_cleanups == 1 && g_power_downs == 1); no_leak();
    g_up = 0;
    assert(fire(&bt.watch, 40000) == RADIO_IDLE);
    assert(g_intent == 0 && g_power_downs == 1);
    reset();
    radio_off(&wifi); expect("off idle", "intent0 cleanup down ");
    struct radio bare = wifi;
    bare.cleanup = NULL;
    clear(); radio_off(&bare); expect("off no cleanup", "intent0 down ");
    reset();
    radio_on(&wifi, 1); g_up = 1; assert(fire(&wifi.watch, 10) == RADIO_UP);
    clear(); radio_off(&wifi);
    expect("off after up", "intent0 cleanup down "); no_leak();
    reset();
    radio_watch_begin(&scanwait); clear();
    radio_off(&bt);
    assert(is_armed(&scanwait));
    radio_watch_cancel(&scanwait); no_leak();
}

static void check_rollback(void){
    reset();
    radio_on(&bt, 1);
    clear();
    assert(fire(&bt.watch, 14000) == RADIO_PENDING && g_log[0] == 0);
    assert(fire(&bt.watch, 28001) == RADIO_TIMEOUT);
    expect("rollback", "disarm intent0 down timeout! ");
    assert(g_intent == 0 && g_power_downs == 1 && g_cleanups == 0); no_leak();
    assert(fire(&bt.watch, 99999) == RADIO_IDLE && g_power_downs == 1);

    reset();
    radio_on(&wifi, 1);
    clear();
    assert(fire(&wifi.watch, 8001) == RADIO_TIMEOUT);
    expect("no rollback", "disarm timeout! ");
    assert(g_intent == 1 && g_power_downs == 0); no_leak();

    reset();
    radio_on(&bt, 1); g_up = 1; clear();
    assert(fire(&bt.watch, 1000) == RADIO_UP);
    expect("success", "disarm up! "); assert(g_intent == 1 && g_power_downs == 0); no_leak();

    reset();
    radio_on(&bt, 1); assert(fire(&bt.watch, 28001) == RADIO_TIMEOUT);
    g_now = 30000; radio_on(&bt, 1);
    assert(g_intent == 1 && is_armed(&bt.watch));
    assert(fire(&bt.watch, 30000 + 28000) == RADIO_PENDING);
    g_up = 1; assert(fire(&bt.watch, 31000) == RADIO_UP); no_leak();
}

static void check_toggle(void){
    reset();
    g_intent = 0;
    assert(radio_toggle(&wifi) == 1);
    expect("wifi toggle on", "intent1 start "); assert(g_arms == 0);
    assert(radio_toggle(&wifi) == 0);
    expect("wifi toggle off", "intent1 start intent0 cleanup down ");
    assert(g_intent == 0 && g_power_downs == 1);
    reset();
    g_intent = 0; g_actual = 1; g_up = 1; clear();
    assert(radio_toggle(&wifi) == 1); expect("wifi follows intent", "intent1 start ");
    reset();
    g_intent = 1; g_actual = 0; g_up = 0;
    assert(radio_toggle(&wifi) == 0); expect("wifi intent on, radio down", "intent0 cleanup down ");

    reset();
    g_actual = 0; g_intent = 0;
    assert(radio_toggle(&bt) == 1);
    expect("bt toggle on", "intent1 start arm "); assert(is_armed(&bt.watch));
    g_actual = 1; clear();
    assert(radio_toggle(&bt) == 0);
    expect("bt toggle off", "intent0 disarm cleanup down "); no_leak();
    reset();
    g_intent = 1; g_actual = 0;
    assert(radio_toggle(&bt) == 1); expect("bt stale intent on", "intent1 start arm ");
    reset();
    g_intent = 0; g_actual = 1;
    assert(radio_toggle(&bt) == 0); expect("bt stale intent off", "intent0 cleanup down ");
    reset();
    g_actual = 0; radio_toggle(&bt); clear();
    assert(fire(&bt.watch, 28001) == RADIO_TIMEOUT);
    expect("bt toggle rollback", "disarm intent0 down timeout! "); no_leak();
}

static void check_excludes(void){
    reset();
    radio_watch_begin(&scanwait); clear();
    radio_on(&bt, 1);
    expect("excludes", "intent1 start disarm arm ");
    assert(!is_armed(&scanwait) && is_armed(&bt.watch));
    radio_watch_cancel(&bt.watch); no_leak();
    reset();
    radio_watch_begin(&scanwait); clear();
    radio_on(&bt, 0);
    expect("excludes unobserved", "intent1 start disarm ");
    assert(!is_armed(&scanwait) && !bt.watch.active); no_leak();
    reset();
    radio_on(&bt, 1); expect("excludes idle", "intent1 start arm ");
    reset();
    radio_watch_begin(&scanwait); clear();
    radio_on(&wifi, 1);
    assert(is_armed(&scanwait) && is_armed(&wifi.watch));
    radio_watch_cancel(&scanwait); radio_watch_cancel(&wifi.watch); no_leak();
    reset();
    g_intent = 1; radio_watch_begin(&scanwait); clear();
    assert(fire(&scanwait, 28001) == RADIO_TIMEOUT);
    expect("scanwait timeout", "disarm timeout! "); assert(g_intent == 1 && g_power_downs == 0);
}

int main(void){
    check_watch();
    check_on();
    check_off();
    check_rollback();
    check_toggle();
    check_excludes();
    printf("radio_test: watch, on, off, rollback, toggle, excludes ok\n");
    return 0;
}
