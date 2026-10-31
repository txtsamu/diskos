/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "modes.h"
#include "config.h"
#include "sdio.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *name, *sub; } modeinfo_t;
static const modeinfo_t MODES[] = {
    { "Local Playback",      "Play from the microSD card" },
    { "USB DAC",             "Be a DAC for a phone or PC" },
    { "Bluetooth Receiving", "Play audio sent from a phone" },
    { "USB Storage",         "Open the card on a computer" },
#ifdef DISKOS_TEST_OUTPUTS
    { "USB Audio",           "Play to an external USB DAC" },
#endif
};
#define N_MODES ((int)(sizeof(MODES)/sizeof(MODES[0])))
#define ROW_USB_AUDIO 4

static lv_obj_t *g_check[N_MODES];
static lv_obj_t *g_row[N_MODES];

static int shown_mode(int src){ return (src == 0 && N_MODES > ROW_USB_AUDIO && modes_output_route() == OUT_USB) ? ROW_USB_AUDIO : src; }

static void mark_selected_mode(int cur){
    for(int i=0;i<N_MODES;i++){
        if(g_check[i]){ lv_label_set_text(g_check[i], i==cur ? LV_SYMBOL_OK : "");
                        lv_obj_set_style_text_color(g_check[i], ui_current_accent(), 0); }
        if(g_row[i]){
            lv_obj_set_style_border_width(g_row[i], i==cur ? 2 : 0, 0);
            lv_obj_set_style_border_color(g_row[i], ui_current_accent(), 0);
            lv_obj_set_style_bg_color(g_row[i], (i==cur ? TC(SURFACE_SELECTED) : TC(SURFACE)), 0);
        }
    }
}
static void mark_selected(void){ mark_selected_mode(sd_switch_failed() ? -1 : shown_mode(ui_get_source_mode())); }
static void modes_ui_refresh(void){
    mark_selected();
    setting_detail_refresh();
    setlist_refresh();
}
static uint32_t g_last_switch = 0;

static void mark_pending(int m){
    for(int i=0;i<N_MODES;i++){
        if(g_check[i]){ lv_label_set_text(g_check[i], i==m ? LV_SYMBOL_REFRESH : "");
                        lv_obj_set_style_text_color(g_check[i], ui_current_accent(), 0); }
        if(g_row[i]){
            lv_obj_set_style_border_width(g_row[i], i==m ? 2 : 0, 0);
            lv_obj_set_style_border_color(g_row[i], ui_current_accent(), 0);
            lv_obj_set_style_bg_color(g_row[i], (i==m ? TC(SURFACE_SELECTED) : TC(SURFACE)), 0);
        }
    }
}

static lv_timer_t *g_settle = NULL;
static int g_pending_mode = -1;
static void on_switch_event(srcswitch_event_t ev){
    if(ev == SRCSWITCH_EV_SETTLE_RESTART){ g_last_switch = lv_tick_get(); return; }
    if(ev == SRCSWITCH_EV_ABANDONED){
        if(g_settle){ lv_timer_del(g_settle); g_settle = NULL; }
        g_pending_mode = -1;
    }
    modes_ui_refresh();
}
static void settle_cb(lv_timer_t *t){
    (void)t;
    if(modes_source_switch_busy()) return;
    if(lv_tick_elaps(g_last_switch) < 3200) return;
    lv_timer_del(g_settle); g_settle = NULL;
    int intended = g_pending_mode; g_pending_mode = -1;
    if(sd_switch_failed()){ mark_selected_mode(-1); return; }
    int show = (intended >= 0) ? intended : ui_get_source_mode();
    if(intended == 0 || intended == 1 || intended == 3){
        int actual = ui_detect_source_mode();
        show = actual;
        if(actual != intended) ui_toast("Mode didn't switch");
    }
    if(intended == ROW_USB_AUDIO) show = ui_get_source_mode();
    mark_selected_mode(shown_mode(show));
}

static void row_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int m = (int)(uintptr_t)lv_event_get_user_data(e);
    if(srcswitch_via_local_pending() || (g_last_switch && lv_tick_elaps(g_last_switch) < 3000)){ ui_toast("Switching..."); return; }
    g_last_switch = lv_tick_get();
    if(m == ROW_USB_AUDIO){
        if(modes_output_mode_switch(OUT_USB) == 0){
            g_pending_mode = m; mark_pending(m);
            if(g_settle) lv_timer_del(g_settle);
            g_settle = lv_timer_create(settle_cb, 500, NULL);
            ui_toast("Switching to USB audio output");
        }
        return;
    }
    if(ui_set_source_mode(m) == 0){
        g_pending_mode = m;
        mark_pending(m);
        if(g_settle) lv_timer_del(g_settle);
        g_settle = lv_timer_create(settle_cb, 500, NULL);
        if(!srcswitch_via_local_pending()) ui_toast(srcswitch_switch_msg(m));
    }
}

void modes_open(void){
    if(g_settle && g_pending_mode >= 0) mark_pending(g_pending_mode);
    else mark_selected();
    screen_show(SCR_WORKMODE);
}

void modes_create(lv_obj_t *root){
    if(cfg_get_int("spdif", 0)) cfg_set_int("spdif", 0);
    srcswitch_set_listener(on_switch_event);
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    ui_header(root, "Working Mode");

    lv_obj_t *col = lv_obj_create(root);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 300, 250); lv_obj_set_pos(col, 30, 76);
    lv_obj_set_style_pad_bottom(col, 44, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);

    for(int i=0;i<N_MODES;i++){
        lv_obj_t *row = lv_button_create(col);
        g_row[i] = row;
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 268, 54);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_bg_color(row, TC(SURFACE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
        ui_on(row, row_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i, "modes.row", UI_CORE);

        lv_obj_t *nm = lv_label_create(row);
        lv_label_set_text(nm, MODES[i].name);
        lv_obj_set_pos(nm, 16, 9);
        lv_obj_set_style_text_font(nm, TF(UI_16), 0);
        lv_obj_set_style_text_color(nm, TC(TEXT_PRIMARY), 0);

        lv_obj_t *sb = lv_label_create(row);
        lv_label_set_text(sb, MODES[i].sub);
        lv_obj_set_pos(sb, 16, 30);
        lv_obj_set_width(sb, 202);
        lv_label_set_long_mode(sb, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(sb, TF(UI_12), 0);
        lv_obj_set_style_text_color(sb, TC(TEXT_MUTED), 0);

        g_check[i] = lv_label_create(row);
        lv_label_set_text(g_check[i], "");
        lv_obj_align(g_check[i], LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_set_style_text_color(g_check[i], ui_current_accent(), 0);
        lv_obj_set_style_text_font(g_check[i], TF(UI_18), 0);
    }
    if(N_MODES > ROW_USB_AUDIO) lv_obj_move_to_index(g_row[ROW_USB_AUDIO], 1);
    mark_selected();
}
