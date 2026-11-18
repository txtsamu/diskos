/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "playerset.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MAXF 64
static char g_log[MAXF][20];
static int g_n;
static int g_fail_next;
static int g_quiet_begins, g_quiet_fails;
static int g_arms, g_cancels, g_blocked;
static int g_book, g_saved_mode;
static const char *g_gain_tag;
static int g_settable;
static int g_owns_volume, g_dacvol_owns_rc, g_dacvol_level;
static int g_artist_mode, g_artist_changed, g_route_resets, g_flushes;
static char g_deferred[8][24]; static int g_deferred_val[8]; static int g_ndef;

static struct { char key[24]; int val; } g_cfg[24]; static int g_ncfg;

static void cfg_put(const char *key, int val){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)){ g_cfg[i].val = val; return; }
    assert(g_ncfg < 24);
    snprintf(g_cfg[g_ncfg].key, sizeof g_cfg[0].key, "%s", key);
    g_cfg[g_ncfg].val = val;
    g_ncfg++;
}
static void env_set_int(const char *key, int v){ cfg_put(key, v); }
static void env_set_int_deferred(const char *key, int v){
    assert(g_ndef < 8);
    snprintf(g_deferred[g_ndef], sizeof g_deferred[0], "%s", key);
    g_deferred_val[g_ndef] = v;
    g_ndef++;
}
static int env_get_int(const char *key, int def){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)) return g_cfg[i].val;
    return def;
}
static int env_send(const char *frame){
    assert(g_n < MAXF);
    snprintf(g_log[g_n], sizeof g_log[0], "%s", frame);
    g_n++;
    if(g_fail_next > 0){ g_fail_next--; return -1; }
    return 0;
}
static void env_quiet_begin(void){ g_quiet_begins++; }
static int env_quiet_end(void){ if(g_quiet_fails > 0){ g_quiet_fails--; return 2; } return 0; }
static const char *env_gain_tag(void){ return g_gain_tag; }
static int env_os_ver(void){ return 240; }
static int env_settable(void){ return g_settable; }
static void env_cfg_flush(void){ g_flushes++; }
static int env_dacvol_owns_volume(void){ return g_owns_volume; }
static int env_dacvol_set_user(int level){ g_dacvol_level = level; return g_dacvol_owns_rc; }
static void env_artist_mode_set(int album_artist){ g_artist_mode = album_artist; }
static void env_artist_mode_changed(void){ g_artist_changed++; }
static void env_route_reset(void){ g_route_resets++; }
static int env_book_active(void){ return g_book; }
static int env_saved_play_mode(void){ return g_saved_mode; }
static int env_output_idle_blocked(void){ return g_blocked; }
static void env_retry_arm(void){ g_arms++; }
static void env_retry_cancel(void){ g_cancels++; }

static const struct playerset_env env = {
    .send = env_send, .quiet_begin = env_quiet_begin, .quiet_end = env_quiet_end,
    .gain_tag = env_gain_tag, .os_ver = env_os_ver,
    .artist_class_settable = env_settable, .folder_jump_settable = env_settable,
    .track_display_settable = env_settable,
    .get_int = env_get_int, .set_int = env_set_int,
    .set_int_deferred = env_set_int_deferred, .cfg_flush = env_cfg_flush,
    .dacvol_owns_volume = env_dacvol_owns_volume, .dacvol_set_user = env_dacvol_set_user,
    .artist_mode_set = env_artist_mode_set, .artist_mode_changed = env_artist_mode_changed,
    .route_reset = env_route_reset, .book_active = env_book_active,
    .saved_play_mode = env_saved_play_mode, .output_idle_blocked = env_output_idle_blocked,
    .retry_arm = env_retry_arm, .retry_cancel = env_retry_cancel,
};

static void reset(void){
    g_n = 0; g_fail_next = 0;
    g_quiet_begins = 0; g_quiet_fails = 0;
    g_arms = g_cancels = g_blocked = 0;
    g_book = 0; g_saved_mode = 4;
    g_gain_tag = "0649"; g_settable = 1;
    g_owns_volume = 0; g_dacvol_owns_rc = 0; g_dacvol_level = -1;
    g_artist_mode = 0; g_artist_changed = 0; g_route_resets = 0; g_flushes = 0;
    g_ndef = 0;
    g_ncfg = 0;
    memset(g_log, 0, sizeof g_log);
    memset(g_deferred, 0, sizeof g_deferred);
}

static void expect_frames(const char *const *want, int n){
    if(g_n != n){
        fprintf(stderr, "sent %d frames, want %d\n", g_n, n);
        for(int i = 0; i < g_n; i++) fprintf(stderr, "  %s\n", g_log[i]);
        assert(0);
    }
    for(int i = 0; i < n; i++)
        if(strcmp(g_log[i], want[i])){ fprintf(stderr, "frame %d is %s, want %s\n", i, g_log[i], want[i]); assert(0); }
}

static void play_mode_fallback_before_any_send(void){
    reset();
    cfg_put("work_mode", 2);
    assert(ui_play_mode_now() == 2);
    g_book = 1;
    assert(ui_play_mode_now() == -1);
    g_book = 0;
    g_ncfg = 0;
    assert(ui_play_mode_now() == 4);
    assert(g_n == 0);
}

static void frame_formats(void){
    char b[16];
    assert(playerset_frame_eq(b, sizeof b, 0) == 0 && !strcmp(b, "0689000C0000"));
    assert(playerset_frame_eq(b, sizeof b, 20) == 0 && !strcmp(b, "0689000C0014"));
    assert(playerset_frame_eq(b, sizeof b, 25) == 0 && !strcmp(b, "0689000C0014"));
    assert(playerset_frame_eq(b, sizeof b, -3) == 0 && !strcmp(b, "0689000C0000"));
    assert(playerset_frame_dre(b, sizeof b, 1) == 0 && !strcmp(b, "0812000C0000"));
    assert(playerset_frame_dre(b, sizeof b, 0) == 0 && !strcmp(b, "0812000C0001"));
    assert(playerset_frame_dre(b, sizeof b, -1) != 0);
    assert(playerset_frame_gain(b, sizeof b, "0645", 1) == 0 && !strcmp(b, "0645000C0001"));
    assert(playerset_frame_gain(b, sizeof b, "0649", 0) == 0 && !strcmp(b, "0649000C0000"));
    assert(playerset_frame_gain(b, sizeof b, NULL, 1) != 0);
    assert(playerset_frame_dac_filter(b, sizeof b, 0) == 0 && !strcmp(b, "0653000C0000"));
    assert(playerset_frame_dac_filter(b, sizeof b, 5) == 0 && !strcmp(b, "0653000C0005"));
    assert(playerset_frame_dac_filter(b, sizeof b, 6) != 0);
    assert(playerset_frame_dac_filter(b, sizeof b, -1) != 0);
    assert(playerset_frame_replay_gain(b, sizeof b, 0) == 0 && !strcmp(b, "0718000C0000"));
    assert(playerset_frame_replay_gain(b, sizeof b, 2) == 0 && !strcmp(b, "0718000C0002"));
    assert(playerset_frame_replay_gain(b, sizeof b, 3) != 0);
    assert(playerset_frame_gapless(b, sizeof b, 1) == 0 && !strcmp(b, "0647000C0001"));
    assert(playerset_frame_gapless(b, sizeof b, 0) == 0 && !strcmp(b, "0647000C0000"));
    assert(playerset_frame_gapless(b, sizeof b, -1) != 0);
    assert(playerset_frame_artist_class(b, sizeof b, 1) == 0 && !strcmp(b, "0648000C0001"));
    assert(playerset_frame_artist_class(b, sizeof b, 0) == 0 && !strcmp(b, "0648000C0000"));
    assert(playerset_frame_folder_jump(b, sizeof b, 1) == 0 && !strcmp(b, "0687000C0001"));
    assert(playerset_frame_folder_jump(b, sizeof b, 0) == 0 && !strcmp(b, "0687000C0000"));
    assert(playerset_frame_track_display(b, sizeof b, 1) == 0 && !strcmp(b, "064d000C0001"));
    assert(playerset_frame_track_display(b, sizeof b, 0) == 0 && !strcmp(b, "064d000C0000"));
    assert(playerset_frame_memory(b, sizeof b, 0) == 0 && !strcmp(b, "0684000C0000"));
    assert(playerset_frame_memory(b, sizeof b, 2) == 0 && !strcmp(b, "0684000C0002"));
    assert(playerset_frame_memory(b, sizeof b, 3) != 0);
    assert(playerset_frame_maxvol(b, sizeof b, 0) == 0 && !strcmp(b, "0711000C0000"));
    assert(playerset_frame_maxvol(b, sizeof b, 120) == 0 && !strcmp(b, "0711000C0078"));
    assert(playerset_frame_maxvol(b, sizeof b, 130) == 0 && !strcmp(b, "0711000C0078"));
    assert(playerset_frame_maxvol(b, sizeof b, -5) == 0 && !strcmp(b, "0711000C0000"));
    assert(playerset_frame_balance(b, sizeof b, 0) == 0 && !strcmp(b, "0713000C0000"));
    assert(playerset_frame_balance(b, sizeof b, 5) == 0 && !strcmp(b, "0713000C0005"));
    assert(playerset_frame_balance(b, sizeof b, -5) == 0 && !strcmp(b, "0713000C0105"));
    assert(playerset_frame_balance(b, sizeof b, 10) == 0 && !strcmp(b, "0713000C000A"));
    assert(playerset_frame_balance(b, sizeof b, -10) == 0 && !strcmp(b, "0713000C010A"));
    assert(playerset_frame_balance(b, sizeof b, 11) != 0);
    assert(playerset_frame_balance(b, sizeof b, -11) != 0);
    assert(playerset_frame_play_mode(b, sizeof b, 0) == 0 && !strcmp(b, "0102000C0000"));
    assert(playerset_frame_play_mode(b, sizeof b, 4) == 0 && !strcmp(b, "0102000C0004"));
    assert(playerset_frame_play_mode(b, sizeof b, -1) == 0 && !strcmp(b, "0102000CFFFF"));
    assert(playerset_frame_volume(b, sizeof b, 0) == 0 && !strcmp(b, "0715000C0000"));
    assert(playerset_frame_volume(b, sizeof b, 33) == 0 && !strcmp(b, "0715000C0021"));
    assert(playerset_frame_volume(b, sizeof b, 120) == 0 && !strcmp(b, "0715000C0078"));
    assert(playerset_frame_favorite(b, sizeof b, 1) == 0 && !strcmp(b, "0104000C0001"));
    assert(playerset_frame_favorite(b, sizeof b, 0) == 0 && !strcmp(b, "0104000C0000"));
}

static void eq_select_saves_only_on_success(void){
    reset();
    assert(ui_eq_select(5) == 0);
    assert(g_n == 1 && !strcmp(g_log[0], "0689000C0005"));
    assert(g_ndef == 2 && !strcmp(g_deferred[0], "eq_last") && g_deferred_val[0] == 5);
    assert(!strcmp(g_deferred[1], "eq_preset") && g_deferred_val[1] == 5);
    assert(g_flushes == 1);
    reset();
    assert(ui_eq_select(0) == 0);
    assert(g_n == 1 && !strcmp(g_log[0], "0689000C0000"));
    assert(g_ndef == 1 && !strcmp(g_deferred[0], "eq_preset") && g_deferred_val[0] == 0);
    assert(g_flushes == 1);
    reset();
    g_fail_next = 1;
    assert(ui_eq_select(7) == -1);
    assert(g_n == 1 && !strcmp(g_log[0], "0689000C0007"));
    assert(g_ndef == 0 && g_flushes == 0);
}

static void gain_tag_gates_the_frame(void){
    reset();
    g_gain_tag = "0645";
    ui_set_gain(1);
    assert(g_n == 1 && !strcmp(g_log[0], "0645000C0001"));
    reset();
    g_gain_tag = "0649";
    ui_set_gain(0);
    assert(g_n == 1 && !strcmp(g_log[0], "0649000C0000"));
    reset();
    g_gain_tag = NULL;
    ui_set_gain(1);
    assert(g_n == 0);
    reset();
    ui_set_gain(-1);
    assert(g_n == 0);
}

static void firmware_gated_setters_refuse(void){
    reset();
    g_settable = 0;
    assert(ui_set_artist_class(1) == -1);
    assert(ui_set_folder_jump(1) == -1);
    ui_set_track_display(1);
    assert(g_n == 0 && g_artist_mode == 0 && g_artist_changed == 0);
    reset();
    g_settable = 1;
    assert(ui_set_artist_class(1) == 0);
    assert(g_n == 1 && !strcmp(g_log[0], "0648000C0001"));
    assert(g_artist_mode == 1 && g_artist_changed == 1);
    assert(ui_set_folder_jump(0) == 0);
    assert(g_n == 2 && !strcmp(g_log[1], "0687000C0000"));
    ui_set_track_display(1);
    assert(g_n == 3 && !strcmp(g_log[2], "064d000C0001"));
    reset();
    g_settable = 1;
    g_fail_next = 1;
    assert(ui_set_artist_class(0) == -1);
    assert(g_artist_mode == 0 && g_artist_changed == 0);
}

static void volume_paths(void){
    reset();
    cfg_put("max_vol", 80);
    ui_set_volume(130);
    assert(g_dacvol_level == 80);
    assert(g_n == 1 && !strcmp(g_log[0], "0715000C0050"));
    reset();
    g_dacvol_owns_rc = 1;
    assert(ui_set_volume(50) == 0);
    assert(g_dacvol_level == 50 && g_n == 0);
    reset();
    ui_set_volume(-5);
    assert(g_dacvol_level == 0);
    assert(g_n == 1 && !strcmp(g_log[0], "0715000C0000"));
    reset();
    cfg_put("max_vol", 5);
    ui_set_volume(130);
    assert(g_dacvol_level == 120);
    assert(g_n == 1 && !strcmp(g_log[0], "0715000C0078"));
    reset();
    playerset_send_volume(200);
    assert(g_n == 1 && !strcmp(g_log[0], "0715000C0078"));
    playerset_send_volume(-3);
    assert(g_n == 2 && !strcmp(g_log[1], "0715000C0000"));
    reset();
    g_owns_volume = 1;
    ui_set_maxvol(80);
    assert(g_n == 0);
    reset();
    ui_set_maxvol(130);
    assert(g_n == 1 && !strcmp(g_log[0], "0711000C0078"));
    playerset_send_maxvol(-5);
    assert(g_n == 2 && !strcmp(g_log[1], "0711000C0000"));
}

static void workmode_persists_and_reports(void){
    reset();
    ui_set_workmode(3);
    assert(g_n == 1 && !strcmp(g_log[0], "0102000C0003"));
    assert(env_get_int("work_mode", -1) == 3);
    assert(ui_play_mode_now() == 3);
    ui_set_workmode(9);
    assert(g_n == 2 && !strcmp(g_log[1], "0102000C0000"));
    assert(env_get_int("work_mode", -1) == 0);
    assert(ui_play_mode_now() == 0);
    g_fail_next = 1;
    ui_set_workmode(2);
    assert(ui_play_mode_now() == -1);
}

static void reapply_order_full(void){
    reset();
    cfg_put("audio_dre", 1);
    cfg_put("audio_gain", 1);
    cfg_put("audio_filter", 2);
    cfg_put("replay_gain", 2);
    cfg_put("work_mode", 3);
    cfg_put("memory_play", 1);
    cfg_put("gapless", 1);
    cfg_put("max_vol", 80);
    cfg_put("balance", -5);
    ui_reapply_audio();
    const char *want[] = {
        "0812000C0000", "0649000C0001", "0653000C0002", "0718000C0002",
        "0102000C0003", "0684000C0001", "0647000C0001", "0711000C0050",
        "0713000C0105",
    };
    expect_frames(want, 9);
    assert(g_route_resets == 1 && g_quiet_begins == 1);
    assert(g_arms == 0 && g_cancels == 1);
    assert(ui_play_mode_now() == 3);
}

static void reapply_order_unmanaged_and_book(void){
    reset();
    ui_reapply_audio();
    const char *want[] = { "0718000C0000" };
    expect_frames(want, 1);
    assert(g_route_resets == 1);
    reset();
    g_book = 1;
    cfg_put("work_mode", 1);
    ui_reapply_audio();
    const char *want2[] = { "0718000C0000", "0102000C0004" };
    expect_frames(want2, 2);
}

static void retry_rules(void){
    reset();
    g_quiet_fails = 1;
    ui_reapply_audio();
    assert(g_arms == 1 && g_quiet_begins == 1 && g_cancels == 1);
    playerset_reapply_retry();
    assert(g_arms == 1 && g_quiet_begins == 2);
    reset();
    g_quiet_fails = 10;
    ui_reapply_audio();
    assert(g_arms == 1 && g_quiet_begins == 1);
    playerset_reapply_retry();
    assert(g_arms == 2 && g_quiet_begins == 2);
    playerset_reapply_retry();
    assert(g_arms == 3 && g_quiet_begins == 3);
    playerset_reapply_retry();
    assert(g_arms == 3 && g_quiet_begins == 4);
    reset();
    g_quiet_fails = 1;
    ui_reapply_audio();
    assert(g_arms == 1 && g_quiet_begins == 1);
    g_blocked = 1;
    playerset_reapply_retry();
    assert(g_arms == 2 && g_quiet_begins == 1);
    g_blocked = 0;
    playerset_reapply_retry();
    assert(g_arms == 2 && g_quiet_begins == 2);
    g_quiet_fails = 1;
    ui_reapply_audio();
    assert(g_cancels == 2 && g_arms == 3 && g_quiet_begins == 3);
}

int main(void){
    playerset_set_env(&env);
    play_mode_fallback_before_any_send();
    frame_formats();
    eq_select_saves_only_on_success();
    gain_tag_gates_the_frame();
    firmware_gated_setters_refuse();
    volume_paths();
    workmode_persists_and_reports();
    reapply_order_full();
    reapply_order_unmanaged_and_book();
    retry_rules();
    puts("playerset_test: ok");
    return 0;
}
