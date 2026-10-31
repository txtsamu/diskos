/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "sdio.h"
#include "session.h"
#include "outseq.h"
#include "ipc.h"
#include "config.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(SD_EXPORT_MARKER) || !defined(SD_IO_FAULT_MARKER)
#error "the Makefile points SD_EXPORT_MARKER and SD_IO_FAULT_MARKER at a temp dir"
#endif

static int g_sent[8], g_nsent, g_pauses;
int ipc_send_cmd(const char *frame){
    if(!strcmp(frame, PLAYER_CMD_PLAYPAUSE)){ g_pauses++; return 0; }
    return 0;
}
unsigned ipc_generation(void){ return 1; }
int ipc_is_ready(void){ return 1; }
void ipc_get_state(track_state_t *out){ memset(out, 0, sizeof *out); }
static struct { char key[24]; int v; } g_cfg[8]; static int g_ncfg;
int cfg_get_int(const char *key, int def){ for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)) return g_cfg[i].v; return def; }
int cfg_set_int(const char *key, int v){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)){ g_cfg[i].v = v; return 0; }
    assert(g_ncfg < 8); snprintf(g_cfg[g_ncfg].key, sizeof g_cfg[0].key, "%s", key); g_cfg[g_ncfg++].v = v; return 0;
}

static uint32_t g_now;
static int g_mounted, g_exported, g_host, g_guarded, g_scan, g_playing, g_restarts, g_returned, g_scans, g_send_fail;
static char g_toast[16][96]; static int g_ntoast;
static uint32_t env_now(void){ return g_now; }
static int env_mounted(void){ return g_mounted; }
static int env_exported(void){ return g_exported; }
static int env_host(void){ return g_host; }
static int env_guarded(void){ return g_guarded; }
static int env_playing(void){ return g_playing; }
static int env_scan_active(void){ return g_scan; }
static void env_scan_empty(void){ g_scans++; }
static void env_nothing(void){}
static void env_returned(void){ g_returned++; }
static void env_restart(void){ g_restarts++; }
static void env_toast(const char *s){ if(g_ntoast < 16) snprintf(g_toast[g_ntoast++], sizeof g_toast[0], "%s", s); }
static int env_send_source(int mode){
    assert(g_nsent < 8);
    g_sent[g_nsent++] = mode;
    return g_send_fail ? -1 : 0;
}
static const struct sd_env env = {
    .now = env_now, .media_mounted = env_mounted, .exported_to_host = env_exported, .host_confirmed = env_host,
    .player_guarded = env_guarded, .playing = env_playing, .scan_active = env_scan_active,
    .scan_if_library_empty = env_scan_empty, .art_cancel = env_nothing, .card_returned = env_returned,
    .restart = env_restart, .toast = env_toast, .send_source = env_send_source,
};
static int toasted(const char *s){ for(int i = 0; i < g_ntoast; i++) if(!strcmp(g_toast[i], s)) return 1; return 0; }

static void marker(const char *content){
    FILE *f = fopen(SD_EXPORT_MARKER, "w"); assert(f);
    fputs(content, f); fclose(f);
}
static int marker_is(const char *want){
    char b[16] = {0};
    FILE *f = fopen(SD_EXPORT_MARKER, "r"); if(!f) return 0;
    size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
    return !strcmp(b, want);
}
static void setup(void){
    unlink(SD_EXPORT_MARKER); unlink(SD_IO_FAULT_MARKER);
    g_now = 1000; g_mounted = 1; g_guarded = 1;
    sd_set_env(&env);
}
static void open_card(void){
    setup();
    assert(sd_gate_poll(1) == SD_GATE_OPENED);
    sd_init();
    assert(sd_io_allowed() && sd_local_playback_allowed() && sd_writable());
}
static void run(int ms){ for(int t = 0; t < ms; t += 100){ g_now += 100; sd_tick(); } }

static void nothing_is_admitted_before_a_positive_verdict(void){
    setup(); sd_init();
    assert(!sd_io_begin() && !sd_io_allowed() && !sd_local_playback_allowed());
    assert(sd_switch_refusal() && !strcmp(sd_switch_refusal(), "SD access is not ready yet"));
    run(500);
    assert(!sd_io_begin() && !sd_io_allowed());
    assert(sd_gate_poll(1) == SD_GATE_OPENED);
    assert(!sd_io_begin());
    run(100);
    assert(sd_io_begin()); sd_io_end();
    assert(sd_local_playback_allowed() && !sd_switch_refusal() && g_scans == 1);
}
static void gate_waits_then_denies_without_a_verdict(void){
    setup(); sd_init();
    for(int i = 0; i < 59; i++) assert(sd_gate_poll(-1) == SD_GATE_WAIT);
    assert(!sd_held());
    assert(sd_gate_poll(-1) == SD_GATE_DENIED);
    assert(sd_held() && sd_switch_failed() && !sd_local_playback_allowed());
    assert(!sd_io_begin() && !sd_write_begin() && !sd_writable());
    assert(sd_gate_poll(1) == SD_GATE_OPENED);
    run(500);
    assert(!sd_io_begin() && !sd_write_begin() && !sd_local_playback_allowed());
    assert(!strcmp(sd_switch_refusal(), "SD access is held this boot"));
}
static void gate_denies_an_unprotected_launch_at_once(void){
    setup(); sd_init();
    assert(sd_gate_poll(0) == SD_GATE_DENIED);
    assert(sd_held() && !sd_io_begin() && !sd_write_begin() && sd_switch_failed());
}
static void gate_opens_once_and_revokes_when_the_verdict_is_lost(void){
    open_card();
    assert(sd_gate_poll(1) == SD_GATE_OPEN);
    assert(sd_io_begin());
    assert(sd_gate_poll(-1) == SD_GATE_DENIED);
    assert(sd_held() && !sd_io_allowed() && !sd_writable() && sd_switch_failed() && !sd_local_playback_allowed());
    assert(!sd_write_begin());
    sd_io_end();
    run(500);
    assert(!sd_io_begin());
}
static void readers_and_writers_drain_together(void){
    open_card();
    assert(sd_drained());
    assert(sd_io_begin() && sd_write_begin());
    assert(!sd_drained());
    sd_io_end(); assert(!sd_drained());
    sd_write_end(); assert(sd_drained());
    assert(sd_write_begin() && !sd_drained());
    sd_write_end();
}
static void quiesce_refuses_new_work_and_waits_for_both_counters(void){
    open_card();
    assert(sd_io_begin() && sd_write_begin());
    sd_quiesce_begin();
    assert(!sd_io_begin() && !sd_write_begin() && !sd_io_allowed() && !sd_writable());
    assert(!sd_drained());
    sd_io_end(); assert(!sd_drained());
    sd_write_end(); assert(sd_drained());
    run(500);
    assert(!sd_io_allowed());
    sd_quiesce_end();
    assert(sd_io_allowed() && sd_writable() && sd_io_begin() && sd_write_begin());
    sd_io_end(); sd_write_end();
}
static void quiesce_does_not_reopen_a_held_card(void){
    open_card();
    sd_quiesce_begin();
    sd_hold();
    sd_quiesce_end();
    assert(!sd_io_allowed() && !sd_writable() && !sd_io_begin());
}
static void writers_are_refused_while_the_card_is_exported(void){
    open_card();
    g_exported = 1;
    assert(!sd_write_begin() && sd_drained());
    g_exported = 0;
    assert(sd_write_begin()); sd_write_end();
}
static void readers_need_a_local_mount(void){
    open_card();
    g_mounted = 0;
    assert(!sd_io_begin());
    g_mounted = 1;
    assert(sd_io_begin()); sd_io_end();
}
static void a_fault_closes_readers_for_good(void){
    open_card();
    assert(sd_io_begin());
    sd_io_fault();
    assert(!sd_io_healthy() && !sd_io_allowed() && !sd_local_playback_allowed());
    sd_io_end();
    assert(!sd_io_begin());
    run(100);
    assert(sd_switch_failed() && !sd_write_begin());
    assert(access(SD_IO_FAULT_MARKER, F_OK) == 0);
    sd_init();
    assert(!sd_io_healthy() && !sd_io_begin() && !sd_local_playback_allowed());
}
static void a_held_boot_stays_held_through_init(void){
    setup();
    sd_hold();
    assert(sd_gate_poll(1) == SD_GATE_OPENED);
    sd_init();
    assert(sd_held() && sd_switch_failed() && !sd_io_begin() && !sd_write_begin() && !sd_writable());
    run(500);
    assert(!sd_io_allowed() && !sd_local_playback_allowed());
}
static void export_drains_readers_then_asks_for_the_host(void){
    open_card();
    assert(sd_io_begin());
    assert(sd_export_begin() == 0);
    assert(sd_switch_pending() && !sd_io_begin() && !sd_write_begin() && session_source_mode() == 3);
    assert(!strcmp(sd_switch_refusal(), "Storage is switching"));
    assert(marker_is("D 3\n"));
    run(300);
    assert(g_nsent == 0);
    sd_io_end();
    run(100);
    assert(g_nsent == 1 && g_sent[0] == 3 && marker_is("E 3\n") && sd_switch_pending());
    g_mounted = 0; g_exported = 1; g_host = 1;
    run(100);
    assert(sd_host_owned() && !sd_switch_pending() && marker_is("H 3\n"));
    assert(sd_export_begin() == 0 && g_nsent == 1);
}
static void export_refusals_leave_the_card_open(void){
    open_card();
    g_scan = 1;
    assert(sd_export_begin() < 0 && toasted("Let the library scan finish first"));
    g_scan = 0; g_guarded = 0;
    assert(sd_export_begin() < 0 && toasted("Storage protection is unavailable"));
    g_guarded = 1; g_mounted = 0;
    assert(sd_export_begin() < 0 && toasted("SD card is not ready"));
    g_mounted = 1;
    assert(sd_io_allowed() && sd_writable() && !sd_switch_pending() && access(SD_EXPORT_MARKER, F_OK) != 0);
}
static void a_lost_export_request_holds_the_card(void){
    open_card();
    assert(sd_export_begin() == 0);
    g_send_fail = 1;
    run(100);
    assert(sd_switch_failed() && !sd_io_allowed() && !sd_writable() && toasted("Storage state uncertain - reboot device"));
    g_send_fail = 0;
    run(30000);
    assert(!sd_io_allowed() && marker_is("E 3\n"));
}
static void leaving_the_host_waits_for_the_local_mount(void){
    open_card();
    assert(sd_export_begin() == 0);
    run(100);
    g_mounted = 0; g_exported = 1; g_host = 1;
    run(100);
    assert(sd_host_owned());
    assert(sd_return_begin(2) == 0 && marker_is("R 2\n") && sd_switch_pending() && g_sent[g_nsent - 1] == 2);
    g_host = 0; g_mounted = 1;
    run(100);
    assert(sd_switch_pending() && !sd_io_allowed());
    g_exported = 0;
    run(100);
    assert(!sd_switch_pending() && sd_local_playback_allowed() && sd_io_allowed() && sd_writable());
    assert(session_source_mode() == 2 && g_returned == 1 && g_restarts == 1 && access(SD_EXPORT_MARKER, F_OK) != 0);
}
static void markers_restore_the_phase_after_a_restart(void){
    setup(); marker("E 3\n"); assert(sd_gate_poll(1) == SD_GATE_OPENED); sd_init();
    assert(sd_switch_pending() && !sd_writable() && !sd_io_allowed() && session_source_mode() == 3);
    run(100);
    assert(g_nsent == 1 && g_sent[0] == 3);
    setup(); marker("H 3\n"); g_mounted = 0; g_exported = 1; g_host = 1; sd_init();
    assert(sd_host_owned() && !sd_writable());
    setup(); marker("R 1\n"); g_mounted = 1; g_exported = 1; g_host = 0; sd_init();
    assert(sd_switch_pending() && !sd_io_allowed());
    setup(); marker("D 3\n"); g_mounted = 1; g_exported = 0; g_host = 0; sd_init();
    assert(!sd_switch_failed() && sd_writable() && access(SD_EXPORT_MARKER, F_OK) != 0);
    setup(); marker("garbage"); sd_init();
    assert(sd_switch_failed() && !sd_writable() && !sd_local_playback_allowed());
    setup(); g_exported = 1; sd_init();
    assert(sd_switch_failed() && !sd_writable());
}
struct export_try { int rc; };
static void *export_thread(void *a){
    struct export_try *t = a;
    t->rc = sd_export_begin();
    return NULL;
}
static void coldplug_and_export_exclude_each_other(void){
    open_card();
    assert(sd_coldplug_begin() == 1);
    struct export_try t = { 99 };
    pthread_t th; pthread_create(&th, NULL, export_thread, &t); pthread_join(th, NULL);
    assert(t.rc < 0 && toasted("SD card is busy - try again") && !sd_switch_pending() && sd_writable());
    sd_coldplug_end();
    pthread_create(&th, NULL, export_thread, &t); pthread_join(th, NULL);
    assert(t.rc == 0 && sd_switch_pending());
    assert(sd_coldplug_begin() == 0);
    sd_coldplug_end();
}
static void coldplug_sees_a_revoked_card_as_not_writable(void){
    open_card();
    assert(sd_gate_poll(0) == SD_GATE_DENIED);
    assert(sd_coldplug_begin() == 0);
    sd_coldplug_end();
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
    S(nothing_is_admitted_before_a_positive_verdict);
    S(gate_waits_then_denies_without_a_verdict);
    S(gate_denies_an_unprotected_launch_at_once);
    S(gate_opens_once_and_revokes_when_the_verdict_is_lost);
    S(readers_and_writers_drain_together);
    S(quiesce_refuses_new_work_and_waits_for_both_counters);
    S(quiesce_does_not_reopen_a_held_card);
    S(writers_are_refused_while_the_card_is_exported);
    S(readers_need_a_local_mount);
    S(a_fault_closes_readers_for_good);
    S(a_held_boot_stays_held_through_init);
    S(export_drains_readers_then_asks_for_the_host);
    S(export_refusals_leave_the_card_open);
    S(a_lost_export_request_holds_the_card);
    S(leaving_the_host_waits_for_the_local_mount);
    S(markers_restore_the_phase_after_a_restart);
    S(coldplug_and_export_exclude_each_other);
    S(coldplug_sees_a_revoked_card_as_not_writable);
    puts("sdio_test: ok");
    return 0;
}
