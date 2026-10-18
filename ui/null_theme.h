/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_NULL_THEME_H
#define DISKOS_NULL_THEME_H
#include "lvgl/lvgl.h"
void null_radio_skin(lv_obj_t *root,const char *name,lv_obj_t *ring,lv_obj_t *glyph,lv_obj_t *device,lv_obj_t *status,lv_obj_t *toggle,lv_obj_t *scan,lv_obj_t *list);
void null_init(lv_obj_t *parent);
const lv_font_t *null_track_font(void);
void null_home_create(lv_obj_t *root);
void null_saver_create(lv_obj_t *root);
void null_saver_clock(const char *clock, const char *date);
void null_saver_weather(const char *weather);
void null_saver_track(const char *title, const char *artist);
void null_home_weather(const char *weather);
void null_home_clock(const char *time);
void null_home_status(int batt, int charging, int wifi, int bt);
void null_home_update(const char *title, const char *artist, int playing);
void null_art_changed(void);
void null_options_changed(void);
void null_menu_refresh(void);
void null_nav_set_open(int open);
void null_screen_entered(int which);
void null_screen_leaving(void);
int null_owns_point(int x, int y);
void null_scroll_line(lv_obj_t **parts, int count, int position, int total);
int null_volume_visible(void);
void null_volume_show(int value);
void null_qs_battery(int percent, int charging);
void null_qs_create(lv_obj_t *root);
void null_qs_build(void);
void null_qs_refresh(void);
void null_qs_slide(int open);
#endif
