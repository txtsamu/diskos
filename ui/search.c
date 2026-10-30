/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "musicdb.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_round.h"

#define MAX_RESULTS 80

static lv_obj_t *g_back;
static lv_obj_t *g_bar;
static lv_obj_t *g_bar_lbl;
static lv_obj_t *g_clear;
static lv_obj_t *g_hint;
static lv_obj_t *g_results;

static void search_layout(int active){
    if(active){
        int edge = ui_edge_nav();
        int def = !strcmp(theme_kit()->id, "default");
        if(g_back){ if(edge) lv_obj_set_pos(g_back, 0, 140); else lv_obj_set_pos(g_back, 72, 24); }
        if(g_back && !edge && !def) lv_obj_set_ext_click_area(g_back, 1);
        lv_obj_set_size(g_bar, edge ? 168 : def ? 152 : 164, 40);
        lv_obj_align(g_bar, LV_ALIGN_TOP_LEFT, edge ? 96 : def ? 128 : 120, 26);
        lv_obj_set_size(g_bar_lbl, def ? 104 : 116, 18);
        lv_obj_set_style_text_align(g_bar_lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(g_bar_lbl, LV_ALIGN_LEFT_MID, 14, 0);
        if(g_clear) lv_obj_set_pos(g_clear, edge ? 228 : 248, 31);
        if(g_hint) lv_obj_add_flag(g_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g_results, UI_LIST_X + 10, 74); lv_obj_set_size(g_results, UI_LIST_W - 20, UI_LIST_H_FROM(74));
        lv_obj_remove_flag(g_results, LV_OBJ_FLAG_HIDDEN);
    } else {
        if(g_back){ if(ui_edge_nav()) lv_obj_set_pos(g_back, 0, 140); else lv_obj_set_pos(g_back, 72, 24); }
        if(g_back && !ui_edge_nav()) lv_obj_set_ext_click_area(g_back, 10);
        lv_obj_set_size(g_bar, ui_edge_nav() ? 150 : 128, 40);
        lv_obj_align(g_bar, LV_ALIGN_CENTER, 0, -8);
        lv_obj_set_width(g_bar_lbl, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(g_bar_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(g_bar_lbl, LV_ALIGN_CENTER, 0, 0);
        if(g_hint){ lv_obj_remove_flag(g_hint, LV_OBJ_FLAG_HIDDEN); lv_obj_align(g_hint, LV_ALIGN_CENTER, 0, 40); }
        lv_obj_add_flag(g_results, LV_OBJ_FLAG_HIDDEN);
    }
    if(g_back && ui_edge_nav()) lv_obj_move_foreground(g_back);
}
static library_song_click_cb_t g_song_cb;
static char g_query[96];

void search_set_song_click_cb(library_song_click_cb_t cb){ g_song_cb = cb; }

static void result_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int id = (int)(uintptr_t)lv_event_get_user_data(e);
    if(!g_song_cb || !g_song_cb(id)) return;
    screen_show(SCR_NOWPLAYING);
}
static void back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

static void rebuild(const char *q){
    lv_obj_clean(g_results);
    static const mdb_song_t *buf[MAX_RESULTS + 1];
    int n = (q && q[0]) ? mdb_search(q, buf, MAX_RESULTS + 1) : 0;
    int shown = n > MAX_RESULTS ? MAX_RESULTS : n;
    if(n<=0){
        lv_obj_t *l = lv_label_create(g_results);
        lv_label_set_text(l, (q && q[0]) ? "No results" : "Tap the bar to search");
        lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
        lv_obj_set_style_text_font(l, TF(UI_14), 0);
        if(q && q[0]) theme_kit()->empty_state(KIT_EMPTY_SEARCH, l);
        return;
    }
    for(int i=0;i<shown;i++){
        const mdb_song_t *s = buf[i];
        lv_obj_t *row = lv_obj_create(g_results);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 268, 46);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, TC(LIST_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_70, LV_STATE_PRESSED);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        ui_on(row, result_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)s->id, "search.result", UI_CORE);
        lv_obj_t *t = lv_label_create(row);
        lv_label_set_text(t, s->title[0]?s->title:"Untitled");
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(t, 12, 5); lv_obj_set_size(t, 244, 19);
        lv_obj_set_style_text_font(t, TF(USER_16), 0);
        lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
        lv_obj_t *a = lv_label_create(row);
        lv_label_set_text(a, s->artist);
        lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(a, 12, 25); lv_obj_set_size(a, 244, 16);
        lv_obj_set_style_text_font(a, TF(USER_14), 0);
        lv_obj_set_style_text_color(a, TC(TEXT_SECONDARY), 0);
    }
    if(n > MAX_RESULTS){
        lv_obj_t *f = lv_label_create(g_results);
        lv_label_set_text(f, "Showing first 80 - refine to narrow");
        lv_obj_set_style_text_color(f, TC(TEXT_MUTED), 0);
        lv_obj_set_style_text_font(f, TF(UI_14), 0);
    }
}

static lv_color_t query_color(int has_query){
    if(lv_color_eq(lv_obj_get_style_bg_color(g_bar, 0), TC(TEXT_PRIMARY))) return TC(CANVAS);
    return has_query ? TC(TEXT_PRIMARY) : TC(TEXT_MUTED);
}
static void on_query(const char *text){
    if(!text) return;
    snprintf(g_query, sizeof g_query, "%s", text);
    if(g_bar_lbl) lv_label_set_text(g_bar_lbl, g_query[0] ? g_query : "Search library");
    if(g_bar_lbl) lv_obj_set_style_text_color(g_bar_lbl,
        query_color(g_query[0] != 0), 0);
    if(g_clear){
        if(g_query[0]) lv_obj_clear_flag(g_clear, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(g_clear, LV_OBJ_FLAG_HIDDEN);
    }
    search_layout(g_query[0] != 0);
    rebuild(g_query);
}
#ifdef DISKOS_HOST
void search_host_query(const char *text){ on_query(text); }
#endif
static void clear_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) on_query(""); }
static void on_query_done(const char *text){
    if(text && text[0]) on_query(text);
}
static void bar_cb(lv_event_t *e){
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) kbinput_open("Search", g_query, on_query_done);
}

void search_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    g_back = lv_button_create(root);
    lv_obj_t *back = g_back;
    lv_obj_remove_style_all(back);
    lv_obj_set_pos(back, 72, 24);
    lv_obj_set_size(back, 44, 40);
    lv_obj_set_ext_click_area(back, 10);
    lv_obj_set_style_radius(back, 20, 0);
    lv_obj_set_style_bg_color(back, TC(LIST_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_70, LV_STATE_PRESSED);
    ui_on(back, back_cb, LV_EVENT_CLICKED, NULL, "search.back", UI_CORE);
    lv_obj_t *bi = lv_label_create(back);
    lv_label_set_text(bi, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(bi, TF(UI_20), 0);
    lv_obj_set_style_text_color(bi, TC(TEXT_SECONDARY), 0);
    lv_obj_align(bi, LV_ALIGN_CENTER, 0, -2);
    if(ui_edge_nav()){
        lv_obj_set_pos(back, 0, 140); lv_obj_set_size(back, 30, 80);
        lv_obj_set_ext_click_area(back, 0);
        ui_back_glyph(bi);
    }

    g_bar = lv_button_create(root);
    lv_obj_remove_style_all(g_bar);
    lv_obj_set_ext_click_area(g_bar, 2);
    lv_obj_set_style_radius(g_bar, 12, 0);
    lv_obj_set_style_bg_color(g_bar, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, 0);
    ui_on(g_bar, bar_cb, LV_EVENT_CLICKED, NULL, "search.bar", UI_CORE);
    g_bar_lbl = lv_label_create(g_bar);
    lv_label_set_text(g_bar_lbl, "Search library");
    lv_label_set_long_mode(g_bar_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_size(g_bar_lbl, 100, 18);
    lv_obj_set_style_text_font(g_bar_lbl, TF(USER_14), 0);
    lv_obj_set_style_text_color(g_bar_lbl, TC(TEXT_MUTED), 0);
    lv_obj_align(g_bar_lbl, LV_ALIGN_LEFT_MID, 14, 0);

    g_clear = lv_button_create(root);
    lv_obj_remove_style_all(g_clear);
    lv_obj_set_pos(g_clear, 219, 31);
    lv_obj_set_size(g_clear, 30, 30);
    lv_obj_set_ext_click_area(g_clear, 7);
    lv_obj_set_style_radius(g_clear, 10, 0);
    lv_obj_set_style_bg_color(g_clear, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(g_clear, LV_OPA_70, LV_STATE_PRESSED);
    ui_on(g_clear, clear_cb, LV_EVENT_CLICKED, NULL, "search.clear", UI_CORE);
    lv_obj_t *ci = lv_label_create(g_clear);
    lv_label_set_text(ci, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ci, TC(TEXT_SECONDARY), 0);
    lv_obj_center(ci);
    lv_obj_add_flag(g_clear, LV_OBJ_FLAG_HIDDEN);

    g_hint = lv_label_create(root);
    lv_label_set_text(g_hint, "Songs, artists, albums");
    lv_obj_set_style_text_font(g_hint, TF(UI_14), 0);
    lv_obj_set_style_text_color(g_hint, TC(TEXT_DISABLED), 0);

    g_results = lv_obj_create(root);
    lv_obj_remove_style_all(g_results);
    lv_obj_set_pos(g_results, UI_LIST_X + 2, 74);
    lv_obj_set_size(g_results, UI_LIST_W, UI_LIST_H_FROM(74));
    lv_obj_set_style_bg_opa(g_results, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(g_results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_results, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_results, 6, 0);
    lv_obj_set_style_pad_bottom(g_results, UI_LIST_PAD_BOTTOM, 0);
    lv_obj_set_scroll_dir(g_results, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_results, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_results, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    search_layout(g_query[0] != 0);
    rebuild(g_query);
}

lv_obj_t *search_scroller(void){ return g_results; }
