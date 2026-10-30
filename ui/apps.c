/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "anim.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "ui_round.h"

#define APPS_DIR "/usr/data/apps"
#define MAX_APPS 24

LV_FONT_DECLARE(font_icons_28)

typedef struct { char name[64]; char exec[256]; } app_t;
static app_t g_apps[MAX_APPS];
static int g_napps;
static lv_obj_t *g_list;

static void scan_apps(void){
    g_napps = 0;
    DIR *d = opendir(APPS_DIR);
    if(!d) return;
    struct dirent *de;
    while((de = readdir(d)) && g_napps < MAX_APPS){
        if(de->d_name[0] == '.') continue;
        app_t *a = &g_apps[g_napps];
        snprintf(a->name, sizeof a->name, "%s", de->d_name);
        snprintf(a->exec, sizeof a->exec, APPS_DIR "/%s/app", de->d_name);
        char conf[320]; snprintf(conf, sizeof conf, APPS_DIR "/%s/app.conf", de->d_name);
        FILE *f = fopen(conf, "r");
        if(f){
            char line[320];
            while(fgets(line, sizeof line, f)){
                char *nl = strpbrk(line, "\r\n"); if(nl) *nl = 0;
                if(!strncmp(line, "name=", 5)) snprintf(a->name, sizeof a->name, "%s", line+5);
                else if(!strncmp(line, "exec=", 5)) snprintf(a->exec, sizeof a->exec, "%s", line+5);
            }
            fclose(f);
        }
        g_napps++;
    }
    closedir(d);
}

static void app_row_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int i = (int)(uintptr_t)lv_event_get_user_data(e);
    if(i>=0 && i<g_napps) app_launch(g_apps[i].exec);
}
static void settings_row_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_show(SCR_SETTINGS); }
static void pet_row_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) tama_open(); }
static void dac_row_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) dac_open(); }

#define TILE_W 128
#define TILE_H 118
#define CHIP_D 84
typedef void (*tile_icon_fn)(lv_obj_t *chip);
static void make_tile_ex(const char *icon, const lv_font_t *ifont, tile_icon_fn build, const char *name, lv_event_cb_t cb, void *ud){
    lv_obj_t *r = lv_button_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, TILE_W, TILE_H);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_pivot_x(r, TILE_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(r, TILE_H / 2, 0);
    lv_obj_set_style_transform_scale(r, 240, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(r, 4);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    ui_on(r, cb, LV_EVENT_CLICKED, ud, "apps.cb", UI_CORE);
    kit_keep(r);
    lv_obj_t *chip = lv_obj_create(r);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, CHIP_D, CHIP_D);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(chip, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 2, 0);
    lv_obj_set_style_border_color(chip, TC(BORDER_STRONG), 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(chip, LV_ALIGN_TOP_MID, 0, 2);
    kit_keep(chip);
    if(build) build(chip);
    else {
        lv_obj_t *ic = lv_label_create(chip);
        lv_label_set_text(ic, icon);
        lv_obj_set_style_text_font(ic, ifont, 0);
        lv_obj_set_style_text_color(ic, TC(TEXT_PRIMARY), 0);
        lv_obj_center(ic);
    }
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, name);
    lv_obj_set_style_text_font(l, TF(UI_14), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -2);
}

static void make_tile(const char *icon, const lv_font_t *ifont, const char *name, lv_event_cb_t cb, void *ud){
    make_tile_ex(icon, ifont, NULL, name, cb, ud);
}

#define DAC_ICON_W 32
#define DAC_ICON_H 32
static void dac_icon_draw_cb(lv_event_t *e){
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a; lv_obj_get_coords(o, &a);
    lv_color_t col = TC(TEXT_PRIMARY);

    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
    rd.bg_color = col; rd.bg_opa = LV_OPA_COVER; rd.radius = 2;
    lv_area_t ra = { a.x1 + 2, a.y1 + 11, a.x1 + 10, a.y1 + 21 };
    lv_draw_rect(layer, &rd, &ra);

    lv_draw_triangle_dsc_t td; lv_draw_triangle_dsc_init(&td);
    td.bg_color = col; td.bg_opa = LV_OPA_COVER;
    td.p[0].x = a.x1 + 6;  td.p[0].y = a.y1 + 16;
    td.p[1].x = a.x1 + 17; td.p[1].y = a.y1 + 5;
    td.p[2].x = a.x1 + 17; td.p[2].y = a.y1 + 27;
    lv_draw_triangle(layer, &td);

    lv_draw_arc_dsc_t ad; lv_draw_arc_dsc_init(&ad);
    ad.color = col; ad.opa = LV_OPA_COVER; ad.width = 3; ad.rounded = 1;
    ad.center.x = a.x1 + 17; ad.center.y = a.y1 + 16;
    ad.start_angle = 320; ad.end_angle = 40;
    ad.radius = 9;  lv_draw_arc(layer, &ad);
    ad.radius = 14; lv_draw_arc(layer, &ad);
}
static void dac_icon_build(lv_obj_t *chip){
    lv_obj_t *ic = lv_obj_create(chip);
    lv_obj_remove_style_all(ic);
    lv_obj_set_size(ic, DAC_ICON_W, DAC_ICON_H);
    lv_obj_clear_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ic, dac_icon_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_center(ic);
    kit_keep(ic);
}

void apps_reload(void){
    if(!g_list) return;
    lv_obj_clean(g_list);
    scan_apps();
    make_tile("\xEF\x80\x84", TF(ICON_28), "Pet", pet_row_cb, NULL);
    make_tile_ex(NULL, NULL, dac_icon_build, "USB DAC", dac_row_cb, NULL);
    for(int i=0;i<g_napps;i++) make_tile(LV_SYMBOL_FILE, TF(ICON_28), g_apps[i].name, app_row_cb, (void*)(uintptr_t)i);
    make_tile(LV_SYMBOL_SETTINGS, TF(ICON_28), "Settings", settings_row_cb, NULL);
}

void apps_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    ui_header(root, "Apps");

    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, 44, 74); lv_obj_set_size(g_list, 272, UI_LIST_H_FROM(74));
    lv_obj_set_style_pad_bottom(g_list, UI_LIST_PAD_BOTTOM, 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(g_list, 10, 0);
    lv_obj_set_style_pad_column(g_list, 10, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);

    apps_reload();
}

lv_obj_t *apps_scroller(void){ return g_list; }
