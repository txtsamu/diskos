/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "srcswitch.h"
#include "sdio.h"
#include "outseq.h"
#include "outroute.h"
#include "ipc.h"
#include "config.h"
#include "session.h"
#include "admit.h"

static const struct srcswitch_env *E;
static uint32_t now_ms(void){ return E->now(); }
static uint32_t elapsed(uint32_t t0){ return now_ms() - t0; }
static void (*g_listener)(srcswitch_event_t);
void srcswitch_set_listener(void (*cb)(srcswitch_event_t)){ g_listener = cb; }
static void notify(srcswitch_event_t ev){ if(g_listener) g_listener(ev); }
static void route_changed(void){ notify(SRCSWITCH_EV_CHANGED); }
void srcswitch_set_env(const struct srcswitch_env *env){ E = env; outroute_set_listener(route_changed); }

typedef struct { int on; uint32_t period, last; } tm_t;
static void tm_start(tm_t *t, uint32_t period){ t->on = 1; t->period = period; t->last = now_ms(); }
static void tm_stop(tm_t *t){ t->on = 0; }
static int  tm_due(tm_t *t){
    if(!t->on || elapsed(t->last) < t->period) return 0;
    t->last = now_ms(); return 1;
}

static tm_t g_bootusb;
static struct { tm_t tm; int mode; uint32_t t0; } g_leave;

static int source_switch(int mode, int exit_done);

static void bootusb_tick(void){
    int src = 0;
    session_restore_t r = session_restore_poll(now_ms(), outroute_busy(), admit_check(ADMIT_OUTPUT_SWITCH) != NULL, &src);
    if(r == SESSION_RESTORE_WAIT) return;
    tm_stop(&g_bootusb);
    if(r == SESSION_RESTORE_SOURCE){
        if(ui_set_source_mode(src) == 0) E->toast(src == 1 ? "Restoring USB DAC mode..." : "Restoring Bluetooth Receiving...");
        notify(SRCSWITCH_EV_CHANGED);
    } else if(r == SESSION_RESTORE_USB_AUDIO){
        int power = E->external_power ? E->external_power() : 0;
        if(power != 0){
            fprintf(stderr, "usb audio: restore skipped, %s\n", power > 0 ? "the port has external power" : "the port power state is unreadable"); fflush(stderr);
            if(power > 0) E->toast("USB Audio not restored - charger connected");
            return;
        }
        if(outroute_pick(OUT_USB, OUTROUTE_MODE) == 0) E->toast("Restoring USB Audio...");
    }
}
void modes_output_boot_restore(void){
    session_restore_arm(now_ms());
    if(!g_bootusb.on) tm_start(&g_bootusb, 1000);
}
int srcswitch_send_source(int mode){
    outseq_rc rc = outseq_source((outseq_src_t)mode);
    if(rc != OUTSEQ_REFUSED) outroute_reset();
    return rc < 0 ? -1 : 0;
}

static struct { int active; int mode; uint32_t t0, gone_at; } g_dacx;
static void leave_end(const char *why){
    tm_stop(&g_leave.tm);
    if(why) E->toast(why);
    notify(SRCSWITCH_EV_ABANDONED);
}
static void dac_exit_tick(void){
    if(!g_dacx.active) return;
    if(elapsed(g_dacx.t0) > 30000){
        g_dacx.active = 0;
        fprintf(stderr, "usb dac: exit timed out waiting for the gadget to go away\n"); fflush(stderr);
        E->toast("Couldn't leave USB DAC - unplug the cable first");
        leave_end(NULL);
        return;
    }
    if(E->dac_gadget_present()){ g_dacx.gone_at = 0; return; }
    if(!g_dacx.gone_at){ g_dacx.gone_at = now_ms() ? now_ms() : 1; return; }
    if(elapsed(g_dacx.gone_at) < 1500) return;
    int m = g_dacx.mode;
    g_dacx.active = 0;
    fprintf(stderr, "usb dac: gadget gone, continuing the switch to mode %d\n", m); fflush(stderr);
    if(source_switch(m, 1) == 0) notify(SRCSWITCH_EV_SETTLE_RESTART);
    else leave_end(NULL);
}
static int dac_exit_begin(int mode){
    if(g_dacx.active){ E->toast("Switching..."); return -1; }
    if(outseq_gadget_none() != OUTSEQ_OK){ E->toast("Player is busy - try again"); return -1; }
    g_dacx.mode = mode; g_dacx.t0 = now_ms(); g_dacx.gone_at = 0;
    g_dacx.active = 1;
    fprintf(stderr, "usb dac: leaving with a host attached: 0642 0 first, then mode %d\n", mode); fflush(stderr);
    E->toast("Leaving USB DAC... (up to 20 s)");
    return 0;
}
static int g_dac_carry_vol = -1;
static uint32_t g_dac_enter_t;
static uint32_t g_dac_vol_due;
static void dac_volume_cb(void){
    if(session_source_mode() != 1 || cfg_get_int("dac_match_volume", 1) == 0) return;
    int want = cfg_get_int("dac_volume", 0); if(want <= 0) want = g_dac_carry_vol;
    int mv = cfg_get_int("max_vol", -1);
    if(want <= 0) return;
    if(mv >= 10 && want > mv) want = mv;
    fprintf(stderr, "usb dac: volume -> %d (%s)\n", want, cfg_get_int("dac_volume", 0) > 0 ? "your last USB DAC level" : "carried over from Local"); fflush(stderr);
    E->set_volume(want);
}

const char *srcswitch_switch_msg(int mode){
    static const char *const MSG[4] = {
        "Switching to local playback", "Switching to USB DAC",
        "Switching to Bluetooth receiving", "Switching to USB storage",
    };
    return mode >= 0 && mode < 4 ? MSG[mode] : "";
}
static void leave_tick(void){
    if(elapsed(g_leave.t0) >= 10000){ leave_end("Couldn't return to Local - mode not changed"); return; }
    if(outroute_busy() || elapsed(g_leave.t0) < 3000) return;
    if(outroute_route() != OUT_INTERNAL || session_source_mode() != 0){ leave_end("Still not on Local - mode not changed"); return; }
    int m = g_leave.mode;
    tm_stop(&g_leave.tm);
    outroute_save_usb_audio(0);
    notify(SRCSWITCH_EV_SETTLE_RESTART);
    if(source_switch(m, 0) != 0){ leave_end(NULL); return; }
    E->toast(srcswitch_switch_msg(m));
}
static int via_local(int mode, int exit_done){
    if(g_leave.tm.on){ E->toast("Switching..."); return -1; }
    const char *msg = "Leaving USB DAC...";
    if(outroute_route() == OUT_USB){
        if(outroute_pick(OUT_INTERNAL, OUTROUTE_MODE_STAY_PAUSED) != 0) return -1;
        msg = "Leaving USB Audio...";
    } else if(source_switch(0, exit_done) != 0) return -1;
    g_leave.mode = mode; g_leave.t0 = now_ms();
    tm_start(&g_leave.tm, 100);
    E->toast(msg);
    return 0;
}
int srcswitch_via_local_pending(void){ return g_leave.tm.on; }
int modes_source_switch_busy(void){
    return outroute_pick_pending() || g_leave.tm.on || g_dacx.active || sd_switch_pending();
}
static int source_switch(int mode, int exit_done){
    { const char *why = admit_check(ADMIT_OUTPUT_IDLE); if(why){ E->toast(why); return -1; } }
    if(mode < 0 || mode > 3) return -1;
    if(session_source_mode() == 1 && mode != 1 && !exit_done && E->host_attached()){
        if(cfg_get_int("dac_safe_exit", 1) == 0){ E->toast("Unplug the USB cable first"); return -1; }
        return dac_exit_begin(mode);
    }
    { const char *why = sd_switch_refusal(); if(why){ E->toast(why); return -1; } }
    if(mode != 0 && (outroute_route() == OUT_USB || (session_source_mode() == 1 && mode == 3))) return via_local(mode, exit_done);
    if(mode == 0 && outroute_route() == OUT_USB) return outroute_pick(OUT_INTERNAL, OUTROUTE_MODE);
    if(mode == 3) return sd_export_begin();
    if(sd_host_owned()) return sd_return_begin(mode);
    int was_playing = E->playing();
    if(was_playing && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0){ E->toast("Player is busy - try again"); return -1; }
    if(session_source_mode() == 0 && mode != 0){ track_state_t vs; ipc_get_state(&vs); if(vs.volume > 0) g_dac_carry_vol = vs.volume; }
    if(srcswitch_send_source(mode) < 0){ E->toast("Couldn't switch mode"); return -1; }
    session_set_source_mode(mode);
    if(mode == 1){ g_dac_enter_t = now_ms(); g_dac_vol_due = g_dac_enter_t + 4500; if(!g_dac_vol_due) g_dac_vol_due = 1; }
    if(mode == 0 && was_playing) ipc_send_cmd(PLAYER_CMD_PLAYPAUSE);
    session_save_source(mode);
    return 0;
}
int ui_set_source_mode(int mode){ return source_switch(mode, 0); }

void srcswitch_init(void){ sd_init(); }

void srcswitch_tick(void){
    outroute_tick();
    if(tm_due(&g_leave.tm)) leave_tick();
    if(tm_due(&g_bootusb)) bootusb_tick();
    dac_exit_tick();
    if(g_dac_vol_due && (int32_t)(now_ms() - g_dac_vol_due) >= 0){ g_dac_vol_due = 0; dac_volume_cb(); }
    sd_tick();
}
int srcswitch_dac_level_settled(void){ return session_source_mode() == 1 && elapsed(g_dac_enter_t) > 8000; }
