/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "playerset.h"
#include "ipc.h"
#include <stdio.h>

static const struct playerset_env *E;
void playerset_set_env(const struct playerset_env *env){ E = env; }

int playerset_frame_eq(char *buf, size_t n, int preset){
    if(preset < 0) preset = 0;
    if(preset > 20) preset = 20;
    return snprintf(buf, n, "0689000C%04X", (unsigned)preset) > 0 ? 0 : -1;
}
int playerset_frame_dre(char *buf, size_t n, int on){
    if(on < 0) return -1;
    return snprintf(buf, n, "%s", on ? "0812000C0000" : "0812000C0001") > 0 ? 0 : -1;
}
int playerset_frame_gain(char *buf, size_t n, const char *tag, int high){
    if(!tag) return -1;
    return snprintf(buf, n, "%s000C%04X", tag, high ? 1u : 0u) > 0 ? 0 : -1;
}
int playerset_frame_dac_filter(char *buf, size_t n, int idx){
    if(idx < 0 || idx > 5) return -1;
    return snprintf(buf, n, "0653000C%04X", (unsigned)idx) > 0 ? 0 : -1;
}
int playerset_frame_replay_gain(char *buf, size_t n, int v){
    if(v < 0 || v > 2) return -1;
    return snprintf(buf, n, "0718000C%04X", (unsigned)v) > 0 ? 0 : -1;
}
int playerset_frame_gapless(char *buf, size_t n, int on){
    if(on < 0) return -1;
    return snprintf(buf, n, "%s", on ? "0647000C0001" : "0647000C0000") > 0 ? 0 : -1;
}
int playerset_frame_artist_class(char *buf, size_t n, int album_artist){
    return snprintf(buf, n, "%s", album_artist ? "0648000C0001" : "0648000C0000") > 0 ? 0 : -1;
}
int playerset_frame_folder_jump(char *buf, size_t n, int on){
    return snprintf(buf, n, "%s", on ? "0687000C0001" : "0687000C0000") > 0 ? 0 : -1;
}
int playerset_frame_track_display(char *buf, size_t n, int on){
    return snprintf(buf, n, "%s", on ? "064d000C0001" : "064d000C0000") > 0 ? 0 : -1;
}
int playerset_frame_memory(char *buf, size_t n, int mode){
    if(mode < 0 || mode > 2) return -1;
    return snprintf(buf, n, "0684000C%04X", (unsigned)mode) > 0 ? 0 : -1;
}
int playerset_frame_maxvol(char *buf, size_t n, int v){
    if(v < 0) v = 0;
    if(v > VOL_MAX) v = VOL_MAX;
    return snprintf(buf, n, "0711000C%04X", (unsigned)v) > 0 ? 0 : -1;
}
int playerset_frame_balance(char *buf, size_t n, int v){
    if(v < -10 || v > 10) return -1;
    int fv = (v > 0) ? (v & 0xFF) : (v < 0) ? (0x0100 | ((-v) & 0xFF)) : 0;
    return snprintf(buf, n, "0713000C%04X", (unsigned)fv) > 0 ? 0 : -1;
}
int playerset_frame_play_mode(char *buf, size_t n, int mode){
    return snprintf(buf, n, "0102000C%04X", (unsigned)(mode & 0xFFFF)) > 0 ? 0 : -1;
}
int playerset_frame_volume(char *buf, size_t n, int vol){
    return snprintf(buf, n, "0715000C%04X", (unsigned)vol) > 0 ? 0 : -1;
}
int playerset_frame_favorite(char *buf, size_t n, int on){
    return snprintf(buf, n, "%s", on ? "0104000C0001" : "0104000C0000") > 0 ? 0 : -1;
}

int playerset_send_volume(int vol){
    vol = vol < 0 ? 0 : vol > VOL_MAX ? VOL_MAX : vol;
    char f[16];
    if(playerset_frame_volume(f, sizeof f, vol) != 0) return -1;
    int rc = E->send(f);
    fprintf(stderr, "set volume %d -> %s (rc=%d)\n", vol, f, rc); fflush(stderr);
    return rc;
}
int playerset_send_maxvol(int v){
    char f[16];
    if(playerset_frame_maxvol(f, sizeof f, v) != 0) return -1;
    return E->send(f);
}

static int g_mode_applied = -2;
int playerset_send_play_mode(int mode){
    char m[16];
    if(playerset_frame_play_mode(m, sizeof m, mode) != 0) return -1;
    int rc = E->send(m);
    g_mode_applied = (rc == 0 && mode >= 0 && mode <= 4) ? mode : -1;
    return rc;
}
int ui_play_mode_now(void){
    if(g_mode_applied >= 0) return g_mode_applied;
    if(E->book_active()) return -1;
    int wm = E->get_int("work_mode", -1);
    if(wm < 0) return E->saved_play_mode();
    if(g_mode_applied == -2 && wm <= 4) return wm;
    return -1;
}

int ui_apply_eq(int preset){
    if(preset < 0) preset = 0;
    if(preset > 20) preset = 20;
    char f[16];
    if(playerset_frame_eq(f, sizeof f, preset) != 0) return -1;
    int rc = E->send(f);
    fprintf(stderr,"eq %d -> %s\n", preset, f); fflush(stderr);
    return rc;
}
int ui_eq_select(int preset){
    if(preset < 0) preset = 0;
    if(preset > 20) preset = 20;
    if(ui_apply_eq(preset) < 0) return -1;
    if(preset > 0) E->set_int_deferred("eq_last", preset);
    E->set_int_deferred("eq_preset", preset);
    E->cfg_flush();
    return 0;
}
void ui_set_dre(int on){
    char f[16];
    if(playerset_frame_dre(f, sizeof f, on) != 0) return;
    E->send(f);
}
void ui_set_gain(int high){
    if(high < 0) return;
    const char *tag = E->gain_tag();
    if(!tag){ fprintf(stderr, "gain: unverified firmware (MAIN_OS_VER=%d) -> not sending\n", E->os_ver()); fflush(stderr); return; }
    char f[16];
    if(playerset_frame_gain(f, sizeof f, tag, high) != 0) return;
    int rc = E->send(f);
    fprintf(stderr, "gain: %s -> %s (rc=%d)\n", f, high ? "high" : "low", rc); fflush(stderr);
}
void ui_set_dac_filter(int idx){
    char f[16];
    if(playerset_frame_dac_filter(f, sizeof f, idx) != 0) return;
    E->send(f);
}
void ui_set_replay_gain(int v){
    char f[16];
    if(playerset_frame_replay_gain(f, sizeof f, v) != 0) return;
    E->send(f);
}
void ui_set_gapless(int on){
    char f[16];
    if(playerset_frame_gapless(f, sizeof f, on) != 0) return;
    E->send(f);
}
int ui_set_artist_class(int album_artist){
    if(!E->artist_class_settable()){ fprintf(stderr, "artist class: unverified firmware (MAIN_OS_VER=%d)\n", E->os_ver()); return -1; }
    char f[16];
    if(playerset_frame_artist_class(f, sizeof f, album_artist) != 0) return -1;
    if(E->send(f) < 0) return -1;
    E->artist_mode_set(album_artist);
    E->artist_mode_changed();
    return 0;
}
int ui_set_folder_jump(int on){
    if(!E->folder_jump_settable()){ fprintf(stderr, "folder jump: unverified firmware (MAIN_OS_VER=%d)\n", E->os_ver()); return -1; }
    char f[16];
    if(playerset_frame_folder_jump(f, sizeof f, on) != 0) return -1;
    return E->send(f) < 0 ? -1 : 0;
}
void ui_set_track_display(int on){
    if(!E->track_display_settable()) return;
    char f[16];
    if(playerset_frame_track_display(f, sizeof f, on) != 0) return;
    E->send(f);
}
void ui_set_memory(int mode){
    char f[16];
    if(playerset_frame_memory(f, sizeof f, mode) != 0) return;
    E->send(f);
}
void ui_set_maxvol(int v){
    if(E->dacvol_owns_volume()) return;
    playerset_send_maxvol(v);
}
void ui_set_balance(int v){
    char f[16];
    if(playerset_frame_balance(f, sizeof f, v) != 0) return;
    E->send(f);
}
int ui_set_volume(int vol){
    vol = vol < 0 ? 0 : vol > VOL_MAX ? VOL_MAX : vol;
    { int mv = E->get_int("max_vol", -1); if(mv >= 10 && vol > mv) vol = mv; }
    if(E->dacvol_set_user(vol)) return 0;
    return playerset_send_volume(vol);
}
void ui_set_favorite(int on){
    char f[16];
    if(playerset_frame_favorite(f, sizeof f, on) != 0) return;
    E->send(f);
}
void ui_set_workmode(int mode){
    if(mode < 0 || mode > 4) mode = 0;
    E->set_int("work_mode", mode);
    int rc = playerset_send_play_mode(mode);
    fprintf(stderr,"workmode %d -> 0102000C%04X (rc=%d)\n", mode, mode, rc); fflush(stderr);
}

static int g_reapply_tries;
static void reapply_sends(void){
    int dre = E->get_int("audio_dre", -1), gain = E->get_int("audio_gain", -1), filter = E->get_int("audio_filter", -1);
    if(dre >= 0)    ui_set_dre(dre);
    if(gain >= 0)   ui_set_gain(gain);
    if(filter >= 0) ui_set_dac_filter(filter);
    ui_set_replay_gain(E->get_int("replay_gain", 0));
    {
        int wm = E->get_int("work_mode", -1);
        if(E->book_active()) playerset_send_play_mode(4);
        else if(wm >= 0 && wm <= 4) ui_set_workmode(wm);
        int mp = E->get_int("memory_play", -1); if(mp >= 0)             ui_set_memory(mp);
        int gp = E->get_int("gapless", -1);     if(gp >= 0)             ui_set_gapless(gp);
        int mv = E->get_int("max_vol", -1);     if(mv >= 10)            ui_set_maxvol(mv);
        int bl = E->get_int("balance", -100);   if(bl >= -10 && bl<=10) ui_set_balance(bl);
    }
}
static void reapply_audio_body(void){
    g_mode_applied = -1;
    E->route_reset();
}
static void reapply_later(void){ E->retry_arm(); }
static void reapply_send_all(void){
    E->quiet_begin();
    reapply_sends();
    int fails = E->quiet_end();
    if(fails && g_reapply_tries < PLAYERSET_RETRY_MAX){ g_reapply_tries++; reapply_later(); }
    else g_reapply_tries = 0;
    if(fails) fprintf(stderr, "reapply: %d sends failed (try %d)\n", fails, g_reapply_tries);
}
void ui_reapply_audio(void){
    E->retry_cancel();
    g_reapply_tries = 0;
    reapply_audio_body();
    reapply_send_all();
}
void playerset_reapply_retry(void){
    if(E->output_idle_blocked()){ reapply_later(); return; }
    reapply_send_all();
}
