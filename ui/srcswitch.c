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
#include "usbdbg.h"
#include "now.h"

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

#define PREF_PENDING "src_pending"
#define PENDING_MIN_MS 15000
#define PENDING_MAX_MS 120000

static tm_t g_bootusb;
static tm_t g_reapply;
static struct { tm_t tm; int mode; uint32_t t0; } g_leave;
static struct { tm_t tm; int mode; uint32_t t0; } g_pend;
static int g_sd_target = -1;

#define TX_SETTLE_MS 3200
#define REAPPLY_MS   2000
static struct {
    int active, from, to, failed;
    int mode;
    uint32_t started_at;
    const char *reason;
} g_tx;

static void tx_fail(const char *why){
    if(!g_tx.active) return;
    g_tx.active = 0; g_tx.failed = 1;
    g_tx.reason = why ? why : "Couldn't switch mode";
    g_tx.mode = session_source_mode();
}

static int source_switch(int mode, int exit_done);

static void bootusb_tick(void){
    int src = SRCSWITCH_MODE_LOCAL;
    session_restore_t r = session_restore_poll(now_ms(), outroute_busy(), admit_check(ADMIT_OUTPUT_SWITCH) != NULL, &src);
    if(r == SESSION_RESTORE_WAIT) return;
    tm_stop(&g_bootusb);
    if(r == SESSION_RESTORE_SOURCE){
        if(ui_set_source_mode(src) == 0) E->toast(src == SRCSWITCH_MODE_USB_DAC ? "Restoring USB DAC mode..." : "Restoring Bluetooth Receiving...");
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
int srcswitch_send_source(int mode){
    outseq_rc rc = outseq_source((outseq_src_t)mode);
    if(rc != OUTSEQ_REFUSED) outroute_reset();
    if(rc == OUTSEQ_OK && (mode == SRCSWITCH_MODE_LOCAL || mode == SRCSWITCH_MODE_USB_DAC || mode == SRCSWITCH_MODE_BT_RECEIVING))
        tm_start(&g_reapply, REAPPLY_MS);
    return rc < 0 ? -1 : 0;
}

static struct { int active; int mode; uint32_t t0, gone_at; } g_dacx;
static void leave_end(const char *why){
    tm_stop(&g_leave.tm);
    tx_fail(why);
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
    if(!g_dacx.gone_at){ g_dacx.gone_at = now_nonzero(now_ms()); return; }
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
static struct { int active; int mode; uint32_t done_at; } g_dbgx;
static void dbg_event(usbdbg_event_t ev){
    if(ev == USBDBG_EV_STOPPED && g_dbgx.active){ g_dbgx.done_at = now_nonzero(now_ms()); return; }
    if(ev == USBDBG_EV_STOP_FAILED && g_dbgx.active){ g_dbgx.active = 0; leave_end(NULL); return; }
    notify(SRCSWITCH_EV_CHANGED);
}
static int dbg_leave_begin(int mode){
    if(g_dbgx.active){ E->toast("Switching..."); return -1; }
    if(usbdbg_stop() != USBDBG_STOP_STARTED) return -1;
    g_dbgx.mode = mode; g_dbgx.done_at = 0; g_dbgx.active = 1;
    fprintf(stderr, "usb debug: leaving, then mode %d\n", mode); fflush(stderr);
    E->toast("Leaving USB Debug...");
    return 0;
}
static void dbg_exit_tick(void){
    if(!g_dbgx.active || !g_dbgx.done_at || elapsed(g_dbgx.done_at) < 1000) return;
    int m = g_dbgx.mode;
    g_dbgx.active = 0;
    fprintf(stderr, "usb debug: gadget gone, continuing the switch to mode %d\n", m); fflush(stderr);
    if(m == 0){ E->toast("USB Debug is off"); notify(SRCSWITCH_EV_SETTLE_RESTART); notify(SRCSWITCH_EV_CHANGED); return; }
    if(source_switch(m, 1) == 0) notify(SRCSWITCH_EV_SETTLE_RESTART);
    else leave_end(NULL);
}
static int g_dac_carry_vol = -1;
static uint32_t g_dac_enter_t;
static uint32_t g_dac_vol_due;
static void dac_volume_cb(void){
    if(session_source_mode() != SRCSWITCH_MODE_USB_DAC || cfg_get_int("dac_match_volume", 1) == 0) return;
    int want = cfg_get_int("dac_volume", 0); if(want <= 0) want = g_dac_carry_vol;
    int mv = cfg_get_int("max_vol", -1);
    if(want <= 0) return;
    if(mv >= 10 && want > mv) want = mv;
    fprintf(stderr, "usb dac: volume -> %d (%s)\n", want, cfg_get_int("dac_volume", 0) > 0 ? "your last USB DAC level" : "carried over from Local"); fflush(stderr);
    E->set_volume(want);
}

const char *srcswitch_switch_msg(int mode){
    static const char *const MSG[6] = {
        "Switching to local playback", "Switching to USB DAC",
        "Switching to Bluetooth receiving", "Switching to USB storage",
        "Switching to USB Debug", "Switching to USB audio output",
    };
    return mode >= 0 && mode <= SRCSWITCH_MODE_USB_AUDIO ? MSG[mode] : "";
}
static void leave_tick(void){
    int sd_busy = sd_switch_pending();
    if(elapsed(g_leave.t0) >= 10000 && !sd_busy){ leave_end("Couldn't return to Local - mode not changed"); return; }
    if(outroute_busy() || sd_busy || g_dbgx.active || usbdbg_busy() || elapsed(g_leave.t0) < 3000) return;
    if(outroute_route() != OUT_INTERNAL || session_source_mode() != SRCSWITCH_MODE_LOCAL){ leave_end("Still not on Local - mode not changed"); return; }
    int m = g_leave.mode;
    tm_stop(&g_leave.tm);
    outroute_save_usb_audio(0);
    notify(SRCSWITCH_EV_SETTLE_RESTART);
    if(source_switch(m, 0) != 0){ leave_end(NULL); return; }
    E->toast(srcswitch_switch_msg(m));
}
static int via_local(int mode, int exit_done){
    if(g_leave.tm.on){ E->toast("Switching..."); return -1; }
    int src = session_source_mode();
    const char *msg = src == SRCSWITCH_MODE_BT_RECEIVING ? "Leaving Bluetooth Receiving..." : "Leaving USB DAC...";
    if(outroute_route() == OUT_USB){
        if(outroute_pick(OUT_INTERNAL, OUTROUTE_MODE_STAY_PAUSED) != 0) return -1;
        msg = "Leaving USB Audio...";
    } else if(source_switch(SRCSWITCH_MODE_LOCAL, exit_done) != 0) return -1;
    g_leave.mode = mode; g_leave.t0 = now_ms();
    tm_start(&g_leave.tm, 100);
    E->toast(msg);
    return 0;
}
static int storage_target_pending(void){ return g_sd_target >= 0 && sd_switch_pending(); }
int srcswitch_via_local_pending(void){ return g_leave.tm.on || g_dbgx.active || g_pend.tm.on || storage_target_pending(); }
static int switch_in_progress(void){
    return outroute_pick_pending() || g_leave.tm.on || g_dacx.active || g_dbgx.active || usbdbg_busy() || sd_switch_pending();
}
int modes_source_switch_busy(void){ return switch_in_progress() || g_pend.tm.on; }

srcswitch_status_t srcswitch_status(void){
    int cur = session_source_mode();
    srcswitch_status_t s = { SRCSWITCH_STATE_IDLE, cur, cur, cur, 0, NULL };
    if(!g_tx.active){
        if(g_tx.failed){
            s.failed = 1; s.reason = g_tx.reason;
            s.from = g_tx.from; s.to = g_tx.to; s.mode = g_tx.mode;
        }
        return s;
    }
    s.from = g_tx.from; s.to = g_tx.to; s.mode = g_tx.mode; s.reason = g_tx.reason;
    if(g_pend.tm.on) s.state = SRCSWITCH_STATE_WAITING;
    else if(g_dacx.active || g_dbgx.active || g_leave.tm.on) s.state = SRCSWITCH_STATE_LEAVING;
    else if(outroute_pick_pending() || sd_switch_pending() || usbdbg_busy()) s.state = SRCSWITCH_STATE_ENTERING;
    else s.state = SRCSWITCH_STATE_SETTLING;
    return s;
}

static void tx_tick(void){
    if(!g_tx.active) return;
    if(switch_in_progress() || g_pend.tm.on) return;
    if(elapsed(g_tx.started_at) < TX_SETTLE_MS) return;
    int observed = g_tx.to;
    if(g_tx.to == SRCSWITCH_MODE_USB_DEBUG || g_tx.to == SRCSWITCH_MODE_USB_AUDIO)
        observed = session_source_mode();
    else if((g_tx.to == SRCSWITCH_MODE_LOCAL || g_tx.to == SRCSWITCH_MODE_USB_DAC || g_tx.to == SRCSWITCH_MODE_USB_STORAGE) && E->detected_mode)
        observed = E->detected_mode();
    g_tx.active = 0;
    g_tx.mode = observed;
    if((g_tx.to == SRCSWITCH_MODE_LOCAL || g_tx.to == SRCSWITCH_MODE_USB_DAC || g_tx.to == SRCSWITCH_MODE_USB_STORAGE) && observed != g_tx.to){
        g_tx.failed = 1;
        g_tx.reason = "Mode didn't switch";
        E->toast(g_tx.reason);
    }
    notify(SRCSWITCH_EV_CHANGED);
}

static void reapply_tick(void){
    tm_stop(&g_reapply);
    if(E->reapply_audio) E->reapply_audio();
}

static void pending_tick(void){
    uint32_t age = elapsed(g_pend.t0);
    int m = g_pend.mode;
    if(age >= PENDING_MAX_MS){
        tm_stop(&g_pend.tm);
        tx_fail("Couldn't continue the mode switch - pick it again");
        E->toast("Couldn't continue the mode switch - pick it again");
        notify(SRCSWITCH_EV_CHANGED);
        return;
    }
    if(age < PENDING_MIN_MS || !ipc_is_ready() || switch_in_progress()) return;
    if(session_source_mode() != SRCSWITCH_MODE_LOCAL){ tm_stop(&g_pend.tm); notify(SRCSWITCH_EV_CHANGED); return; }
    if(m == SRCSWITCH_MODE_USB_AUDIO && admit_check(ADMIT_OUTPUT_SWITCH) != NULL) return;
    tm_stop(&g_pend.tm);
    if(source_switch(m, 0) == 0){
        notify(SRCSWITCH_EV_SETTLE_RESTART);
        E->toast(srcswitch_switch_msg(m));
    }
    notify(SRCSWITCH_EV_CHANGED);
}
static void pending_arm(void){
    int m = cfg_get_int(PREF_PENDING, 0);
    if(m == 0) return;
    cfg_set_int(PREF_PENDING, 0);
    if(m < 1 || m > SRCSWITCH_MODE_USB_AUDIO) return;
    g_pend.mode = m; g_pend.t0 = now_ms();
    tm_start(&g_pend.tm, 500);
}
void modes_output_boot_restore(void){
    session_restore_arm(now_ms());
    if(!g_bootusb.on) tm_start(&g_bootusb, 1000);
    pending_arm();
}
static int storage_leave(int mode){
    if(cfg_set_int(PREF_PENDING, mode) != 0){ E->toast("Couldn't save the next mode - try again"); return -1; }
    g_sd_target = mode;
    if(sd_return_begin(SRCSWITCH_MODE_LOCAL) != 0){ g_sd_target = -1; cfg_set_int(PREF_PENDING, 0); return -1; }
    E->toast("Leaving USB Storage...");
    return 0;
}
static int needs_local(int mode){
    int src = session_source_mode();
    if(mode == SRCSWITCH_MODE_LOCAL || mode == src) return 0;
    if(mode == SRCSWITCH_MODE_USB_AUDIO) return src == SRCSWITCH_MODE_USB_DAC || src == SRCSWITCH_MODE_BT_RECEIVING;
    return outroute_route() == OUT_USB || src == SRCSWITCH_MODE_USB_DAC || src == SRCSWITCH_MODE_BT_RECEIVING;
}
static int dbg_enter(int exit_done){
    if(usbdbg_state() != USBDBG_OFF){ E->toast(usbdbg_busy() ? "USB Debug is busy" : "USB Debug is already on"); return -1; }
    { const char *why = sd_switch_refusal(); if(why){ E->toast(why); return -1; } }
    if(sd_host_owned()) return storage_leave(SRCSWITCH_MODE_USB_DEBUG);
    if(session_source_mode() != SRCSWITCH_MODE_LOCAL || outroute_route() == OUT_USB) return via_local(SRCSWITCH_MODE_USB_DEBUG, exit_done);
    return usbdbg_start() == 0 ? 0 : -1;
}
static int source_switch(int mode, int exit_done){
    if(g_leave.tm.on || g_pend.tm.on){ E->toast("Switching..."); return -1; }
    { const char *why = admit_check(ADMIT_OUTPUT_IDLE); if(why){ E->toast(why); return -1; } }
    if(mode < 0 || mode > SRCSWITCH_MODE_USB_AUDIO) return -1;
    if(session_source_mode() == SRCSWITCH_MODE_USB_DAC && mode != SRCSWITCH_MODE_USB_DAC && !exit_done && E->host_attached()){
        if(cfg_get_int("dac_safe_exit", 1) == 0){ E->toast("Unplug the USB cable first"); return -1; }
        return dac_exit_begin(mode);
    }
    if(mode == SRCSWITCH_MODE_USB_DEBUG) return dbg_enter(exit_done);
    if(usbdbg_state() != USBDBG_OFF) return dbg_leave_begin(mode);
    { const char *why = sd_switch_refusal(); if(why){ E->toast(why); return -1; } }
    if(mode != SRCSWITCH_MODE_LOCAL && mode != SRCSWITCH_MODE_USB_STORAGE && sd_host_owned()) return storage_leave(mode);
    if(needs_local(mode)) return via_local(mode, exit_done);
    if(mode == SRCSWITCH_MODE_LOCAL && outroute_route() == OUT_USB) return outroute_pick(OUT_INTERNAL, OUTROUTE_MODE);
    if(mode == SRCSWITCH_MODE_USB_AUDIO) return outroute_pick(OUT_USB, OUTROUTE_MODE);
    g_sd_target = -1;
    if(mode == SRCSWITCH_MODE_USB_STORAGE) return sd_export_begin();
    if(sd_host_owned()) return sd_return_begin(mode);
    int was_playing = E->playing();
    if(was_playing && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0){ E->toast("Player is busy - try again"); return -1; }
    if(session_source_mode() == SRCSWITCH_MODE_LOCAL && mode != SRCSWITCH_MODE_LOCAL){ player_view_t vs; ipc_get_view(&vs); if(vs.volume > 0) g_dac_carry_vol = vs.volume; }
    if(srcswitch_send_source(mode) < 0){ E->toast("Couldn't switch mode"); return -1; }
    session_set_source_mode(mode);
    if(mode == SRCSWITCH_MODE_LOCAL){ fprintf(stderr, "mode: local playback on the internal DAC (0666 6)\n"); fflush(stderr); }
    if(mode == SRCSWITCH_MODE_USB_DAC){ g_dac_enter_t = now_ms(); g_dac_vol_due = g_dac_enter_t + 4500; if(!g_dac_vol_due) g_dac_vol_due = 1; }
    if(mode == SRCSWITCH_MODE_LOCAL && was_playing) ipc_send_cmd(PLAYER_CMD_PLAYPAUSE);
    session_save_source(mode);
    return 0;
}
int ui_set_source_mode(int mode){
    int from = session_source_mode();
    int rc = source_switch(mode, 0);
    if(rc == 0){
        g_tx.active = 1; g_tx.failed = 0; g_tx.reason = NULL;
        g_tx.from = from; g_tx.to = mode; g_tx.mode = mode;
        g_tx.started_at = now_nonzero(now_ms());
    }
    return rc;
}

void srcswitch_init(void){ sd_init(); usbdbg_set_listener(dbg_event); usbdbg_init(); tm_stop(&g_reapply); }

void srcswitch_tick(void){
    outroute_tick();
    if(tm_due(&g_leave.tm)) leave_tick();
    if(tm_due(&g_bootusb)) bootusb_tick();
    if(tm_due(&g_pend.tm)) pending_tick();
    if(tm_due(&g_reapply)) reapply_tick();
    dac_exit_tick();
    usbdbg_tick();
    dbg_exit_tick();
    tx_tick();
    if(g_dac_vol_due && (int32_t)(now_ms() - g_dac_vol_due) >= 0){ g_dac_vol_due = 0; dac_volume_cb(); }
    sd_tick();
}
int srcswitch_dac_level_settled(void){ return session_source_mode() == SRCSWITCH_MODE_USB_DAC && elapsed(g_dac_enter_t) > 8000; }
