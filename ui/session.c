/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "session.h"
#include "config.h"
#include "ipc.h"
#include "outseq.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

/* See session.h. Hot-load note: the three DISKOS_* variables are the wire format between the old and the new process of
 * a relaunch, so their names and number format must not change (a hot-loaded build has to interoperate with itself). */
#define ENV_RELOAD "DISKOS_THEME_RELOAD"
#define ENV_ROUTE  "DISKOS_RELOAD_ROUTE"
#define ENV_SRC    "DISKOS_RELOAD_SRC"

#define NO_GEN 0xFFFFFFFFu   /* a Player generation that never exists */

/* ---- what the Player is on ---- */
static int g_route = OUT_INTERNAL;
static unsigned g_route_gen = 0;
static _Atomic int g_source_mode = 0;   /* written on the UI thread, read on the coldplug worker: seq_cst on one word is enough */

void session_set_route(int route){
    g_route = route; g_route_gen = ipc_generation();
    if(cfg_get_int("spdif", 0) != (route == OUT_SPDIF)) cfg_set_int("spdif", route == OUT_SPDIF);   /* the Settings toggle mirrors the real route, never a wish */
}
int session_route(void){
    if(g_route != OUT_INTERNAL && g_route_gen != ipc_generation()) session_set_route(OUT_INTERNAL);   /* a restarted Player is back on the internal DAC */
    return g_route;
}
int  session_source_mode(void){ return g_source_mode; }
void session_set_source_mode(int mode){ g_source_mode = mode; }

/* ---- how this process was entered ---- */
static session_entry_t g_entry = SESSION_COLD_BOOT;
static struct { int valid, route, src; } g_hand;   /* the state a relaunch was handed, until adopted */
static unsigned g_adopted_gen = NO_GEN;            /* Player generation whose route/mode was adopted: no Local init for it */
static int g_startup_route_checked = 0;            /* the saved-BT-route recovery has looked (or an intentional route superseded it) */

void session_init(void){
    if(!getenv(ENV_RELOAD)) return;   /* a cold boot: the Player's state is whatever diskOS boots it into */
    const char *rr = getenv(ENV_ROUTE), *rs = getenv(ENV_SRC);
    int route = rr ? atoi(rr) : 0, src = rs ? atoi(rs) : 0;
    g_entry = SESSION_RELAUNCH;
    /* the default pair carries nothing to keep; an out-of-range pair is not ours */
    g_hand.valid = (route != OUT_INTERNAL || src != 0) && route >= OUT_INTERNAL && route <= OUT_USB && src >= 0 && src <= 2;
    g_hand.route = route; g_hand.src = src;
    unsetenv(ENV_RELOAD); unsetenv(ENV_ROUTE); unsetenv(ENV_SRC);
}
session_entry_t session_entry(void){ return g_entry; }

/* Adopt at startup, not when /ui is re-attached after a Player restart: that only happens on a restart, which a plain
 * relaunch never is, so the route was never kept (and the saved-preference restore then re-ran the whole USB Audio
 * switch, restarting the output and the song). ipc_generation() only moves on a Player restart, so it is stable. */
int session_adopt(unsigned gen){
    if(!g_hand.valid) return 0;
    g_hand.valid = 0;
    session_set_source_mode(g_hand.src);
    session_set_route(g_hand.route);
    g_adopted_gen = gen;
    g_startup_route_checked = 1;
    return 1;
}

void session_handoff_export(void){
    char v[16];
    setenv(ENV_RELOAD, "1", 1);
    snprintf(v, sizeof v, "%d", session_route());       setenv(ENV_ROUTE, v, 1);
    snprintf(v, sizeof v, "%d", session_source_mode()); setenv(ENV_SRC, v, 1);
}
void session_handoff_cancel(void){ unsetenv(ENV_RELOAD); unsetenv(ENV_ROUTE); unsetenv(ENV_SRC); }

/* ---- what may a startup restore still do ---- */
int session_autoplay_pause_allowed(void){ return g_entry == SESSION_COLD_BOOT; }

#define PREF_SRC "src_mode"
#define PREF_USB "usb_audio_on"
int  session_saved_source(void){ return cfg_get_int(PREF_SRC, 0); }
int  session_saved_usb_audio(void){ return cfg_get_int(PREF_USB, 0) == 1; }
void session_save_source(int mode){ cfg_set_int(PREF_SRC, mode); cfg_set_int(PREF_USB, 0); }
void session_save_usb_audio(int on){ cfg_set_int(PREF_USB, on ? 1 : 0); }

static struct { int armed; uint32_t t0; } g_restore;
void session_restore_arm(uint32_t now_ms){ g_restore.armed = 1; g_restore.t0 = now_ms; }
session_restore_t session_restore_poll(uint32_t now_ms, int busy, int blocked, int *src){
    if(!g_restore.armed) return SESSION_RESTORE_NONE;
    uint32_t age = now_ms - g_restore.t0;
    int want = session_saved_source(), usb = session_saved_usb_audio();
    if((want != 1 && want != 2 && !usb) || age > 120000){ g_restore.armed = 0; return SESSION_RESTORE_NONE; }
    if(age < 15000) return SESSION_RESTORE_WAIT;   /* past the Player's ~9 s settle and the boot Local init */
    if(session_source_mode() != 0){ g_restore.armed = 0; return SESSION_RESTORE_NONE; }   /* already left Local (the user, or a restore) */
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

int  session_startup_route_pending(void){ return !g_startup_route_checked; }
void session_startup_route_settled(void){ g_startup_route_checked = 1; }

static unsigned g_local_init_gen = NO_GEN;   /* generation whose Local init is done (sent, or moot) */
int  session_local_init_allowed(unsigned gen){ return gen != g_adopted_gen; }
int  session_local_init_done(unsigned gen){ return gen == g_local_init_gen; }
void session_local_init_mark(unsigned gen){ g_local_init_gen = gen; }
int  session_local_init_send(unsigned gen){
    if(!session_local_init_allowed(gen) || session_local_init_done(gen)) return 1;
    if(outseq_local_boot((outseq_route_t)session_route()) < 0) return -1;   /* the first send failing stops it: the caller retries */
    g_local_init_gen = gen;   /* only once ALL the frames were actually sent */
    return 0;
}
