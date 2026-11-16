/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "screens_ui.h"
#include "theme.h"
#include "theme_kit.h"
#include "folderbrowser.h"
#include "books.h"
#include "anim.h"
#include "config.h"
#include <unistd.h>

void mclock_sync(int backlight_on);
int ui_screen_backlight_on(void);
void ui_np_title_scroll(int visible);

#define SCR_W 360

static lv_obj_t *s_roots[SCR_COUNT];
static lv_obj_t *s_scrim;
static lv_obj_t *s_scrim_panel;

static void scrim_clip(lv_event_t *e)
{
    lv_obj_t *o = s_scrim_panel;
    if (!o || lv_obj_get_style_bg_opa(o, 0) != LV_OPA_COVER ||
        lv_obj_get_style_opa_recursive(o, 0) != LV_OPA_COVER ||
        lv_obj_get_style_opa_layered(o, 0) != LV_OPA_COVER ||
        lv_obj_get_style_radius(o, 0) != 0) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    if (a.y1 <= layer->_clip_area.y1 && a.y2 >= layer->_clip_area.y2 &&
        a.x2 >= layer->_clip_area.x2 && a.x1 <= layer->_clip_area.x2)
        layer->_clip_area.x2 = a.x1 - 1;
}
typedef struct {
    void (*create)(lv_obj_t *root);
    void (*refresh)(void);
    void (*visible)(int on);
    unsigned char figure;
} screen_def_t;

static const screen_def_t SCREENS[SCR_COUNT] = {
    [SCR_HOME]           = { home_create,           NULL,                 NULL,          1 },
    [SCR_LIBRARY]        = { library_create,        library_enter,        NULL,          2 },
    [SCR_NOWPLAYING]     = { ui_create,             ui_np_enter,          NULL,          3 },
    [SCR_SETTINGS]       = { settings_create,       settings_refresh_list,NULL,          4 },
    [SCR_SETTING_DETAIL] = { setting_detail_create, setting_detail_refresh,NULL,         22 },
    [SCR_SEARCH]         = { search_create,         NULL,                 NULL,          5 },
    [SCR_SAVER]          = { saver_create,          saver_show_sync,      NULL,         15 },
    [SCR_QUICK]          = { quicksettings_create,  quicksettings_build,  NULL,         24 },
    [SCR_SONGINFO]       = { songinfo_create,       NULL,                 NULL,         10 },
    [SCR_NPMENU]         = { npmenu_create,         NULL,                 NULL,         11 },
    [SCR_TUNE]           = { tune_create,           tune_refresh,         NULL,         13 },
    [SCR_EQ]             = { eqcustom_create,       eqcustom_refresh,     NULL,         14 },
    [SCR_APPS]           = { apps_create,           NULL,                 NULL,          6 },
    [SCR_NPHUB]          = { nphub_create,          nphub_refresh,        NULL,         12 },
    [SCR_PLPICK]         = { plpick_create,         NULL,                 NULL,         18 },
    [SCR_PLVIEW]         = { plview_create,         plview_refresh,       NULL,         19 },
    [SCR_WIFI]           = { wifi_create,           NULL,                 NULL,         28 },
    [SCR_WIFI_INFO]      = { wifi_info_create,      NULL,                 NULL,         29 },
    [SCR_BT]             = { bt_create,             NULL,                 NULL,         30 },
    [SCR_BT_INFO]        = { bt_info_create,        NULL,                 NULL,         31 },
    [SCR_WEATHER]        = { weather_app_create,    NULL,                 NULL,          7 },
    [SCR_LYRICS]         = { lyrics_create,         NULL,                 NULL,          9 },
    [SCR_COLORPICK]      = { colorpick_create,      NULL,                 NULL,         26 },
    [SCR_WORKMODE]       = { modes_create,          NULL,                 NULL,         32 },
    [SCR_DEBUG]          = { debug_create,          NULL,                 NULL,         34 },
    [SCR_FOLDER]         = { folderbrowser_create,  NULL,                 NULL,         16 },
    [SCR_BOOKS]          = { books_create,          NULL,                 NULL,         20 },
    [SCR_CHAPTERS]       = { chapters_create,       NULL,                 NULL,         21 },
    [SCR_SETLIST]        = { setlist_create,        setlist_refresh,      NULL,         23 },
    [SCR_QSCONFIG]       = { qsconfig_create,       qsconfig_refresh,     NULL,         25 },
    [SCR_ALBUMWALL]      = { albumwall_create,      albumwall_refresh,    NULL,         17 },
    [SCR_UPNEXT]         = { upnext_create,         upnext_refresh,       NULL,          8 },
    [SCR_DATETIME]       = { datetime_create,       datetime_refresh,     NULL,         27 },
    [SCR_TAMA]           = { tama_create,           NULL,                 tama_visible, 35 },
    [SCR_DAC]            = { dac_create,            NULL,                 dac_visible,  36 },
    [SCR_SETSUB]         = { setsub_create,         setsub_refresh,       NULL,         37 },
};
static int s_current = SCR_HOME;
int screen_figure_number(int which){
    return which >= 0 && which < SCR_COUNT ? SCREENS[which].figure : 0;
}
static int s_stack[16];
static int s_sp = 0;
#define PUSH_MS 300
#define POP_MS  240
static int s_anim = 1;

static lv_obj_t *screen_make_root(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root, 0, 0);
    theme_screen_bg(root);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    anim_panel_shadow(root);
    return root;
}

static void root_rest(lv_obj_t *o)
{
    lv_obj_set_x(o, 0);
    anim_panel_hairline(o, 0);
    lv_obj_set_style_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale_x(o, 256, 0);
    lv_obj_set_style_transform_scale_y(o, 256, 0);
}


static void show_raw(int which)
{
    if (which < 0 || which >= SCR_COUNT) return;

    for (int i = 0; i < SCR_COUNT; i++) {
        if (s_roots[i]) lv_obj_add_flag(s_roots[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (s_roots[which]) {
        lv_obj_clear_flag(s_roots[which], LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_roots[which]);
    }
    ui_np_title_scroll(ui_screen_backlight_on());
}

static void scrim_off(void)
{
    for (int i = 0; i < SCR_COUNT; i++) if (s_roots[i]) anim_panel_hairline(s_roots[i], 0);
    s_scrim_panel = NULL;
    if (s_scrim) {
        lv_anim_delete(s_scrim, NULL);
        lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_scrim, LV_OPA_TRANSP, 0);
    }
}

static void anim_done_hide(lv_anim_t *a)
{
    lv_obj_t *o = (lv_obj_t *)a->var;
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    root_rest(o);
    ui_np_title_scroll(ui_screen_backlight_on());
    scrim_off();
}

static int s_push_hide = -1;
static void anim_done_push(lv_anim_t *a)
{
    (void)a;
    int h = s_push_hide; s_push_hide = -1;
    if (h >= 0 && h < SCR_COUNT && s_roots[h]) {
        lv_obj_add_flag(s_roots[h], LV_OBJ_FLAG_HIDDEN);
        root_rest(s_roots[h]);
    }
    ui_np_title_scroll(ui_screen_backlight_on());
    scrim_off();
}

static void transition(int from, int to, int dir)
{
    lv_obj_t *nw = (to >= 0 && to < SCR_COUNT) ? s_roots[to] : NULL;
    lv_obj_t *od = (from >= 0 && from < SCR_COUNT) ? s_roots[from] : NULL;

    npmenu_close_transients();
    ui_np_close_overlays();

    if (to >= 0 && to < SCR_COUNT && SCREENS[to].refresh) SCREENS[to].refresh();
    if (from >= 0 && from < SCR_COUNT && from != to && SCREENS[from].visible) SCREENS[from].visible(0);
    if (to >= 0 && to < SCR_COUNT && from != to && SCREENS[to].visible) SCREENS[to].visible(1);

    kit_pass(nw);
    mclock_sync(ui_screen_backlight_on());
    kit_cursor_sync(ui_screen_backlight_on());

    if (nw) { lv_anim_delete(nw, NULL); root_rest(nw); }
    if (od) { lv_anim_delete(od, NULL); root_rest(od); }

    for (int i = 0; i < SCR_COUNT; i++) {
        if (i == to || i == from || !s_roots[i]) continue;
        lv_anim_delete(s_roots[i], NULL);
        lv_obj_add_flag(s_roots[i], LV_OBJ_FLAG_HIDDEN);
        root_rest(s_roots[i]);
    }
    if (s_scrim) { lv_anim_delete(s_scrim, NULL); scrim_off(); }

    if (!nw || to == from || !s_anim) { show_raw(to); return; }
    if (to == SCR_SAVER || from == SCR_SAVER) { show_raw(to); return; }
    if (to == SCR_QUICK || from == SCR_QUICK) { show_raw(to); return; }

    if (dir > 0) {
        s_scrim_panel = nw;
        if (od) { lv_obj_set_x(od, 0); lv_obj_clear_flag(od, LV_OBJ_FLAG_HIDDEN); }
        if (s_scrim && od) {
            lv_obj_set_style_opa(s_scrim, LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_scrim);
            anim_scrim_fade(s_scrim, 1, PUSH_MS);
        }
        lv_obj_set_x(nw, SCR_W);
        lv_obj_clear_flag(nw, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(nw);
        s_push_hide = (od && from != to) ? from : -1;
        anim_panel_hairline(nw, 1);
        anim_page_slide(nw, SCR_W, 0, PUSH_MS, anim_done_push);
    } else {
        s_scrim_panel = od;
        lv_obj_set_x(nw, 0);
        lv_obj_clear_flag(nw, LV_OBJ_FLAG_HIDDEN);
        if (s_scrim) {
            lv_obj_set_style_opa(s_scrim, 36, 0);
            lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_scrim);
            anim_scrim_fade(s_scrim, 0, POP_MS);
        }
        if (od) {
            lv_obj_set_x(od, 0);
            lv_obj_clear_flag(od, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(od);
            anim_panel_hairline(od, 1);
            anim_page_slide(od, 0, SCR_W, POP_MS, anim_done_hide);
        } else {
            lv_obj_move_foreground(nw);
        }
    }
    ui_np_title_scroll(ui_screen_backlight_on());
}

void screen_show(int which)
{
    if (which < 0 || which >= SCR_COUNT) return;
    int from = s_current;
    if (which != s_current) {
        int cap = (int)(sizeof(s_stack)/sizeof(s_stack[0]));
        if (s_sp >= cap) {
            for (int i = 2; i < cap; i++) s_stack[i-1] = s_stack[i];
            s_sp = cap - 1;
        }
        s_stack[s_sp++] = s_current;
    }
    s_current = which;
    transition(from, which, +1);
}

void screen_back(void)
{
    if (s_sp <= 0) return;
    int from = s_current;
    int prev = s_stack[--s_sp];
    s_current = prev;
    transition(from, prev, -1);
}

lv_obj_t *screen_get_root(int which)
{
    if (which < 0 || which >= SCR_COUNT) return NULL;
    return s_roots[which];
}

int screen_current(void){ return s_current; }

void screens_init(void)
{
    if (s_roots[SCR_HOME]) {
        screen_show(SCR_HOME);
        return;
    }

    s_anim = cfg_get_int("anim", 1);
    if (access("/usr/data/anim_off", 0) == 0) s_anim = 0;

    kit_install();
    lv_obj_t *parent = lv_screen_active();
    lv_obj_set_style_bg_color(parent, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < SCR_COUNT; i++) s_roots[i] = screen_make_root(parent);

    s_scrim = lv_obj_create(parent);
    lv_obj_remove_style_all(s_scrim);
    lv_obj_set_size(s_scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_scrim, TC(SCRIM), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_scrim, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_scrim, scrim_clip, LV_EVENT_DRAW_MAIN_BEGIN, NULL);

    for (int i = 0; i < SCR_COUNT; i++) if (SCREENS[i].create) SCREENS[i].create(s_roots[i]);

    screen_show(SCR_HOME);
}

void screen_set_anim(int on){ s_anim = on ? 1 : 0; }
