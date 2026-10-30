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

/* The Bluetooth sink Local playback is routed to: "" = none (the analog route above). Kept for validation and dedup of
 * a route request, and read by every admission rule that must never send 0666 under an A2DP sink. A restarted Player
 * is back on the analog route, so the caller clears it on a Player reconnect. */
static char g_bt_mac[20] = "";
const char *session_bt_mac(void){ return g_bt_mac; }
int  session_bt_routed(void){ return g_bt_mac[0] != 0; }
void session_set_bt_mac(const char *mac){ snprintf(g_bt_mac, sizeof g_bt_mac, "%s", mac ? mac : ""); }
void session_clear_bt_mac(void){ g_bt_mac[0] = 0; }

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

int session_adopt_detected(unsigned gen, int detected){
    if(g_entry != SESSION_COLD_BOOT || session_source_mode() != 0) return 0;
    if(detected != 1 || session_saved_source() != 1) return 0;   /* the Player is on what the restore would switch it to */
    session_set_source_mode(1);
    g_adopted_gen = gen;   /* the Local init's 0642 0 would close the gadget it is on */
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
void session_save_source(int mode){ if(mode < 0 || mode > 2) return; cfg_set_int(PREF_SRC, mode); cfg_set_int(PREF_USB, 0); }   /* USB Storage (3) is never remembered */
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

/* ---- the startup Local init: when does it go out ---- */
/* The mechanism (RE'd from mq_player_v240):
 *  - v2.40's player needs 0666(local route)+0657(LOCALPLAYER work-mode) set at runtime or local
 *    start_local fails ("work mode NULL"/NO_WORK_MODE -> g_fiio_local null). Protocol 0657=8 maps to
 *    internal work-mode 1 (LOCALPLAYER); 0 = NO_WORK_MODE.
 *  - WHY an early 0657 doesn't stick: an async mode-control thread (player 0x4dca64) keeps running AFTER
 *    the /player command server goes live. It runs ~50x100ms polls + up to ~2s of 1s sleeps (~7s worst
 *    case), calling close_player() (resets work-mode to 0) and re-initing per late hardware state - which
 *    clobbers any 0657 sent inside that window. So we wait past it (~8s guard; we use 9s) before sending.
 *  - WHY there is no a607 oracle on /ui: the 0607 query handler writes its a607 reply to a SEPARATE
 *    fd-based transport (mask 0, raw write to fd 0x83e5f4), never the /ui mqueue - so we can never see it.
 *    There is NO deterministic /ui frame meaning "late init finished". a1=needs playback, a2=server
 *    liveness only, a714=volume. The best anchor is: solicit a2 with 02020008, then a fixed settle delay.
 *  - IDLE PLAYER IS SILENT: a fresh cold-boot player emits NOTHING unsolicited, so ipc_rx_frames() stays
 *    0 forever unless we poke it. We therefore actively send 02020008 every tick until it answers (a2),
 *    anchor the settle timer on that first response. The V2.40/V2.57 local init then sends 0666+0657
 *    once per player generation; the saved-BT-route check runs only once per UI process. Both require
 *    Local source + analog output intent (no BT route), and wait while a play is pending. */
static const struct session_startup_env *SE;
void session_startup_set_env(const struct session_startup_env *env){ SE = env; }

/* The saved-Bluetooth-route recovery looks once per process (g_startup_route_checked above). */
static int startup_route_pending(void){ return !g_startup_route_checked; }
void session_startup_route_settled(void){ g_startup_route_checked = 1; }

/* The one-shot: once per Player generation, never for the generation a relaunch adopted. */
static unsigned g_local_init_gen = NO_GEN;   /* generation whose Local init is done (sent, or moot) */
static int  local_init_allowed(unsigned gen){ return gen != g_adopted_gen; }
static int  local_init_done(unsigned gen){ return gen == g_local_init_gen; }
static void local_init_mark(unsigned gen){ g_local_init_gen = gen; }
/* 0 sent, 1 nothing to do, -1 a frame was refused (not marked: the caller retries on its next tick) */
static int  local_init_send(unsigned gen){
    if(!local_init_allowed(gen) || local_init_done(gen)) return 1;
    if(outseq_local_boot((outseq_route_t)session_route()) < 0) return -1;   /* the first send failing stops it: the caller retries */
    g_local_init_gen = gen;   /* only once ALL the frames were actually sent */
    return 0;
}

/* Generation and time of the last transport command sent (or playback observed): that generation's one-shot is moot. */
static unsigned g_play_sent_gen = NO_GEN;
static uint32_t g_play_sent_at;
static unsigned g_play_track_seen_gen = NO_GEN;
void session_note_transport_sent(unsigned gen, uint32_t now_ms){ g_play_sent_gen = gen; g_play_sent_at = now_ms; }

/* A sent play can precede its first track reply. A loaded track may still be paused,
 * so wait for the player to unload it and for the last play evidence to age out. */
static int startup_playback_busy(unsigned gen, uint32_t now){
    if(SE->playing()) g_play_track_seen_gen = gen;
    if(SE->play_starting() || SE->playing()) return 1;
    if(g_play_sent_gen != gen) return 0;
    int have_track = SE->have_track();
    if(have_track) g_play_track_seen_gen = gen;
    return have_track || g_play_track_seen_gen != gen || now - g_play_sent_at < 10000;
}

/* 1 = this generation needs no other local init, 0 = normal init may continue,
 * -1 = evidence or a route send is pending. An inconclusive sink probe waits
 * at least five seconds before another shell probe on this generation. */
static unsigned g_sink_retry_gen = NO_GEN;
static uint32_t g_sink_retry_at = 0;
static int startup_route_reconcile(unsigned gen, uint32_t now){
    if(!startup_route_pending()) return 0;
    if(session_route() != OUT_INTERNAL || cfg_get_int("spdif", 0)){
        session_startup_route_settled(); return 0;
    }
    if(g_sink_retry_gen == gen && now - g_sink_retry_at < 5000) return -1;
    /* A route from this live player is newer than its bulk-saved OUT_DEV. */
    if(!session_bt_routed()){
        int saved_bt = SE->saved_bt_route();
        if(saved_bt <= 0){ session_startup_route_settled(); return 0; }
    }
    int sink = SE->connected_sink(session_bt_mac());
    if(sink < 0){ g_sink_retry_gen = gen; g_sink_retry_at = now; return -1; }
    if(sink){ session_startup_route_settled(); return 1; }   /* the auto-route poll owns this speaker */
    if(SE->route_analog() < 0) return -1;
    session_startup_route_settled();
    return 1;   /* route_analog logs the single recovery line */
}

session_startup_t session_startup_tick(uint32_t now){
    static unsigned wait_gen = NO_GEN;   /* generation whose settle timer is running */
    static uint32_t wait_start = 0;
    static unsigned rx_gen  = NO_GEN;    /* generation the rx_base snapshot belongs to */
    static unsigned rx_base = 0;         /* cumulative rx_frames at the start of THIS generation */
    static unsigned age_gen = NO_GEN;
    static int player_old = 0;
    /* These commands drive the player and its USB/card ownership, so they need the same admission as any
     * other card access: a qualified launch verdict, SD not held, no decoder fault. A pending verdict waits
     * (the timer retries), it is never a reason to go ahead. Also: only while Local source, and no output
     * switch owns 0666/0642/0657 right now. */
    if(!SE->admitted()) return SESSION_STARTUP_WAIT;
    unsigned gen = ipc_generation();
    if(!local_init_allowed(gen)) return SESSION_STARTUP_WAIT;   /* a theme re-launch kept the player's route: 0642/0666/0657 here would pull it off a USB DAC */
    /* Read the live player's age once per generation. An old idle player need not answer
     * the track probe before a route recovery; a young player still uses the reply/settle path. */
    if(age_gen != gen){ age_gen = gen; player_old = startup_route_pending() && SE->player_old(); }
    if(startup_route_pending() && player_old && !startup_playback_busy(gen, now)){
        int startup = startup_route_reconcile(gen, now);
        if(startup > 0) local_init_mark(gen);
        if(startup > 0 && !SE->fw_needs_init()) return SESSION_STARTUP_DONE;
    }
    if(SE->play_starting()) return SESSION_STARTUP_WAIT;
    if(local_init_done(gen)) return SESSION_STARTUP_WAIT;
    unsigned rx = SE->rx_frames();            /* CUMULATIVE across generations -> anchor per-generation via rx_base */
    if(rx_gen != gen){ rx_gen = gen; rx_base = rx; }           /* new generation -> snapshot the count */
    if(rx == rx_base) return SESSION_STARTUP_PROBE;            /* SOLICIT: THIS generation's player hasn't answered yet (idle player is silent) */
    if(wait_gen != gen){ wait_gen = gen; wait_start = now; return SESSION_STARTUP_WAIT; }   /* first response this gen -> start settle */
    if(now - wait_start < 9000) return SESSION_STARTUP_WAIT;   /* settle past the ~7s mode-control-thread window */
    if(startup_route_pending() && startup_playback_busy(gen, now)) return SESSION_STARTUP_WAIT;
    int startup = startup_route_reconcile(gen, now);
    if(startup < 0) return SESSION_STARTUP_WAIT;
    if(session_bt_routed()){ local_init_mark(gen); return SE->fw_needs_init() ? SESSION_STARTUP_WAIT : SESSION_STARTUP_DONE; }
    if(!SE->fw_needs_init()) return SESSION_STARTUP_DONE;
    if(startup > 0){ local_init_mark(gen); return SESSION_STARTUP_WAIT; }
    if(g_play_sent_gen == gen){                                /* a play was started this generation (the play path sends its own preamble while stopped): playing is only inferred, so never risk 0666 mid-stream */
        local_init_mark(gen);
        fprintf(stderr,"v2.40 workmode: local-init skipped, play already sent (gen %u)\n", gen); fflush(stderr);
        return SESSION_STARTUP_WAIT;
    }
    /* Already PLAYING locally (the user started a song inside the settle window): the player has a working local
     * route and work-mode and the card isn't exported - exactly the state this one-shot exists to establish - and
     * 0666 would close the stream mid-song (seen live on V2.57). Count this generation as initialised. */
    if(SE->playing()){
        local_init_mark(gen);
        fprintf(stderr,"v2.40 workmode: local-init skipped, already playing (gen %u)\n", gen); fflush(stderr);
        return SESSION_STARTUP_WAIT;
    }
    /* USB gadget selector -> Local/no-export: tears down any mass-storage the stock player
     * auto-bound from a persisted WORK_MODE=4 (recovers hand-deploys + belt-and-suspenders for
     * the flashed boot gate). Sent past the settle window so the async mode-control thread can't
     * clobber it. If the send fails, DON'T mark this generation done - retry next tick rather than
     * leave the card exported. */
    if(local_init_send(gen) != 0) return SESSION_STARTUP_WAIT;   /* outseq_local_boot on the current route: 0642 gadget (0 = none, 5 = USB host), 0666 local route (6 internal, 4 SPDIF, 3 USB audio: (re)inits the local device, creates g_fiio_local), 0657 8 LOCALPLAYER work-mode (fixes NO_WORK_MODE); marks this generation initialised only once ALL three were actually sent */
    fprintf(stderr,"v2.40 workmode: local-init sent once (gen %u)\n", gen); fflush(stderr);
    return SESSION_STARTUP_WAIT;
}
