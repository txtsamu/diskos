/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef ANIM_H
#define ANIM_H
#include "lvgl/lvgl.h"

#define ANIM_FAST   180
#define ANIM_BASE   260
#define ANIM_SLOW   340

int32_t anim_path_spring(const lv_anim_t *a);
int32_t anim_path_spring_bouncy(const lv_anim_t *a);

void anim_slide_x(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done);
void anim_slide_y(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done);
void anim_fade(lv_obj_t *o, lv_opa_t from, lv_opa_t to, uint32_t ms, lv_anim_completed_cb_t done);
void anim_scale(lv_obj_t *o, int from_pct, int to_pct, uint32_t ms);
void anim_press(lv_obj_t *o);

void anim_panel_shadow(lv_obj_t *root);
void anim_panel_hairline(lv_obj_t *root, int on);
void anim_page_slide(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done);
void anim_scrim_fade(lv_obj_t *scrim, int in, uint32_t ms);
void anim_zoom(lv_obj_t *o, int in, uint32_t ms, lv_anim_completed_cb_t done);
void anim_arc_value(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done);

#endif
