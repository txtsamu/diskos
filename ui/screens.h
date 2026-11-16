/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef SCREENS_H
#define SCREENS_H
#include "lvgl/lvgl.h"
#include "srcswitch.h"
#include "ipc.h"
#include "fontchain.h"
#include "transport.h"
#include <stdbool.h>
enum { SCR_HOME, SCR_LIBRARY, SCR_NOWPLAYING, SCR_SETTINGS, SCR_SETTING_DETAIL, SCR_SEARCH, SCR_SAVER, SCR_QUICK, SCR_SONGINFO, SCR_NPMENU, SCR_TUNE, SCR_EQ, SCR_APPS, SCR_NPHUB, SCR_PLPICK, SCR_PLVIEW, SCR_WIFI, SCR_WIFI_INFO, SCR_BT, SCR_BT_INFO, SCR_WEATHER, SCR_LYRICS, SCR_COLORPICK, SCR_WORKMODE, SCR_DEBUG, SCR_FOLDER, SCR_BOOKS, SCR_CHAPTERS, SCR_SETLIST, SCR_QSCONFIG, SCR_ALBUMWALL, SCR_UPNEXT, SCR_DATETIME, SCR_TAMA, SCR_DAC, SCR_SETSUB, SCR_COUNT };
void screens_init(void);
void screen_show(int which);
void screen_back(void);
void screen_set_anim(int on);
int  screen_current(void);
void ui_toast(const char *msg);
void splash_start(void);
int  mclock_new(int h, lv_obj_t *parent, int y);
void mclock_set(int h, const char *time_text);
void mclock_place(int h, int y, int shown);
int  ui_edge_nav(void);
int  ui_round_width(int y, int margin);
void ui_back_glyph(lv_obj_t *label);
int  ui_toast_hint(const char *msg);
void ui_status_refresh(void);
lv_obj_t *screen_get_root(int which);
void ui_rtc_save_now(void);
void ui_note_transport_sent(void);
const lv_image_dsc_t *sharp_cover_get(void);
void chargemode_boot(void);
void chargemode_tick(void);
int  chargemode_active(void);
int  ui_detect_source_mode(void);
void ui_set_favorite(int on);
#define CFG_PDBG "pdbg"
void ui_timekeep_save_now(void);
const char *ui_route_mac(void);
void app_launch(const char *exec);

lv_color_t ui_current_accent(void);
lv_obj_t *ui_header(lv_obj_t *root, const char *title);
lv_obj_t *ui_header_cb(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);
int  ui_run_bounded(char *const argv[], int timeout_ms);

void ui_set_dre(int on);
void ui_set_gain(int high);
void ui_set_output(int spdif);
int  ui_route_bt(const char *mac);
int  ui_route_analog(void);
void ui_set_dac_filter(int idx);
void ui_set_replay_gain(int v);
void ui_set_gapless(int on);
void ui_set_memory(int mode);
void ui_set_maxvol(int v);
void ui_set_balance(int v);
void ui_reapply_audio(void);
void ui_request_sleep(void);
void ui_set_brightness(int v);
int  ui_get_brightness(void);
int  ui_effective_brightness(void);
void ui_backlight(int v);
int  ui_backlight_written(void);
unsigned ui_backlight_write_seq(void);
int  ui_backlight_take_foreign(int *dark, unsigned *seq);
void ui_set_sleep_timer(int minutes);
void ui_set_sleep_eoc(long target_ms, const char *path, int at_book_end);
void ui_disarm_book_eoc(void);
int  ui_book_active(void);
int  ui_sleep_state(int *secs_left);
void ui_np_close_overlays(void);
int  ui_np_overlay_active(void);
long ui_smart_rewind_ms(long idle_seconds);
void ui_defer_sleep(void);
typedef void (*kbinput_done_cb_t)(const char *text);
void kbinput_open(const char *title, const char *initial, kbinput_done_cb_t cb);
void kbinput_open_password(const char *title, const char *initial, kbinput_done_cb_t cb);
int  kbinput_active(void);
void ui_clock_refresh(void);
int ui_seek_to(long ms);
void ui_cancel_book_resume(void);
void ui_book_user_seeked(long target_ms);
int  ui_set_volume(int vol);
void ui_set_workmode(int mode);
int ui_apply_eq(int preset);
int ui_eq_select(int preset);
int ui_is_playing(void);
void ui_pp_glyph(lv_obj_t *label, int playing);
int  ui_glyph_ink_dy(const lv_font_t *f, uint32_t cp);
void ui_glyph_center_ink(lv_obj_t *label);
const char *ui_eq_name(int i);
int  ui_theme_reload(const char *screen);
int  ui_play_mode_now(void);
int  ui_queue_jump(int ord1);
int  ui_play_list(int list_type, const char *name, int pos1);
int  ui_play_favorite(int love_id, int pos1);
int  ui_press_travel(void);
int  ui_play_song_by_path(const char *path);
int  ui_play_playlist(long pid, int pos);
void ui_set_swipe_thresh(int px);
void ui_apply_swipe_thresh(int px);
int  ui_get_swipe_thresh(void);
void ui_set_accent_config(int mode, int rgb);
int  ui_accent_is_static(void);
int  ui_run_cap_bounded(const char *cmd, char *out, int cap, int timeout_ms);
int  ui_main_is_idle(void);
void ui_show_volume(int vol);
#endif
