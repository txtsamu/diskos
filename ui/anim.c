/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "anim.h"
#include "theme.h"

#define ANIM_MAX_ACTIVE 4
static void on_done(lv_anim_t *a){
    if(a->user_data){ ((lv_anim_completed_cb_t)a->user_data)(a); }
}

int32_t anim_path_spring(const lv_anim_t *a){ return lv_anim_path_ease_out(a); }
int32_t anim_path_spring_bouncy(const lv_anim_t *a){ return lv_anim_path_overshoot(a); }

static void exec_x(void *o, int32_t v){ lv_obj_set_x((lv_obj_t*)o, (int)v); }
static void exec_y(void *o, int32_t v){ lv_obj_set_y((lv_obj_t*)o, (int)v); }
static void exec_opa(void *o, int32_t v){ lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
static void exec_zoom(void *o, int32_t v){
    lv_obj_set_style_transform_scale_x((lv_obj_t*)o, (int)v, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t*)o, (int)v, 0);
}

static void start(lv_obj_t *o, lv_anim_exec_xcb_t xcb, int from, int to,
                  uint32_t ms, lv_anim_path_cb_t path, lv_anim_completed_cb_t done){
    lv_anim_delete(o, xcb);
    if(lv_anim_count_running() >= ANIM_MAX_ACTIVE){
        xcb(o, to);
        if(done){ lv_anim_t tmp; lv_anim_init(&tmp); tmp.var=o; done(&tmp); }
        return;
    }
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_exec_cb(&a, xcb);
    lv_anim_set_path_cb(&a, path ? path : anim_path_spring);
    a.user_data = (void*)done;
    lv_anim_set_completed_cb(&a, on_done);
    lv_anim_start(&a);
}

void anim_slide_x(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done){
    start(o, exec_x, from, to, ms, anim_path_spring, done);
}
void anim_slide_y(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done){
    start(o, exec_y, from, to, ms, anim_path_spring, done);
}
void anim_fade(lv_obj_t *o, lv_opa_t from, lv_opa_t to, uint32_t ms, lv_anim_completed_cb_t done){
    start(o, exec_opa, from, to, ms, anim_path_spring, done);
}
void anim_scale(lv_obj_t *o, int from_pct, int to_pct, uint32_t ms){
    start(o, exec_zoom, from_pct, to_pct, ms, anim_path_spring_bouncy, NULL);
}

static void press_up(lv_anim_t *a){
    lv_obj_t *o = (lv_obj_t*)a->var;
    start(o, exec_zoom, 240, 256, ANIM_FAST, anim_path_spring_bouncy, NULL);
}
void anim_press(lv_obj_t *o){
    start(o, exec_zoom, 256, 240, 90, anim_path_spring, press_up);
}

void anim_panel_shadow(lv_obj_t *root){
    lv_obj_set_style_border_side(root, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(root, TC(OUTLINE_BRIGHT), 0);
    lv_obj_set_style_border_opa(root, 26, 0);
    anim_panel_hairline(root, 0);
}
void anim_panel_hairline(lv_obj_t *root, int on){
    if(!root) return;
    lv_obj_set_style_border_width(root, on ? 1 : 0, 0);
    lv_obj_set_style_pad_left(root, on ? -1 : 0, 0);
}

static void start_expo(lv_obj_t *o, lv_anim_exec_xcb_t xcb, int from, int to,
                       uint32_t ms, lv_anim_completed_cb_t done){
    lv_anim_delete(o, xcb);
    if(lv_anim_count_running() >= ANIM_MAX_ACTIVE){
        xcb(o, to);
        if(done){ lv_anim_t tmp; lv_anim_init(&tmp); tmp.var=o; done(&tmp); }
        return;
    }
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_exec_cb(&a, xcb);
    lv_anim_set_path_cb(&a, lv_anim_path_custom_bezier3);
    LV_ANIM_SET_EASE_OUT_CUBIC(&a);
    a.user_data = (void*)done;
    lv_anim_set_completed_cb(&a, on_done);
    lv_anim_start(&a);
}

void anim_page_slide(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done){
    start_expo(o, exec_x, from, to, ms, done);
}

void anim_scrim_fade(lv_obj_t *scrim, int in, uint32_t ms){
    start_expo(scrim, exec_opa, in ? 0 : 36, in ? 36 : 0, ms, NULL);
}

static void exec_arc(void *o, int32_t v){ lv_arc_set_value((lv_obj_t*)o, (int)v); }
void anim_arc_value(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done){
    start_expo(o, exec_arc, from, to, ms, done);
}

void anim_zoom(lv_obj_t *o, int in, uint32_t ms, lv_anim_completed_cb_t done){
    start_expo(o, exec_zoom, in ? 246 : 256, in ? 256 : 246, ms, done);
    start_expo(o, exec_opa,  in ? 0   : 255, in ? 255 : 0,   ms, NULL);
}
