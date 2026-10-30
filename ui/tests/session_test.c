/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Host test for session.c: what the Player is on, how this process was entered, what a startup restore may still do, and
 * when the startup Local init goes out. Seams are faked here: ipc_send_cmd() records frames (outseq.c is linked for real),
 * ipc_generation() is a knob, cfg_*() is a tiny in-memory store, the facts and system probes of session_startup_env are
 * knobs with call counters, and time is just the number handed to the module. Each scenario runs in a forked child so
 * the module's statics start fresh (a cold boot) every time. */
#include "session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- fakes ---- */
#define MAXF 32
static char g_log[MAXF][48];
static int g_n;
static int g_send_rc = 0;   /* what the Player's command channel answers: 0 queued, -1 refused */
int ipc_send_cmd(const char *frame){ assert(g_n < MAXF); snprintf(g_log[g_n++], sizeof g_log[0], "%s", frame); return g_send_rc; }
static unsigned g_gen = 1;
unsigned ipc_generation(void){ return g_gen; }
static struct { char key[24]; int v; } g_cfg[8]; static int g_ncfg;
int cfg_get_int(const char *key, int def){ for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)) return g_cfg[i].v; return def; }
int cfg_set_int(const char *key, int v){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)){ g_cfg[i].v = v; return 0; }
    assert(g_ncfg < 8); snprintf(g_cfg[g_ncfg].key, sizeof g_cfg[0].key, "%s", key); g_cfg[g_ncfg++].v = v; return 0;
}

/* ---- the startup environment: facts and probes as knobs, calls counted ---- */
static struct {
    int admitted, play_starting, playing, have_track, fw_needs_init;
    unsigned rx;                                       /* cumulative frames received from the Player */
    int player_old, saved_bt, sink, route_analog_rc;   /* probe answers */
    int n_player_old, n_saved_bt, n_sink, n_route_analog;
    int clear_bt_on_analog;                            /* the real route_analog() drops the Bluetooth route when it lands */
} F;
static int f_admitted(void){ return F.admitted; }
static unsigned f_rx(void){ return F.rx; }
static int f_play_starting(void){ return F.play_starting; }
static int f_playing(void){ return F.playing; }
static int f_have_track(void){ return F.have_track; }
static int f_fw(void){ return F.fw_needs_init; }
static int f_player_old(void){ F.n_player_old++; return F.player_old; }
static int f_saved_bt(void){ F.n_saved_bt++; return F.saved_bt; }
static int f_sink(const char *mac){ (void)mac; F.n_sink++; return F.sink; }
static int f_route_analog(void){
    F.n_route_analog++;
    if(F.route_analog_rc == 0 && F.clear_bt_on_analog) session_clear_bt_mac();
    return F.route_analog_rc;
}
static const struct session_startup_env k_env = {
    .admitted = f_admitted, .rx_frames = f_rx, .play_starting = f_play_starting, .playing = f_playing,
    .have_track = f_have_track, .fw_needs_init = f_fw, .player_old = f_player_old, .saved_bt_route = f_saved_bt,
    .connected_sink = f_sink, .route_analog = f_route_analog,
};
/* a V2.57 Disc just booted: admitted, nothing playing, the Player has not answered yet, its saved output is analog */
static void startup_env_reset(void){
    memset(&F, 0, sizeof F);
    F.admitted = 1; F.fw_needs_init = 1; F.rx = 3;
    session_startup_set_env(&k_env);
}
#define TICK(t) session_startup_tick(t)
/* The Player answers its first probe at `t0`: the settle window starts there. Returns after asserting the probe-then-answer. */
static void player_answers(uint32_t t0){
    assert(TICK(t0 - 1) == SESSION_STARTUP_PROBE);
    F.rx++;
    assert(TICK(t0) == SESSION_STARTUP_WAIT);
}

#define R6 "0666000C0006"
#define R3 "0666000C0003"
#define G0 "0642000C0000"
#define G5 "0642000C0005"
#define W8 "0657000C0008"

static void expect_frames(const char *what, const char *const *want, int n){
    if(g_n != n){ fprintf(stderr, "%s: %d frames sent, want %d\n", what, g_n, n); assert(0); }
    for(int i = 0; i < n; i++)
        if(strcmp(g_log[i], want[i])){ fprintf(stderr, "%s: frame %d is %s, want %s\n", what, i, g_log[i], want[i]); assert(0); }
}

/* what a theme change leaves behind for the next process */
static void old_process_exports(int route, int src){
    session_set_source_mode(src); session_set_route(route);
    session_handoff_export();
}
/* the new process, as main() starts it: init, then adoption once the Player generation is known */
static int new_process_starts(void){ session_init(); return session_adopt(ipc_generation()); }
/* env of a bare relaunch with no state worth keeping */
static void clear_env(void){ unsetenv("DISKOS_THEME_RELOAD"); unsetenv("DISKOS_RELOAD_ROUTE"); unsetenv("DISKOS_RELOAD_SRC"); }

/* ---- scenarios ---- */
static void route_and_source(void){
    assert(session_route() == OUT_INTERNAL && session_source_mode() == 0);
    session_set_route(OUT_SPDIF); session_set_source_mode(2);
    assert(session_route() == OUT_SPDIF && session_source_mode() == 2);
    assert(cfg_get_int("spdif", 0) == 1);             /* the Settings toggle mirrors the real route */
    session_set_route(OUT_USB);
    assert(cfg_get_int("spdif", 0) == 0);
}
static void player_restart_clears_route(void){
    session_set_route(OUT_USB); session_set_source_mode(0);
    assert(session_route() == OUT_USB);
    g_gen++;                                          /* /ui re-attached: a new Player */
    assert(session_route() == OUT_INTERNAL);          /* it is back on the internal DAC */
    session_set_route(OUT_SPDIF); assert(cfg_get_int("spdif", 0) == 1);
    g_gen++; assert(session_route() == OUT_INTERNAL && cfg_get_int("spdif", 1) == 0);   /* and the toggle follows */
}
static void player_restart_reruns_local_init(void){
    session_set_route(OUT_USB);
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);      /* the boot Local init on the USB route */
    const char *usb[] = { R3, G5, W8 };
    expect_frames("init on USB", usb, 3);
    assert(TICK(10500) == SESSION_STARTUP_WAIT && g_n == 3);   /* once per generation */
    g_gen = 2;                                        /* the Player restarted: route reads internal, init is due again */
    player_answers(20000);
    assert(TICK(29000) == SESSION_STARTUP_WAIT);
    const char *both[] = { R3, G5, W8, G0, R6, W8 };
    expect_frames("init after restart", both, 6);
}
static void cold_boot_entry(void){
    clear_env();
    assert(!new_process_starts());
    assert(session_entry() == SESSION_COLD_BOOT);     /* no handoff: the Resume Playback pause and the charging screen are on */
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);      /* and the Local init is due */
    const char *init[] = { G0, R6, W8 };
    expect_frames("cold boot init", init, 3);
}
static void relaunch_on_usb_audio_sends_nothing(void){
    cfg_set_int("usb_audio_on", 1);                   /* the saved preference that a boot restore acts on */
    old_process_exports(OUT_USB, 0);
    session_save_usb_audio(1);
    /* ---- exec: a fresh process, same Player ---- */
    assert(new_process_starts() == 1);
    assert(session_entry() == SESSION_RELAUNCH);
    assert(session_route() == OUT_USB && session_source_mode() == 0);
    F.rx = 3;
    for(uint32_t t = 0; t <= 60000; t += 500){        /* the startup timer runs a minute while the Player answers */
        F.rx++;
        assert(TICK(t) == SESSION_STARTUP_WAIT);      /* 0642/0666/0657 would pull it off the DAC: no probe, no init */
    }
    assert(F.n_saved_bt == 0 && F.n_sink == 0 && F.n_route_analog == 0);   /* the startup route recovery is not this process's business */
    session_restore_arm(1000);
    int src = -1;
    for(uint32_t t = 1000; t <= 1000 + 130000; t += 1000){
        session_restore_t r = session_restore_poll(t, 0, 0, &src);
        assert(r == SESSION_RESTORE_WAIT || r == SESSION_RESTORE_NONE);   /* never SOURCE / USB_AUDIO */
    }
    assert(g_n == 0);                                 /* not one frame to the Player */
}
static void relaunch_on_usb_dac_keeps_source(void){
    cfg_set_int("src_mode", 1);
    old_process_exports(OUT_INTERNAL, 1);
    assert(new_process_starts() == 1);
    assert(session_source_mode() == 1 && session_route() == OUT_INTERNAL);
    session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(14999, 0, 0, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE);   /* already off Local */
    assert(session_restore_poll(16000, 0, 0, &src) == SESSION_RESTORE_NONE && g_n == 0);
}
static void relaunch_adopts_env(void){
    old_process_exports(OUT_SPDIF, 2);
    assert(getenv("DISKOS_THEME_RELOAD") && !strcmp(getenv("DISKOS_THEME_RELOAD"), "1"));
    assert(!strcmp(getenv("DISKOS_RELOAD_ROUTE"), "1"));   /* the wire format between two builds */
    assert(!strcmp(getenv("DISKOS_RELOAD_SRC"), "2"));
    g_gen = 7;
    assert(new_process_starts() == 1);
    assert(getenv("DISKOS_THEME_RELOAD") == NULL && getenv("DISKOS_RELOAD_ROUTE") == NULL && getenv("DISKOS_RELOAD_SRC") == NULL);
    assert(session_route() == OUT_SPDIF && session_source_mode() == 2);
    assert(session_adopt(7) == 0);                    /* taken over once */
    F.rx = 3;
    for(uint32_t t = 0; t <= 20000; t += 500){ F.rx++; assert(TICK(t) == SESSION_STARTUP_WAIT); }   /* the adopted generation gets no Local init */
    assert(g_n == 0);
    g_gen = 8;                                        /* the Player restarts later: normal rules again */
    assert(session_route() == OUT_INTERNAL);
    player_answers(30000);
    assert(TICK(39000) == SESSION_STARTUP_WAIT);
    const char *init[] = { G0, R6, W8 };
    expect_frames("init after the adopted generation", init, 3);
}
static void relaunch_ignores_default_and_bad_state(void){
    setenv("DISKOS_THEME_RELOAD", "1", 1); setenv("DISKOS_RELOAD_ROUTE", "0", 1); setenv("DISKOS_RELOAD_SRC", "0", 1);
    assert(!new_process_starts());                    /* nothing to keep */
    assert(session_entry() == SESSION_RELAUNCH);      /* even the default pair: the relaunch itself is what counts (no Resume Playback pause, no charging screen) */
    assert(getenv("DISKOS_THEME_RELOAD") == NULL);
    player_answers(1000);                             /* nothing was adopted: the Player is on the default, so the Local init still runs */
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    const char *init[] = { G0, R6, W8 };
    expect_frames("init on a default-pair relaunch", init, 3);
}
static void relaunch_rejects_out_of_range(void){
    setenv("DISKOS_THEME_RELOAD", "1", 1); setenv("DISKOS_RELOAD_ROUTE", "3", 1); setenv("DISKOS_RELOAD_SRC", "0", 1);
    assert(!new_process_starts() && session_route() == OUT_INTERNAL);
}
static void failed_exec_cancels_handoff(void){
    old_process_exports(OUT_USB, 0);
    session_handoff_cancel();
    assert(getenv("DISKOS_THEME_RELOAD") == NULL && getenv("DISKOS_RELOAD_ROUTE") == NULL && getenv("DISKOS_RELOAD_SRC") == NULL);
    assert(session_route() == OUT_USB);               /* this process is still the one running */
}
/* The Resume Playback pause and the charging screen are decided in main.c from session_entry() alone; what they rely on is this. */
static void entry_is_cold_boot_without_a_handoff(void){
    clear_env();
    session_init();
    assert(session_entry() == SESSION_COLD_BOOT);
}
static void entry_is_relaunch_for_any_handoff(void){
    old_process_exports(OUT_INTERNAL, 0);
    assert(!new_process_starts());                    /* the default pair carries nothing to adopt ... */
    assert(session_entry() == SESSION_RELAUNCH);      /* ... but the relaunch itself is what counts */
}
static void entry_stays_relaunch_after_adoption(void){
    old_process_exports(OUT_USB, 0);
    assert(new_process_starts() == 1);
    assert(session_entry() == SESSION_RELAUNCH);
}
static void cold_boot_restores_usb_audio_once(void){
    clear_env();
    cfg_set_int("usb_audio_on", 1);
    assert(!new_process_starts());
    session_restore_arm(5000);
    int src = -1;
    assert(session_restore_poll(5000, 0, 0, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(19999, 0, 0, &src) == SESSION_RESTORE_WAIT);       /* 14.999 s: still settling */
    assert(session_restore_poll(20000, 1, 0, &src) == SESSION_RESTORE_WAIT);       /* an output switch is running */
    assert(session_restore_poll(20500, 0, 1, &src) == SESSION_RESTORE_WAIT);       /* output blocked (e.g. a song just started) */
    assert(session_restore_poll(21000, 0, 0, &src) == SESSION_RESTORE_USB_AUDIO);
    assert(session_restore_poll(22000, 0, 0, &src) == SESSION_RESTORE_NONE);       /* exactly once */
    assert(session_restore_poll(60000, 0, 0, &src) == SESSION_RESTORE_NONE);
    assert(g_n == 0);                                 /* the caller runs the switch; the module only decides */
}
static void cold_boot_restores_source_once(void){
    clear_env();
    session_save_source(1);
    assert(cfg_get_int("usb_audio_on", 9) == 0);      /* a pick ends a remembered USB Audio output */
    session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(15000, 1, 0, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(15000, 0, 1, &src) == SESSION_RESTORE_SOURCE && src == 1);   /* blocked only holds back USB Audio */
    assert(session_restore_poll(16000, 0, 0, &src) == SESSION_RESTORE_NONE);
}
/* a hot-load / UI restart while the Disc is already a USB DAC: the cold-boot entry, but the Player is on the saved mode */
static void start_while_in_usb_dac_sends_no_frames(void){
    clear_env();
    cfg_set_int("src_mode", 1);
    assert(!new_process_starts());                    /* no handoff: a cold-boot entry */
    assert(session_source_mode() == 0);               /* the fresh process believes Local ... */
    assert(session_adopt_detected(ipc_generation(), 1) == 1);   /* ... until the gadget state says otherwise */
    assert(session_source_mode() == 1);
    for(uint32_t t = 0; t <= 60000; t += 500){ F.rx++; assert(TICK(t) == SESSION_STARTUP_WAIT); }   /* its 0642 0 would close the gadget */
    session_restore_arm(0);
    int src = -1;
    for(uint32_t t = 0; t <= 130000; t += 1000){
        session_restore_t r = session_restore_poll(t, 0, 0, &src);
        assert(r == SESSION_RESTORE_WAIT || r == SESSION_RESTORE_NONE);   /* never SOURCE: no 0666 6 / 0642 2 / 0657 1 */
    }
    assert(g_n == 0);                                 /* the stream is not touched */
    assert(cfg_get_int("src_mode", 0) == 1);          /* and the saved pick stays */
}
static void cold_boot_with_player_still_local_restores_once(void){
    clear_env();
    cfg_set_int("src_mode", 1);
    assert(!new_process_starts());
    assert(session_adopt_detected(ipc_generation(), 0) == 0);   /* gadget not bound: the Player has not switched */
    assert(session_source_mode() == 0);
    player_answers(1000);                             /* still Local: the Local init is due */
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 3);
    session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(14999, 0, 0, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_SOURCE && src == 1);
    assert(session_restore_poll(16000, 0, 0, &src) == SESSION_RESTORE_NONE);   /* once */
}
static void adopt_detected_only_what_was_saved(void){
    clear_env();
    cfg_set_int("src_mode", 2);                       /* BT Receiving is gadget-invisible: never adopted */
    assert(!new_process_starts());
    assert(session_adopt_detected(g_gen, 1) == 0 && session_adopt_detected(g_gen, 0) == 0);
    cfg_set_int("src_mode", 0);                       /* the gadget is up but Local is the pick: not ours to claim */
    assert(session_adopt_detected(g_gen, 1) == 0);
    cfg_set_int("src_mode", 1);
    assert(session_adopt_detected(g_gen, 3) == 0);    /* USB Storage is another path (the SD marker) */
    assert(session_source_mode() == 0);               /* nothing adopted: the Local init is still due */
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 3);
}
static void adopt_detected_leaves_a_relaunch_alone(void){
    cfg_set_int("src_mode", 1);
    old_process_exports(OUT_INTERNAL, 0);
    assert(!new_process_starts());                    /* default pair: relaunch, nothing adopted from the handoff */
    assert(session_adopt_detected(g_gen, 1) == 0);    /* the relaunch handoff is the only authority there */
    session_set_source_mode(2);
    assert(session_adopt_detected(g_gen, 1) == 0 && session_source_mode() == 2);
}
/* leaving USB Storage restarts the Disc: the pick made there must be what the next boot restores, not the one before it */
static void leaving_storage_for_local_cancels_old_restore(void){
    clear_env();
    session_save_source(2);                           /* was in BT Receiving, then opened Storage */
    session_save_source(3);                           /* Storage itself is never remembered */
    assert(session_saved_source() == 2);
    session_save_source(0);                           /* picked Local to leave: saved before the restart */
    assert(!new_process_starts());                    /* the restarted UI */
    session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE && g_n == 0);   /* no switch back into BT */
}
static void leaving_storage_for_usb_dac_restores_it(void){
    clear_env();
    session_save_source(0);                           /* Local before Storage; USB DAC picked to leave it */
    session_save_source(1);
    assert(!new_process_starts());
    session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_SOURCE && src == 1);
}
static void restore_gives_up(void){
    clear_env();
    session_save_source(2); session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(121000, 0, 0, &src) == SESSION_RESTORE_NONE);       /* never acts on a stale boot */
    session_save_source(0); session_restore_arm(0);
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE);        /* nothing saved */
}
static void restore_stands_down_when_already_moved(void){
    clear_env();
    cfg_set_int("usb_audio_on", 1); session_restore_arm(0);
    int src = -1;
    session_set_route(OUT_USB);                       /* the user (or an earlier restore) got there first */
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE);
    cfg_set_int("usb_audio_on", 1); session_set_route(OUT_INTERNAL); session_set_source_mode(1); session_restore_arm(0);
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE);
}
/* ---- the startup Local init decision ---- */
static const char *const INIT_INTERNAL[] = { G0, R6, W8 };
static void expect_init(const char *what){ expect_frames(what, INIT_INTERNAL, 3); }

static void startup_probes_until_the_player_answers(void){
    for(uint32_t t = 0; t < 60000; t += 500) assert(TICK(t) == SESSION_STARTUP_PROBE);   /* an idle Player is silent: ask, and ask again */
    assert(g_n == 0);                                 /* a probe is not a command; nothing went out */
}
static void startup_settle_window_is_nine_seconds_from_the_first_answer(void){
    player_answers(1000);                             /* the answer arrives: the window starts here, nothing sent yet */
    assert(TICK(1000 + 8999) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(1000 + 9000) == SESSION_STARTUP_WAIT);   /* sent on this tick, then waits (the one-shot is spent) */
    expect_init("init at the end of the window");
    assert(TICK(1000 + 9500) == SESSION_STARTUP_WAIT && g_n == 3);   /* never twice */
    assert(TICK(1000 + 60000) == SESSION_STARTUP_WAIT && g_n == 3);
}
static void startup_settle_window_wraps_with_the_tick_counter(void){
    player_answers(0xFFFFF000u);
    assert(TICK(0xFFFFF000u + 8999) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(0xFFFFF000u + 9000) == SESSION_STARTUP_WAIT);     /* crosses 2^32 */
    expect_init("init across the tick wrap");
}
static void startup_rx_base_is_per_generation(void){
    F.rx = 500;                                       /* the count is cumulative across Player generations */
    assert(TICK(0) == SESSION_STARTUP_PROBE);         /* gen 1 has not answered yet, however many frames came before */
    F.rx = 501;
    assert(TICK(500) == SESSION_STARTUP_WAIT);        /* answered: window starts at 500 */
    g_gen = 2;                                        /* the Player restarted before the window ran out */
    assert(TICK(9500) == SESSION_STARTUP_PROBE && g_n == 0);   /* gen 2's snapshot is 501: silent again, even though gen 1's window has passed */
    F.rx = 502;
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0);   /* gen 2's own window starts here */
    assert(TICK(10000 + 8999) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(10000 + 9000) == SESSION_STARTUP_WAIT);
    expect_init("init for the second generation only");
}
static void startup_waits_while_not_admitted(void){
    F.admitted = 0;                                   /* output switch running, SD held, not Local source, no launch verdict */
    for(uint32_t t = 0; t < 30000; t += 500) assert(TICK(t) == SESSION_STARTUP_WAIT);   /* no probe either */
    F.admitted = 1;
    assert(TICK(30000) == SESSION_STARTUP_PROBE);     /* and the clock starts from the first allowed tick */
}
static void startup_waits_while_a_play_is_starting(void){
    player_answers(1000);
    F.play_starting = 1;                              /* a play was sent and its track has not arrived, or an audiobook is active */
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(20000) == SESSION_STARTUP_WAIT && g_n == 0);
    F.play_starting = 0;
    assert(TICK(20500) == SESSION_STARTUP_WAIT);
    expect_init("init after the play started");
}
static void startup_firmware_without_the_init_is_done_at_the_window(void){
    F.fw_needs_init = 0;                              /* V2.09/V2.28 do not drift: 0666 could only disturb them */
    player_answers(1000);
    assert(TICK(1000 + 8999) == SESSION_STARTUP_WAIT);
    assert(TICK(1000 + 9000) == SESSION_STARTUP_DONE);   /* the timer can be dropped */
    assert(g_n == 0);
}
static void startup_playing_holds_the_pending_route_recovery(void){
    player_answers(1000);
    F.playing = 1;                                    /* the recovery may change the route: not under a stream */
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0 && F.n_saved_bt == 0);
    F.playing = 0;
    assert(TICK(10500) == SESSION_STARTUP_WAIT && F.n_saved_bt == 1);   /* the stream ended: the recovery looks, then the init */
    expect_init("init once the stream ended");
}
static void startup_skips_the_init_when_already_playing(void){
    session_startup_route_settled();                  /* the recovery is out of the way (an intentional route, or already looked) */
    player_answers(1000);
    F.playing = 1;                                    /* the user started a song inside the window: 0666 would close the stream */
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0);
    F.playing = 0;
    assert(TICK(11000) == SESSION_STARTUP_WAIT && g_n == 0);   /* counted as initialised: not sent after it stops either */
    assert(TICK(60000) == SESSION_STARTUP_WAIT && g_n == 0);
}
static void startup_skips_the_init_when_a_play_was_already_sent(void){
    player_answers(1000);
    F.playing = 1; assert(TICK(2000) == SESSION_STARTUP_WAIT);   /* playback observed: the recovery's evidence that the track came */
    F.playing = 0;
    session_note_transport_sent(g_gen, 3000);         /* ... then it stopped; the play path sent its own preamble */
    assert(TICK(10000) == SESSION_STARTUP_WAIT);      /* still inside the 10 s the last play evidence takes to age out */
    assert(TICK(13000) == SESSION_STARTUP_WAIT);
    assert(g_n == 0);                                 /* never 0666 on top of a stream the play path set up */
    assert(TICK(60000) == SESSION_STARTUP_WAIT && g_n == 0);
}
static void startup_play_sent_in_an_older_generation_does_not_count(void){
    session_note_transport_sent(g_gen, 0);
    g_gen = 2;                                        /* the Player restarted since: the new one needs its init */
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    expect_init("init for a new generation after an old play");
}
static void startup_busy_playback_holds_the_recovery(void){
    session_note_transport_sent(g_gen, 0);
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0);   /* a play was sent but no track has been seen: busy (route recovery pending) */
    F.have_track = 1;                                 /* the track arrived (paused or not): still busy */
    assert(TICK(20000) == SESSION_STARTUP_WAIT && g_n == 0);
    F.have_track = 0;                                 /* unloaded, and the evidence is old */
    assert(TICK(30000) == SESSION_STARTUP_WAIT && g_n == 0);   /* ... a play was sent this generation: its init is moot */
    assert(F.n_saved_bt == 1);                        /* the recovery ran once the playback went quiet */
}
static void startup_bt_route_marks_the_generation_without_frames(void){
    session_set_bt_mac("AA:BB:CC:DD:EE:FF");          /* a route landed (route restored or chosen): never 0666 under an A2DP sink */
    F.sink = 1;                                       /* the auto-route poll owns this speaker */
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(g_n == 0 && F.n_saved_bt == 0 && F.n_route_analog == 0);   /* a live route is newer than the saved one: no saved-route probe */
    assert(TICK(10500) == SESSION_STARTUP_WAIT && g_n == 0);
}
static void startup_bt_route_on_a_firmware_without_the_init_is_done(void){
    F.fw_needs_init = 0;
    session_set_bt_mac("AA:BB:CC:DD:EE:FF");
    F.sink = 1;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_DONE && g_n == 0);
}
static void startup_bt_route_without_a_sink_goes_back_to_analog(void){
    session_set_bt_mac("AA:BB:CC:DD:EE:FF");          /* the previous UI left a route; the speaker is gone */
    F.sink = 0; F.clear_bt_on_analog = 1;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_route_analog == 1 && !session_bt_routed());
    assert(g_n == 0);                                 /* route_analog is the adapter's (its Output sequence is its own); the init is moot */
    assert(TICK(10500) == SESSION_STARTUP_WAIT && F.n_route_analog == 1 && g_n == 0);
}
static void startup_saved_bt_route_with_no_sink_is_recovered_once(void){
    F.saved_bt = 1; F.sink = 0;                       /* the Player's saved OUT_DEV is a sink, this UI has not routed one */
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_saved_bt == 1 && F.n_sink == 1 && F.n_route_analog == 1 && g_n == 0);
    assert(TICK(10500) == SESSION_STARTUP_WAIT && F.n_route_analog == 1 && F.n_saved_bt == 1);   /* once per process */
}
static void startup_saved_analog_or_unknown_route_runs_the_init(void){
    F.saved_bt = -1;                                  /* absent, locked, malformed: do not infer a sink */
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_sink == 0 && F.n_route_analog == 0);
    expect_init("init when the saved route is unknown");
}
static void startup_spdif_intent_skips_the_recovery(void){
    cfg_set_int("spdif", 1); session_set_route(OUT_SPDIF);   /* the Settings route is SPDIF, not the internal DAC the recovery guards */
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_saved_bt == 0 && F.n_sink == 0 && F.n_route_analog == 0);
    const char *spdif[] = { G0, "0666000C0004", W8 };
    expect_frames("init on the SPDIF route", spdif, 3);
}
static void startup_inconclusive_sink_probe_waits_five_seconds(void){
    F.saved_bt = 1; F.sink = -1;                      /* bluealsa did not answer in time */
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT && F.n_sink == 1);
    assert(TICK(10000 + 4999) == SESSION_STARTUP_WAIT && F.n_sink == 1);   /* no second shell probe inside the retry window */
    assert(TICK(10000 + 5000) == SESSION_STARTUP_WAIT && F.n_sink == 2);
    F.sink = 0;
    assert(TICK(15000 + 5000) == SESSION_STARTUP_WAIT && F.n_sink == 3 && F.n_route_analog == 1);
    assert(g_n == 0);
}
static void startup_failed_analog_route_is_retried_every_tick(void){
    F.saved_bt = 1; F.sink = 0; F.route_analog_rc = -1;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT && F.n_route_analog == 1);
    assert(TICK(10500) == SESSION_STARTUP_WAIT && F.n_route_analog == 2);   /* no retry window for a refused route */
    F.route_analog_rc = 0;
    assert(TICK(11000) == SESSION_STARTUP_WAIT && F.n_route_analog == 3);
    assert(TICK(11500) == SESSION_STARTUP_WAIT && F.n_route_analog == 3 && g_n == 0);
}
static void startup_old_player_recovers_before_it_answers(void){
    F.player_old = 1; F.saved_bt = 1; F.sink = 0;     /* up long enough that it need not answer the track probe first */
    assert(TICK(0) == SESSION_STARTUP_WAIT);          /* recovered on the very first tick: no probe needed */
    assert(F.n_player_old == 1 && F.n_route_analog == 1 && g_n == 0);
    assert(TICK(500) == SESSION_STARTUP_WAIT && F.n_player_old == 1);   /* its age is read once per generation */
}
static void startup_old_player_on_a_firmware_without_the_init_is_done(void){
    F.player_old = 1; F.saved_bt = 1; F.sink = 0; F.fw_needs_init = 0;
    assert(TICK(0) == SESSION_STARTUP_DONE && F.n_route_analog == 1);
}
static void startup_old_player_waits_for_playback_before_recovering(void){
    F.player_old = 1; F.saved_bt = 1; F.sink = 0;
    F.playing = 1;                                    /* the route change would pause the stream */
    assert(TICK(0) == SESSION_STARTUP_PROBE && F.n_route_analog == 0);
}
static void startup_refused_init_frame_is_retried(void){
    player_answers(1000);
    g_send_rc = -1;                                   /* the command channel refuses the first frame */
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(g_n == 1);                                 /* the first send failing stops the init */
    g_n = 0; g_send_rc = 0;
    assert(TICK(10500) == SESSION_STARTUP_WAIT);      /* not marked: the whole init goes again on the next tick */
    expect_init("the retried init");
    assert(TICK(11000) == SESSION_STARTUP_WAIT && g_n == 3);   /* and only then is it spent */
}
static void startup_intentional_route_supersedes_the_recovery(void){
    F.saved_bt = 1; F.sink = 0;
    session_startup_route_settled();                  /* ui_route_bt landed a route */
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_saved_bt == 0 && F.n_sink == 0 && F.n_route_analog == 0);
    expect_init("init after an intentional route");
}

static void bt_route_is_a_session_fact(void){
    assert(!session_bt_routed() && !strcmp(session_bt_mac(), ""));     /* a cold boot is on the analog route */
    session_set_bt_mac("AA:BB:CC:DD:EE:FF");
    assert(session_bt_routed() && !strcmp(session_bt_mac(), "AA:BB:CC:DD:EE:FF"));
    session_set_bt_mac("11:22:33:44:55:66");                               /* a new sink replaces the old one */
    assert(!strcmp(session_bt_mac(), "11:22:33:44:55:66"));
    session_set_route(OUT_SPDIF);                                          /* the Output route does not touch it */
    assert(session_bt_routed());
    session_clear_bt_mac();
    assert(!session_bt_routed() && !strcmp(session_bt_mac(), ""));
    session_set_bt_mac(NULL);                                              /* NULL reads as no route */
    assert(!session_bt_routed());
    session_set_bt_mac("AA:BB:CC:DD:EE:FF:00:11:22:33");                   /* never overruns its 20-byte buffer */
    assert(strlen(session_bt_mac()) == 19);
}

static void run(const char *name, void (*fn)(void)){
    fflush(stdout);
    startup_env_reset();
    pid_t p = fork();
    assert(p >= 0);
    if(p == 0){ fn(); _exit(0); }
    int st = 0; waitpid(p, &st, 0);
    if(!WIFEXITED(st) || WEXITSTATUS(st) != 0){ fprintf(stderr, "session_test FAILED: %s\n", name); exit(1); }
    printf("  ok %s\n", name);
}
#define RUN(f) run(#f, f)

int main(void){
    RUN(route_and_source);
    RUN(bt_route_is_a_session_fact);
    RUN(player_restart_clears_route);
    RUN(player_restart_reruns_local_init);
    RUN(cold_boot_entry);
    RUN(relaunch_on_usb_audio_sends_nothing);
    RUN(relaunch_on_usb_dac_keeps_source);
    RUN(relaunch_adopts_env);
    RUN(relaunch_ignores_default_and_bad_state);
    RUN(relaunch_rejects_out_of_range);
    RUN(failed_exec_cancels_handoff);
    RUN(entry_is_cold_boot_without_a_handoff);
    RUN(entry_is_relaunch_for_any_handoff);
    RUN(entry_stays_relaunch_after_adoption);
    RUN(cold_boot_restores_usb_audio_once);
    RUN(cold_boot_restores_source_once);
    RUN(start_while_in_usb_dac_sends_no_frames);
    RUN(cold_boot_with_player_still_local_restores_once);
    RUN(adopt_detected_only_what_was_saved);
    RUN(adopt_detected_leaves_a_relaunch_alone);
    RUN(leaving_storage_for_local_cancels_old_restore);
    RUN(leaving_storage_for_usb_dac_restores_it);
    RUN(restore_gives_up);
    RUN(restore_stands_down_when_already_moved);
    RUN(startup_probes_until_the_player_answers);
    RUN(startup_settle_window_is_nine_seconds_from_the_first_answer);
    RUN(startup_settle_window_wraps_with_the_tick_counter);
    RUN(startup_rx_base_is_per_generation);
    RUN(startup_waits_while_not_admitted);
    RUN(startup_waits_while_a_play_is_starting);
    RUN(startup_firmware_without_the_init_is_done_at_the_window);
    RUN(startup_playing_holds_the_pending_route_recovery);
    RUN(startup_skips_the_init_when_already_playing);
    RUN(startup_skips_the_init_when_a_play_was_already_sent);
    RUN(startup_play_sent_in_an_older_generation_does_not_count);
    RUN(startup_busy_playback_holds_the_recovery);
    RUN(startup_bt_route_marks_the_generation_without_frames);
    RUN(startup_bt_route_on_a_firmware_without_the_init_is_done);
    RUN(startup_bt_route_without_a_sink_goes_back_to_analog);
    RUN(startup_saved_bt_route_with_no_sink_is_recovered_once);
    RUN(startup_saved_analog_or_unknown_route_runs_the_init);
    RUN(startup_spdif_intent_skips_the_recovery);
    RUN(startup_inconclusive_sink_probe_waits_five_seconds);
    RUN(startup_failed_analog_route_is_retried_every_tick);
    RUN(startup_old_player_recovers_before_it_answers);
    RUN(startup_old_player_on_a_firmware_without_the_init_is_done);
    RUN(startup_old_player_waits_for_playback_before_recovering);
    RUN(startup_refused_init_frame_is_retried);
    RUN(startup_intentional_route_supersedes_the_recovery);
    puts("session_test: ok");
    return 0;
}
