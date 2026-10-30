/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "musicdb.h"
#include "fwcaps.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int ui_apply_eq(int preset);
#define NBAND 10
#define SLOT_MIN 11
#define SLOT_MAX 20

static const int         FREQHZ[NBAND] = { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
static const char *const FREQLBL[NBAND]= { "32","64","125","250","500","1k","2k","4k","8k","16k" };

static int       g_slot = SLOT_MIN;
static lv_obj_t *g_band[NBAND];
static lv_obj_t *g_bval[NBAND];
static lv_obj_t *g_master;
static lv_obj_t *g_mval;
static lv_obj_t *g_slotlbl;
static int       g_curve_known = 1;
static int       g_slot_parametric = 0;
static int       g_slot_foreign = 0;

static void band_key(char *buf, int n, int slot, int band){ snprintf(buf, n, "eq%d_b%d", slot-10, band); }
static void master_key(char *buf, int n, int slot){ snprintf(buf, n, "eq%d_master", slot-10); }

#define EQ_ABSENT (-1000)
static void migrate_legacy_user1(void){
    int moved = 0;
    for(int i=0;i<NBAND;i++){
        char nk[24]; band_key(nk, sizeof nk, SLOT_MIN, i);
        if(cfg_get_int(nk, EQ_ABSENT) != EQ_ABSENT) continue;
        char ok[16]; snprintf(ok, sizeof ok, "eq_b%d", i);
        int lv = cfg_get_int(ok, EQ_ABSENT);
        if(lv != EQ_ABSENT){ cfg_set_int_deferred(nk, lv); moved = 1; }
    }
    char nmk[24]; master_key(nmk, sizeof nmk, SLOT_MIN);
    if(cfg_get_int(nmk, EQ_ABSENT) == EQ_ABSENT){
        int lm = cfg_get_int("eq_master", EQ_ABSENT);
        if(lm != EQ_ABSENT){ cfg_set_int_deferred(nmk, lm); moved = 1; }
    }
    if(moved) cfg_flush();
}

static int apply_now(void){
    if(!fw_custom_peq_writable()) return 0;
    char json[1100]; int n = 0;
    n += snprintf(json+n, sizeof json-n, "[");
    for(int i=0;i<NBAND;i++){
        if(n < 0 || n > (int)sizeof json - 90) return 0;
        int g = g_band[i] ? lv_slider_get_value(g_band[i]) : 0;
        n += snprintf(json+n, sizeof json-n,
            "%s{\"filterType\":0,\"frequency\":%d,\"position\":%d,\"gain\":\"%d.0\",\"qValue\":\"0.7\"}",
            i?",":"", FREQHZ[i], i, g);
    }
    if(n < 0 || n > (int)sizeof json - 2) return 0;
    n += snprintf(json+n, sizeof json-n, "]");
    int master = g_master ? lv_slider_get_value(g_master) : 0;
    if(mdb_set_peq(g_slot, (double)master, json)){
        if(ui_eq_select(g_slot) < 0) return 0;
        return 1;
    }
    return 0;
}

static void show_curve(const int *gains, int master){
    for(int i=0;i<NBAND;i++){
        int v = gains ? gains[i] : 0;
        if(v < -12) v = -12; else if(v > 12) v = 12;
        if(g_band[i]) lv_slider_set_value(g_band[i], v, LV_ANIM_OFF);
        if(g_bval[i]){ char b[8]; snprintf(b,sizeof b,"%+d", v); lv_label_set_text(g_bval[i], v?b:"0"); }
    }
    if(master < -12) master = -12; else if(master > 12) master = 12;
    if(g_master) lv_slider_set_value(g_master, master, LV_ANIM_OFF);
    if(g_mval){ char b[8]; snprintf(b,sizeof b,"%+d", master); lv_label_set_text(g_mval, master?b:"0"); }
}

static int slot_has_cfg(int slot){
    char k[24];
    for(int i=0;i<NBAND;i++){ band_key(k,sizeof k,slot,i); if(cfg_get_int(k,EQ_ABSENT)!=EQ_ABSENT) return 1; }
    master_key(k,sizeof k,slot); return cfg_get_int(k,EQ_ABSENT)!=EQ_ABSENT;
}

static void persist_slot(int slot){
    char k[24];
    for(int i=0;i<NBAND;i++){ int v=g_band[i]?lv_slider_get_value(g_band[i]):0; band_key(k,sizeof k,slot,i); cfg_set_int_deferred(k,v); }
    int m=g_master?lv_slider_get_value(g_master):0; master_key(k,sizeof k,slot); cfg_set_int_deferred(k,m);
    cfg_flush();
}

static void load_slot(int slot){
    g_slot_parametric = 0;
    g_slot_foreign = 0;
    if(slot_has_cfg(slot)){
        int cg[NBAND]; char k[24];
        for(int i=0;i<NBAND;i++){ band_key(k,sizeof k,slot,i); cg[i]=cfg_get_int(k,0); }
        char mk[24]; master_key(mk,sizeof mk,slot);
        int cm = cfg_get_int(mk,0);
        int dg[NBAND] = {0}, dm = 0;
        int r  = mdb_get_peq(slot, &dm, dg);
        int gr = (r > 0) ? mdb_peq_is_graphic(slot)
                         : (r == 0 ? 1 : -1);
        if(r < 0 || gr < 0){
            show_curve(cg, cm); g_curve_known = 0; g_slot_foreign = 1;
        } else if(gr == 0){
            show_curve(dg, dm); g_slot_parametric = 1;
            g_curve_known = 0;  g_slot_foreign = 1;
        } else if(dm == cm && memcmp(dg, cg, sizeof dg) == 0){
            show_curve(cg, cm); g_curve_known = 1;
        } else {
            show_curve(dg, dm); g_curve_known = 1; g_slot_foreign = 1;
        }
    } else {
        int g[NBAND], m=0;
        int r = mdb_get_peq(slot,&m,g);
        if(r > 0){
            show_curve(g, m);
            int gr = mdb_peq_is_graphic(slot);
            g_slot_parametric = (gr == 0);
            g_curve_known = (gr == 1);
        }
        else if(r == 0) { show_curve(NULL, 0); g_curve_known = 1; }
        else            { show_curve(NULL, 0); g_curve_known = 0; }
    }
}

static void set_slot(int slot){
    if(slot < SLOT_MIN) slot = SLOT_MIN;
    if(slot > SLOT_MAX) slot = SLOT_MAX;
    g_slot = slot;
    cfg_set_int("eq_slot", g_slot);
    if(g_slotlbl){ char b[16]; snprintf(b,sizeof b,"USER%d", g_slot-10); lv_label_set_text(g_slotlbl, b); }
    load_slot(g_slot);
    if(!fw_custom_peq_writable()) return;
    if(slot_has_cfg(g_slot) && !g_slot_foreign){
        if(!apply_now()) ui_toast("Couldn't apply EQ");
    } else if(ui_eq_select(g_slot) < 0){
        ui_toast("Couldn't apply EQ");
    }
}

static void slot_dir_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    set_slot(g_slot + dir);
}

static void band_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    int v = lv_slider_get_value(lv_event_get_target(e));
    lv_obj_t *lbl = (i==NBAND) ? g_mval : (i>=0 && i<NBAND ? g_bval[i] : NULL);
    if(lbl){ char b[8]; snprintf(b,sizeof b,"%+d", v); lv_label_set_text(lbl, v?b:"0"); }
}
static void band_release_cb(lv_event_t *e){
    (void)e;
    if(!fw_custom_peq_writable()){ ui_toast("Custom EQ: not on this firmware yet"); load_slot(g_slot); return; }
    if(!g_curve_known){
        ui_toast(g_slot_parametric ? "Advanced preset - edit it on the stock player" : "EQ read failed, try again");
        load_slot(g_slot); return;
    }
    if(apply_now()) persist_slot(g_slot);
    else ui_toast("Couldn't apply EQ");
}

static void flat_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(!fw_custom_peq_writable()){ ui_toast("Custom EQ: not on this firmware yet"); return; }
    for(int i=0;i<NBAND;i++){
        lv_slider_set_value(g_band[i], 0, LV_ANIM_OFF);
        if(g_bval[i]) lv_label_set_text(g_bval[i], "0");
    }
    lv_slider_set_value(g_master, 0, LV_ANIM_OFF);
    if(g_mval) lv_label_set_text(g_mval, "0");
    if(apply_now()){ persist_slot(g_slot); g_curve_known = 1; }
    else ui_toast("Couldn't apply EQ");
}

static int g_eq_drop;
static void make_col(lv_obj_t *parent, int idx, const char *flabel,
                     lv_obj_t **slot_slider, lv_obj_t **slot_val){
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 42, 206 - g_eq_drop);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *val = lv_label_create(col);
    lv_obj_set_pos(val, 0, 0); lv_obj_set_width(val, 42);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(val, TF(UI_14), 0);
    lv_obj_set_style_text_color(val, TC(ACCENT_PRIMARY), 0);
    lv_label_set_text(val, "0");

    lv_obj_t *sl = lv_slider_create(col);
    lv_obj_set_size(sl, 16, 150 - g_eq_drop);
    lv_obj_set_ext_click_area(sl, 8);
    lv_obj_set_pos(sl, 13, 22);
    lv_slider_set_mode(sl, LV_SLIDER_MODE_SYMMETRICAL);
    lv_slider_set_range(sl, -12, 12);
    lv_slider_set_value(sl, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, TC(ACCENT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, TC(CONTROL_KNOB), LV_PART_KNOB);
    ui_on(sl, band_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)idx, "eqcustom.band.value", UI_CORE);
    lv_obj_add_event_cb(sl, band_release_cb, LV_EVENT_RELEASED, (void*)(intptr_t)idx);
    lv_obj_add_event_cb(sl, band_release_cb, LV_EVENT_PRESS_LOST, (void*)(intptr_t)idx);

    lv_obj_t *fl = lv_label_create(col);
    lv_obj_set_pos(fl, 0, 182 - g_eq_drop); lv_obj_set_width(fl, 42);
    lv_label_set_text(fl, flabel);
    lv_obj_set_style_text_align(fl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(fl, TF(UI_14), 0);
    lv_obj_set_style_text_color(fl, (idx==NBAND ? TC(TEXT_SECONDARY) : TC(TEXT_MUTED)), 0);

    *slot_slider = sl;
    *slot_val = val;
}

static void view_only_tap_cb(lv_event_t *e){
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) ui_toast("Not on this firmware");
}
static void view_only_slider(lv_obj_t *sl){
    if(!sl) return;
    lv_obj_add_state(sl, LV_STATE_DISABLED);
    lv_obj_set_style_opa(sl, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_remove_flag(sl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *col = lv_obj_get_parent(sl);
    lv_obj_add_flag(col, LV_OBJ_FLAG_CLICKABLE);
    ui_on(col, view_only_tap_cb, LV_EVENT_CLICKED, NULL, "eqcustom.view_only", UI_CORE);
}

static void make_slot_selector(lv_obj_t *root){
    lv_obj_t *sel = lv_obj_create(root);
    lv_obj_remove_style_all(sel);
    lv_obj_set_pos(sel, 100, 48 + g_eq_drop); lv_obj_set_size(sel, 160, 32);
    lv_obj_clear_flag(sel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lb = lv_button_create(sel); lv_obj_remove_style_all(lb);
    lv_obj_set_size(lb, 40, 30); lv_obj_align(lb, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_ext_click_area(lb, 8);
    lv_obj_t *li = lv_label_create(lb); lv_label_set_text(li, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(li, TC(TEXT_PRIMARY), 0); lv_obj_center(li);
    ui_on(lb, slot_dir_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1, "eqcustom.slot_dir", UI_CORE);

    g_slotlbl = lv_label_create(sel);
    lv_obj_set_width(g_slotlbl, 80); lv_obj_align(g_slotlbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(g_slotlbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_slotlbl, TF(UI_16), 0);
    lv_obj_set_style_text_color(g_slotlbl, TC(ACCENT_PRIMARY), 0);
    { char b[16]; snprintf(b,sizeof b,"USER%d", g_slot-10); lv_label_set_text(g_slotlbl, b); }

    lv_obj_t *rb = lv_button_create(sel); lv_obj_remove_style_all(rb);
    lv_obj_set_size(rb, 40, 30); lv_obj_align(rb, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_ext_click_area(rb, 8);
    lv_obj_t *ri = lv_label_create(rb); lv_label_set_text(ri, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(ri, TC(TEXT_PRIMARY), 0); lv_obj_center(ri);
    ui_on(rb, slot_dir_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1, "eqcustom.slot_dir", UI_CORE);
}

void eqcustom_refresh(void){
    if(!g_slotlbl) return;
    int init = cfg_get_int("eq_slot", SLOT_MIN);
    int pre  = cfg_get_int("eq_preset", -1);
    if(pre >= SLOT_MIN && pre <= SLOT_MAX) init = pre;
    if(init < SLOT_MIN || init > SLOT_MAX) init = SLOT_MIN;
    g_slot = init;
    cfg_set_int("eq_slot", g_slot);
    char b[16]; snprintf(b, sizeof b, "USER%d", g_slot-10); lv_label_set_text(g_slotlbl, b);
    load_slot(g_slot);
}

void eqcustom_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    migrate_legacy_user1();

    int init = cfg_get_int("eq_slot", SLOT_MIN);
    int pre  = cfg_get_int("eq_preset", -1);
    if(pre >= SLOT_MIN && pre <= SLOT_MAX) init = pre;
    if(init < SLOT_MIN || init > SLOT_MAX) init = SLOT_MIN;
    g_slot = init;
    cfg_set_int("eq_slot", g_slot);

    lv_obj_t *title = ui_header(root, "Custom EQ");
    g_eq_drop = 0;
    lv_obj_update_layout(root);
    if(title){
        lv_area_t a; lv_obj_get_coords(title, &a);
        if(lv_obj_get_style_bg_opa(title, 0) > LV_OPA_50) g_eq_drop = a.y2 + 2 - 48;
        else if(a.y2 > 56) g_eq_drop = a.y2 - 56 + 4;
        if(g_eq_drop < 0) g_eq_drop = 0;
        if(g_eq_drop > 40) g_eq_drop = 40;
    }

    make_slot_selector(root);

    lv_obj_t *scr = lv_obj_create(root);
    lv_obj_remove_style_all(scr);
    lv_obj_set_pos(scr, 44, 80 + g_eq_drop);
    lv_obj_set_size(scr, 272, 212 - g_eq_drop);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(scr, LV_DIR_HOR);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_column(scr, 4, 0);
    lv_obj_set_scroll_snap_x(scr, LV_SCROLL_SNAP_START);

    make_col(scr, NBAND, "MSTR", &g_master, &g_mval);
    for(int i=0;i<NBAND;i++)
        make_col(scr, i, FREQLBL[i], &g_band[i], &g_bval[i]);

    load_slot(g_slot);

    lv_obj_t *flat = lv_button_create(root);
    lv_obj_remove_style_all(flat);
    lv_obj_set_size(flat, 120, 32); lv_obj_align(flat, LV_ALIGN_TOP_MID, 0, 288);
    lv_obj_set_ext_click_area(flat, 6);
    lv_obj_set_style_radius(flat, 16, 0);
    lv_obj_set_style_bg_color(flat, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(flat, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(flat, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    ui_on(flat, flat_cb, LV_EVENT_CLICKED, NULL, "eqcustom.flat", UI_CORE);
    lv_obj_t *fll=lv_label_create(flat); lv_label_set_text(fll, "Flat");
    lv_obj_set_style_text_font(fll, TF(UI_14), 0);
    lv_obj_set_style_text_color(fll, TC(TEXT_PRIMARY), 0); lv_obj_center(fll);
    if(!fw_custom_peq_writable()){
        for(int i=0;i<=NBAND;i++) view_only_slider(i < NBAND ? g_band[i] : g_master);
        lv_obj_set_width(flat, 180);
        lv_label_set_text(fll, "Not on this firmware");
        lv_obj_set_style_text_color(fll, TC(TEXT_MUTED), 0);
    }
}
