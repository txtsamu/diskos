/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "srcswitch.h"
#include "sdio.h"
#include "session.h"
#include "outseq.h"
#include "outroute.h"
#include "admit.h"
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

#define MAXF 64
static char g_log[MAXF][16];
static int g_n, g_fail_from = -1;
static int g_fail_at = -1;
static int g_playing;
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

static uint32_t g_now;
static int g_mounted = 1, g_exported, g_attached, g_gadget, g_scan;
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
const char *admit_check(admit_what_t what){ (void)what; return NULL; }
static int env_seek(long ms){ (void)ms; return 0; }
static const struct srcswitch_env env = {
    .now = env_now, .host_attached = env_attached, .dac_gadget_present = env_gadget,
    .playing = env_playing, .toast = env_toast, .set_volume = env_volume,
    .seek_to = env_seek,
};
static const struct sd_env sd_env = {
    .now = env_now, .media_mounted = env_mounted, .exported_to_host = env_exported, .host_confirmed = env_host_confirmed,
    .player_guarded = env_guarded, .playing = env_playing, .scan_active = env_scan_active, .scan_if_library_empty = env_nothing,
    .art_cancel = env_nothing, .card_returned = env_nothing, .restart = env_nothing, .toast = env_toast,
    .send_source = srcswitch_send_source,
};
static const char *env_switch_blocked(void){ return NULL; }
static const struct outroute_env renv = { .now = env_now, .playing = env_playing, .switch_blocked = env_switch_blocked, .toast = env_toast, .seek_to = env_seek };
static void on_event(srcswitch_event_t ev){ assert(g_nev < 32); g_ev[g_nev++] = ev; }

#define R6 "0666000C0006"
#define R3 "0666000C0003"
#define G0 "0642000C0000"
#define G1 "0642000C0001"
#define G2 "0642000C0002"
#define G5 "0642000C0005"
#define W8 "0657000C0008"
#define W1 "0657000C0001"

static void usb_card(int present){
    char p[160]; snprintf(p, sizeof p, "%s/usbid", OUT_USB_CARD);
    if(present){ int fd = open(p, O_WRONLY | O_CREAT, 0600); assert(fd >= 0); close(fd); } else unlink(p);
}
static void setup(void){
    char d[160]; snprintf(d, sizeof d, "%s", OUT_USB_CARD);
    char *slash = strrchr(d, '/'); if(slash){ *slash = 0; mkdir(d, 0700); *slash = '/'; }
    mkdir(d, 0700); usb_card(0); unlink(SD_EXPORT_MARKER);
    g_now = 1000;
    outroute_init(&renv); srcswitch_set_env(&env); sd_set_env(&sd_env); srcswitch_set_listener(on_event);
    assert(sd_gate_poll(1) == SD_GATE_OPENED);
    srcswitch_init();
    assert(sd_local_playback_allowed());
}
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

static void usb_audio_to_usb_dac_via_local(void){
    setup(); usb_card(1);
    outroute_set(OUT_USB);
    assert(ui_set_source_mode(1) == 0);
    assert(g_n == 0 && outroute_busy() && modes_source_switch_busy() && srcswitch_via_local_pending());
    assert(toasted("Leaving USB Audio..."));
    assert(ui_set_source_mode(2) < 0);
    run(400); assert(g_n == 0);
    run(600);
    const char *internal[] = { R6, G0, W8 };
    expect_frames("route to internal", internal, 3);
    assert(outroute_route() == OUT_INTERNAL && !outroute_busy());
    assert(modes_source_switch_busy() && session_source_mode() == 0);
    run(1900); assert(g_n == 3 && session_source_mode() == 0);
    run(500);
    const char *all[] = { R6, G0, W8, R6, G2, W1 };
    expect_frames("then USB DAC", all, 6);
    assert(session_source_mode() == 1 && !srcswitch_via_local_pending() && !modes_source_switch_busy());
    assert(evented(SRCSWITCH_EV_SETTLE_RESTART) && evented(SRCSWITCH_EV_CHANGED) && !evented(SRCSWITCH_EV_ABANDONED));
    assert(toasted("Switching to USB DAC") && cfg_get_int("src_mode", -1) == 1 && cfg_get_int("usb_audio_on", -1) == 0);
}
static void usb_dac_ordered_exit(void){
    setup(); session_set_source_mode(1); g_attached = 1; g_gadget = 1;
    assert(ui_set_source_mode(0) == 0);
    const char *g0[] = { G0 };
    expect_frames("exit starts with 0642 0", g0, 1);
    assert(toasted("Leaving USB DAC... (up to 20 s)") && modes_source_switch_busy() && !outroute_busy());
    assert(ui_set_source_mode(2) < 0 && g_n == 1);
    run(5000); assert(g_n == 1 && session_source_mode() == 1);
    g_gadget = 0; run(100);
    run(1300); assert(g_n == 1 && modes_source_switch_busy());
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
static void usb_dac_to_storage_exit_then_local_then_export(void){
    setup(); session_set_source_mode(1); g_attached = 1; g_gadget = 1;
    assert(ui_set_source_mode(3) == 0);
    g_gadget = 0; run(2000);
    const char *s1[] = { G0, R6, G0, W8 };
    expect_frames("exit + Local", s1, 4);
    assert(session_source_mode() == 0 && srcswitch_via_local_pending() && evented(SRCSWITCH_EV_SETTLE_RESTART));
    run(2400); assert(g_n == 4);
    run(300);
    assert(session_source_mode() == 3 && sd_switch_pending() && !srcswitch_via_local_pending());
    run(200);
    const char *s2[] = { G0, R6, G0, W8, R6, G1, W8 };
    expect_frames("then the export", s2, 7);
    assert(count(G0) == 2 && toasted("Switching to USB storage"));
}
static void host_role_never_goes_straight_to_storage(void){
    setup(); usb_card(1);
    g_playing = 1; outroute_set(OUT_USB);
    assert(ui_set_source_mode(3) == 0);
    assert(g_n == 1 && !strcmp(g_log[0], PAUSE) && !g_playing);
    run(5000);
    assert(sd_switch_pending());
    run(300);
    const char *want[] = { PAUSE, R6, G0, W8, R6, G1, W8 };
    expect_frames("storage from USB Audio", want, 7);
    assert(idx(G1, 0) > idx(G0, 0) && count(PAUSE) == 1);
    assert(outroute_route() == OUT_INTERNAL);
}
static void local_to_storage_is_direct(void){
    setup();
    assert(ui_set_source_mode(3) == 0 && g_n == 0 && session_source_mode() == 3);
    run(200);
    const char *want[] = { R6, G1, W8 };
    expect_frames("direct storage", want, 3);
}
static void failed_leave_abandons_and_recovers(void){
    setup(); usb_card(1);
    outroute_set(OUT_USB);
    g_fail_from = 1;
    assert(ui_set_source_mode(1) == 0);
    run(1000);
    assert(toasted("Output state uncertain - recovering") && outroute_busy() && outroute_route() == OUT_INTERNAL);
    run(9500);
    assert(evented(SRCSWITCH_EV_ABANDONED) && toasted("Couldn't return to Local - mode not changed"));
    assert(!srcswitch_via_local_pending() && session_source_mode() == 0);
    assert(idx(G2, 0) < 0 && idx(W1, 0) < 0 && outroute_busy());
    g_fail_from = -1; run(600);
    assert(!outroute_busy() && toasted("Internal DAC restored"));
    assert(outroute_route() == OUT_INTERNAL);
}
static void failed_step_then_retry_lands(void){
    setup(); usb_card(1);
    outroute_set(OUT_USB);
    g_fail_at = 1;
    assert(ui_set_source_mode(2) == 0);
    run(700);
    const char *retried[] = { R6, G0, R6, G0, W8 };
    expect_frames("internal sequence retried in full", retried, 5);
    assert(toasted("Output switch failed - internal DAC restored") && !outroute_busy() && outroute_route() == OUT_INTERNAL);
    run(3000);
    assert(session_source_mode() == 2 && idx("0657000C0006", 0) > 0 && toasted("Switching to Bluetooth receiving"));
}
static void deferred_pick_refused_is_abandoned(void){
    setup(); session_set_source_mode(1);
    assert(ui_set_source_mode(3) == 0 && session_source_mode() == 0 && srcswitch_via_local_pending());
    const char *l[] = { R6, G0, W8 };
    expect_frames("to Local", l, 3);
    g_scan = 1; run(3300);
    assert(evented(SRCSWITCH_EV_ABANDONED) && toasted("Let the library scan finish first"));
    assert(!srcswitch_via_local_pending() && !modes_source_switch_busy() && session_source_mode() == 0 && g_n == 3);
}
static void failed_source_frame_refuses(void){
    setup(); g_fail_from = 0;
    assert(ui_set_source_mode(1) < 0 && session_source_mode() == 0 && toasted("Couldn't switch mode"));
    g_fail_from = -1; g_n = 0; g_ntoast = 0;
    g_fail_from = 1;
    assert(ui_set_source_mode(1) < 0 && session_source_mode() == 0 && toasted("Couldn't switch mode"));
    assert(outroute_route() == OUT_INTERNAL);
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
    S(boot_restore_replays_the_saved_mode);
    puts("srcswitch_test: ok");
    return 0;
}
