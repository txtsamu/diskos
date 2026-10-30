/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "session.h"
#include "config.h"
#include "ipc.h"
#include "outseq.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define ENV_RELOAD "DISKOS_THEME_RELOAD"
#define ENV_ROUTE  "DISKOS_RELOAD_ROUTE"
#define ENV_SRC    "DISKOS_RELOAD_SRC"

#define NO_GEN 0xFFFFFFFFu

static int g_route = OUT_INTERNAL;
static unsigned g_route_gen = 0;
static _Atomic int g_source_mode = 0;

void session_set_route(int route){
    g_route = route; g_route_gen = ipc_generation();
    if(cfg_get_int("spdif", 0) != (route == OUT_SPDIF)) cfg_set_int("spdif", route == OUT_SPDIF);
}
int session_route(void){
    if(g_route != OUT_INTERNAL && g_route_gen != ipc_generation()) session_set_route(OUT_INTERNAL);
    return g_route;
}
int  session_source_mode(void){ return g_source_mode; }
void session_set_source_mode(int mode){ g_source_mode = mode; }

static char g_bt_mac[20] = "";
const char *session_bt_mac(void){ return g_bt_mac; }
int  session_bt_routed(void){ return g_bt_mac[0] != 0; }
void session_set_bt_mac(const char *mac){ snprintf(g_bt_mac, sizeof g_bt_mac, "%s", mac ? mac : ""); }
void session_clear_bt_mac(void){ g_bt_mac[0] = 0; }

static session_entry_t g_entry = SESSION_COLD_BOOT;
static struct { int valid, route, src; } g_hand;
static unsigned g_adopted_gen = NO_GEN;
static int g_startup_route_checked = 0;

void session_init(void){
    if(!getenv(ENV_RELOAD)) return;
    const char *rr = getenv(ENV_ROUTE), *rs = getenv(ENV_SRC);
    int route = rr ? atoi(rr) : 0, src = rs ? atoi(rs) : 0;
    g_entry = SESSION_RELAUNCH;
    g_hand.valid = (route != OUT_INTERNAL || src != 0) && route >= OUT_INTERNAL && route <= OUT_USB && src >= 0 && src <= 2;
    g_hand.route = route; g_hand.src = src;
    unsetenv(ENV_RELOAD); unsetenv(ENV_ROUTE); unsetenv(ENV_SRC);
}
session_entry_t session_entry(void){ return g_entry; }

int session_adopt(unsigned gen){
    if(!g_hand.valid) return 0;
    g_hand.valid = 0;
    session_set_source_mode(g_hand.src);
    session_set_route(g_hand.route);
    g_adopted_gen = gen;
    g_startup_route_checked = 1;
    return 1;
}

int session_adopt_detected(unsigned gen, int detected){
    if(g_entry != SESSION_COLD_BOOT || session_source_mode() != 0) return 0;
    if(detected != 1 || session_saved_source() != 1) return 0;
    session_set_source_mode(1);
    g_adopted_gen = gen;
    return 1;
}

void session_handoff_export(void){
    char v[16];
    setenv(ENV_RELOAD, "1", 1);
    snprintf(v, sizeof v, "%d", session_route());       setenv(ENV_ROUTE, v, 1);
    snprintf(v, sizeof v, "%d", session_source_mode()); setenv(ENV_SRC, v, 1);
}
void session_handoff_cancel(void){ unsetenv(ENV_RELOAD); unsetenv(ENV_ROUTE); unsetenv(ENV_SRC); }

#define PREF_SRC "src_mode"
#define PREF_USB "usb_audio_on"
int  session_saved_source(void){ return cfg_get_int(PREF_SRC, 0); }
int  session_saved_usb_audio(void){ return cfg_get_int(PREF_USB, 0) == 1; }
void session_save_source(int mode){ if(mode < 0 || mode > 2) return; cfg_set_int(PREF_SRC, mode); cfg_set_int(PREF_USB, 0); }
void session_save_usb_audio(int on){ cfg_set_int(PREF_USB, on ? 1 : 0); }

static struct { int armed; uint32_t t0; } g_restore;
void session_restore_arm(uint32_t now_ms){ g_restore.armed = 1; g_restore.t0 = now_ms; }
session_restore_t session_restore_poll(uint32_t now_ms, int busy, int blocked, int *src){
    if(!g_restore.armed) return SESSION_RESTORE_NONE;
    uint32_t age = now_ms - g_restore.t0;
    int want = session_saved_source(), usb = session_saved_usb_audio();
    if((want != 1 && want != 2 && !usb) || age > 120000){ g_restore.armed = 0; return SESSION_RESTORE_NONE; }
    if(age < 15000) return SESSION_RESTORE_WAIT;
    if(session_source_mode() != 0){ g_restore.armed = 0; return SESSION_RESTORE_NONE; }
    if(want == 1 || want == 2){
        if(busy) return SESSION_RESTORE_WAIT;
        g_restore.armed = 0; if(src) *src = want;
        return SESSION_RESTORE_SOURCE;
    }
    if(session_route() == OUT_USB){ g_restore.armed = 0; return SESSION_RESTORE_NONE; }
    if(busy || blocked) return SESSION_RESTORE_WAIT;
    g_restore.armed = 0;
    return SESSION_RESTORE_USB_AUDIO;
}

static const struct session_startup_env *SE;
void session_startup_set_env(const struct session_startup_env *env){ SE = env; }

static int startup_route_pending(void){ return !g_startup_route_checked; }
void session_startup_route_settled(void){ g_startup_route_checked = 1; }

static unsigned g_local_init_gen = NO_GEN;
static int  local_init_allowed(unsigned gen){ return gen != g_adopted_gen; }
static int  local_init_done(unsigned gen){ return gen == g_local_init_gen; }
static void local_init_mark(unsigned gen){ g_local_init_gen = gen; }
static int  local_init_send(unsigned gen){
    if(!local_init_allowed(gen) || local_init_done(gen)) return 1;
    if(outseq_local_boot((outseq_route_t)session_route()) < 0) return -1;
    g_local_init_gen = gen;
    return 0;
}

static unsigned g_play_sent_gen = NO_GEN;
static uint32_t g_play_sent_at;
static unsigned g_play_track_seen_gen = NO_GEN;
void session_note_transport_sent(unsigned gen, uint32_t now_ms){ g_play_sent_gen = gen; g_play_sent_at = now_ms; }

static int startup_playback_busy(unsigned gen, uint32_t now){
    if(SE->playing()) g_play_track_seen_gen = gen;
    if(SE->play_starting() || SE->playing()) return 1;
    if(g_play_sent_gen != gen) return 0;
    int have_track = SE->have_track();
    if(have_track) g_play_track_seen_gen = gen;
    return have_track || g_play_track_seen_gen != gen || now - g_play_sent_at < 10000;
}

static unsigned g_sink_retry_gen = NO_GEN;
static uint32_t g_sink_retry_at = 0;
static int startup_route_reconcile(unsigned gen, uint32_t now){
    if(!startup_route_pending()) return 0;
    if(session_route() != OUT_INTERNAL || cfg_get_int("spdif", 0)){
        session_startup_route_settled(); return 0;
    }
    if(g_sink_retry_gen == gen && now - g_sink_retry_at < 5000) return -1;
    if(!session_bt_routed()){
        int saved_bt = SE->saved_bt_route();
        if(saved_bt <= 0){ session_startup_route_settled(); return 0; }
    }
    int sink = SE->connected_sink(session_bt_mac());
    if(sink < 0){ g_sink_retry_gen = gen; g_sink_retry_at = now; return -1; }
    if(sink){ session_startup_route_settled(); return 1; }
    if(SE->route_analog() < 0) return -1;
    session_startup_route_settled();
    return 1;
}

session_startup_t session_startup_tick(uint32_t now){
    static unsigned wait_gen = NO_GEN;
    static uint32_t wait_start = 0;
    static unsigned rx_gen  = NO_GEN;
    static unsigned rx_base = 0;
    static unsigned age_gen = NO_GEN;
    static int player_old = 0;
    if(!SE->admitted()) return SESSION_STARTUP_WAIT;
    unsigned gen = ipc_generation();
    if(!local_init_allowed(gen)) return SESSION_STARTUP_WAIT;
    if(age_gen != gen){ age_gen = gen; player_old = startup_route_pending() && SE->player_old(); }
    if(startup_route_pending() && player_old && !startup_playback_busy(gen, now)){
        int startup = startup_route_reconcile(gen, now);
        if(startup > 0) local_init_mark(gen);
        if(startup > 0 && !SE->fw_needs_init()) return SESSION_STARTUP_DONE;
    }
    if(SE->play_starting()) return SESSION_STARTUP_WAIT;
    if(local_init_done(gen)) return SESSION_STARTUP_WAIT;
    unsigned rx = SE->rx_frames();
    if(rx_gen != gen){ rx_gen = gen; rx_base = rx; }
    if(rx == rx_base) return SESSION_STARTUP_PROBE;
    if(wait_gen != gen){ wait_gen = gen; wait_start = now; return SESSION_STARTUP_WAIT; }
    if(now - wait_start < 9000) return SESSION_STARTUP_WAIT;
    if(startup_route_pending() && startup_playback_busy(gen, now)) return SESSION_STARTUP_WAIT;
    int startup = startup_route_reconcile(gen, now);
    if(startup < 0) return SESSION_STARTUP_WAIT;
    if(session_bt_routed()){ local_init_mark(gen); return SE->fw_needs_init() ? SESSION_STARTUP_WAIT : SESSION_STARTUP_DONE; }
    if(!SE->fw_needs_init()) return SESSION_STARTUP_DONE;
    if(startup > 0){ local_init_mark(gen); return SESSION_STARTUP_WAIT; }
    if(g_play_sent_gen == gen){
        local_init_mark(gen);
        fprintf(stderr,"v2.40 workmode: local-init skipped, play already sent (gen %u)\n", gen); fflush(stderr);
        return SESSION_STARTUP_WAIT;
    }
    if(SE->playing()){
        local_init_mark(gen);
        fprintf(stderr,"v2.40 workmode: local-init skipped, already playing (gen %u)\n", gen); fflush(stderr);
        return SESSION_STARTUP_WAIT;
    }
    if(local_init_send(gen) != 0) return SESSION_STARTUP_WAIT;
    fprintf(stderr,"v2.40 workmode: local-init sent once (gen %u)\n", gen); fflush(stderr);
    return SESSION_STARTUP_WAIT;
}
