/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "usbdbg.h"
#include <stdio.h>

#define START_LIMIT_MS 45000
#define STOP_LIMIT_MS  30000
#define POLL_MS        1000

enum { RC_USAGE = 1, RC_STOCK_GADGET = 2, RC_NO_UDC = 3, RC_BIND = 4, RC_SETUP = 5, RC_NO_TTY = 6, RC_NOT_FOUND = 127 };

static const struct usbdbg_env *E;
static void (*g_listener)(usbdbg_event_t);
static usbdbg_state_t g_state = USBDBG_OFF;
static int g_pid, g_host;
static uint32_t g_t0, g_polled;

void usbdbg_set_env(const struct usbdbg_env *env){ E = env; }
void usbdbg_set_listener(void (*cb)(usbdbg_event_t)){ g_listener = cb; }
static void notify(usbdbg_event_t ev){ if(g_listener) g_listener(ev); }

usbdbg_state_t usbdbg_state(void){ return g_state; }
int usbdbg_active(void){ return g_state != USBDBG_OFF; }
int usbdbg_busy(void){ return g_state == USBDBG_STARTING || g_state == USBDBG_STOPPING; }
int usbdbg_host(void){ return g_state == USBDBG_ON && g_host; }

static void begin(usbdbg_state_t st, int pid){
    g_state = st; g_pid = pid; g_t0 = E->now();
    notify(USBDBG_EV_CHANGED);
}

void usbdbg_init(void){
    g_state = E->bound() ? USBDBG_ON : USBDBG_OFF;
    g_host = g_state == USBDBG_ON ? E->host_connected() : 0;
    g_polled = E->now();
}

static const char *start_failure(int rc){
    switch(rc){
    case RC_STOCK_GADGET: return "Leave USB DAC or USB Storage first";
    case RC_NO_UDC:       return "No USB controller found";
    case RC_BIND:         return "Couldn't bind the USB port";
    case RC_SETUP:        return "Couldn't set up USB Debug";
    case RC_NO_TTY:       return "USB serial port did not appear";
    case RC_USAGE:
    case RC_NOT_FOUND:    return "USB Debug needs a newer diskOS image";
    default:              return "USB Debug failed";
    }
}

static void end_stop(int ok){
    if(ok || !E->bound()){
        g_state = USBDBG_OFF;
        notify(USBDBG_EV_STOPPED);
        notify(USBDBG_EV_CHANGED);
        return;
    }
    g_state = USBDBG_ON;
    g_host = E->host_connected();
    E->toast("Couldn't stop USB Debug");
    notify(USBDBG_EV_STOP_FAILED);
    notify(USBDBG_EV_CHANGED);
}

static void cleanup_after_failure(void){
    int pid = (E->present() || E->bound()) ? E->spawn("usb-off") : -1;
    if(pid <= 0){ g_state = USBDBG_OFF; notify(USBDBG_EV_CHANGED); return; }
    begin(USBDBG_STOPPING, pid);
}

static void start_failed(const char *why){
    E->toast(why);
    fprintf(stderr, "usb debug: %s\n", why); fflush(stderr);
    cleanup_after_failure();
}

int usbdbg_start(void){
    if(g_state == USBDBG_ON){ E->toast("USB Debug is already on"); return -1; }
    if(g_state != USBDBG_OFF){ E->toast("USB Debug is busy"); return -1; }
    if(E->stock_gadget()){ E->toast("Leave USB DAC or USB Storage first"); return -1; }
    int pid = E->spawn("usb-on");
    if(pid <= 0){ E->toast("Couldn't start USB Debug"); return -1; }
    begin(USBDBG_STARTING, pid);
    return 0;
}

int usbdbg_stop(void){
    if(g_state == USBDBG_OFF) return USBDBG_STOP_NOOP;
    if(g_state == USBDBG_STOPPING) return USBDBG_STOP_STARTED;
    if(g_state == USBDBG_STARTING){ E->toast("USB Debug is still starting"); return -1; }
    int pid = E->spawn("usb-off");
    if(pid <= 0){ E->toast("Couldn't stop USB Debug"); return -1; }
    begin(USBDBG_STOPPING, pid);
    return USBDBG_STOP_STARTED;
}

static void starting_tick(uint32_t now){
    int rc = E->reap(g_pid);
    if(rc == USBDBG_RUNNING){
        if(now - g_t0 > START_LIMIT_MS){ E->abort(g_pid); start_failed("USB Debug timed out"); }
        return;
    }
    if(rc == 0 && E->bound()){
        g_state = USBDBG_ON; g_host = E->host_connected(); g_polled = now;
        fprintf(stderr, "usb debug: on (host %s)\n", g_host ? "connected" : "not connected"); fflush(stderr);
        E->toast("USB Debug is on");
        notify(USBDBG_EV_CHANGED);
        return;
    }
    start_failed(start_failure(rc));
}

static void stopping_tick(uint32_t now){
    int rc = E->reap(g_pid);
    if(rc == USBDBG_RUNNING){
        if(now - g_t0 > STOP_LIMIT_MS){ E->abort(g_pid); end_stop(0); }
        return;
    }
    end_stop(rc == 0);
}

static void on_tick(uint32_t now){
    if(now - g_polled < POLL_MS) return;
    g_polled = now;
    if(!E->bound()){
        g_state = USBDBG_OFF; g_host = 0;
        fprintf(stderr, "usb debug: the gadget was removed from outside\n"); fflush(stderr);
        notify(USBDBG_EV_CHANGED);
        return;
    }
    int h = E->host_connected();
    if(h != g_host){ g_host = h; notify(USBDBG_EV_CHANGED); }
}

static void off_tick(uint32_t now){
    if(now - g_polled < POLL_MS) return;
    g_polled = now;
    if(!E->bound()) return;
    g_state = USBDBG_ON; g_host = E->host_connected();
    fprintf(stderr, "usb debug: adopted a gadget that was started from outside\n"); fflush(stderr);
    notify(USBDBG_EV_CHANGED);
}

void usbdbg_tick(void){
    uint32_t now = E->now();
    if(g_state == USBDBG_STARTING) starting_tick(now);
    else if(g_state == USBDBG_STOPPING) stopping_tick(now);
    else if(g_state == USBDBG_ON) on_tick(now);
    else off_tick(now);
}
