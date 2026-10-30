/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef THEME_KIT_H
#define THEME_KIT_H
#include "lvgl/lvgl.h"

typedef enum {
    UI_CORE,
    UI_REACHABLE,
    UI_DECORATIVE,
} ui_tier_t;

void ui_on(lv_obj_t *obj, lv_event_cb_t cb, lv_event_code_t code, void *user_data, const char *action, ui_tier_t tier);
const char *ui_transport_action(const char *cmd, const char *prefix, char *buf, int cap);

#ifdef DISKOS_HOST
const char *ui_action_of(const lv_obj_t *obj);
lv_obj_t   *ui_action_find(const char *name);
int         ui_action_tier(const lv_obj_t *obj);
#endif

typedef enum {
    KIT_SCREEN,
    KIT_TITLE,
    KIT_BACK,
    KIT_ROW,
    KIT_ROW_TEXT,
    KIT_ROW_SUB,
    KIT_ROW_CHEVRON,
    KIT_BUTTON,
    KIT_ICON_BUTTON,
    KIT_CARD,
    KIT_TEXT,
    KIT_TOGGLE, KIT_SLIDER, KIT_ROLLER, KIT_ARC, KIT_BAR, KIT_TEXTAREA, KIT_KEYBOARD, KIT_IMAGE,
    KIT_SCRIM,
    KIT_POPUP,
    KIT_OTHER,
} kit_role_t;
void kit_pass(lv_obj_t *root);
void kit_restyle(lv_obj_t *root);
void kit_install(void);
void kit_keep(lv_obj_t *obj);
typedef enum { KIT_ROW_PLAIN, KIT_ROW_ICON, KIT_ROW_SWATCH, KIT_ROW_SYMBOL } kit_row_layout_t;
void kit_row_layout(lv_obj_t *row, kit_row_layout_t layout);
kit_row_layout_t kit_row_layout_get(const lv_obj_t *row);
void kit_some_dot_arrow(lv_obj_t *glyph, int left);

typedef struct {
    lv_obj_t *root, *status, *clock, *date, *weather, *search, *library, *hint, *np, *np_thumb, *np_art, *np_glyph,
             *np_title, *np_artist, *pp, *pp_label;
} home_parts_t;

typedef struct {
    lv_obj_t *root, *backdrop, *ring, *cover, *cover_img, *cover_note, *fav, *fav_icon, *sleep, *sleep_icon, *mode,
             *mode_icon, *mode_one, *title, *artist, *album, *prev, *pp, *next, *elapsed, *sep, *remain, *dots[2];
    lv_obj_t *pos;
} np_parts_t;

#define QS_TILES_MAX 6
typedef struct {
    lv_obj_t *root, *grab, *bright, *prev, *pp, *next;
    int n; lv_obj_t *tile[QS_TILES_MAX], *cap[QS_TILES_MAX];
} qs_parts_t;

typedef struct { lv_obj_t *root, *clock, *date, *weather, *track, *artist, *face; int style; } saver_parts_t;
typedef enum { KIT_EMPTY_LYRICS, KIT_EMPTY_UPNEXT, KIT_EMPTY_LIBRARY, KIT_EMPTY_SEARCH } kit_empty_kind_t;
typedef enum { KIT_VALUE_COUNT, KIT_VALUE_DURATION } kit_value_kind_t;
void kit_row_value(lv_obj_t *row, lv_obj_t *title, lv_obj_t *sub, lv_obj_t *value, kit_value_kind_t kind);
void kit_cursor_sync(int backlight_on);
#ifdef DISKOS_HOST
int kit_cursor_host_paused(void);
int kit_cursor_host_on(void);
void kit_cursor_host_phase(int on);
#endif

typedef struct theme_kit {
    const char *id;
    lv_obj_t *(*header)(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);
    void (*style)(lv_obj_t *obj, kit_role_t role);
    void (*home)(const home_parts_t *p);
    void (*home_clock)(const char *time);
    void (*home_progress)(lv_obj_t *pill, int progress, int have_track, lv_color_t accent);
    void (*nowplaying)(const np_parts_t *p);
    void (*np_progress)(int value);
    void (*np_cover_state)(lv_obj_t *cover, lv_obj_t *note, int show, int loading);
    void (*empty_state)(kit_empty_kind_t kind, lv_obj_t *message);
    void (*row_value)(lv_obj_t *row, lv_obj_t *title, lv_obj_t *sub, lv_obj_t *value, kit_value_kind_t kind);
    void (*row_decor)(lv_obj_t *row, lv_obj_t *title);
    void (*quicksettings)(const qs_parts_t *p);
    void (*qs_tile)(lv_obj_t *tile, lv_obj_t *glyph, lv_obj_t *cap, int on, int toggle);
    void (*saver)(const saver_parts_t *p);
    void (*saver_clock)(const char *t);
    void (*pass_end)(void);
    void (*np_state)(int playing, int have_track);
    void (*title)(lv_obj_t *label, const char *text);
    void (*title_fit)(lv_obj_t *label, const char *text);
    const char *back_text;
    int saver_mer_apart;
} theme_kit_t;

const theme_kit_t *theme_kit(void);
extern const theme_kit_t theme_kit_default;
const theme_kit_t *theme_kit_for_active(void);
#endif
