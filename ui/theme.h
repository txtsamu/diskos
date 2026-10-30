/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef THEME_H
#define THEME_H
#include <stdint.h>
#include "lvgl/lvgl.h"

#include "theme_roles.h"

typedef enum {
    THEME_FONT_UI_10, THEME_FONT_UI_12, THEME_FONT_UI_14, THEME_FONT_UI_16, THEME_FONT_UI_18, THEME_FONT_UI_20,
    THEME_FONT_UI_22, THEME_FONT_UI_24, THEME_FONT_UI_28, THEME_FONT_UI_32, THEME_FONT_UI_36, THEME_FONT_UI_40,
    THEME_FONT_USER_14, THEME_FONT_USER_16, THEME_FONT_USER_18, THEME_FONT_USER_20,
    THEME_FONT_HEADER,
    THEME_FONT_CLOCK,
    THEME_FONT_SAVER_CLOCK,
    THEME_FONT_DATE,
    THEME_FONT_ICON_20, THEME_FONT_ICON_28,
    THEME_FONT_HEADER_SM,
    THEME_FONT_COUNT
} theme_font_role_t;
const lv_font_t *theme_font(theme_font_role_t role);
int theme_font_size(void);
#define TF(role) theme_font(THEME_FONT_##role)

void        theme_init(void);
int         theme_variant(void);
int         theme_outdoor(void);
#define THEME_PRESET_COUNT 8
extern const char *const theme_preset_names[THEME_PRESET_COUNT];
int         theme_preset(void);
#define THEME_PRESET_CUSTOM 100
int         theme_custom_active(void);
lv_color_t  theme_preset_color(int preset, theme_color_role_t role);

typedef enum {
    THEME_TRAIT_DOT_GRID,
    THEME_TRAIT_DOT_TITLES,
    THEME_TRAIT_MATRIX_CLOCK,
    THEME_TRAIT_QS_RING,
    THEME_TRAIT_FLAT_LISTS,
    THEME_TRAIT_NP_BIG_PLAY,
    THEME_TRAIT_COUNT
} theme_trait_t;
int         theme_trait(theme_trait_t t);
typedef enum { THEME_TEX_NONE, THEME_TEX_DOTS, THEME_TEX_GRID, THEME_TEX_SCANLINES, THEME_TEX_PAPER, THEME_TEX_BEZEL } theme_texture_t;
typedef enum { THEME_CASE_KEEP, THEME_CASE_UPPER, THEME_CASE_LOWER } theme_case_t;
enum { THEME_PLAIN_HOME = 1, THEME_PLAIN_LIBRARY = 2, THEME_PLAIN_NOWPLAYING = 4 };
struct theme_kit;
typedef struct theme_def {
    const char *name;
    struct {
        const lv_font_t *face;
        uint8_t px;
    } font[THEME_FONT_COUNT];
    unsigned traits;
    theme_texture_t texture;
    theme_case_t title_case;
    unsigned plain;
    int fixed_accent;
    const struct theme_kit *kit;
} theme_def_t;
const theme_def_t *theme_def(void);
int         theme_screen_plain(unsigned which);
const lv_font_t *theme_font_substitute(const lv_font_t *f);   
const theme_def_t *theme_def_find(const char *name);
void        theme_list_row(lv_obj_t *row);
char       *theme_lower(char *dst, int cap, const char *src);
void        theme_case_text(lv_obj_t *label, const char *text);
const lv_font_t *theme_font_smaller(const lv_font_t *f);
const lv_font_t *theme_title_smaller(const lv_font_t *f);
extern const char theme_title_tag;
#define THEME_TITLE_STEPPED LV_OBJ_FLAG_USER_3
void        theme_title_text(lv_obj_t *label, const char *text);
char       *theme_upper(char *dst, int cap, const char *src);
void        theme_screen_bg(lv_obj_t *root);
void        theme_screen_solid(lv_obj_t *root);
const lv_font_t *theme_font_base(int px);
lv_color_t  theme_color(theme_color_role_t role);
lv_color_t  theme_color_from_rgb(uint32_t rgb);
lv_color_t  theme_on_color(lv_color_t bg);
lv_color_t  theme_accent_from_rgb(uint32_t rgb);
lv_color_t  theme_accent_from_hsv(uint16_t h, uint8_t s, uint8_t v);
uint32_t    theme_rgb(theme_color_role_t role);
#define TC(role) theme_color(THEME_CLR_##role)

#endif
