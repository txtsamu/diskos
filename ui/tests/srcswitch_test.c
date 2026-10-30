/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Host test for srcswitch.c as ONE module: the Source-mode switch and the Output-route switch together, including the two
 * detours that cross between them (USB Audio -> USB DAC / BT / Storage "via Local", and the ordered USB DAC exit). The
 * module, session.c, outseq.c and sdio.c are linked for real. The seams that are faked: ipc_send_cmd() records every frame
 * the Player would get (and can be made to fail), the rest of ipc.h (generation, state, ready) are knobs, cfg_*() is a
 * tiny in-memory store, and time is the number this test hands to the module through struct srcswitch_env. USB card and
 * SD handoff paths are pointed at a temp dir by the Makefile. Each scenario runs in a forked child so the module's
 * statics start fresh every time. */
#include "srcswitch.h"
#include "session.h"
#include "outseq.h"
#include "ipc.h"
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#if !defined(OUT_USB_CARD) || !defined(SD_EXPORT_MARKER) || !defined(OUT_USB_ANY_CARD)
#error "the Makefile points OUT_USB_CARD, OUT_USB_ANY_CARD and SD_EXPORT_MARKER at a temp dir"
#endif

/* ---- the seam: what the Player is sent ---- */
#define MAXF 64
static char g_log[MAXF][16];
static int g_n, g_fail_from = -1;        /* every send from this index on fails (and is still logged) */
static int g_fail_at = -1;               /* just this one send fails */
static int g_playing;                    /* the Player's play state; a pause toggle flips it */
static unsigned g_gen = 1;
#define PAUSE "0201000C0000"
int ipc_send_cmd(const char *frame){
    assert(g_n < MAXF);
    snprintf(g_log[g_n], sizeof g_log[0], "%s", frame);
    int idx = g_n++;
    if((g_fail_from >= 0 && idx >= g_fail_from) || idx == g_fail_at) return -1;
    if(!strcmp(frame, PAUSE)) g_playing = !g_playing;
    return 0;
}
unsigned ipc_generation(void){ return g_gen; }
int ipc_is_ready(void){ return 1; }
void ipc_get_state(track_state_t *out){
    memset(out, 0, sizeof *out);
    out->have_track = 1; snprintf(out->path, sizeof out->path, "/music/a.flac");
    out->position_ms = 0; out->volume = 40;
}
static struct { char key[24]; int v; } g_cfg[8]; static int g_ncfg;
int cfg_get_int(const char *key, int def){ for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)) return g_cfg[i].v; return def; }
int cfg_set_int(const char *key, int v){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)){ g_cfg[i].v = v; return 0; }
    assert(g_ncfg < 8); snprintf(g_cfg[g_ncfg].key, sizeof g_cfg[0].key, "%s", key); g_cfg[g_ncfg++].v = v; return 0;
}

/* ---- the environment table ---- */
static uint32_t g_now;
static int g_mounted = 1, g_exported, g_attached, g_gadget, g_scan, g_blocked;
static char g_toast[64][96]; static int g_ntoast;
static srcswitch_event_t g_ev[32]; static int g_nev;
static uint32_t env_now(void){ return g_now; }
static int env_mounted(void){ return g_mounted; }
static int env_exported(void){ return g_exported; }
static int env_host_confirmed(void){ return 0; }
static int env_attached(void){ return g_attached; }
static int env_gadget(void){ return g_gadget; }
static int env_guarded(void){ return 1; }
static int env_playing(void){ return g_playing; }
static int env_scan_active(void){ return g_scan; }
static void env_nothing(void){}
static void env_toast(const char *s){ assert(g_ntoast < 64); snprintf(g_toast[g_ntoast++], sizeof g_toast[0], "%s", s); }
static void env_volume(int v){ (void)v; }
static const char *env_blocked(void){ return g_blocked ? "Player is busy - try again" : NULL; }
static int env_seek(long ms){ (void)ms; return 0; }
static const struct srcswitch_env env = {
    .now = env_now, .media_mounted = env_mounted, .exported_to_host = env_exported, .host_confirmed = env_host_confirmed,
    .host_attached = env_attached, .dac_gadget_present = env_gadget, .player_guarded = env_guarded,
    .playing = env_playing, .scan_active = env_scan_active, .scan_if_library_empty = env_nothing, .art_cancel = env_nothing,
    .card_returned = env_nothing, .restart = env_nothing, .toast = env_toast, .set_volume = env_volume,
    .output_blocked = env_blocked, .seek_to = env_seek,
};
static void on_event(srcswitch_event_t ev){ assert(g_nev < 32); g_ev[g_nev++] = ev; }

#define R6 "0666000C0006"
#define R3 "0666000C0003"
#define G0 "0642000C0000"
#define G1 "0642000C0001"
#define G2 "0642000C0002"
#define G5 "0642000C0005"
#define W8 "0657000C0008"
#define W1 "0657000C0001"

/* ---- helpers ---- */
static void usb_card(int present){
    char p[160]; snprintf(p, sizeof p, "%s/usbid", OUT_USB_CARD);
    if(present){ int fd = open(p, O_WRONLY | O_CREAT, 0600); assert(fd >= 0); close(fd); } else unlink(p);
}
static void setup(void){
    char d[160]; snprintf(d, sizeof d, "%s", OUT_USB_CARD);
    char *slash = strrchr(d, '/'); if(slash){ *slash = 0; mkdir(d, 0700); *slash = '/'; }   /* the temp dir, then the card dir in it */
    mkdir(d, 0700); usb_card(0); unlink(SD_EXPORT_MARKER);
    g_now = 1000;
    srcswitch_set_env(&env); srcswitch_set_listener(on_event);
    srcswitch_set_launch_verdict(1);
    srcswitch_init();
    assert(ui_local_playback_allowed());
}
/* let the module's clock run: one srcswitch_tick() per 100 ms, as main.c's timer does */
static void run(int ms){ for(int t = 0; t < ms; t += 100){ g_now += 100; srcswitch_tick(); } }
static int idx(const char *frame, int from){ for(int i = from; i < g_n; i++) if(!strcmp(g_log[i], frame)) return i; return -1; }
static int count(const char *frame){ int c = 0; for(int i = 0; i < g_n; i++) if(!strcmp(g_log[i], frame)) c++; return c; }
static int toasted(const char *s){ for(int i = 0; i < g_ntoast; i++) if(!strcmp(g_toast[i], s)) return 1; return 0; }
static int evented(srcswitch_event_t e){ for(int i = 0; i < g_nev; i++) if(g_ev[i] == e) return 1; return 0; }
static void expect_frames(const char *what, const char *const *want, int n){
    if(g_n != n){ fprintf(stderr, "%s: %d frames sent, want %d:", what, g_n, n); for(int i = 0; i < g_n; i++) fprintf(stderr, " %s", g_log[i]); fprintf(stderr, "\n"); assert(0); }
    for(int i = 0; i < n; i++)
        if(strcmp(g_log[i], want[i])){ fprintf(stderr, "%s: frame %d is %s, want %s\n", what, i, g_log[i], want[i]); assert(0); }
}

/* ---- scenarios ---- */
/* USB Audio -> USB DAC: a direct 0642 2 from host role never worked, so the module goes to the internal DAC first (the Output
 * sequence, after the pause/quiet wait) and only 3 s after the pick switches the Source mode. One call; no re-entry. */
static void usb_audio_to_usb_dac_via_local(void){
    setup(); usb_card(1);
    session_set_route(OUT_USB);
    assert(ui_set_source_mode(1) == 0);
    assert(g_n == 0 && modes_output_busy() && modes_source_switch_busy() && srcswitch_via_local_pending());
    assert(toasted("Leaving USB Audio..."));
    assert(ui_set_source_mode(2) < 0);                     /* a second pick while it runs is refused */
    run(400); assert(g_n == 0);                             /* PCM must be proven quiet for 500 ms first */
    run(600);
    const char *internal[] = { R6, G0, W8 };
    expect_frames("route to internal", internal, 3);
    assert(session_route() == OUT_INTERNAL && !modes_output_busy());
    assert(modes_source_switch_busy() && session_source_mode() == 0);
    run(1900); assert(g_n == 3 && session_source_mode() == 0);   /* the picker's 3 s have not passed */
    run(500);
    const char *all[] = { R6, G0, W8, R6, G2, W1 };
    expect_frames("then USB DAC", all, 6);
    assert(session_source_mode() == 1 && !srcswitch_via_local_pending() && !modes_source_switch_busy());
    assert(evented(SRCSWITCH_EV_SETTLE_RESTART) && evented(SRCSWITCH_EV_CHANGED) && !evented(SRCSWITCH_EV_ABANDONED));
    assert(toasted("Switching to USB DAC") && cfg_get_int("src_mode", -1) == 1 && cfg_get_int("usb_audio_on", -1) == 0);
}
/* USB DAC with a computer on the cable: 0642 0 first, wait for the gadget to be gone (+1.5 s), only then the normal switch. */
static void usb_dac_ordered_exit(void){
    setup(); session_set_source_mode(1); g_attached = 1; g_gadget = 1;
    assert(ui_set_source_mode(0) == 0);
    const char *g0[] = { G0 };
    expect_frames("exit starts with 0642 0", g0, 1);
    assert(toasted("Leaving USB DAC... (up to 20 s)") && modes_source_switch_busy() && !modes_output_busy());
    assert(ui_set_source_mode(2) < 0 && g_n == 1);         /* refused while it waits */
    run(5000); assert(g_n == 1 && session_source_mode() == 1);   /* gadget still bound */
    g_gadget = 0; run(100);
    run(1300); assert(g_n == 1 && modes_source_switch_busy());   /* the 1.5 s margin */
    assert(!evented(SRCSWITCH_EV_SETTLE_RESTART));
    run(200);
    const char *all[] = { G0, R6, G0, W8 };
    expect_frames("then Local", all, 4);
    assert(session_source_mode() == 0 && !modes_source_switch_busy());
    assert(evented(SRCSWITCH_EV_SETTLE_RESTART) && !evented(SRCSWITCH_EV_ABANDONED));
}
static void usb_dac_exit_times_out(void){
    setup(); session_set_source_mode(1); g_attached = 1; g_gadget = 1;
    assert(ui_set_source_mode(0) == 0);
    run(30000); assert(modes_source_switch_busy());
    run(200);
    assert(!modes_source_switch_busy() && session_source_mode() == 1 && g_n == 1);
    assert(toasted("Couldn't leave USB DAC - unplug the cable first") && evented(SRCSWITCH_EV_ABANDONED));
}
/* USB DAC -> USB Storage with a computer attached: the ordered exit, then the pass through Local, then the SD export. The
 * pass through Local must not start the ordered exit a second time (the second lone 0642 0 would be it). */
static void usb_dac_to_storage_exit_then_local_then_export(void){
    setup(); session_set_source_mode(1); g_attached = 1; g_gadget = 1;
    assert(ui_set_source_mode(3) == 0);
    g_gadget = 0; run(2000);                                /* gadget gone, margin passed */
    const char *s1[] = { G0, R6, G0, W8 };
    expect_frames("exit + Local", s1, 4);
    assert(session_source_mode() == 0 && srcswitch_via_local_pending() && evented(SRCSWITCH_EV_SETTLE_RESTART));
    run(2400); assert(g_n == 4);                            /* 0.4 s of the 3 s were spent inside the run above */
    run(300);                                               /* Local landed and settled: the SD handoff starts */
    assert(session_source_mode() == 3 && ui_source_switch_pending() && !srcswitch_via_local_pending());
    run(200);
    const char *s2[] = { G0, R6, G0, W8, R6, G1, W8 };
    expect_frames("then the export", s2, 7);
    assert(count(G0) == 2 && toasted("Switching to USB storage"));
}
/* USB Audio -> USB Storage while playing: pause, route to internal, and ONLY THEN 0642 1. A direct 0642 1 from host role
 * never reached the computer and held SD access until a restart. */
static void host_role_never_goes_straight_to_storage(void){
    setup(); usb_card(1);
    g_playing = 1; session_set_route(OUT_USB);
    assert(ui_set_source_mode(3) == 0);
    assert(g_n == 1 && !strcmp(g_log[0], PAUSE) && !g_playing);
    run(5000);
    assert(ui_source_switch_pending());
    run(300);
    const char *want[] = { PAUSE, R6, G0, W8, R6, G1, W8 };
    expect_frames("storage from USB Audio", want, 7);
    assert(idx(G1, 0) > idx(G0, 0) && count(PAUSE) == 1);   /* never resumed into the jack on the way through */
    assert(session_route() == OUT_INTERNAL);
}
/* from Local nothing is in the way: the export frames are the only ones */
static void local_to_storage_is_direct(void){
    setup();
    assert(ui_set_source_mode(3) == 0 && g_n == 0 && session_source_mode() == 3);
    run(200);
    const char *want[] = { R6, G1, W8 };
    expect_frames("direct storage", want, 3);
}
/* a failing step in the pass through Local: the route moved but the tail failed, even the retry fails -> recovery pending;
 * the picked mode is dropped (nothing but the internal sequence is ever sent), and the recovery lands once sends work. */
static void failed_leave_abandons_and_recovers(void){
    setup(); usb_card(1);
    session_set_route(OUT_USB);
    g_fail_from = 1;                                        /* R6 lands, 0642 0 and everything after fail */
    assert(ui_set_source_mode(1) == 0);
    run(1000);
    assert(toasted("Output state uncertain - recovering") && modes_output_busy() && session_route() == OUT_INTERNAL);
    run(9500);
    assert(evented(SRCSWITCH_EV_ABANDONED) && toasted("Couldn't return to Local - mode not changed"));
    assert(!srcswitch_via_local_pending() && session_source_mode() == 0);
    assert(idx(G2, 0) < 0 && idx(W1, 0) < 0 && modes_output_busy());   /* never got as far as USB DAC; still recovering */
    g_fail_from = -1; run(600);
    assert(!modes_output_busy() && toasted("Internal DAC restored"));
    assert(session_route() == OUT_INTERNAL);
}
/* the output step failed once but the retry lands: internal DAC restored, and the pass through Local goes on */
static void failed_step_then_retry_lands(void){
    setup(); usb_card(1);
    session_set_route(OUT_USB);
    g_fail_at = 1;                                          /* 0666 6 lands, 0642 0 fails once */
    assert(ui_set_source_mode(2) == 0);
    run(700);
    const char *retried[] = { R6, G0, R6, G0, W8 };
    expect_frames("internal sequence retried in full", retried, 5);
    assert(toasted("Output switch failed - internal DAC restored") && !modes_output_busy() && session_route() == OUT_INTERNAL);
    run(3000);                                              /* the pick still goes through afterwards */
    assert(session_source_mode() == 2 && idx("0657000C0006", 0) > 0 && toasted("Switching to Bluetooth receiving"));
}
/* a pick refused after the pass through Local (library scan started meanwhile): the picker is told, nothing is sent */
static void deferred_pick_refused_is_abandoned(void){
    setup(); session_set_source_mode(1);                    /* USB DAC, no computer attached: only the pass through Local */
    assert(ui_set_source_mode(3) == 0 && session_source_mode() == 0 && srcswitch_via_local_pending());
    const char *l[] = { R6, G0, W8 };
    expect_frames("to Local", l, 3);
    g_scan = 1; run(3300);
    assert(evented(SRCSWITCH_EV_ABANDONED) && toasted("Let the library scan finish first"));
    assert(!srcswitch_via_local_pending() && !modes_source_switch_busy() && session_source_mode() == 0 && g_n == 3);
}
/* a direct source switch whose Player frames fail: refused, the Source mode does not move */
static void failed_source_frame_refuses(void){
    setup(); g_fail_from = 0;
    assert(ui_set_source_mode(1) < 0 && session_source_mode() == 0 && toasted("Couldn't switch mode"));
    g_fail_from = -1; g_n = 0; g_ntoast = 0;
    g_fail_from = 1;                                        /* the route frame lands, the gadget frame fails */
    assert(ui_set_source_mode(1) < 0 && session_source_mode() == 0 && toasted("Couldn't switch mode"));
    assert(session_route() == OUT_INTERNAL);
}
/* a Local -> USB Audio switch, the wait for the DAC, then the unplug watcher taking it back to the internal DAC */
static void usb_audio_enter_and_unplug(void){
    setup(); usb_card(1);
    assert(modes_output_mode_switch(OUT_USB) == 0);
    run(700);
    const char *to_usb[] = { R3, G5, W8 };
    expect_frames("to USB", to_usb, 3);
    assert(toasted("Powering USB DAC...") && session_route() == OUT_USB && modes_output_busy());
    run(1500);
    assert(toasted("USB DAC connected") && !modes_output_busy() && cfg_get_int("usb_audio_on", 0) == 1);
    usb_card(0); run(2500);                                 /* two missed polls, then pause -> quiet -> route */
    run(700);
    assert(toasted("USB DAC disconnected - internal DAC") && session_route() == OUT_INTERNAL);
    const char *back[] = { R3, G5, W8, R6, G0, W8 };
    expect_frames("back to internal", back, 6);
    assert(cfg_get_int("usb_audio_on", 1) == 0);
}
static void usb_audio_without_a_dac_falls_back(void){
    setup();
    assert(modes_output_mode_switch(OUT_USB) == 0);
    run(21000);
    assert(toasted("No USB DAC found - internal DAC restored") && session_route() == OUT_INTERNAL && !modes_output_busy());
    const char *want[] = { R3, G5, W8, R6, G0, W8 };
    expect_frames("fallback", want, 6);
}
static void boot_restore_replays_the_saved_mode(void){
    setup(); cfg_set_int("src_mode", 1);
    modes_output_boot_restore();
    run(14000); assert(g_n == 0);
    run(2000);
    const char *want[] = { R6, G2, W1 };
    expect_frames("restore", want, 3);
    assert(session_source_mode() == 1 && toasted("Restoring USB DAC mode..."));
}
static void blocked_output_switch_is_refused(void){
    setup(); g_blocked = 1;
    assert(modes_output_switch(OUT_SPDIF) < 0 && toasted("Player is busy - try again") && !modes_output_busy());
    g_blocked = 0;
    assert(modes_output_switch(OUT_SPDIF) == 0);
    run(700);
    const char *want[] = { "0666000C0004", W8 };
    expect_frames("spdif quick form", want, 2);
    assert(session_route() == OUT_SPDIF);
}

static void scenario(const char *name, void (*fn)(void)){
    fflush(stdout);
    pid_t p = fork();
    if(p == 0){ fn(); _exit(0); }
    int st = 0; waitpid(p, &st, 0);
    if(!WIFEXITED(st) || WEXITSTATUS(st) != 0){ fprintf(stderr, "FAIL %s\n", name); exit(1); }
    printf("  ok %s\n", name);
}
#define S(f) scenario(#f, f)
int main(void){
    S(usb_audio_to_usb_dac_via_local);
    S(usb_dac_ordered_exit);
    S(usb_dac_exit_times_out);
    S(usb_dac_to_storage_exit_then_local_then_export);
    S(host_role_never_goes_straight_to_storage);
    S(local_to_storage_is_direct);
    S(failed_leave_abandons_and_recovers);
    S(failed_step_then_retry_lands);
    S(deferred_pick_refused_is_abandoned);
    S(failed_source_frame_refuses);
    S(usb_audio_enter_and_unplug);
    S(usb_audio_without_a_dac_falls_back);
    S(boot_restore_replays_the_saved_mode);
    S(blocked_output_switch_is_refused);
    puts("srcswitch_test: ok");
    return 0;
}
