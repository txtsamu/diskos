/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "session.h"
#include "outroute.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXF 32
static char g_log[MAXF][48];
static int g_n;
static int g_send_rc = 0;
int ipc_send_cmd(const char *frame){ assert(g_n < MAXF); snprintf(g_log[g_n++], sizeof g_log[0], "%s", frame); return g_send_rc; }
static unsigned g_gen = 1;
unsigned ipc_generation(void){ return g_gen; }
int ipc_is_ready(void){ return 1; }
void ipc_get_state(track_state_t *out){ memset(out, 0, sizeof *out); }
static struct { char key[24]; int v; } g_cfg[8]; static int g_ncfg;
int cfg_get_int(const char *key, int def){ for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)) return g_cfg[i].v; return def; }
int cfg_set_int(const char *key, int v){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)){ g_cfg[i].v = v; return 0; }
    assert(g_ncfg < 8); snprintf(g_cfg[g_ncfg].key, sizeof g_cfg[0].key, "%s", key); g_cfg[g_ncfg++].v = v; return 0;
}

static struct {
    int admitted, play_starting, playing, have_track, fw_needs_init;
    unsigned rx;
    int player_old, saved_bt, sink, route_analog_rc;
    int n_player_old, n_saved_bt, n_sink, n_route_analog;
    int clear_bt_on_analog;
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
    if(F.route_analog_rc == 0 && F.clear_bt_on_analog) outroute_set_bt(NULL);
    return F.route_analog_rc;
}
static const struct session_startup_env k_env = {
    .admitted = f_admitted, .rx_frames = f_rx, .play_starting = f_play_starting, .playing = f_playing,
    .have_track = f_have_track, .fw_needs_init = f_fw, .player_old = f_player_old, .saved_bt_route = f_saved_bt,
    .connected_sink = f_sink, .route_analog = f_route_analog,
};
static void startup_env_reset(void){
    memset(&F, 0, sizeof F);
    F.admitted = 1; F.fw_needs_init = 1; F.rx = 3;
    session_startup_set_env(&k_env);
}
#define TICK(t) session_startup_tick(t)
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

static void old_process_exports(int route, int src){
    session_set_source_mode(src); outroute_set(route);
    session_handoff_export();
}
static int new_process_starts(void){ session_init(); return session_adopt(ipc_generation()); }
static void clear_env(void){ unsetenv("DISKOS_THEME_RELOAD"); unsetenv("DISKOS_RELOAD_ROUTE"); unsetenv("DISKOS_RELOAD_SRC"); }

static void source_mode_is_a_session_fact(void){
    assert(session_source_mode() == 0);
    session_set_source_mode(2);
    assert(session_source_mode() == 2);
}
static void player_restart_reruns_local_init(void){
    outroute_set(OUT_USB);
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    const char *usb[] = { R3, G5, W8 };
    expect_frames("init on USB", usb, 3);
    assert(TICK(10500) == SESSION_STARTUP_WAIT && g_n == 3);
    g_gen = 2;
    player_answers(20000);
    assert(TICK(29000) == SESSION_STARTUP_WAIT);
    const char *both[] = { R3, G5, W8, G0, R6, W8 };
    expect_frames("init after restart", both, 6);
}
static void cold_boot_entry(void){
    clear_env();
    assert(!new_process_starts());
    assert(session_entry() == SESSION_COLD_BOOT);
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    const char *init[] = { G0, R6, W8 };
    expect_frames("cold boot init", init, 3);
}
static void relaunch_on_usb_audio_sends_nothing(void){
    cfg_set_int("usb_audio_on", 1);
    old_process_exports(OUT_USB, 0);
    outroute_save_usb_audio(1);
    assert(new_process_starts() == 1);
    assert(session_entry() == SESSION_RELAUNCH);
    assert(outroute_route() == OUT_USB && session_source_mode() == 0);
    F.rx = 3;
    for(uint32_t t = 0; t <= 60000; t += 500){
        F.rx++;
        assert(TICK(t) == SESSION_STARTUP_WAIT);
    }
    assert(F.n_saved_bt == 0 && F.n_sink == 0 && F.n_route_analog == 0);
    session_restore_arm(1000);
    int src = -1;
    for(uint32_t t = 1000; t <= 1000 + 130000; t += 1000){
        session_restore_t r = session_restore_poll(t, 0, 0, &src);
        assert(r == SESSION_RESTORE_WAIT || r == SESSION_RESTORE_NONE);
    }
    assert(g_n == 0);
}
static void relaunch_on_usb_dac_keeps_source(void){
    cfg_set_int("src_mode", 1);
    old_process_exports(OUT_INTERNAL, 1);
    assert(new_process_starts() == 1);
    assert(session_source_mode() == 1 && outroute_route() == OUT_INTERNAL);
    session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(14999, 0, 0, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE);
    assert(session_restore_poll(16000, 0, 0, &src) == SESSION_RESTORE_NONE && g_n == 0);
}
static void relaunch_adopts_env(void){
    old_process_exports(OUT_SPDIF, 2);
    assert(getenv("DISKOS_THEME_RELOAD") && !strcmp(getenv("DISKOS_THEME_RELOAD"), "1"));
    assert(!strcmp(getenv("DISKOS_RELOAD_ROUTE"), "1"));
    assert(!strcmp(getenv("DISKOS_RELOAD_SRC"), "2"));
    g_gen = 7;
    assert(new_process_starts() == 1);
    assert(getenv("DISKOS_THEME_RELOAD") == NULL && getenv("DISKOS_RELOAD_ROUTE") == NULL && getenv("DISKOS_RELOAD_SRC") == NULL);
    assert(outroute_route() == OUT_SPDIF && session_source_mode() == 2);
    assert(session_adopt(7) == 0);
    F.rx = 3;
    for(uint32_t t = 0; t <= 20000; t += 500){ F.rx++; assert(TICK(t) == SESSION_STARTUP_WAIT); }
    assert(g_n == 0);
    g_gen = 8;
    assert(outroute_route() == OUT_INTERNAL);
    player_answers(30000);
    assert(TICK(39000) == SESSION_STARTUP_WAIT);
    const char *init[] = { G0, R6, W8 };
    expect_frames("init after the adopted generation", init, 3);
}
static void relaunch_ignores_default_and_bad_state(void){
    setenv("DISKOS_THEME_RELOAD", "1", 1); setenv("DISKOS_RELOAD_ROUTE", "0", 1); setenv("DISKOS_RELOAD_SRC", "0", 1);
    assert(!new_process_starts());
    assert(session_entry() == SESSION_RELAUNCH);
    assert(getenv("DISKOS_THEME_RELOAD") == NULL);
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    const char *init[] = { G0, R6, W8 };
    expect_frames("init on a default-pair relaunch", init, 3);
}
static void relaunch_rejects_out_of_range(void){
    setenv("DISKOS_THEME_RELOAD", "1", 1); setenv("DISKOS_RELOAD_ROUTE", "3", 1); setenv("DISKOS_RELOAD_SRC", "0", 1);
    assert(!new_process_starts() && outroute_route() == OUT_INTERNAL);
}
static void failed_exec_cancels_handoff(void){
    old_process_exports(OUT_USB, 0);
    session_handoff_cancel();
    assert(getenv("DISKOS_THEME_RELOAD") == NULL && getenv("DISKOS_RELOAD_ROUTE") == NULL && getenv("DISKOS_RELOAD_SRC") == NULL);
    assert(outroute_route() == OUT_USB);
}
static void entry_is_cold_boot_without_a_handoff(void){
    clear_env();
    session_init();
    assert(session_entry() == SESSION_COLD_BOOT);
}
static void entry_is_relaunch_for_any_handoff(void){
    old_process_exports(OUT_INTERNAL, 0);
    assert(!new_process_starts());
    assert(session_entry() == SESSION_RELAUNCH);
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
    assert(session_restore_poll(19999, 0, 0, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(20000, 1, 0, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(20500, 0, 1, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(21000, 0, 0, &src) == SESSION_RESTORE_USB_AUDIO);
    assert(session_restore_poll(22000, 0, 0, &src) == SESSION_RESTORE_NONE);
    assert(session_restore_poll(60000, 0, 0, &src) == SESSION_RESTORE_NONE);
    assert(g_n == 0);
}
static void cold_boot_restores_source_once(void){
    clear_env();
    session_save_source(1);
    assert(cfg_get_int("usb_audio_on", 9) == 0);
    session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(15000, 1, 0, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(15000, 0, 1, &src) == SESSION_RESTORE_SOURCE && src == 1);
    assert(session_restore_poll(16000, 0, 0, &src) == SESSION_RESTORE_NONE);
}
static void start_while_in_usb_dac_sends_no_frames(void){
    clear_env();
    cfg_set_int("src_mode", 1);
    assert(!new_process_starts());
    assert(session_source_mode() == 0);
    assert(session_adopt_detected(ipc_generation(), 1) == 1);
    assert(session_source_mode() == 1);
    for(uint32_t t = 0; t <= 60000; t += 500){ F.rx++; assert(TICK(t) == SESSION_STARTUP_WAIT); }
    session_restore_arm(0);
    int src = -1;
    for(uint32_t t = 0; t <= 130000; t += 1000){
        session_restore_t r = session_restore_poll(t, 0, 0, &src);
        assert(r == SESSION_RESTORE_WAIT || r == SESSION_RESTORE_NONE);
    }
    assert(g_n == 0);
    assert(cfg_get_int("src_mode", 0) == 1);
}
static void cold_boot_with_player_still_local_restores_once(void){
    clear_env();
    cfg_set_int("src_mode", 1);
    assert(!new_process_starts());
    assert(session_adopt_detected(ipc_generation(), 0) == 0);
    assert(session_source_mode() == 0);
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 3);
    session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(14999, 0, 0, &src) == SESSION_RESTORE_WAIT);
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_SOURCE && src == 1);
    assert(session_restore_poll(16000, 0, 0, &src) == SESSION_RESTORE_NONE);
}
static void adopt_detected_only_what_was_saved(void){
    clear_env();
    cfg_set_int("src_mode", 2);
    assert(!new_process_starts());
    assert(session_adopt_detected(g_gen, 1) == 0 && session_adopt_detected(g_gen, 0) == 0);
    cfg_set_int("src_mode", 0);
    assert(session_adopt_detected(g_gen, 1) == 0);
    cfg_set_int("src_mode", 1);
    assert(session_adopt_detected(g_gen, 3) == 0);
    assert(session_source_mode() == 0);
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 3);
}
static void adopt_detected_leaves_a_relaunch_alone(void){
    cfg_set_int("src_mode", 1);
    old_process_exports(OUT_INTERNAL, 0);
    assert(!new_process_starts());
    assert(session_adopt_detected(g_gen, 1) == 0);
    session_set_source_mode(2);
    assert(session_adopt_detected(g_gen, 1) == 0 && session_source_mode() == 2);
}
static void leaving_storage_for_local_cancels_old_restore(void){
    clear_env();
    session_save_source(2);
    session_save_source(3);
    assert(session_saved_source() == 2);
    session_save_source(0);
    assert(!new_process_starts());
    session_restore_arm(0);
    int src = -1;
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE && g_n == 0);
}
static void leaving_storage_for_usb_dac_restores_it(void){
    clear_env();
    session_save_source(0);
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
    assert(session_restore_poll(121000, 0, 0, &src) == SESSION_RESTORE_NONE);
    session_save_source(0); session_restore_arm(0);
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE);
}
static void restore_stands_down_when_already_moved(void){
    clear_env();
    cfg_set_int("usb_audio_on", 1); session_restore_arm(0);
    int src = -1;
    outroute_set(OUT_USB);
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE);
    cfg_set_int("usb_audio_on", 1); outroute_set(OUT_INTERNAL); session_set_source_mode(1); session_restore_arm(0);
    assert(session_restore_poll(15000, 0, 0, &src) == SESSION_RESTORE_NONE);
}
static const char *const INIT_INTERNAL[] = { G0, R6, W8 };
static void expect_init(const char *what){ expect_frames(what, INIT_INTERNAL, 3); }

static void startup_probes_until_the_player_answers(void){
    for(uint32_t t = 0; t < 60000; t += 500) assert(TICK(t) == SESSION_STARTUP_PROBE);
    assert(g_n == 0);
}
static void startup_settle_window_is_nine_seconds_from_the_first_answer(void){
    player_answers(1000);
    assert(TICK(1000 + 8999) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(1000 + 9000) == SESSION_STARTUP_WAIT);
    expect_init("init at the end of the window");
    assert(TICK(1000 + 9500) == SESSION_STARTUP_WAIT && g_n == 3);
    assert(TICK(1000 + 60000) == SESSION_STARTUP_WAIT && g_n == 3);
}
static void startup_settle_window_wraps_with_the_tick_counter(void){
    player_answers(0xFFFFF000u);
    assert(TICK(0xFFFFF000u + 8999) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(0xFFFFF000u + 9000) == SESSION_STARTUP_WAIT);
    expect_init("init across the tick wrap");
}
static void startup_rx_base_is_per_generation(void){
    F.rx = 500;
    assert(TICK(0) == SESSION_STARTUP_PROBE);
    F.rx = 501;
    assert(TICK(500) == SESSION_STARTUP_WAIT);
    g_gen = 2;
    assert(TICK(9500) == SESSION_STARTUP_PROBE && g_n == 0);
    F.rx = 502;
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(10000 + 8999) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(10000 + 9000) == SESSION_STARTUP_WAIT);
    expect_init("init for the second generation only");
}
static void startup_waits_while_not_admitted(void){
    F.admitted = 0;
    for(uint32_t t = 0; t < 30000; t += 500) assert(TICK(t) == SESSION_STARTUP_WAIT);
    F.admitted = 1;
    assert(TICK(30000) == SESSION_STARTUP_PROBE);
}
static void startup_waits_while_a_play_is_starting(void){
    player_answers(1000);
    F.play_starting = 1;
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(20000) == SESSION_STARTUP_WAIT && g_n == 0);
    F.play_starting = 0;
    assert(TICK(20500) == SESSION_STARTUP_WAIT);
    expect_init("init after the play started");
}
static void startup_firmware_without_the_init_is_done_at_the_window(void){
    F.fw_needs_init = 0;
    player_answers(1000);
    assert(TICK(1000 + 8999) == SESSION_STARTUP_WAIT);
    assert(TICK(1000 + 9000) == SESSION_STARTUP_DONE);
    assert(g_n == 0);
}
static void startup_playing_holds_the_pending_route_recovery(void){
    player_answers(1000);
    F.playing = 1;
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0 && F.n_saved_bt == 0);
    F.playing = 0;
    assert(TICK(10500) == SESSION_STARTUP_WAIT && F.n_saved_bt == 1);
    expect_init("init once the stream ended");
}
static void startup_skips_the_init_when_already_playing(void){
    session_startup_route_settled();
    player_answers(1000);
    F.playing = 1;
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0);
    F.playing = 0;
    assert(TICK(11000) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(TICK(60000) == SESSION_STARTUP_WAIT && g_n == 0);
}
static void startup_skips_the_init_when_a_play_was_already_sent(void){
    player_answers(1000);
    F.playing = 1; assert(TICK(2000) == SESSION_STARTUP_WAIT);
    F.playing = 0;
    session_note_transport_sent(g_gen, 3000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(TICK(13000) == SESSION_STARTUP_WAIT);
    assert(g_n == 0);
    assert(TICK(60000) == SESSION_STARTUP_WAIT && g_n == 0);
}
static void startup_play_sent_in_an_older_generation_does_not_count(void){
    session_note_transport_sent(g_gen, 0);
    g_gen = 2;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    expect_init("init for a new generation after an old play");
}
static void startup_busy_playback_holds_the_recovery(void){
    session_note_transport_sent(g_gen, 0);
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT && g_n == 0);
    F.have_track = 1;
    assert(TICK(20000) == SESSION_STARTUP_WAIT && g_n == 0);
    F.have_track = 0;
    assert(TICK(30000) == SESSION_STARTUP_WAIT && g_n == 0);
    assert(F.n_saved_bt == 1);
}
static void startup_bt_route_marks_the_generation_without_frames(void){
    outroute_set_bt("AA:BB:CC:DD:EE:FF");
    F.sink = 1;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(g_n == 0 && F.n_saved_bt == 0 && F.n_route_analog == 0);
    assert(TICK(10500) == SESSION_STARTUP_WAIT && g_n == 0);
}
static void startup_bt_route_on_a_firmware_without_the_init_is_done(void){
    F.fw_needs_init = 0;
    outroute_set_bt("AA:BB:CC:DD:EE:FF");
    F.sink = 1;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_DONE && g_n == 0);
}
static void startup_bt_route_without_a_sink_goes_back_to_analog(void){
    outroute_set_bt("AA:BB:CC:DD:EE:FF");
    F.sink = 0; F.clear_bt_on_analog = 1;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_route_analog == 1 && !outroute_bt_routed());
    assert(g_n == 0);
    assert(TICK(10500) == SESSION_STARTUP_WAIT && F.n_route_analog == 1 && g_n == 0);
}
static void startup_saved_bt_route_with_no_sink_is_recovered_once(void){
    F.saved_bt = 1; F.sink = 0;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_saved_bt == 1 && F.n_sink == 1 && F.n_route_analog == 1 && g_n == 0);
    assert(TICK(10500) == SESSION_STARTUP_WAIT && F.n_route_analog == 1 && F.n_saved_bt == 1);
}
static void startup_saved_analog_or_unknown_route_runs_the_init(void){
    F.saved_bt = -1;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_sink == 0 && F.n_route_analog == 0);
    expect_init("init when the saved route is unknown");
}
static void startup_spdif_intent_skips_the_recovery(void){
    cfg_set_int("spdif", 1); outroute_set(OUT_SPDIF);
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_saved_bt == 0 && F.n_sink == 0 && F.n_route_analog == 0);
    const char *spdif[] = { G0, "0666000C0004", W8 };
    expect_frames("init on the SPDIF route", spdif, 3);
}
static void startup_inconclusive_sink_probe_waits_five_seconds(void){
    F.saved_bt = 1; F.sink = -1;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT && F.n_sink == 1);
    assert(TICK(10000 + 4999) == SESSION_STARTUP_WAIT && F.n_sink == 1);
    assert(TICK(10000 + 5000) == SESSION_STARTUP_WAIT && F.n_sink == 2);
    F.sink = 0;
    assert(TICK(15000 + 5000) == SESSION_STARTUP_WAIT && F.n_sink == 3 && F.n_route_analog == 1);
    assert(g_n == 0);
}
static void startup_failed_analog_route_is_retried_every_tick(void){
    F.saved_bt = 1; F.sink = 0; F.route_analog_rc = -1;
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT && F.n_route_analog == 1);
    assert(TICK(10500) == SESSION_STARTUP_WAIT && F.n_route_analog == 2);
    F.route_analog_rc = 0;
    assert(TICK(11000) == SESSION_STARTUP_WAIT && F.n_route_analog == 3);
    assert(TICK(11500) == SESSION_STARTUP_WAIT && F.n_route_analog == 3 && g_n == 0);
}
static void startup_old_player_recovers_before_it_answers(void){
    F.player_old = 1; F.saved_bt = 1; F.sink = 0;
    assert(TICK(0) == SESSION_STARTUP_WAIT);
    assert(F.n_player_old == 1 && F.n_route_analog == 1 && g_n == 0);
    assert(TICK(500) == SESSION_STARTUP_WAIT && F.n_player_old == 1);
}
static void startup_old_player_on_a_firmware_without_the_init_is_done(void){
    F.player_old = 1; F.saved_bt = 1; F.sink = 0; F.fw_needs_init = 0;
    assert(TICK(0) == SESSION_STARTUP_DONE && F.n_route_analog == 1);
}
static void startup_old_player_waits_for_playback_before_recovering(void){
    F.player_old = 1; F.saved_bt = 1; F.sink = 0;
    F.playing = 1;
    assert(TICK(0) == SESSION_STARTUP_PROBE && F.n_route_analog == 0);
}
static void startup_refused_init_frame_is_retried(void){
    player_answers(1000);
    g_send_rc = -1;
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(g_n == 1);
    g_n = 0; g_send_rc = 0;
    assert(TICK(10500) == SESSION_STARTUP_WAIT);
    expect_init("the retried init");
    assert(TICK(11000) == SESSION_STARTUP_WAIT && g_n == 3);
}
static void startup_intentional_route_supersedes_the_recovery(void){
    F.saved_bt = 1; F.sink = 0;
    session_startup_route_settled();
    player_answers(1000);
    assert(TICK(10000) == SESSION_STARTUP_WAIT);
    assert(F.n_saved_bt == 0 && F.n_sink == 0 && F.n_route_analog == 0);
    expect_init("init after an intentional route");
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
    RUN(source_mode_is_a_session_fact);
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
