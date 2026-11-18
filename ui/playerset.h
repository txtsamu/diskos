/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_PLAYERSET_H
#define DISKOS_PLAYERSET_H
#include <stddef.h>

#define PLAYERSET_RETRY_MS  1500u
#define PLAYERSET_RETRY_MAX 3

struct playerset_env {
    int         (*send)(const char *frame);
    void        (*quiet_begin)(void);
    int         (*quiet_end)(void);
    const char *(*gain_tag)(void);
    int         (*os_ver)(void);
    int         (*artist_class_settable)(void);
    int         (*folder_jump_settable)(void);
    int         (*track_display_settable)(void);
    int         (*get_int)(const char *key, int def);
    void        (*set_int)(const char *key, int v);
    void        (*set_int_deferred)(const char *key, int v);
    void        (*cfg_flush)(void);
    int         (*dacvol_owns_volume)(void);
    int         (*dacvol_set_user)(int level);
    void        (*artist_mode_set)(int album_artist);
    void        (*artist_mode_changed)(void);
    void        (*route_reset)(void);
    int         (*book_active)(void);
    int         (*saved_play_mode)(void);
    int         (*output_idle_blocked)(void);
    void        (*retry_arm)(void);
    void        (*retry_cancel)(void);
};
void playerset_set_env(const struct playerset_env *env);

int playerset_frame_eq(char *buf, size_t n, int preset);
int playerset_frame_dre(char *buf, size_t n, int on);
int playerset_frame_gain(char *buf, size_t n, const char *tag, int high);
int playerset_frame_dac_filter(char *buf, size_t n, int idx);
int playerset_frame_replay_gain(char *buf, size_t n, int v);
int playerset_frame_gapless(char *buf, size_t n, int on);
int playerset_frame_artist_class(char *buf, size_t n, int album_artist);
int playerset_frame_folder_jump(char *buf, size_t n, int on);
int playerset_frame_track_display(char *buf, size_t n, int on);
int playerset_frame_memory(char *buf, size_t n, int mode);
int playerset_frame_maxvol(char *buf, size_t n, int v);
int playerset_frame_balance(char *buf, size_t n, int v);
int playerset_frame_play_mode(char *buf, size_t n, int mode);
int playerset_frame_volume(char *buf, size_t n, int vol);
int playerset_frame_favorite(char *buf, size_t n, int on);

int playerset_send_volume(int vol);
int playerset_send_maxvol(int v);
int playerset_send_play_mode(int mode);

int  ui_apply_eq(int preset);
int  ui_eq_select(int preset);
void ui_set_dre(int on);
void ui_set_gain(int high);
void ui_set_dac_filter(int idx);
void ui_set_replay_gain(int v);
void ui_set_gapless(int on);
int  ui_set_artist_class(int album_artist);
int  ui_set_folder_jump(int on);
void ui_set_track_display(int on);
void ui_set_memory(int mode);
void ui_set_maxvol(int v);
void ui_set_balance(int v);
int  ui_set_volume(int vol);
void ui_set_workmode(int mode);
void ui_set_favorite(int on);
int  ui_play_mode_now(void);
void ui_reapply_audio(void);
void playerset_reapply_retry(void);
#endif
