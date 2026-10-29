/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Host test for session.c: what the Player is on, how this process was entered, and what a startup restore may still do.
 * Seams are faked here: ipc_send_cmd() records frames (outseq.c is linked for real), ipc_generation() is a knob, cfg_*()
 * is a tiny in-memory store, and time is just the number handed to the module. Each scenario runs in a forked child so
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
int ipc_send_cmd(const char *frame){ assert(g_n < MAXF); snprintf(g_log[g_n++], sizeof g_log[0], "%s", frame); return 0; }
static unsigned g_gen = 1;
unsigned ipc_generation(void){ return g_gen; }
static struct { char key[24]; int v; } g_cfg[8]; static int g_ncfg;
int cfg_get_int(const char *key, int def){ for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)) return g_cfg[i].v; return def; }
int cfg_set_int(const char *key, int v){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)){ g_cfg[i].v = v; return 0; }
    assert(g_ncfg < 8); snprintf(g_cfg[g_ncfg].key, sizeof g_cfg[0].key, "%s", key); g_cfg[g_ncfg++].v = v; return 0;
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
    assert(session_local_init_send(1) == 0);          /* the boot Local init on the USB route */
    const char *usb[] = { R3, G5, W8 };
    expect_frames("init on USB", usb, 3);
    assert(session_local_init_send(1) == 1 && g_n == 3);   /* once per generation */
    g_gen = 2;                                        /* the Player restarted: route reads internal, init is due again */
    assert(!session_local_init_done(2));
    assert(session_local_init_send(2) == 0);
    const char *both[] = { R3, G5, W8, G0, R6, W8 };
    expect_frames("init after restart", both, 6);
}
static void cold_boot_entry(void){
    clear_env();
    assert(!new_process_starts());
    assert(session_entry() == SESSION_COLD_BOOT && session_autoplay_pause_allowed(1));
    assert(session_startup_route_pending());
    assert(session_local_init_allowed(g_gen));
}
static void relaunch_on_usb_audio_sends_nothing(void){
    cfg_set_int("usb_audio_on", 1);                   /* the saved preference that a boot restore acts on */
    old_process_exports(OUT_USB, 0);
    session_save_usb_audio(1);
    /* ---- exec: a fresh process, same Player ---- */
    assert(new_process_starts() == 1);
    assert(session_entry() == SESSION_RELAUNCH);
    assert(session_route() == OUT_USB && session_source_mode() == 0);
    assert(!session_local_init_allowed(g_gen));       /* 0642/0666/0657 would pull it off the DAC */
    assert(session_local_init_send(g_gen) == 1);
    assert(!session_startup_route_pending());         /* the startup route recovery is not this process's business */
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
    assert(!strcmp(getenv("DISKOS_THEME_RELOAD"), "1"));
    assert(!strcmp(getenv("DISKOS_RELOAD_ROUTE"), "1"));   /* the wire format between two builds */
    assert(!strcmp(getenv("DISKOS_RELOAD_SRC"), "2"));
    g_gen = 7;
    assert(new_process_starts() == 1);
    assert(getenv("DISKOS_THEME_RELOAD") == NULL && getenv("DISKOS_RELOAD_ROUTE") == NULL && getenv("DISKOS_RELOAD_SRC") == NULL);
    assert(session_route() == OUT_SPDIF && session_source_mode() == 2);
    assert(session_adopt(7) == 0);                    /* taken over once */
    assert(!session_local_init_allowed(7) && session_local_init_allowed(8));
    g_gen = 8;                                        /* the Player restarts later: normal rules again */
    assert(session_route() == OUT_INTERNAL);
    assert(session_local_init_send(8) == 0);
}
static void relaunch_ignores_default_and_bad_state(void){
    setenv("DISKOS_THEME_RELOAD", "1", 1); setenv("DISKOS_RELOAD_ROUTE", "0", 1); setenv("DISKOS_RELOAD_SRC", "0", 1);
    assert(!new_process_starts());                    /* nothing to keep */
    assert(session_entry() == SESSION_RELAUNCH && !session_autoplay_pause_allowed(1));
    assert(session_startup_route_pending() && session_local_init_allowed(g_gen));
    assert(getenv("DISKOS_THEME_RELOAD") == NULL);
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
static void cold_boot_allows_autoplay_pause(void){
    clear_env();
    session_init();
    assert(session_autoplay_pause_allowed(1));        /* a boot: Resume Playback starts the song, the pause keeps it quiet */
    assert(session_autoplay_pause_allowed(2));
}
static void cold_boot_with_resume_off_never_pauses(void){
    clear_env();
    session_init();
    assert(!session_autoplay_pause_allowed(0));       /* explicitly Off: the Player starts nothing, a headset's play or a key press is the user's */
    /* UNSET is not Off: the UI never sent 0684, so the Player keeps its own setting. On the development Disc that autoplays at
     * boot (the boot log shows the pause firing with memory_play absent from diskos.conf), so the pause must stay. */
    assert(session_autoplay_pause_allowed(cfg_get_int("memory_play", -1)));
    cfg_set_int("memory_play", 0);
    assert(!session_autoplay_pause_allowed(cfg_get_int("memory_play", -1)));
    cfg_set_int("memory_play", 2);
    assert(session_autoplay_pause_allowed(cfg_get_int("memory_play", -1)));
}
static void relaunch_does_not_rerun_autoplay_pause(void){
    old_process_exports(OUT_USB, 0);
    assert(new_process_starts() == 1);
    assert(!session_autoplay_pause_allowed(1));       /* even with Resume Playback on */
}
static void cold_boot_allows_chargemode_boot(void){
    clear_env();
    session_init();
    assert(session_chargemode_boot_allowed());        /* a charger woke the Disc: the charging screen may show */
}
static void relaunch_does_not_show_chargemode(void){
    old_process_exports(OUT_INTERNAL, 0);
    assert(!new_process_starts());                    /* even the default pair: the relaunch itself is what counts */
    assert(session_entry() == SESSION_RELAUNCH && !session_chargemode_boot_allowed());
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
    assert(!session_local_init_allowed(g_gen));       /* its 0642 0 would close the gadget */
    assert(session_local_init_send(g_gen) == 1);
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
    assert(session_source_mode() == 0 && session_local_init_allowed(g_gen));
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
    assert(session_source_mode() == 0 && session_local_init_allowed(g_gen));
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
static void startup_route_checked_once(void){
    assert(session_startup_route_pending());
    session_startup_route_settled();
    assert(!session_startup_route_pending());
}
static void local_init_marks_and_failure_retries(void){
    session_local_init_mark(4);
    assert(session_local_init_done(4) && !session_local_init_done(5));
    assert(session_local_init_send(4) == 1 && g_n == 0);     /* moot for this generation */
}

static void run(const char *name, void (*fn)(void)){
    fflush(stdout);
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
    RUN(player_restart_clears_route);
    RUN(player_restart_reruns_local_init);
    RUN(cold_boot_entry);
    RUN(relaunch_on_usb_audio_sends_nothing);
    RUN(relaunch_on_usb_dac_keeps_source);
    RUN(relaunch_adopts_env);
    RUN(relaunch_ignores_default_and_bad_state);
    RUN(relaunch_rejects_out_of_range);
    RUN(failed_exec_cancels_handoff);
    RUN(cold_boot_allows_autoplay_pause);
    RUN(cold_boot_with_resume_off_never_pauses);
    RUN(relaunch_does_not_rerun_autoplay_pause);
    RUN(cold_boot_allows_chargemode_boot);
    RUN(relaunch_does_not_show_chargemode);
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
    RUN(startup_route_checked_once);
    RUN(local_init_marks_and_failure_retries);
    puts("session_test: ok");
    return 0;
}
