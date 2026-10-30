/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "ipc.h"
#include "dacinfo.h"
#include <stdio.h>

#define TICK_MS 500

static lv_obj_t *g_rate, *g_fmt_pill, *g_fmt, *g_vol_lbl, *g_vol_bar, *g_note, *g_btn;
static lv_timer_t *g_timer;

static void show_stream(int on){
    lv_obj_t *w[] = { g_rate, g_fmt_pill, g_vol_lbl, g_vol_bar };
    for(unsigned i = 0; i < sizeof w / sizeof w[0]; i++){
        if(on) lv_obj_clear_flag(w[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(w[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_note(const char *text, int show_btn){
    lv_label_set_text(g_note, text);
    lv_obj_clear_flag(g_note, LV_OBJ_FLAG_HIDDEN);
    if(show_btn) lv_obj_clear_flag(g_btn, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_btn, LV_OBJ_FLAG_HIDDEN);
}

static void refresh(void){
    if(!g_rate) return;
    if(ui_get_source_mode() != 1){
        show_stream(0);
        set_note("USB DAC mode is off", 1);
        return;
    }
    track_state_t st; ipc_get_state(&st);
    dac_stream_t s = dacinfo_get();

    char vb[24]; snprintf(vb, sizeof vb, "Volume  %d", st.volume);
    lv_label_set_text(g_vol_lbl, vb);
    lv_bar_set_value(g_vol_bar, st.volume < 0 ? 0 : st.volume > VOL_MAX ? VOL_MAX : st.volume, LV_ANIM_OFF);

    if(s.rate_hz <= 0){
        lv_obj_add_flag(g_rate, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_fmt_pill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_vol_lbl, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(g_vol_bar, LV_OBJ_FLAG_HIDDEN);
        set_note("Waiting for the computer or phone to play", 0);
        return;
    }
    lv_obj_add_flag(g_note, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_btn, LV_OBJ_FLAG_HIDDEN);
    show_stream(1);
    char rb[24]; dacinfo_format_rate(rb, sizeof rb, s.rate_hz);
    lv_label_set_text(g_rate, rb);
    lv_label_set_text(g_fmt, dacinfo_fmt_name(s.fmt));
}

static void tick_cb(lv_timer_t *t){ (void)t; refresh(); }

static void open_modes_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) modes_open(); }

void dac_visible(int on){
    if(on){
        if(!g_timer) g_timer = lv_timer_create(tick_cb, TICK_MS, NULL);
        refresh();
    } else if(g_timer){
        lv_timer_delete(g_timer); g_timer = NULL;
    }
}

void dac_open(void){
    refresh();
    screen_show(SCR_DAC);
}

void dac_create(lv_obj_t *root){
    ui_header(root, "USB DAC");
    dacinfo_init();

    g_rate = lv_label_create(root);
    lv_obj_set_width(g_rate, 260); lv_obj_set_style_text_align(g_rate, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_rate, TF(UI_24), 0);
    lv_obj_set_style_text_color(g_rate, ui_current_accent(), 0);
    lv_obj_align(g_rate, LV_ALIGN_TOP_MID, 0, 96);

    g_fmt_pill = lv_obj_create(root);
    lv_obj_remove_style_all(g_fmt_pill);
    lv_obj_set_size(g_fmt_pill, 84, 34);
    lv_obj_set_style_radius(g_fmt_pill, 17, 0);
    lv_obj_set_style_bg_color(g_fmt_pill, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(g_fmt_pill, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_fmt_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(g_fmt_pill, LV_ALIGN_TOP_MID, 0, 138);
    g_fmt = lv_label_create(g_fmt_pill);
    lv_obj_set_style_text_font(g_fmt, TF(UI_24), 0);
    lv_obj_set_style_text_color(g_fmt, TC(TEXT_PRIMARY), 0);
    lv_obj_center(g_fmt);

    g_vol_lbl = lv_label_create(root);
    lv_obj_set_width(g_vol_lbl, 240); lv_obj_set_style_text_align(g_vol_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_vol_lbl, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_vol_lbl, LV_ALIGN_TOP_MID, 0, 196);

    g_vol_bar = lv_bar_create(root);
    lv_obj_set_size(g_vol_bar, 220, 10);
    lv_bar_set_range(g_vol_bar, 0, VOL_MAX);
    lv_obj_set_style_radius(g_vol_bar, 5, LV_PART_MAIN); lv_obj_set_style_radius(g_vol_bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_vol_bar, TC(SURFACE_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_vol_bar, ui_current_accent(), LV_PART_INDICATOR);
    lv_obj_align(g_vol_bar, LV_ALIGN_TOP_MID, 0, 224);

    g_note = lv_label_create(root);
    lv_obj_set_width(g_note, 250); lv_label_set_long_mode(g_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_note, TC(TEXT_MUTED), 0);
    lv_obj_align(g_note, LV_ALIGN_TOP_MID, 0, 128);

    g_btn = lv_button_create(root);
    lv_obj_set_size(g_btn, 190, 44);
    lv_obj_set_style_shadow_width(g_btn, 0, 0);
    lv_obj_align(g_btn, LV_ALIGN_TOP_MID, 0, 196);
    lv_color_t acc = ui_current_accent();
    lv_obj_set_style_bg_color(g_btn, acc, 0);
    lv_obj_set_style_bg_opa(g_btn, LV_OPA_COVER, 0);
    ui_on(g_btn, open_modes_cb, LV_EVENT_CLICKED, NULL, "dac_ui.open_modes", UI_CORE);
    lv_obj_t *bl = lv_label_create(g_btn); lv_label_set_text(bl, "Working Mode"); lv_obj_center(bl);
    lv_obj_set_style_text_color(bl, theme_on_color(acc), 0);

    lv_obj_add_flag(g_btn, LV_OBJ_FLAG_HIDDEN);
    refresh();
}
