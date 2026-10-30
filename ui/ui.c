/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "ui.h"
#include "theme.h"
#include "theme_kit.h"
#include "lvgl/lvgl.h"
#include "art.h"
#include "netart.h"
#include "ipc.h"
#include "admit.h"
#include "musicdb.h"
#include "scanner.h"
#include "artcache.h"
#include "config.h"
#include "i18n.h"
#include "screens.h"
#include "modes.h"
#include "sdio.h"
#include "anim.h"
#include "fonts_intl.h"
#include "fontft.h"
int screen_figure_number(int which);

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>


LV_FONT_DECLARE(font_icons_28)
#define HEART_FILLED  "\xEF\x80\x84"
#define HEART_OUTLINE "\xEF\x82\x8A"
#define MOON_ICON     "\xEF\x86\x86"
#define MODE_ARROW    "\xEF\x85\xB8"

#define ARC_D        332
#define ARC_SWEEP    286
#define ARC_ROT      127

#define COVER_D      148

static lv_obj_t *ring;
static lv_obj_t *cover;
static lv_obj_t *cover_img;
static lv_obj_t *cover_note;
static lv_obj_t *spindle;
static lv_obj_t *fsart, *fsart_img, *fsart_title, *fsart_artist;
static int fsart_on;
static lv_obj_t *title;
static lv_obj_t *artist;
static lv_obj_t *album;
static lv_obj_t *album_pos;
static lv_obj_t *btn_pp;
static lv_obj_t *btn_prev;
static lv_obj_t *btn_next;
static lv_obj_t *btn_fav;
static lv_obj_t *fav_icon;
static lv_obj_t *btn_sleep;
static lv_obj_t *sleep_icon;
static int g_np_fav, g_np_have, g_fav_px, g_fav_py;
static char g_np_curpath[520];
static int  g_favp_active = 0, g_favp_val = 0;
static uint32_t g_favp_set = 0;
static char g_favp_path[520];
static lv_obj_t *btn_mode;
static lv_obj_t *mode_icon;
static lv_obj_t *mode_one;
static int g_np_mode = -1, g_mode_px, g_mode_py;
static lv_obj_t *t_elapsed;
static lv_obj_t *t_remain;

static lv_color_t accent;
static char last_path[256];
static char g_stat_path[256];
static char last_art_key[420];
static char want_art_key[420];
static char last_seed_a[IPC_TITLE_CAPACITY];
static char last_seed_b[160];
static char cover_src[48];
static int  cover_valid;
static int  coverdsc_valid;
static char thumb_src[48];
static int  thumb_valid;
static char backdrop_src[48];
static int  backdrop_valid;
static int displayed_idx;
static int32_t shown_progress;
static int  g_scrubbing;
static long g_track_dur;
static long g_seek_lo, g_seek_hi;
static uint32_t g_seek_hold_until = 0;
static long     g_seek_target_ms = -1;
static long     g_scrub_lo, g_scrub_hi;
static char     g_scrub_path[520];
static int g_seek_sx, g_seek_sy;
static int g_seek_cand;
static int g_seek_on;

static lv_color_t accent_from(const char *a, const char *b)
{
    unsigned h = 2166136261u;
    for(const char *p = a; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    for(const char *p = b; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    return theme_accent_from_hsv(h % 360, 65, 84);
}

static void mmss(long ms, char *buf, size_t len)
{
    if(ms < 0) ms = 0;
    long total = ms / 1000;
    long sec = total % 60;
    long hr  = total / 3600;
    if(hr > 0){
        long min = (total / 60) % 60;
        if(hr > 999) hr = 999;
        snprintf(buf, len, "%ld:%02ld:%02ld", hr, min, sec);
    } else {
        snprintf(buf, len, "%ld:%02ld", total / 60, sec);
    }
}

static void copy_cstr(char *dst, size_t dst_len, const char *src)
{
    if(dst_len == 0) return;
    if(src == NULL) src = "";
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

int ui_glyph_ink_dy(const lv_font_t *f, uint32_t cp)
{
    lv_font_glyph_dsc_t g;
    if(!f || !lv_font_get_glyph_dsc(f, &g, cp, 0)) return 0;
    int lh = lv_font_get_line_height(f);
    int ink_top = (lh - f->base_line) - (g.ofs_y + g.box_h);
    return lh / 2 - (ink_top + g.box_h / 2);
}
void ui_glyph_center_ink(lv_obj_t *label)
{
    if(!label || !lv_obj_check_type(label, &lv_label_class)) return;
    const unsigned char *t = (const unsigned char *)lv_label_get_text(label);
    if(!t || (t[0] & 0xF0) != 0xE0 || (t[1] & 0xC0) != 0x80 || (t[2] & 0xC0) != 0x80) return;
    uint32_t cp = ((uint32_t)(t[0] & 0x0F) << 12) | ((uint32_t)(t[1] & 0x3F) << 6) | (t[2] & 0x3F);
    if(cp < 0xE000) return;
    lv_obj_set_style_translate_y(label, ui_glyph_ink_dy(lv_obj_get_style_text_font(label, 0), cp), 0);
}
void ui_pp_glyph(lv_obj_t *label, int playing)
{
    if(label == NULL) return;
    const char *t = playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
    if(strcmp(lv_label_get_text(label), t) != 0) lv_label_set_text(label, t);
    const lv_font_t *f = lv_obj_get_style_text_font(label, 0);
    int dx = 0;
    lv_font_glyph_dsc_t g;
    if(!playing && lv_font_get_glyph_dsc(f, &g, 0xF04B, 0)) dx = (g.box_w + 5) / 10;
    if(lv_obj_get_style_pad_left(label, 0) != dx){
        lv_obj_set_style_pad_left(label, dx, 0);
        lv_obj_set_style_pad_right(label, -dx, 0);
    }
    int32_t sh = lv_obj_get_style_height(label, 0);
    if(sh != LV_SIZE_CONTENT && !LV_COORD_IS_PCT(sh) && sh > 0){
        int bw = lv_obj_get_style_border_width(label, 0), lh = lv_font_get_line_height(f);
        int pt = sh / 2 - bw - lh / 2 + ui_glyph_ink_dy(f, playing ? 0xF04C : 0xF04B);
        if(pt < 0) pt = 0;
        if(lv_obj_get_style_pad_top(label, 0) != pt) lv_obj_set_style_pad_top(label, pt, 0);
    }
}

typedef struct { char *text; } label_input_t;
static void label_input_free(lv_event_t *e)
{
    label_input_t *in = lv_event_get_user_data(e);
    if(in){ free(in->text); free(in); }
}
int ui_label_input_changed(lv_obj_t *obj, const char *txt)
{
    if(obj == NULL) return 0;
    if(txt == NULL) txt = "";
    label_input_t *in = NULL;
    for(uint32_t i = 0; i < lv_obj_get_event_count(obj); i++){
        lv_event_dsc_t *d = lv_obj_get_event_dsc(obj, i);
        if(lv_event_dsc_get_cb(d) == label_input_free){ in = lv_event_dsc_get_user_data(d); break; }
    }
    if(in && in->text && strcmp(in->text, txt) == 0) return 0;
    if(!in){
        in = calloc(1, sizeof *in);
        if(!in) return 1;
        if(!lv_obj_add_event_cb(obj, label_input_free, LV_EVENT_DELETE, in)){ free(in); return 1; }
    }
    free(in->text);
    in->text = strdup(txt);
    return 1;
}
int ui_label_set_changed(lv_obj_t *obj, const char *txt)
{
    if(!ui_label_input_changed(obj, txt)) return 0;
    lv_label_set_text(obj, txt ? txt : "");
    return 1;
}
static void set_label_text_changed(lv_obj_t *obj, const char *txt)
{
    ui_label_set_changed(obj, txt);
}

#define RING_CX  180
#define RING_CY  180
static int g_ring_d = ARC_D;
#define RING_R   (g_ring_d / 2)

static int g_np_backdrop_on = 1;
static lv_obj_t *backdrop;
void ui_np_backdrop_enabled(int on){ g_np_backdrop_on = on; if(!on && backdrop) lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN); }
static int g_pp_filled;
void ui_np_pp_filled(int on){ g_pp_filled = on; }
static lv_obj_t *g_seek_bar;
static int32_t g_seek_shown;
#define SOME_RING_DOTS 112
static lv_obj_t *g_some_ring_dot[SOME_RING_DOTS];
static int g_some_ring_bucket;
#define HIFI_TICKS 47
static lv_obj_t *g_hifi_tick[HIFI_TICKS];
static lv_point_precise_t g_hifi_tick_pts[HIFI_TICKS][2];
static int g_hifi_bucket;
static int hifi_np_active(void){ return !strcmp(theme_kit()->id, "hi-fi"); }
static void hifi_ticks_show(int value){
    int bucket = (value * HIFI_TICKS + 500) / 1000;
    if(bucket < 0) bucket = 0;
    if(bucket > HIFI_TICKS) bucket = HIFI_TICKS;
    int lo = bucket < g_hifi_bucket ? bucket : g_hifi_bucket;
    int hi = bucket > g_hifi_bucket ? bucket : g_hifi_bucket;
    for(int i = lo; i < hi; i++)
        if(g_hifi_tick[i]) lv_obj_set_style_line_color(g_hifi_tick[i],
                                i < bucket ? TC(ACCENT_PRIMARY) : TC(DOT_GRID), 0);
    g_hifi_bucket = bucket;
}
static void hifi_ticks_create(lv_obj_t *parent){
    g_hifi_bucket = 0;
    for(int i = 0; i < HIFI_TICKS; i++){
        int deg = 132 + 6 * i;
        double a = deg * 3.141592653589793 / 180.0;
        int major = (deg % 30) == 0;
        g_hifi_tick_pts[i][0] = (lv_point_precise_t){ 180 + (major ? 166 : 170) * cos(a),
                                                       180 + (major ? 166 : 170) * sin(a) };
        g_hifi_tick_pts[i][1] = (lv_point_precise_t){ 180 + 175 * cos(a), 180 + 175 * sin(a) };
        lv_obj_t *tick = lv_line_create(parent);
        lv_line_set_points(tick, g_hifi_tick_pts[i], 2);
        lv_obj_set_style_line_width(tick, major ? 2 : 1, 0);
        lv_obj_set_style_line_color(tick, TC(DOT_GRID), 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        g_hifi_tick[i] = tick;
    }
}
static void some_ring_show(int value){
    int bucket = (value * SOME_RING_DOTS + 500) / 1000;
    if(bucket < 0) bucket = 0;
    if(bucket > SOME_RING_DOTS) bucket = SOME_RING_DOTS;
    if(bucket == g_some_ring_bucket) return;
    int lo = bucket < g_some_ring_bucket ? bucket : g_some_ring_bucket;
    int hi = bucket > g_some_ring_bucket ? bucket : g_some_ring_bucket;
    for(int i = lo; i < hi; i++){
        lv_obj_t *dot = g_some_ring_dot[i];
        if(dot) lv_obj_set_style_bg_color(dot, i < bucket ? accent : TC(CONTROL_TRACK), 0);
    }
    g_some_ring_bucket = bucket;
}
static void some_ring_create(lv_obj_t *parent){
    int radius = g_ring_d / 2 - 3;
    for(int i = 0; i < SOME_RING_DOTS; i++){
        double a = (ARC_ROT + (double)ARC_SWEEP * i / (SOME_RING_DOTS - 1)) * 3.141592653589793 / 180.0;
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 4, 4);
        lv_obj_set_pos(dot, RING_CX + (int)lround(radius * cos(a)) - 2,
                            RING_CY + (int)lround(radius * sin(a)) - 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, TC(CONTROL_TRACK), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        g_some_ring_dot[i] = dot;
    }
    lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
}
static void seek_show(int32_t v){
    g_seek_shown = v;
    int hifi = hifi_np_active();
    if(theme_trait(THEME_TRAIT_MATRIX_CLOCK)) some_ring_show(v);
    else if(!hifi) lv_arc_set_value(ring, v);
    if(hifi) hifi_ticks_show(v);
    if(theme_kit()->np_progress) theme_kit()->np_progress(v);
    if(g_seek_bar){
        if(lv_obj_check_type(g_seek_bar, &lv_slider_class)) lv_slider_set_value(g_seek_bar, v, LV_ANIM_OFF);
        else lv_bar_set_value(g_seek_bar, v, LV_ANIM_OFF);
    }
}
void ui_np_seek_line(lv_obj_t *bar){
    g_seek_bar = bar;
    if(bar){
        if(lv_obj_check_type(bar, &lv_slider_class)) lv_slider_set_range(bar, 0, 1000); else lv_bar_set_range(bar, 0, 1000);
        seek_show(g_seek_shown);
    }
}
static int seek_on_band(int x, int y, int tol){
    if(g_seek_bar){
        lv_area_t a; lv_obj_get_coords(g_seek_bar, &a);
        return x >= a.x1 - 12 && x <= a.x2 + 12 && y >= a.y1 - tol && y <= a.y2 + tol;
    }
    int dx = x - RING_CX, dy = y - RING_CY;
    int d2 = dx*dx + dy*dy;
    int lo = RING_R - tol, hi = RING_R + tol;
    return d2 >= lo*lo && d2 <= hi*hi;
}
static double seek_rel_angle(int x, int y){
    double a = atan2((double)(y - RING_CY), (double)(x - RING_CX)) * 57.2957795;
    double rel = a - ARC_ROT;
    while(rel < 0) rel += 360.0;
    while(rel >= 360.0) rel -= 360.0;
    return rel;
}
static int32_t seek_pt_to_value(int x, int y){
    if(g_seek_bar){
        lv_area_t a; lv_obj_get_coords(g_seek_bar, &a);
        int w = a.x2 - a.x1; if(w < 1) w = 1;
        int32_t v = (int32_t)((x - a.x1) * 1000L / w);
        return v < 0 ? 0 : v > 1000 ? 1000 : v;
    }
    double rel = seek_rel_angle(x, y);
    if(rel > ARC_SWEEP) rel = (rel - ARC_SWEEP < 360.0 - rel) ? ARC_SWEEP : 0.0;
    int32_t v = (int32_t)(rel / ARC_SWEEP * 1000.0 + 0.5);
    if(v < 0) v = 0;
    if(v > 1000) v = 1000;
    return v;
}

int ui_np_seek_press(int x, int y){
    g_seek_cand = 0; g_seek_on = 0; g_scrubbing = 0;
    if(y < 40) return 0;
    if(g_track_dur <= 0) return 0;
    if(!seek_on_band(x, y, 34)) return 0;
    if(g_seek_bar){ if(x < 64) return 0; }
    else if(seek_rel_angle(x, y) > ARC_SWEEP) return 0;
    g_seek_sx = x; g_seek_sy = y; g_seek_cand = 1;
    return 1;
}
int ui_np_seek_move(int x, int y){
    if(!g_seek_cand) return 0;
    int dx = x - g_seek_sx, dy = y - g_seek_sy;
    int adx = dx<0?-dx:dx, ady = dy<0?-dy:dy;
    if(!seek_on_band(x, y, 48)){
        if(g_seek_on){ g_seek_on = 0; g_scrubbing = 0; seek_show(shown_progress); }
        g_seek_cand = 0; return 0;
    }
    if(!g_seek_on){
        if(adx < 26 && ady < 26) return 0;
        if(!g_seek_bar && adx > ady*2){ g_seek_cand = 0; return 0; }
        g_seek_on = 1; g_scrubbing = 1;
        g_scrub_lo = g_seek_lo; g_scrub_hi = g_seek_hi;
        if(g_scrub_hi <= g_scrub_lo){ g_scrub_lo = 0; g_scrub_hi = g_track_dur; }
        copy_cstr(g_scrub_path, sizeof g_scrub_path, g_np_curpath);
    }
    int32_t v = seek_pt_to_value(x, y);
    seek_show(v);
    shown_progress = v;
    {
        long lo = g_scrub_lo, hi = g_scrub_hi;
        if(hi > lo){
            long span = hi - lo;
            long rel  = (long)((int64_t)v * span / 1000);
            char b[12]; mmss(rel, b, sizeof b); set_label_text_changed(t_elapsed, b);
            char r[12]; mmss(span - rel, r, sizeof r);
            char rr[14]; snprintf(rr, sizeof rr, "-%s", r); set_label_text_changed(t_remain, rr);
        }
    }
    return 1;
}
int ui_np_seek_release(int x, int y){
    (void)x; (void)y;
    int consumed = 0;
    if(g_seek_on && g_scrub_path[0]){
        track_state_t s; ipc_get_state(&s);
        if(strcmp(g_scrub_path, s.path) != 0){
            g_seek_cand = 0; g_seek_on = 0; g_scrubbing = 0;
            seek_show(shown_progress);
            return 1;
        }
    }
    if(g_seek_on && g_track_dur > 0){
        int32_t v = g_seek_shown;
        long lo = g_scrub_lo, hi = g_scrub_hi;
        if(hi <= lo){ lo = 0; hi = g_track_dur; }
        long ms = (hi > lo) ? lo + (long)((int64_t)v * (hi - lo) / 1000) : 0;
        g_seek_target_ms = ms;
        g_seek_hold_until = lv_tick_get() + 2500;
        if(ui_seek_to(ms) == 0) ui_book_user_seeked(ms);
        consumed = 1;
    }
    g_seek_cand = 0; g_seek_on = 0; g_scrubbing = 0;
    return consumed;
}

static void set_progress_changed(int32_t value)
{
    if(g_scrubbing) return;
    if(value < 0) value = 0;
    if(value > 1000) value = 1000;

    if(ring && shown_progress != value) {
        seek_show(value);
        shown_progress = value;
    }
}
int ui_np_progress(void){ return shown_progress; }

static void style_text(lv_obj_t *obj, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(obj, 0, LV_PART_MAIN);
}

void ui_np_seek_hold(long target_ms){
    g_seek_target_ms = target_ms;
    g_seek_hold_until = lv_tick_get() + 2500;
}
void ui_np_pp_refresh(int playing){ ui_pp_glyph(btn_pp, playing); }
static void transport_cb(lv_event_t *e)
{
    ui_transport_command((const char *)lv_event_get_user_data(e));
}

static int g_cover_px, g_cover_py;
static char fsart_path[256];
static void fsart_reload_img(void){
    if(!fsart_img) return;
    lv_image_set_src(fsart_img, NULL);
    if(access("/usr/data/fiio/cover.jpg", 0) == 0)
        lv_image_set_src(fsart_img, "A:/usr/data/fiio/cover.jpg");
    else if(backdrop_valid)
        lv_image_set_src(fsart_img, backdrop_src);
    else if(cover_valid)
        lv_image_set_src(fsart_img, cover_src);
}
static void fsart_refresh_text(void){
    if(fsart_title)  lv_label_set_text(fsart_title,  lv_label_get_text(title));
    if(fsart_artist) lv_label_set_text(fsart_artist, lv_label_get_text(artist));
}
void ui_np_fsart_open(void){
    if(!fsart || fsart_on || !g_np_have) return;
    fsart_path[0] = '\0';
    fsart_reload_img();
    fsart_refresh_text();
    lv_obj_set_style_opa(fsart, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(fsart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(fsart);
    anim_fade(fsart, LV_OPA_TRANSP, LV_OPA_COVER, 240, NULL);
    fsart_on = 1;
}
static void fsart_hidden_cb(lv_anim_t *a){ (void)a; if(fsart) lv_obj_add_flag(fsart, LV_OBJ_FLAG_HIDDEN); }
void ui_np_fsart_close(void){
    if(!fsart || !fsart_on) return;
    fsart_on = 0;
    anim_fade(fsart, LV_OPA_COVER, LV_OPA_TRANSP, 200, fsart_hidden_cb);
}
int ui_np_fsart_active(void){ return fsart_on; }
static void fsart_click_cb(lv_event_t *e){
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) ui_np_fsart_close();
}

static void cover_click_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *id = lv_indev_active();
    if(code == LV_EVENT_PRESSED) {
        if(id){ lv_point_t p; lv_indev_get_point(id, &p); g_cover_px = p.x; g_cover_py = p.y; }
    }
}
static void make_clickable(lv_obj_t *o, const char *cmd)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(o, 18);
    char an[24];
    ui_on(o, transport_cb, LV_EVENT_CLICKED, (void *)cmd, ui_transport_action(cmd, "np", an, sizeof an), UI_CORE);
}

static void apply_accent(void)
{
    if(ring) {
        lv_obj_set_style_arc_color(ring, accent, LV_PART_INDICATOR);
    }

    if(cover_note) {
        lv_obj_set_style_text_color(cover_note, TC(TEXT_PRIMARY), LV_PART_MAIN);
    }

    if(btn_pp) {
        if(theme_trait(THEME_TRAIT_NP_BIG_PLAY) || g_pp_filled) lv_obj_set_style_bg_color(btn_pp, accent, LV_PART_MAIN);
        else lv_obj_set_style_text_color(btn_pp, accent, LV_PART_MAIN);
    }

    if(fav_icon && g_np_fav) {
        lv_obj_set_style_text_color(fav_icon, accent, LV_PART_MAIN);
    }

    if(mode_icon && g_np_mode > 0) {
        lv_obj_set_style_text_color(mode_icon, accent, LV_PART_MAIN);
    }

    if(spindle) {
        lv_obj_set_style_bg_color(spindle, accent, LV_PART_MAIN);
    }

    home_set_accent(accent);
    saver_set_accent(accent);
}

lv_color_t ui_current_accent(void){ return accent; }

static void ui_header_back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }
static int g_back_held;
static void back_press_cb(lv_event_t *e){ (void)e; g_back_held = 0; }
static void back_long_cb(lv_event_t *e){ (void)e; g_back_held = 1; screen_show(SCR_HOME); }
static void back_click_filter_cb(lv_event_t *e){ if(g_back_held){ g_back_held = 0; lv_event_stop_processing(e); } }
static void back_hold_home(lv_obj_t *back){
    lv_obj_add_event_cb(back, back_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(back, back_click_filter_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back, back_long_cb, LV_EVENT_LONG_PRESSED, NULL);
}
static void header_back_raise(void *o){ lv_obj_move_foreground((lv_obj_t *)o); }
static void header_back_deleted(lv_event_t *e){ lv_async_call_cancel(header_back_raise, lv_event_get_target(e)); }
int ui_round_width(int y, int margin){
    int dy = y < 180 ? 180 - y : y - 180;
    if(dy >= 180) return 0;
    int w = 2 * (int)sqrtf((float)(180 * 180 - dy * dy)) - 2 * margin;
    return w > 0 ? w : 0;
}
static void title_fit(lv_obj_t *t, const char *text, const lv_font_t *f, int cy, int pad_top,
                      const lv_font_t *(*next)(const lv_font_t *)){
    for(int k = 0; k < 3; k++){
        int h = lv_font_get_line_height(f) + pad_top, w = ui_round_width(cy - h / 2 + pad_top + 2, 10);
        if(w > 250) w = 250;
        lv_point_t sz; lv_text_get_size(&sz, text, f, lv_obj_get_style_text_letter_space(t, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const lv_font_t *sm = next ? next(f) : NULL;
        if(sz.x <= w || !sm || k == 2){
            lv_obj_set_style_text_font(t, f, 0);
            lv_obj_set_user_data(t, (void *)&theme_title_tag);
            lv_obj_set_pos(t, 180 - w / 2, cy - h / 2); lv_obj_set_size(t, w, h);
            lv_obj_set_style_pad_top(t, pad_top, 0);
            return;
        }
        f = sm;
    }
}
static const lv_font_t *dot_smaller(const lv_font_t *f){ return f == TF(HEADER) ? TF(DATE) : NULL; }
lv_obj_t *ui_header_cb(lv_obj_t *root, const char *title, lv_event_cb_t back_cb){ return theme_kit()->header(root, tr(title), back_cb); }
static lv_obj_t *header_back(lv_obj_t *root, lv_event_cb_t back_cb, int edge_glyph)
{
    lv_obj_t *back = lv_button_create(root);
    lv_obj_remove_style_all(back);
    lv_obj_set_style_radius(back, 20, 0);
    lv_obj_set_style_bg_color(back, TC(LIST_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_70, LV_STATE_PRESSED);
    back_hold_home(back);
    ui_on(back, back_cb ? back_cb : ui_header_back_cb, LV_EVENT_CLICKED, NULL, "nav.back", UI_CORE);
    lv_obj_t *ic = lv_label_create(back);
    if(edge_glyph) ui_back_glyph(ic);
    else {
        lv_label_set_text(ic, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(ic, TF(UI_20), 0);
        lv_obj_set_style_text_color(ic, TC(TEXT_SECONDARY), 0);
    }
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -2);
    if(edge_glyph && theme_trait(THEME_TRAIT_DOT_TITLES) && theme_trait(THEME_TRAIT_MATRIX_CLOCK)) kit_some_dot_arrow(ic, 1);
    return back;
}
static void header_back_edge(lv_obj_t *back)
{
    lv_obj_set_pos(back, 0, 140); lv_obj_set_size(back, 30, 80);
    lv_obj_set_ext_click_area(back, 0);
    lv_async_call(header_back_raise, back);
    lv_obj_add_event_cb(back, header_back_deleted, LV_EVENT_DELETE, NULL);
}
lv_obj_t *kit_default_header(lv_obj_t *root, const char *title, lv_event_cb_t back_cb)
{
    int dots = theme_trait(THEME_TRAIT_DOT_TITLES);
    lv_obj_t *back = header_back(root, back_cb, dots);
    if(dots) header_back_edge(back);
    else {
        lv_obj_set_pos(back, 72, 24); lv_obj_set_size(back, 44, 40);
        lv_obj_set_ext_click_area(back, 10);
    }

    lv_obj_t *t = lv_label_create(root);
    if(theme_trait(THEME_TRAIT_DOT_TITLES)){
        char up[160];
        title_fit(t, theme_upper(up, sizeof up, title), TF(HEADER), 46, 6, dot_smaller);
        theme_title_text(t, title);
    } else {
    theme_title_text(t, title);
    lv_obj_set_pos(t, 104, 30); lv_obj_set_size(t, 152, 26);
    }
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    if(!theme_trait(THEME_TRAIT_DOT_TITLES))
        lv_obj_set_style_text_font(t, TF(HEADER), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    return t;
}
lv_obj_t *ui_header(lv_obj_t *root, const char *title){ return ui_header_cb(root, title, NULL); }
lv_obj_t *kit_edge_header(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);
void ui_back_glyph(lv_obj_t *label){
    const char *t = theme_kit()->back_text;
    lv_label_set_text(label, t ? t : "\xEF\x81\xA0");
    lv_obj_set_style_text_font(label, t ? TF(UI_20) : TF(ICON_20), 0);
    lv_obj_set_style_text_color(label, TC(TEXT_PRIMARY), 0);
}
int ui_edge_nav(void){
    return theme_kit()->header != theme_kit_default.header || theme_trait(THEME_TRAIT_DOT_TITLES);
}
static lv_obj_t *ui_back_button(lv_obj_t *root, int ring_shown)
{
    lv_obj_t *back = header_back(root, NULL, ui_edge_nav());
    if(ring_shown){ lv_obj_set_pos(back, 66, 58); lv_obj_set_size(back, 40, 40); }
    else { lv_obj_set_pos(back, 0, 140); lv_obj_set_size(back, 30, 80); lv_obj_set_ext_click_area(back, 8); }
    return back;
}
lv_obj_t *kit_edge_header(lv_obj_t *root, const char *title, lv_event_cb_t back_cb)
{
    header_back_edge(header_back(root, back_cb, 1));
    lv_obj_t *t = lv_label_create(root);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    theme_title_text(t, title);
    char shown[160]; lv_snprintf(shown, sizeof shown, "%s", lv_label_get_text(t));
    title_fit(t, shown, TF(HEADER), 46, 0, theme_title_smaller);
    theme_title_text(t, title);
    return t;
}

static void fav_refresh(int on, int have)
{
    g_np_fav = on; g_np_have = have;
    if(!btn_fav || !fav_icon) return;
    if(have == lv_obj_has_flag(btn_fav, LV_OBJ_FLAG_HIDDEN)){
        if(have) lv_obj_remove_flag(btn_fav, LV_OBJ_FLAG_HIDDEN);
        else     lv_obj_add_flag(btn_fav, LV_OBJ_FLAG_HIDDEN);
    }
    set_label_text_changed(fav_icon, on ? HEART_FILLED : HEART_OUTLINE);
    lv_color_t c = on ? accent : TC(TEXT_MUTED);
    if(!lv_color_eq(lv_obj_get_style_text_color(fav_icon, LV_PART_MAIN), c)) lv_obj_set_style_text_color(fav_icon, c, LV_PART_MAIN);
}

static void fav_click_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *id = lv_indev_active();
    if(code == LV_EVENT_PRESSED) {
        if(id) { lv_point_t p; lv_indev_get_point(id, &p); g_fav_px = p.x; g_fav_py = p.y; }
    } else if(code == LV_EVENT_CLICKED) {
        if(!g_np_have) return;
        if(id) {
            lv_point_t p; lv_indev_get_point(id, &p);
            int dx = p.x - g_fav_px, dy = p.y - g_fav_py;
            if((dx<0?-dx:dx) > 18 || (dy<0?-dy:dy) > 18) return;
        }
        fav_refresh(!g_np_fav, 1);
        ui_set_favorite(g_np_fav);
        g_favp_active = 1; g_favp_val = g_np_fav; g_favp_set = lv_tick_get();
        snprintf(g_favp_path, sizeof g_favp_path, "%s", g_np_curpath);
        ui_toast(g_np_fav ? "Added to Favourites" : "Removed from Favourites");
    }
}

#define WORKMODE_COUNT 5
static void mode_refresh(int wm)
{
    if(!btn_mode || !mode_icon) return;
    if(wm == g_np_mode) return;
    g_np_mode = wm;
    const char *icon = (wm == 1) ? LV_SYMBOL_SHUFFLE
                     : (wm == 2 || wm == 3) ? LV_SYMBOL_LOOP
                     : MODE_ARROW;
    lv_label_set_text(mode_icon, icon);
    lv_obj_set_style_text_color(mode_icon, wm ? accent : TC(TEXT_MUTED), LV_PART_MAIN);
    if(wm == 2 || wm == 4) lv_obj_remove_flag(mode_one, LV_OBJ_FLAG_HIDDEN);
    else                   lv_obj_add_flag(mode_one, LV_OBJ_FLAG_HIDDEN);
}

static void mode_click_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *id = lv_indev_active();
    if(code == LV_EVENT_PRESSED) {
        if(id) { lv_point_t p; lv_indev_get_point(id, &p); g_mode_px = p.x; g_mode_py = p.y; }
    } else if(code == LV_EVENT_CLICKED) {
        if(id) {
            lv_point_t p; lv_indev_get_point(id, &p);
            int dx = p.x - g_mode_px, dy = p.y - g_mode_py;
            if((dx<0?-dx:dx) > 18 || (dy<0?-dy:dy) > 18) return;
        }
        int wm = (cfg_get_int("work_mode", 0) + 1) % WORKMODE_COUNT;
        cfg_set_int("work_mode", wm);
        ui_set_workmode(wm);
        mode_refresh(wm);
        static const char *const MODE_NAMES[WORKMODE_COUNT] =
            { "Sequential", "Shuffle", "Repeat One", "Repeat All", "Single" };
        if(wm >= 0 && wm < WORKMODE_COUNT) ui_toast(MODE_NAMES[wm]);
    }
}

static lv_label_long_mode_t g_title_scroll_mode = LV_LABEL_LONG_CLIP;

void ui_np_title_scroll(int visible)
{
    if(!title) return;
    visible = visible && !lv_obj_has_flag(lv_obj_get_parent(title), LV_OBJ_FLAG_HIDDEN);
    lv_label_long_mode_t mode = lv_label_get_long_mode(title);
    if(visible){
        if(mode == LV_LABEL_LONG_CLIP && g_title_scroll_mode != LV_LABEL_LONG_CLIP)
            lv_label_set_long_mode(title, g_title_scroll_mode);
        g_title_scroll_mode = LV_LABEL_LONG_CLIP;
    } else if(mode == LV_LABEL_LONG_SCROLL || mode == LV_LABEL_LONG_SCROLL_CIRCULAR){
        g_title_scroll_mode = mode;
        lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    }
}

static int g_np_vinyl = 0;
static int g_spinning  = 0;

static void spin_cb(void *var, int32_t v){ lv_image_set_rotation((lv_obj_t *)var, v % 3600); }

void ui_vinyl_spin(int want)
{
    want = want && g_np_vinyl && cover_img &&
           !lv_obj_has_flag(cover_img, LV_OBJ_FLAG_HIDDEN) && lv_image_get_src(cover_img);
    if(want == g_spinning) return;
    g_spinning = want;
    if(want){
        int32_t cur = lv_image_get_rotation(cover_img);
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, cover_img);
        lv_anim_set_exec_cb(&a, spin_cb);
        lv_anim_set_values(&a, cur, cur + 3600);
        lv_anim_set_time(&a, 9000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    } else {
        lv_anim_delete(cover_img, spin_cb);
    }
}

void ui_set_np_style(int vinyl)
{
    if(!cover) return;
    g_np_vinyl = vinyl;
    lv_obj_set_style_radius(cover, !strcmp(theme_kit()->id, "bauhaus") ? 0 : vinyl ? (COVER_D/2) : 14, LV_PART_MAIN);
    if(spindle) {
        if(vinyl) lv_obj_remove_flag(spindle, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(spindle, LV_OBJ_FLAG_HIDDEN);
    }
    if(!vinyl){
        ui_vinyl_spin(0);
        if(cover_img) lv_image_set_rotation(cover_img, 0);
    }
}

static void set_cover_fallback(bool show, bool loading)
{
    if(show) {
        ui_vinyl_spin(0);
        if(cover_img) lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
        if(cover_note) lv_obj_remove_flag(cover_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        if(cover_img) lv_obj_remove_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
        if(cover_note) lv_obj_add_flag(cover_note, LV_OBJ_FLAG_HIDDEN);
    }
    theme_kit()->np_cover_state(cover, cover_note, show, loading);
    if(g_np_vinyl && strcmp(theme_kit()->id, "bauhaus"))
        lv_obj_set_style_radius(cover, COVER_D/2, LV_PART_MAIN);
}

#ifdef DISKOS_HOST
void ui_host_cover_state(int state){
    if(state == 0){
        enum { HOST_ART_SIZE = 148 };
        static uint32_t pixels[HOST_ART_SIZE * HOST_ART_SIZE];
        static lv_image_dsc_t art;
        for(int y = 0; y < HOST_ART_SIZE; y++) for(int x = 0; x < HOST_ART_SIZE; x++)
            pixels[y * HOST_ART_SIZE + x] = 0xff000000u | ((uint32_t)(30 + x) << 16) | ((uint32_t)(35 + y) << 8) | 90u;
        art.header.magic = LV_IMAGE_HEADER_MAGIC; art.header.cf = LV_COLOR_FORMAT_ARGB8888;
        art.header.w = HOST_ART_SIZE; art.header.h = HOST_ART_SIZE; art.header.stride = HOST_ART_SIZE * 4;
        art.data = (const uint8_t *)pixels; art.data_size = sizeof pixels;
        lv_image_set_src(cover_img, &art);
    }
    set_cover_fallback(state != 0, state == 1);
}
#endif

#define CBMP 148
static uint8_t g_coverbuf[CBMP*CBMP*4];
static lv_image_dsc_t g_coverdsc;
static int load_cover_dsc(const char *path)
{
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    uint8_t hdr[54];
    if(fread(hdr,1,54,f)!=54){ fclose(f); return -1; }
    uint32_t off = hdr[10]|(hdr[11]<<8)|(hdr[12]<<16)|((uint32_t)hdr[13]<<24);
    int w = hdr[18]|(hdr[19]<<8), h = hdr[22]|(hdr[23]<<8);
    int bpp = hdr[28]|(hdr[29]<<8);
    uint32_t comp = hdr[30]|(hdr[31]<<8)|(hdr[32]<<16)|((uint32_t)hdr[33]<<24);
    if(w!=CBMP || h!=CBMP || bpp!=24 || comp!=0){ fclose(f); return -1; }
    static uint8_t row[CBMP*3];
    if(fseek(f, off, SEEK_SET)!=0){ fclose(f); return -1; }
    for(int yy=0; yy<CBMP; yy++){
        if(fread(row,1,CBMP*3,f)!=(size_t)(CBMP*3)){ fclose(f); return -1; }
        uint8_t *d = g_coverbuf + (CBMP-1-yy)*CBMP*4;
        for(int x=0;x<CBMP;x++){
            d[x*4+0]=row[x*3+0]; d[x*4+1]=row[x*3+1];
            d[x*4+2]=row[x*3+2]; d[x*4+3]=0xFF;
        }
    }
    fclose(f);
    g_coverdsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    g_coverdsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
    g_coverdsc.header.w      = CBMP;
    g_coverdsc.header.h      = CBMP;
    g_coverdsc.header.stride = CBMP*4;
    g_coverdsc.data          = g_coverbuf;
    g_coverdsc.data_size     = sizeof g_coverbuf;
    return 0;
}

#pragma GCC push_options
#pragma GCC optimize ("O0")

static float g_lin_lut[256]; static int g_lin_lut_init = 0;
static void lin_lut_init(void){
    for(int i=0;i<256;i++){ float x=i/255.0f; g_lin_lut[i]= x<=0.04045f ? x/12.92f : powf((x+0.055f)/1.055f, 2.4f); }
    g_lin_lut_init=1;
}
static float a_clamp01(float x){ return x<0?0:(x>1?1:x); }
static float a_smooth(float a,float b,float x){ if(b<=a) return x>=b?1.0f:0.0f; float t=a_clamp01((x-a)/(b-a)); return t*t*(3.0f-2.0f*t); }
static void rgb2oklab(int R,int G,int B, float *L,float *a,float *bb){
    float r=g_lin_lut[R], g=g_lin_lut[G], bl=g_lin_lut[B];
    float l=0.4122214708f*r+0.5363325363f*g+0.0514459929f*bl;
    float m=0.2119034982f*r+0.6806995451f*g+0.1073969566f*bl;
    float s=0.0883024619f*r+0.2817188376f*g+0.6299787005f*bl;
    float l_=cbrtf(l), m_=cbrtf(m), s_=cbrtf(s);
    *L =0.2104542553f*l_+0.7936177850f*m_-0.0040720468f*s_;
    *a =1.9779984951f*l_-2.4285922050f*m_+0.4505937099f*s_;
    *bb=0.0259040371f*l_+0.7827717662f*m_-0.8086757660f*s_;
}
static int oklab2rgb(float L,float a,float b, int *R,int *G,int *B){
    float l_=L+0.3963377774f*a+0.2158037573f*b;
    float m_=L-0.1055613458f*a-0.0638541728f*b;
    float s_=L-0.0894841775f*a-1.2914855480f*b;
    float l=l_*l_*l_, m=m_*m_*m_, s=s_*s_*s_;
    float lin[3];
    lin[0]= 4.0767416621f*l-3.3077115913f*m+0.2309699292f*s;
    lin[1]=-1.2684380046f*l+2.6097574011f*m-0.3413193965f*s;
    lin[2]=-0.0041960863f*l-0.7034186147f*m+1.7076147010f*s;
    int in = 1; int *o[3]={R,G,B};
    for(int i=0;i<3;i++){
        float x=lin[i];
        if(x<-0.001f||x>1.001f) in=0;
        if(x<0)x=0; if(x>1)x=1;
        float v = x<=0.0031308f ? 12.92f*x : 1.055f*powf(x,1.0f/2.4f)-0.055f;
        *o[i] = (int)(v*255.0f+0.5f);
    }
    return in;
}
static float wcag_lum(int R,int G,int B){ return 0.2126f*g_lin_lut[R]+0.7152f*g_lin_lut[G]+0.0722f*g_lin_lut[B]; }

static int read_cover_bmp(const char *path, uint8_t *buf)
{
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    uint8_t hdr[54];
    if(fread(hdr,1,54,f)!=54){ fclose(f); return -1; }
    uint32_t off = hdr[10]|(hdr[11]<<8)|(hdr[12]<<16)|((uint32_t)hdr[13]<<24);
    int w = hdr[18]|(hdr[19]<<8), h = hdr[22]|(hdr[23]<<8);
    int bpp = hdr[28]|(hdr[29]<<8);
    uint32_t comp = hdr[30]|(hdr[31]<<8)|(hdr[32]<<16)|((uint32_t)hdr[33]<<24);
    if(w!=CBMP || h!=CBMP || bpp!=24 || comp!=0){ fclose(f); return -1; }
    uint8_t row[CBMP*3];
    if(fseek(f, off, SEEK_SET)!=0){ fclose(f); return -1; }
    for(int yy=0; yy<CBMP; yy++){
        if(fread(row,1,CBMP*3,f)!=(size_t)(CBMP*3)){ fclose(f); return -1; }
        uint8_t *d = buf + (CBMP-1-yy)*CBMP*4;
        for(int x=0;x<CBMP;x++){
            d[x*4+0]=row[x*3+0]; d[x*4+1]=row[x*3+1];
            d[x*4+2]=row[x*3+2]; d[x*4+3]=0xFF;
        }
    }
    fclose(f);
    return 0;
}

static int accent_from_buf(const uint8_t *cbuf, uint32_t *out_rgb)
{
    if(!cbuf) return 0;
    float *pL=malloc(2025*sizeof(float)), *pA=malloc(2025*sizeof(float)),
          *pB=malloc(2025*sizeof(float)), *pW=malloc(2025*sizeof(float));
    if(!pL||!pA||!pB||!pW){ free(pL);free(pA);free(pB);free(pW); return 0; }
    int rc = 0;
    int n=0; float wsum=0,csum=0;
    for(int y=8;y<140;y+=3){
        const uint8_t *rp=cbuf+(size_t)y*CBMP*4;
        for(int x=8;x<140 && n<2025;x+=3){
            const uint8_t *p=rp+x*4; int B=p[0],G=p[1],R=p[2];
            float L,a,b; rgb2oklab(R,G,B,&L,&a,&b);
            float C=sqrtf(a*a+b*b), w=1.0f;
            if(C<0.025f||L<0.08f||L>0.96f) w=0.35f;
            pL[n]=L;pA[n]=a;pB[n]=b;pW[n]=w; wsum+=w; csum+=C*w; n++;
        }
    }
    if(n<32) goto done;
    if(wsum>0 && csum/wsum < 0.025f){ *out_rgb=0xAEB4BE; rc=1; goto done; }

    { const int K=8; float cL[8],cA[8],cB[8],pop[8];
    { int bi=0; float bc=-1; for(int i=0;i<n;i++){ float c=pA[i]*pA[i]+pB[i]*pB[i]; if(c>bc){bc=c;bi=i;} }
      cL[0]=pL[bi];cA[0]=pA[bi];cB[0]=pB[bi]; }
    for(int k=1;k<K;k++){ int bi=0; float bd=-1;
        for(int i=0;i<n;i++){ float md=1e9f;
            for(int j=0;j<k;j++){ float dL=pL[i]-cL[j],da=pA[i]-cA[j],db=pB[i]-cB[j]; float d=1.25f*dL*dL+da*da+db*db; if(d<md)md=d; }
            if(md>bd){bd=md;bi=i;} }
        cL[k]=pL[bi];cA[k]=pA[bi];cB[k]=pB[bi]; }
    for(int it=0;it<10;it++){
        float sL[8]={0},sA[8]={0},sB[8]={0},sw[8]={0};
        for(int i=0;i<n;i++){ int bj=0; float bd=1e9f;
            for(int j=0;j<K;j++){ float dL=pL[i]-cL[j],da=pA[i]-cA[j],db=pB[i]-cB[j]; float d=1.25f*dL*dL+da*da+db*db; if(d<bd){bd=d;bj=j;} }
            float w=pW[i]; sL[bj]+=pL[i]*w; sA[bj]+=pA[i]*w; sB[bj]+=pB[i]*w; sw[bj]+=w; }
        for(int j=0;j<K;j++){ if(sw[j]>0){cL[j]=sL[j]/sw[j];cA[j]=sA[j]/sw[j];cB[j]=sB[j]/sw[j];} pop[j]=sw[j]; }
    }
    float best=-1; int bk=-1;
    for(int j=0;j<K;j++){
        float L=cL[j],a=cA[j],b=cB[j]; float C=sqrtf(a*a+b*b);
        float pf = wsum>0 ? pop[j]/wsum : 0;
        if(pf<0.008f||C<0.035f||L<0.18f||L>0.92f) continue;
        float popS=sqrtf(pf);
        float lS=1.0f-a_clamp01(fabsf(L-0.72f)/0.32f);
        float cS=1.0f-a_clamp01(fabsf(C-0.16f)/0.14f);
        float vivid=a_smooth(0.07f,0.18f,C);
        float darkP=a_smooth(0.18f,0.38f,L);
        float lightP=1.0f-a_smooth(0.88f,0.98f,L);
        float sc=(0.42f*popS+0.28f*cS+0.22f*lS+0.08f*vivid)*darkP*lightP;
        if(pf<0.02f && C<0.12f) sc*=0.5f;
        if(sc>best){best=sc;bk=j;}
    }
    if(bk<0){ *out_rgb=0xAEB4BE; rc=1; goto done; }

    float L=cL[bk],a=cA[bk],b=cB[bk]; float C=sqrtf(a*a+b*b), H=atan2f(b,a);
    if(L<0.58f)L=0.58f; else if(L>0.84f)L=0.84f;
    if(C<0.075f)C=0.075f; else if(C>0.22f)C=0.22f;
    if(C>0.18f&&L>0.72f) C*=0.92f;
    if(C<0.10f) C=C+(0.10f-C)*0.35f;
    a=C*cosf(H); b=C*sinf(H);
    float Ybg=wcag_lum(0x1C,0x1C,0x1E); int R=0,G=0,Bo=0;
    for(int iter=0;iter<48;iter++){
        a=C*cosf(H); b=C*sinf(H);
        int in=oklab2rgb(L,a,b,&R,&G,&Bo);
        if(!in && C>0.04f){ C*=0.94f; continue; }
        float Y=wcag_lum(R,G,Bo); float hi=Y>Ybg?Y:Ybg, lo=Y>Ybg?Ybg:Y;
        if((hi+0.05f)/(lo+0.05f) >= 4.5f || L>=0.88f) break;
        L+=0.015f;
    }
    a=C*cosf(H); b=C*sinf(H); oklab2rgb(L,a,b,&R,&G,&Bo);
    *out_rgb = ((uint32_t)R<<16)|((uint32_t)G<<8)|(uint32_t)Bo;
    rc=1;
    }
done:
    free(pL);free(pA);free(pB);free(pW);
    return rc;
}
#pragma GCC pop_options

static int g_accent_gray = 0;
static _Atomic int g_accent_mode = 0;
static uint32_t    g_accent_color = 0xF23260;
static uint32_t    g_static_last  = 0xFFFFFFFFu;
int ui_accent_is_static(void){ return g_accent_mode == 1; }

static void update_accent_seed(const track_state_t *st)
{
    if(g_accent_mode == 1){
        if(g_static_last != g_accent_color){
            accent = theme_accent_from_rgb(g_accent_color);
            apply_accent();
            g_static_last = g_accent_color;
        }
        return;
    }
    if(!st || !st->have_track){
        if(!g_accent_gray){
            accent = TC(ACCENT_IDLE);
            g_accent_gray = 1;
            last_seed_a[0] = 0; last_seed_b[0] = 0;
            apply_accent();
        }
        return;
    }
    g_accent_gray = 0;
    const char *seed_a = st->album[0] ? st->album : (st->title_ready ? st->title : "");
    const char *seed_b = st->artist;

    if(strcmp(last_seed_a, seed_a) == 0 && strcmp(last_seed_b, seed_b) == 0) {
        return;
    }

    copy_cstr(last_seed_a, sizeof(last_seed_a), seed_a);
    copy_cstr(last_seed_b, sizeof(last_seed_b), seed_b);
    int rgb = mdb_song_accent(st->path);
    if(rgb){ accent = theme_accent_from_rgb(rgb); apply_accent(); }
}

static int g_art_applied = 0;

static int g_np_small_valid, g_np_cover_loading;

void ui_np_cover_changed(void)
{
    if (!cover_img) return;
    int bauhaus = !strcmp(theme_kit()->id, "bauhaus");
    const lv_image_dsc_t *sharp = bauhaus ? sharp_cover_get() : NULL;
    int d = sharp ? 360 : COVER_D;
    lv_image_set_src(cover_img, NULL);
    lv_image_set_src(cover_img, sharp ? (const void *)sharp :
                     g_np_small_valid ? (coverdsc_valid ? (const void *)&g_coverdsc : (const void *)cover_src) : NULL);
    lv_obj_set_size(cover_img, d, d);
    lv_image_set_inner_align(cover_img, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_pivot(cover_img, d / 2, d / 2);
    lv_image_set_scale(cover_img, bauhaus && !sharp ? 623 : 256);
    lv_obj_center(cover_img);
    set_cover_fallback(!sharp && !g_np_small_valid, g_np_cover_loading != 0);
}

static void np_sharp_demand(void)
{
    if (!strcmp(theme_kit()->id, "bauhaus") && screen_current() == SCR_NOWPLAYING) sharp_cover_request();
}

static void clear_art_state(void)
{
    cover_valid = thumb_valid = backdrop_valid = 0;
    coverdsc_valid = 0;
    last_art_key[0] = '\0';
    if(backdrop) lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
    if(cover_note) lv_label_set_text(cover_note, LV_SYMBOL_AUDIO);
    g_np_small_valid = g_np_cover_loading = 0;
    ui_np_cover_changed();
    g_art_applied = 1;
}

typedef struct {
    char track[256];
    int  idx;
    char out[40], tout[40], bout[40];
    char art_key[420];
    int  is_clear;
    char artist[160], album[160], title[IPC_TITLE_CAPACITY];
    int  online;
    int  online_only;
    int  from_net;
} art_req_t;

static pthread_mutex_t g_decode_mu = PTHREAD_MUTEX_INITIALIZER;
void ui_decode_lock(void){ pthread_mutex_lock(&g_decode_mu); }
void ui_decode_unlock(void){ pthread_mutex_unlock(&g_decode_mu); }

#define PWQ_MAX 48
static char            pwq[PWQ_MAX][512];
static int             pwq_head, pwq_tail;
static pthread_mutex_t pwq_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  pwq_cv;
static pthread_once_t  pwq_once = PTHREAD_ONCE_INIT;
static _Atomic int g_prewarm_mode = 0;
static unsigned pwq_mode_seq;
static void pwq_init(void){
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&pwq_cv, &attr);
    pthread_condattr_destroy(&attr);
}
void ui_set_prewarm_mode(int m){
    m = (m<0)?0:(m>2?2:m);
    pthread_once(&pwq_once, pwq_init);
    pthread_mutex_lock(&pwq_mu);
    if(g_prewarm_mode != m){
        g_prewarm_mode = m;
        pwq_mode_seq++;
        pthread_cond_signal(&pwq_cv);
    }
    pthread_mutex_unlock(&pwq_mu);
}
int ui_prewarm_enqueue(const char *path){
    if(!path || !path[0]) return 1;
    int ok = 0;
    pthread_once(&pwq_once, pwq_init);
    pthread_mutex_lock(&pwq_mu);
    int nt = (pwq_tail+1) % PWQ_MAX;
    if(nt != pwq_head){
        int dup = 0;
        for(int i=pwq_head; i!=pwq_tail; i=(i+1)%PWQ_MAX) if(!strcmp(pwq[i], path)){ dup=1; break; }
        if(dup) ok = 1;
        else { copy_cstr(pwq[pwq_tail], sizeof pwq[pwq_tail], path); pwq_tail = nt; ok = 1; pthread_cond_signal(&pwq_cv); }
    }
    pthread_mutex_unlock(&pwq_mu);
    return ok;
}
static int pwq_pop(char *out, int cap){
    int got = 0;
    pthread_mutex_lock(&pwq_mu);
    if(pwq_head != pwq_tail){ copy_cstr(out, cap, pwq[pwq_head]); pwq_head = (pwq_head+1)%PWQ_MAX; got = 1; }
    pthread_mutex_unlock(&pwq_mu);
    return got;
}
static void pw_sleep(int secs){
    struct timespec until;
    clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_sec += secs;
    pthread_once(&pwq_once, pwq_init);
    pthread_mutex_lock(&pwq_mu);
    unsigned seq = pwq_mode_seq;
    while(pwq_head == pwq_tail && seq == pwq_mode_seq){
        if(g_prewarm_mode == 0) pthread_cond_wait(&pwq_cv, &pwq_mu);
        else if(pthread_cond_timedwait(&pwq_cv, &pwq_mu, &until) == ETIMEDOUT) break;
    }
    pthread_mutex_unlock(&pwq_mu);
}

static pthread_mutex_t g_art_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_art_title_pending;
static uint64_t g_art_token;
static unsigned g_art_generation;
static art_req_t g_art_target;
static unsigned  g_art_req      = 0;
static int       g_art_inflight = 0;
static int       g_art_ready    = 0;
static int       g_art_rc       = -1;
static unsigned  g_art_done_req = 0;
static art_req_t g_art_result;

static void apply_art(const art_req_t *job)
{
    snprintf(cover_src,    sizeof cover_src,    "A:/tmp/cover%d.bmp",    job->idx);
    snprintf(thumb_src,    sizeof thumb_src,    "A:/tmp/thumb%d.bmp",    job->idx);
    snprintf(backdrop_src, sizeof backdrop_src, "A:/tmp/backdrop%d.bmp", job->idx);
    coverdsc_valid = (load_cover_dsc(job->out) == 0);
    cover_valid = thumb_valid = backdrop_valid = 1;
    g_np_small_valid = 1; g_np_cover_loading = 0;
    ui_np_cover_changed();
    np_sharp_demand();
    if(g_accent_mode != 1){
        int acc_rgb = mdb_song_accent(job->track);
        if(acc_rgb){ accent = theme_accent_from_rgb(acc_rgb); apply_accent(); }
    }
    copy_cstr(last_art_key, sizeof last_art_key, job->art_key);
    if(backdrop && g_np_backdrop_on) {
        lv_image_set_src(backdrop, backdrop_src);
        lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(backdrop);
    }
    displayed_idx = job->idx;
    g_art_applied = 1;
}

static void *art_worker(void *arg)
{
    (void)arg;
    for(;;){
        art_req_t job; unsigned my_req, my_gen;
        pthread_mutex_lock(&g_art_mu);
        job = g_art_target; my_req = g_art_req;
        my_gen = art_cancel_gen();
        pthread_mutex_unlock(&g_art_mu);

        int rc, from_net = 0;
        char fp0[24] = "";
        if(job.is_clear) rc = -1;
        else if(!job.online_only && artcache_get(job.track, job.out, job.tout, job.bout) == 0){ rc = 0;
               if(artcache_key(job.track, fp0, sizeof fp0) != 0) fp0[0] = 0; }
        else { int have_fp = artcache_key(job.track, fp0, sizeof fp0) == 0;
               pthread_mutex_lock(&g_decode_mu);
               rc = job.online_only ? -1 : art_make_all_ex_gen(job.track, job.out, job.tout, job.bout, 1, my_gen);
               char np[300];
               if(rc != 0 && rc != -2 && job.online && netart_have(job.artist, job.album, job.title, np, sizeof np)){
                   if(netart_touch(np)){
                       rc = art_make_all_ex_gen(np, job.out, job.tout, job.bout, 1, my_gen);
                       from_net = rc == 0;
                       int ex = art_last_exit();
                       if(rc != 0 && (ex == 3 || ex == 4 || ex == 5)) netart_reject(np);
                   }
               }
               pthread_mutex_unlock(&g_decode_mu);
               if(rc == 0 && have_fp && !from_net) artcache_put_if(job.track, fp0, job.out, job.tout, job.bout); }

        if(rc == 0 && !from_net && !job.is_clear && !ui_accent_is_static() && fp0[0] && !mdb_song_accent_fresh(job.track, fp0)){
            uint8_t *buf = malloc(CBMP*CBMP*4);
            if(buf){
                uint32_t rgb;
                if(read_cover_bmp(job.out, buf) == 0 && accent_from_buf(buf, &rgb))
                    mdb_set_song_accent(job.track, (int)(rgb & 0xFFFFFF), fp0);
                free(buf);
            }
        }

        job.from_net = from_net;
        pthread_mutex_lock(&g_art_mu);
        if(g_art_req == my_req){
            g_art_rc = rc; g_art_result = job; g_art_done_req = my_req;
            g_art_ready = 1; g_art_inflight = 0;
            pthread_mutex_unlock(&g_art_mu);
            return NULL;
        }
        pthread_mutex_unlock(&g_art_mu);
    }
}

static void art_request_clear(void)
{
    g_art_title_pending = 0;
    g_np_small_valid = 0;
    sharp_cover_set_source("");
    clear_art_state();
    pthread_mutex_lock(&g_art_mu);
    g_art_req++;
    memset(&g_art_target, 0, sizeof g_art_target);
    g_art_target.is_clear = 1;
    pthread_mutex_unlock(&g_art_mu);
    art_cancel();
}

#define NP_MAXCHAP 256
static chapter_t g_chaps[NP_MAXCHAP];
static int       g_nchaps;
static char      g_chap_path[256];

static int book_meta_line(const track_state_t *st, char *out, int cap)
{
    if(!mdb_is_book_path(st->path)) return 0;
    if(strcmp(g_chap_path, st->path) != 0){
        g_nchaps = scan_read_chapters(st->path, g_chaps, NP_MAXCHAP);
        copy_cstr(g_chap_path, sizeof g_chap_path, st->path);
    }
    if(g_nchaps <= 0){ out[0] = '\0'; return 1; }
    int cur = 0;
    for(int i = 0; i < g_nchaps; i++){ if(g_chaps[i].start_ms <= st->position_ms) cur = i; else break; }
    char autoname[24]; snprintf(autoname, sizeof autoname, "Chapter %d", cur + 1);
    if(strcmp(g_chaps[cur].title, autoname) == 0)
        snprintf(out, cap, "Chapter %d/%d", cur + 1, g_nchaps);
    else
        snprintf(out, cap, "%d/%d  \xC2\xB7  %s", cur + 1, g_nchaps, g_chaps[cur].title);
    return 1;
}

static int g_np_bookmode = -1;
static void np_transport_glyphs(int book)
{
    if(book == g_np_bookmode) return;
    g_np_bookmode = book;
    lv_obj_set_style_text_font(btn_prev, book ? TF(UI_22) : TF(UI_28), LV_PART_MAIN);
    lv_obj_set_style_text_font(btn_next, book ? TF(UI_22) : TF(UI_28), LV_PART_MAIN);
    lv_label_set_text(btn_prev, book ? "-15" : LV_SYMBOL_PREV);
    lv_label_set_text(btn_next, book ? "+30" : LV_SYMBOL_NEXT);
}

static long book_cur_chapter_end(const track_state_t *st)
{
    if(strcmp(g_chap_path, st->path) != 0){
        g_nchaps = scan_read_chapters(st->path, g_chaps, NP_MAXCHAP);
        copy_cstr(g_chap_path, sizeof g_chap_path, st->path);
    }
    if(g_nchaps <= 0) return st->duration_ms;
    int cur = 0;
    for(int i = 0; i < g_nchaps; i++){ if(g_chaps[i].start_ms <= st->position_ms) cur = i; else break; }
    if(cur + 1 < g_nchaps) return g_chaps[cur+1].start_ms;
    return st->duration_ms > 0 ? st->duration_ms : g_chaps[cur].start_ms;
}

static int book_chapter_window(const track_state_t *st, long *lo, long *hi)
{
    if(!mdb_is_book_path(st->path)) return 0;
    if(strcmp(g_chap_path, st->path) != 0){
        g_nchaps = scan_read_chapters(st->path, g_chaps, NP_MAXCHAP);
        copy_cstr(g_chap_path, sizeof g_chap_path, st->path);
    }
    if(g_nchaps <= 0) return 0;
    int cur = 0;
    for(int i = 0; i < g_nchaps; i++){ if(g_chaps[i].start_ms <= st->position_ms) cur = i; else break; }
    long a = g_chaps[cur].start_ms;
    long b = (cur + 1 < g_nchaps) ? g_chaps[cur+1].start_ms
                                  : (st->duration_ms > 0 ? st->duration_ms : a);
    if(b <= a) return 0;
    *lo = a; *hi = b;
    return 1;
}

static lv_obj_t *g_sleep_dlg;
void ui_np_close_overlays(void){ if(g_sleep_dlg){ lv_obj_del(g_sleep_dlg); g_sleep_dlg = NULL; } }
int ui_np_cover_hit(int x, int y){
    if(!cover || lv_obj_has_flag(cover, LV_OBJ_FLAG_HIDDEN)) return 0;
    lv_area_t a; lv_obj_get_coords(cover, &a);
    return x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2;
}
int  ui_np_overlay_active(void){ return g_sleep_dlg != NULL; }

static void sleep_opt_cb(lv_event_t *e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if(v == -1){
        track_state_t st; ipc_get_state(&st);
        if(!mdb_is_book_path(st.path)){ ui_set_sleep_timer(0); ui_np_close_overlays(); return; }
        long tgt = book_cur_chapter_end(&st);
        int at_end = (st.duration_ms > 0 && tgt >= st.duration_ms);
        if(tgt > st.position_ms){ ui_set_sleep_eoc(tgt, st.path, at_end); ui_toast("Sleep at end of chapter"); }
        else { ui_set_sleep_timer(0); ui_toast("Sleep timer off"); }
    } else if(v > 0){
        ui_set_sleep_timer(v);
        char b[24]; snprintf(b, sizeof b, "Sleep in %d min", v); ui_toast(b);
    } else {
        ui_set_sleep_timer(0); ui_toast("Sleep timer off");
    }
    ui_np_close_overlays();
}
static void sleep_scrim_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) ui_np_close_overlays(); }

static void sleep_click_cb(lv_event_t *e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(g_sleep_dlg){ ui_np_close_overlays(); return; }
    lv_obj_t *scrim = lv_obj_create(lv_layer_top());
    g_sleep_dlg = scrim;
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, 360, 360); lv_obj_center(scrim);
    lv_obj_set_style_bg_color(scrim, TC(SCRIM), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_60, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(scrim, sleep_scrim_cb, LV_EVENT_CLICKED, NULL, "ui.sleep_scrim", UI_CORE);

    lv_obj_t *panel = lv_obj_create(scrim);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 204, 272); lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_bg_color(panel, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, TC(BORDER), 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_style_pad_ver(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_label_create(panel);
    lv_label_set_text(hdr, tr("Sleep Timer"));
    lv_obj_set_style_text_font(hdr, TF(UI_16), 0);
    lv_obj_set_style_text_color(hdr, TC(TEXT_PRIMARY), 0);
    if(!strcmp(theme_kit()->id, "blueprint")){
        char figure[96];
        lv_snprintf(figure, sizeof figure, "fig.%d  %s", screen_figure_number(screen_current()), tr("Sleep Timer"));
        lv_point_t size;
        lv_text_get_size(&size, figure, TF(UI_16), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if(size.x <= 188) lv_label_set_text(hdr, figure);
        lv_obj_set_style_text_color(hdr, TC(ACCENT_PRIMARY), 0);
    }

    static const char *OPT_T[] = { "15 min", "30 min", "45 min", "60 min", "End of chapter", "Off" };
    static const int   OPT_V[] = { 15, 30, 45, 60, -1, 0 };
    for(unsigned i = 0; i < sizeof OPT_V / sizeof OPT_V[0]; i++){
        lv_obj_t *b = lv_button_create(panel);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 188, 32);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, TC(RAISED_PRESSED), LV_STATE_PRESSED);
        ui_on(b, sleep_opt_cb, LV_EVENT_CLICKED, (void *)(intptr_t)OPT_V[i], "ui.sleep_opt", UI_CORE);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, tr(OPT_T[i]));
        lv_obj_set_style_text_font(l, TF(UI_16), 0);
        lv_obj_set_style_text_color(l, OPT_V[i] == -1 ? accent : TC(TEXT_PRIMARY), 0);
        lv_obj_center(l);
    }
}

static void make_art_key(const track_state_t *st, char *out, int cap)
{
    if(mdb_is_book_path(st->path)){ snprintf(out, cap, "%s", st->path); return; }
    char dir[256]; snprintf(dir, sizeof dir, "%s", st->path);
    char *slash = strrchr(dir, '/'); if(slash) *slash = '\0'; else dir[0] = '\0';
    snprintf(out, cap, "%s\n%s", st->album, dir);
}

static int g_art_force;
void ui_art_retry(void){ sharp_cover_invalidate(); last_path[0] = '\0'; last_art_key[0] = '\0'; g_art_force = 1; }
void ui_art_redo(void){ ui_art_retry(); }
int ui_take_art_force(void){ int f = g_art_force; g_art_force = 0; return f; }

void ui_np_enter(void)
{
    if (!g_np_small_valid && (strcmp(theme_kit()->id, "bauhaus") || !sharp_cover_get())) ui_art_retry();
    g_art_force = 1;
    ui_np_cover_changed();
    if (!strcmp(theme_kit()->id, "bauhaus")) sharp_cover_request();
}

static void update_cover_for_path(const track_state_t *st)
{
    int path_changed = strcmp(last_path, st->path) != 0;
    int identity_changed = st->playback_token != g_art_token ||
                           st->player_generation != g_art_generation;
    if(path_changed) g_np_small_valid = 0;
    sharp_cover_set_source(st->path);
    if(path_changed || identity_changed){
        g_art_token = st->playback_token;
        g_art_generation = st->player_generation;
        g_art_title_pending = st->have_track && !st->title_ready &&
                              cfg_get_int("online_art", 0);
    }
    int online_only = 0;
    if(!path_changed){
        if(!g_art_title_pending) return;
        if(!st->have_track){ g_art_title_pending = 0; return; }
        if(!st->title_ready) return;
        pthread_mutex_lock(&g_art_mu);
        int waiting = g_art_inflight || g_art_ready;
        int missed = g_art_rc != 0 && g_art_rc != -2;
        pthread_mutex_unlock(&g_art_mu);
        if(waiting) return;
        g_art_title_pending = 0;
        if(!missed || !cfg_get_int("online_art", 0)) return;
        online_only = 1;
    }
    np_sharp_demand();
    char art_key[420]; make_art_key(st, art_key, sizeof art_key);
    copy_cstr(want_art_key, sizeof want_art_key, art_key);

    if(st->album[0] && cover_valid && strcmp(last_art_key, art_key) == 0) {
        g_art_title_pending = 0;
        copy_cstr(last_path, sizeof(last_path), st->path);
        if(cover_note) lv_label_set_text(cover_note, LV_SYMBOL_AUDIO);
        g_np_small_valid = 1; g_np_cover_loading = 0;
        ui_np_cover_changed();
        return;
    }

    copy_cstr(last_path, sizeof(last_path), st->path);

    if(!st->path[0]) { art_request_clear(); return; }

    if(cover_note) lv_label_set_text(cover_note, LV_SYMBOL_REFRESH);
    g_np_small_valid = 0; g_np_cover_loading = 1;
    ui_np_cover_changed();
    coverdsc_valid = 0;

    int nidx = displayed_idx ^ 1;
    art_req_t job; memset(&job, 0, sizeof job);
    job.idx = nidx;
    copy_cstr(job.track, sizeof job.track, st->path);
    copy_cstr(job.art_key, sizeof job.art_key, art_key);
    copy_cstr(job.artist, sizeof job.artist, st->artist);
    copy_cstr(job.album,  sizeof job.album,  st->album);
    copy_cstr(job.title,  sizeof job.title,  st->title_ready ? st->title : "");
    job.online_only = online_only;
    job.online = st->title_ready && cfg_get_int("online_art", 0);
    snprintf(job.out,  sizeof job.out,  "/tmp/cover%d.bmp",    nidx);
    snprintf(job.tout, sizeof job.tout, "/tmp/thumb%d.bmp",    nidx);
    snprintf(job.bout, sizeof job.bout, "/tmp/backdrop%d.bmp", nidx);

    int launch = 0;
    pthread_mutex_lock(&g_art_mu);
    g_art_req++;
    g_art_target = job;
    if(!g_art_inflight){ g_art_inflight = 1; launch = 1; }
    else art_cancel();
    pthread_mutex_unlock(&g_art_mu);

    if(launch){
        pthread_t th;
        if(pthread_create(&th, NULL, art_worker, NULL) == 0){
            pthread_detach(th);
        } else {
            pthread_mutex_lock(&g_art_mu); g_art_inflight = 0; pthread_mutex_unlock(&g_art_mu);
            clear_art_state();
        }
    }
}

void ui_art_poll(lv_timer_t *t)
{
    (void)t;
    if(g_art_title_pending){
        track_state_t st; ipc_get_state(&st); update_cover_for_path(&st);
    }
    { char got[512];
      if(netart_take_ready(got, sizeof got) && cfg_get_int("online_art", 0) && strcmp(got, last_path) == 0) ui_art_retry(); }
    int ready = 0, rc = -1; art_req_t res; unsigned done_req = 0, cur_req = 0;
    pthread_mutex_lock(&g_art_mu);
    if(g_art_ready){
        ready = 1; g_art_ready = 0;
        rc = g_art_rc; res = g_art_result; done_req = g_art_done_req; cur_req = g_art_req;
    }
    pthread_mutex_unlock(&g_art_mu);
    if(!ready) return;
    if(done_req != cur_req) return;
    if(res.is_clear)      clear_art_state();
    else if(rc == 0 && res.from_net && !cfg_get_int("online_art", 0)){
        clear_art_state();
    }
    else if(rc == 0){
        if(strcmp(res.art_key, want_art_key) == 0){
            apply_art(&res);
            if(res.from_net) last_art_key[0] = '\0';
        }
    }
    else {
        clear_art_state();
        if(rc != -2 && res.online && cfg_get_int("online_art", 0) && strcmp(res.art_key, want_art_key) == 0)
            netart_request(res.track, res.artist, res.album, res.title);
    }
}

int ui_take_art_applied(void)
{
    if(!g_art_applied) return 0;
    g_art_applied = 0;
    return 1;
}

void ui_set_accent_config(int mode, int rgb){
    g_accent_mode  = mode ? 1 : 0;
    g_accent_color = (uint32_t)rgb & 0xFFFFFF;
    if(g_accent_mode){
        accent = theme_accent_from_rgb(g_accent_color);
        g_accent_gray = 0;
    } else {
        last_seed_a[0] = '\0'; last_seed_b[0] = '\0';
    }
    apply_accent();
    g_static_last = g_accent_mode ? g_accent_color : 0xFFFFFFFFu;
    g_art_applied = 1;
}

static int pw_temp_dc(void){
    FILE *f=fopen("/sys/class/power_supply/cw221X-bat/temp","r"); if(!f) return -1;
    int t=-1; if(fscanf(f,"%d",&t)!=1) t=-1; fclose(f); return t;
}
static int pw_charging(void){
    long cur=0;
    FILE *f=fopen("/sys/class/power_supply/cw221X-bat/current_now","r"); if(!f) return 0;
    if(fscanf(f,"%ld",&cur)!=1) cur=0; fclose(f);
    return cur > 0;
}
static int pw_window_open(void){
    if(!sd_io_allowed()) return 0;
    int m=g_prewarm_mode; if(m==0) return 0;
    if(!ui_main_is_idle()) return 0;
    if(m==2) return pw_charging();
    return 1;
}

static void pw_wait_window(int *hot){
    for(;;){
        if(!sd_io_allowed()){ usleep(200000); continue; }
        int t = pw_temp_dc();
        if(t < 0){ sleep(15); continue; }
        if(*hot){ if(t < 400) *hot=0; else { sleep(15); continue; } }
        else if(t >= 420){ *hot=1; sleep(15); continue; }
        int busy; pthread_mutex_lock(&g_art_mu); busy=g_art_inflight; pthread_mutex_unlock(&g_art_mu);
        if(busy){ usleep(300*1000); continue; }
        return;
    }
}

static void *art_prewarm_worker(void *arg)
{
    (void)arg;
    nice(15);
    const char *PC="/tmp/pw_cover.bmp", *PT="/tmp/pw_thumb.bmp", *PB="/tmp/pw_backdrop.bmp";
    int id = 0, hot = 0, did_work = 0;
    for(;;){
        char qpath[512];
        if(pwq_pop(qpath, sizeof qpath)){
            pw_wait_window(&hot);
            char fp0[24];
            int cached = artcache_lookup(qpath, fp0, sizeof fp0);
            if(cached == 1){ usleep(15*1000); continue; }
            if(cached == 0){
                pthread_mutex_lock(&g_decode_mu);
                int r = art_make_all(qpath, PC, PT, PB);
                pthread_mutex_unlock(&g_decode_mu);
                if(r == 0) artcache_put_if(qpath, fp0, PC, PT, PB);
            }
            usleep(120*1000);
            continue;
        }
        if(!pw_window_open()){ id=0; did_work=0; pw_sleep(5); continue; }

        int t = pw_temp_dc();
        if(t < 0){ sleep(15); continue; }
        if(hot){ if(t < 400) hot=0; else { sleep(15); continue; } }
        else if(t >= 420){ hot=1; sleep(15); continue; }

        { int busy; pthread_mutex_lock(&g_art_mu); busy=g_art_inflight; pthread_mutex_unlock(&g_art_mu);
          if(busy){ usleep(300*1000); continue; } }

        char path[300];
        if(!mdb_prewarm_next(id, &id, path, sizeof path)){
            id=0; pw_sleep(did_work?60:1800); did_work=0; continue;
        }
        if(!path[0]) continue;

        int have_art = artcache_has(path);
        char acc_fp[24];
        int need_acc = !ui_accent_is_static() && artcache_key(path, acc_fp, sizeof acc_fp) == 0
                       && !mdb_song_accent_fresh(path, acc_fp);
        if(have_art && !need_acc){ usleep(15*1000); continue; }

        if(!have_art){
            char fp0[24];
            if(artcache_key(path, fp0, sizeof fp0) != 0){ usleep(300*1000); continue; }
            pthread_mutex_lock(&g_decode_mu);
            int r = art_make_all(path, PC, PT, PB);
            pthread_mutex_unlock(&g_decode_mu);
            if(r == 0) artcache_put_if(path, fp0, PC, PT, PB);
            else { usleep(300*1000); continue; }
        }
        if(need_acc){
            int cover_ready = have_art ? (artcache_get_cover(path, PC) == 0) : 1;
            if(cover_ready){
                uint8_t *buf = malloc(CBMP*CBMP*4);
                if(buf){
                    uint32_t rgb;
                    if(read_cover_bmp(PC, buf) == 0 && accent_from_buf(buf, &rgb))
                        mdb_set_song_accent(path, (int)(rgb & 0xFFFFFF), acc_fp);
                    free(buf);
                }
            }
        }
        did_work = 1;
        sleep(3);
    }
    return NULL;
}

void ui_start_art_prewarm(void)
{
    pthread_t th;
    if(pthread_create(&th, NULL, art_prewarm_worker, NULL) == 0) pthread_detach(th);
}

const char *ui_current_cover_src(void)
{
    return cover_valid ? cover_src : NULL;
}

const void *ui_current_cover_dsc(void)
{
    return coverdsc_valid ? (const void *)&g_coverdsc : NULL;
}

const char *ui_current_thumb_src(void)
{
    return thumb_valid ? thumb_src : NULL;
}

const char *ui_current_backdrop_src(void)
{
    return backdrop_valid ? backdrop_src : NULL;
}

void ui_create(lv_obj_t *root)
{
    lv_obj_t *scr = root;
    if(theme_screen_plain(THEME_PLAIN_NOWPLAYING)) theme_screen_solid(scr);

    ui_fonts_init();
    lin_lut_init();
    accent = (g_accent_mode == 1) ? theme_accent_from_rgb(g_accent_color) : TC(ACCENT_PRIMARY);
    last_path[0] = '\0';
    last_art_key[0] = '\0';
    want_art_key[0] = '\0';
    last_seed_a[0] = '\0';
    last_seed_b[0] = '\0';
    cover_src[0] = '\0';
    cover_valid = 0;
    coverdsc_valid = 0;
    thumb_src[0] = '\0';
    thumb_valid = 0;
    backdrop_valid = 0;
    displayed_idx = 0;
    shown_progress = 0;

    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, TC(CANVAS), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    backdrop_src[0] = '\0';
    backdrop = lv_image_create(scr);
    lv_obj_set_size(backdrop, 360, 360);
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_image_recolor(backdrop, TC(IMAGE_TINT), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(backdrop, 150, LV_PART_MAIN);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ring = lv_arc_create(scr);
    if(theme_trait(THEME_TRAIT_NP_BIG_PLAY)) g_ring_d = 352;
    lv_obj_set_size(ring, g_ring_d, g_ring_d);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_range(ring, 0, 1000);
    lv_arc_set_value(ring, 0);
    lv_arc_set_bg_angles(ring, 0, ARC_SWEEP);
    lv_arc_set_rotation(ring, ARC_ROT);
    lv_arc_set_mode(ring, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ring, TC(CONTROL_KNOB), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(ring, 5, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    if(theme_trait(THEME_TRAIT_MATRIX_CLOCK)) some_ring_create(scr);
    if(hifi_np_active()) hifi_ticks_create(scr);

    cover = lv_obj_create(scr);
    lv_obj_remove_style_all(cover);
    lv_obj_set_size(cover, COVER_D, COVER_D);
    lv_obj_align(cover, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_radius(cover, 14, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(cover, true, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cover, TC(ART_PLACEHOLDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cover, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cover, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cover, cover_click_cb, LV_EVENT_PRESSED, NULL);
    ui_on(cover, cover_click_cb, LV_EVENT_CLICKED, NULL, "ui.cover", UI_DECORATIVE);

    cover_img = lv_image_create(cover);
    lv_obj_set_size(cover_img, COVER_D, COVER_D);
    lv_obj_center(cover_img);
    lv_image_set_inner_align(cover_img, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_pivot(cover_img, COVER_D/2, COVER_D/2);
    lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cover_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    cover_note = lv_label_create(cover);
    style_text(cover_note, TF(UI_28), TC(TEXT_PRIMARY));
    lv_label_set_text(cover_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_opa(cover_note, LV_OPA_90, LV_PART_MAIN);
    lv_obj_center(cover_note);
    lv_obj_clear_flag(cover_note, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    set_cover_fallback(true, false);

    spindle = lv_obj_create(cover);
    lv_obj_remove_style_all(spindle);
    lv_obj_set_size(spindle, 34, 34);
    lv_obj_center(spindle);
    lv_obj_set_style_radius(spindle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(spindle, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(spindle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(spindle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *hole = lv_obj_create(spindle);
    lv_obj_remove_style_all(hole);
    lv_obj_set_size(hole, 8, 8);
    lv_obj_center(hole);
    lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hole, TC(CANVAS), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(spindle, LV_OBJ_FLAG_HIDDEN);

    btn_fav = lv_button_create(scr);
    lv_obj_remove_style_all(btn_fav);
    lv_obj_set_size(btn_fav, 44, 44);
    lv_obj_align(btn_fav, LV_ALIGN_TOP_MID, 118, 126);
    lv_obj_add_flag(btn_fav, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_fav, fav_click_cb, LV_EVENT_PRESSED, NULL);
    ui_on(btn_fav, fav_click_cb, LV_EVENT_CLICKED, NULL, "ui.fav", UI_CORE);
    fav_icon = lv_label_create(btn_fav);
    lv_obj_set_style_text_font(fav_icon, TF(ICON_28), LV_PART_MAIN);
    lv_obj_set_style_text_color(fav_icon, TC(TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_text(fav_icon, HEART_OUTLINE);
    lv_obj_center(fav_icon);
    lv_obj_add_flag(btn_fav, LV_OBJ_FLAG_HIDDEN);

    btn_sleep = lv_button_create(scr);
    lv_obj_remove_style_all(btn_sleep);
    lv_obj_set_size(btn_sleep, 44, 44);
    lv_obj_align(btn_sleep, LV_ALIGN_TOP_MID, 118, 126);
    lv_obj_add_flag(btn_sleep, LV_OBJ_FLAG_CLICKABLE);
    ui_on(btn_sleep, sleep_click_cb, LV_EVENT_CLICKED, NULL, "ui.sleep", UI_CORE);
    sleep_icon = lv_label_create(btn_sleep);
    lv_obj_set_style_text_font(sleep_icon, TF(ICON_28), LV_PART_MAIN);
    lv_obj_set_style_text_color(sleep_icon, TC(TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_text(sleep_icon, MOON_ICON);
    lv_obj_center(sleep_icon);
    lv_obj_add_flag(btn_sleep, LV_OBJ_FLAG_HIDDEN);

    btn_mode = lv_button_create(scr);
    lv_obj_remove_style_all(btn_mode);
    lv_obj_set_size(btn_mode, 44, 44);
    lv_obj_align(btn_mode, LV_ALIGN_TOP_MID, -118, 126);
    lv_obj_add_flag(btn_mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_mode, mode_click_cb, LV_EVENT_PRESSED, NULL);
    ui_on(btn_mode, mode_click_cb, LV_EVENT_CLICKED, NULL, "ui.mode", UI_CORE);
    mode_icon = lv_label_create(btn_mode);
    lv_obj_set_style_text_font(mode_icon, TF(ICON_28), LV_PART_MAIN);
    lv_label_set_text(mode_icon, MODE_ARROW);
    lv_obj_center(mode_icon);
    mode_one = lv_label_create(btn_mode);
    lv_obj_set_style_text_font(mode_one, TF(UI_10), LV_PART_MAIN);
    lv_obj_set_style_text_color(mode_one, TC(TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_text(mode_one, "1");
    lv_obj_center(mode_one);
    lv_obj_add_flag(mode_one, LV_OBJ_FLAG_HIDDEN);
    mode_refresh(cfg_get_int("work_mode", 0));

    title = lv_label_create(scr);
    g_title_scroll_mode = LV_LABEL_LONG_CLIP;
    lv_obj_set_width(title, 260);
    style_text(title, TF(USER_20), TC(TEXT_PRIMARY));
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(title, tr("No Track"));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 198);

    artist = lv_label_create(scr);
    lv_obj_set_width(artist, 238);
    style_text(artist, TF(USER_16), TC(TEXT_SECONDARY));
    lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
    lv_label_set_text(artist, "");
    lv_obj_align(artist, LV_ALIGN_TOP_MID, 0, 226);

    album = lv_label_create(scr);
    lv_obj_set_width(album, 218);
    style_text(album, TF(USER_14), TC(TEXT_MUTED));
    lv_label_set_long_mode(album, LV_LABEL_LONG_DOT);
    lv_label_set_text(album, "");
    lv_obj_align(album, LV_ALIGN_TOP_MID, 0, 246);

    album_pos = lv_label_create(scr);
    style_text(album_pos, TF(USER_14), TC(TEXT_MUTED));
    lv_label_set_long_mode(album_pos, LV_LABEL_LONG_DOT);
    lv_label_set_text(album_pos, "");
    lv_obj_add_flag(album_pos, LV_OBJ_FLAG_HIDDEN);

    btn_prev = lv_label_create(scr);
    style_text(btn_prev, TF(UI_28), TC(TEXT_MUTED));
    lv_label_set_text(btn_prev, LV_SYMBOL_PREV);
    lv_obj_align(btn_prev, LV_ALIGN_TOP_MID, -52, 278);

    btn_pp = lv_label_create(scr);
    style_text(btn_pp, TF(UI_32), accent);
    ui_pp_glyph(btn_pp, 0);
    lv_obj_align(btn_pp, LV_ALIGN_TOP_MID, 0, 280);

    btn_next = lv_label_create(scr);
    style_text(btn_next, TF(UI_28), TC(TEXT_MUTED));
    lv_label_set_text(btn_next, LV_SYMBOL_NEXT);
    lv_obj_align(btn_next, LV_ALIGN_TOP_MID, 52, 278);

    if(theme_trait(THEME_TRAIT_NP_BIG_PLAY)){
        style_text(title, TF(HEADER), TC(TEXT_PRIMARY));
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 194);
        lv_obj_align(artist, LV_ALIGN_TOP_MID, 0, 224);
        lv_obj_align(album, LV_ALIGN_TOP_MID, 0, 242);
        lv_obj_set_size(btn_pp, 52, 52);
        lv_obj_set_style_radius(btn_pp, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(btn_pp, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn_pp, accent, 0);
        lv_obj_set_style_text_color(btn_pp, TC(ON_ACCENT), 0);
        lv_obj_set_style_text_align(btn_pp, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(btn_pp, 9, 0);
        lv_obj_align(btn_pp, LV_ALIGN_TOP_MID, 0, 264);
        lv_obj_set_style_text_color(btn_prev, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_color(btn_next, TC(TEXT_PRIMARY), 0);
        lv_obj_align(btn_prev, LV_ALIGN_TOP_MID, -70, 274);
        lv_obj_align(btn_next, LV_ALIGN_TOP_MID, 70, 274);
    }
    make_clickable(btn_prev, "0201000C0002");
    make_clickable(btn_pp,   PLAYER_CMD_PLAYPAUSE);
    make_clickable(btn_next, "0201000C0001");
    lv_obj_set_ext_click_area(btn_prev, 14);
    lv_obj_set_ext_click_area(btn_next, 14);
    lv_obj_set_ext_click_area(btn_pp, 12);

    t_elapsed = lv_label_create(scr);
    lv_obj_set_width(t_elapsed, 80);
    style_text(t_elapsed, TF(UI_14), TC(TEXT_MUTED));
    lv_label_set_text(t_elapsed, "0:00");
    lv_obj_align(t_elapsed, LV_ALIGN_TOP_MID, -46, 318);

    lv_obj_t *t_sep = lv_label_create(scr);
    style_text(t_sep, TF(UI_14), TC(TEXT_SEPARATOR));
    lv_label_set_text(t_sep, "/");
    lv_obj_align(t_sep, LV_ALIGN_TOP_MID, 0, 318);

    t_remain = lv_label_create(scr);
    lv_obj_set_width(t_remain, 80);
    style_text(t_remain, TF(UI_14), TC(TEXT_MUTED));
    lv_label_set_text(t_remain, "0:00");
    lv_obj_align(t_remain, LV_ALIGN_TOP_MID, 46, 318);

    lv_obj_t *dots[2];
    for(int i=0;i<2;i++){
        lv_obj_t *dot = dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 7, 7);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, i==0 ? -8 : 8, -14);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, (i==0 ? TC(PAGE_DOT_ACTIVE) : TC(PAGE_DOT_DIM)), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, i==0 ? LV_OPA_COVER : LV_OPA_60, LV_PART_MAIN);
    }

    if(theme_kit()->nowplaying){
        np_parts_t np = { scr, backdrop, ring, cover, cover_img, cover_note, btn_fav, fav_icon, btn_sleep, sleep_icon,
                          btn_mode, mode_icon, mode_one, title, artist, album, btn_prev, btn_pp, btn_next, t_elapsed,
                          t_sep, t_remain, { dots[0], dots[1] }, album_pos };
        theme_kit()->nowplaying(&np);
    }

    ui_back_button(scr, !lv_obj_has_flag(ring, LV_OBJ_FLAG_HIDDEN));

    fsart = lv_obj_create(scr);
    lv_obj_remove_style_all(fsart);
    lv_obj_set_size(fsart, 360, 360);
    lv_obj_set_pos(fsart, 0, 0);
    lv_obj_set_style_bg_color(fsart, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(fsart, LV_OPA_COVER, 0);
    lv_obj_add_flag(fsart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(fsart, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(fsart, fsart_click_cb, LV_EVENT_CLICKED, NULL, "ui.fsart", UI_CORE);
    lv_obj_add_flag(fsart, LV_OBJ_FLAG_HIDDEN);

    fsart_img = lv_image_create(fsart);
    lv_obj_center(fsart_img);
    lv_obj_clear_flag(fsart_img, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *scrim = lv_obj_create(fsart);
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, 360, 130);
    lv_obj_align(scrim, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(scrim, TC(SCRIM), 0);
    lv_obj_set_style_bg_grad_color(scrim, TC(SCRIM), 0);
    lv_obj_set_style_bg_grad_dir(scrim, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(scrim, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(scrim, LV_OPA_80, 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_CLICKABLE);

    fsart_title = lv_label_create(fsart);
    lv_obj_set_width(fsart_title, 240);
    style_text(fsart_title, TF(USER_20), TC(TEXT_PRIMARY));
    lv_obj_set_style_text_align(fsart_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(fsart_title, LV_LABEL_LONG_DOT);
    lv_obj_align(fsart_title, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_clear_flag(fsart_title, LV_OBJ_FLAG_CLICKABLE);

    fsart_artist = lv_label_create(fsart);
    lv_obj_set_width(fsart_artist, 176);
    style_text(fsart_artist, TF(USER_16), TC(TEXT_SECONDARY));
    lv_obj_set_style_text_align(fsart_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(fsart_artist, LV_LABEL_LONG_DOT);
    lv_obj_align(fsart_artist, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_clear_flag(fsart_artist, LV_OBJ_FLAG_CLICKABLE);

    apply_accent();
    ui_set_np_style(cfg_get_int("np_style", 0));
}

void ui_update(const track_state_t *st)
{
    char elapsed_buf[12];
    char remain_core[12];
    char remain_buf[14];
    long dur;
    long pos;
    long remain;
    int32_t progress;

    mode_refresh(cfg_get_int("work_mode", 0));

    if(st == NULL || !st->have_track) {
        last_path[0] = '\0';
        g_stat_path[0] = '\0';
        update_accent_seed(st);

        set_label_text_changed(title, "No Track");
        set_label_text_changed(artist, "");
        set_label_text_changed(album, "");
        if(album_pos) set_label_text_changed(album_pos, "");
        set_label_text_changed(t_elapsed, "0:00");
        set_label_text_changed(t_remain, "0:00");
        ui_pp_glyph(btn_pp, 0);
        if(theme_kit()->np_state) theme_kit()->np_state(0, 0);
        art_request_clear();
        g_np_curpath[0] = '\0'; g_favp_active = 0;
        fav_refresh(0, 0);
        if(btn_sleep) lv_obj_add_flag(btn_sleep, LV_OBJ_FLAG_HIDDEN);
        ui_np_close_overlays();
        g_track_dur = 0;
        set_progress_changed(0);
        return;
    }

    update_cover_for_path(st);
    update_accent_seed(st);

    if(st->state==2 && st->path[0] && strcmp(g_stat_path, st->path) != 0){
        copy_cstr(g_stat_path, sizeof g_stat_path, st->path);
        if(!mdb_is_book_path(st->path)) mdb_record_play(st->path);
    }

    set_label_text_changed(title, st->title[0] ? st->title : "Untitled");
    set_label_text_changed(artist, st->artist[0] ? st->artist : "");
    if(fsart_on){
        fsart_refresh_text();
        if(strcmp(fsart_path, st->path) != 0){ copy_cstr(fsart_path, sizeof fsart_path, st->path); fsart_reload_img(); }
    }
    {
        char ab[240];
        int pos_own_line = album_pos && !lv_obj_has_flag(album_pos, LV_OBJ_FLAG_HIDDEN);
        const char *pos_text = "";
        if(!book_meta_line(st, ab, sizeof ab)){
            const char *alb = st->album;
            if(pos_own_line){
                snprintf(ab, sizeof ab, "%s", alb);
                pos_text = st->playing_num;
            } else if(alb[0] && st->playing_num[0])
                snprintf(ab, sizeof ab, "%s  \xC2\xB7  %s", alb, st->playing_num);
            else if(alb[0])
                snprintf(ab, sizeof ab, "%s", alb);
            else if(st->playing_num[0]){
                const char *sl = strchr(st->playing_num, '/');
                if(sl && sl[1]) snprintf(ab, sizeof ab, "%.*s / %s", (int)(sl - st->playing_num), st->playing_num, sl + 1);
                else            snprintf(ab, sizeof ab, "%s", st->playing_num);
            } else ab[0] = '\0';
        }
        set_label_text_changed(album, ab);
        if(pos_own_line) set_label_text_changed(album_pos, pos_text);
    }
    snprintf(g_np_curpath, sizeof g_np_curpath, "%s", st->path);
    int fav_show = st->is_favorite;
    if(g_favp_active){
        if(strcmp(g_favp_path, st->path) != 0)          g_favp_active = 0;
        else if(st->is_favorite == g_favp_val)          g_favp_active = 0;
        else if(lv_tick_elaps(g_favp_set) > 4000)       g_favp_active = 0;
        else                                            fav_show = g_favp_val;
    }
    fav_refresh(fav_show, 1);

    {
        int is_book = mdb_is_book_path(st->path);
        np_transport_glyphs(is_book);
        if(is_book){
            lv_obj_add_flag(btn_fav, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(btn_mode, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(btn_sleep, LV_OBJ_FLAG_HIDDEN);
            int armed = ui_sleep_state(NULL);
            lv_obj_set_style_text_color(sleep_icon, armed ? accent : TC(TEXT_MUTED), LV_PART_MAIN);
        } else {
            lv_obj_remove_flag(btn_mode, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(btn_sleep, LV_OBJ_FLAG_HIDDEN);
            ui_np_close_overlays();
        }
    }

    dur = st->duration_ms;
    g_track_dur = dur;
    pos = st->position_ms;
    if(dur < 0) dur = 0;
    if(pos < 0) pos = 0;
    if(dur > 0 && pos > dur) pos = dur;

    if(!book_chapter_window(st, &g_seek_lo, &g_seek_hi)){ g_seek_lo = 0; g_seek_hi = dur; }
    long span = g_seek_hi - g_seek_lo;
    long rel  = pos - g_seek_lo;
    if(rel < 0) rel = 0;
    if(span > 0 && rel > span) rel = span;

    progress = (span > 0) ? (int32_t)(((long long)rel * 1000LL) / (long long)span) : 0;

    if(g_seek_hold_until) {
        long d = pos - g_seek_target_ms; if(d < 0) d = -d;
        if(lv_tick_get() < g_seek_hold_until && d > 3000) {
            ui_pp_glyph(btn_pp, ui_pp_icon_playing(st->state == 2));
            if(theme_kit()->np_state) theme_kit()->np_state(ui_pp_icon_playing(st->state == 2), 1);
            return;
        }
        g_seek_hold_until = 0; g_seek_target_ms = -1;
    }

    mmss(rel, elapsed_buf, sizeof(elapsed_buf));
    set_label_text_changed(t_elapsed, elapsed_buf);

    if(span > 0) {
        remain = span - rel;
        if(remain < 0) remain = 0;
        mmss(remain, remain_core, sizeof(remain_core));
        snprintf(remain_buf, sizeof(remain_buf), "-%s", remain_core);
    } else {
        snprintf(remain_buf, sizeof(remain_buf), "0:00");
    }
    set_label_text_changed(t_remain, remain_buf);
    set_progress_changed(progress);

    ui_pp_glyph(btn_pp, ui_pp_icon_playing(st->state == 2));
    if(theme_kit()->np_state) theme_kit()->np_state(ui_pp_icon_playing(st->state == 2), 1);
}

static lv_obj_t *g_vol_panel, *g_vol_arc, *g_vol_num;
static lv_timer_t *g_vol_timer;
static int g_vol_suppress;
static uint32_t g_vol_last_send;
static int g_vol_pending;
static void vol_hide_cb(lv_timer_t *t){
    if(g_vol_panel) lv_obj_add_flag(g_vol_panel, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
}
static void vol_arc_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(g_vol_suppress) return;
    int v = lv_arc_get_value(g_vol_arc);
    char b[12]; snprintf(b, sizeof b, "%d/%d", v, VOL_MAX); lv_label_set_text(g_vol_num, b);
    if(code == LV_EVENT_VALUE_CHANGED){
        if(lv_tick_elaps(g_vol_last_send) >= 120){
            ui_set_volume(v); g_vol_last_send = lv_tick_get(); g_vol_pending = 0;
        } else {
            g_vol_pending = 1;
        }
        if(g_vol_timer){ lv_timer_reset(g_vol_timer); lv_timer_resume(g_vol_timer); }
    } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST){
        if(g_vol_pending){ ui_set_volume(v); g_vol_pending = 0; g_vol_last_send = lv_tick_get(); }
    }
}
void ui_show_volume(int vol)
{
    if(!g_vol_panel){
        lv_obj_t *top = lv_layer_top();
        g_vol_panel = lv_obj_create(top);
        lv_obj_remove_style_all(g_vol_panel);
        lv_obj_set_size(g_vol_panel, 200, 200);
        lv_obj_center(g_vol_panel);
        lv_obj_set_style_radius(g_vol_panel, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(g_vol_panel, TC(CANVAS), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(g_vol_panel, 240, LV_PART_MAIN);
        lv_obj_clear_flag(g_vol_panel, LV_OBJ_FLAG_SCROLLABLE);
        kit_keep(g_vol_panel);

        g_vol_arc = lv_arc_create(g_vol_panel);
        lv_obj_set_size(g_vol_arc, 184, 184);
        lv_obj_center(g_vol_arc);
        lv_arc_set_rotation(g_vol_arc, 135);
        lv_arc_set_bg_angles(g_vol_arc, 0, 270);
        lv_arc_set_range(g_vol_arc, 0, VOL_MAX);
        lv_obj_set_style_arc_width(g_vol_arc, 10, LV_PART_MAIN);
        lv_obj_set_style_arc_width(g_vol_arc, 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(g_vol_arc, TC(CONTROL_TRACK_STRONG), LV_PART_MAIN);
        lv_obj_set_style_arc_color(g_vol_arc, TC(CONTROL_FILL), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(g_vol_arc, TC(ACCENT_PRIMARY), LV_PART_KNOB);
        ui_on(g_vol_arc, vol_arc_cb, LV_EVENT_VALUE_CHANGED, NULL, "ui.vol_arc.value", UI_CORE);
        lv_obj_add_event_cb(g_vol_arc, vol_arc_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(g_vol_arc, vol_arc_cb, LV_EVENT_PRESS_LOST, NULL);

        lv_obj_t *spk = lv_label_create(g_vol_panel);
        lv_label_set_text(spk, LV_SYMBOL_VOLUME_MAX);
        lv_obj_align(spk, LV_ALIGN_CENTER, 0, -22);
        lv_obj_set_style_text_color(spk, TC(TEXT_SECONDARY), LV_PART_MAIN);

        g_vol_num = lv_label_create(g_vol_panel);
        lv_obj_align(g_vol_num, LV_ALIGN_CENTER, 0, 10);
        lv_obj_set_style_text_font(g_vol_num, TF(UI_28), LV_PART_MAIN);
        lv_obj_set_style_text_color(g_vol_num, TC(TEXT_PRIMARY), LV_PART_MAIN);

        g_vol_timer = lv_timer_create(vol_hide_cb, 1800, NULL);
        lv_timer_pause(g_vol_timer);
    }
    if(vol < 0) vol = 0; if(vol > VOL_MAX) vol = VOL_MAX;
    if(!(lv_obj_get_state(g_vol_arc) & LV_STATE_PRESSED)){
        g_vol_suppress = 1;
        lv_arc_set_value(g_vol_arc, vol);
        g_vol_suppress = 0;
        char b[12]; snprintf(b, sizeof b, "%d/%d", vol, VOL_MAX); lv_label_set_text(g_vol_num, b);
    }
    lv_obj_remove_flag(g_vol_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_vol_panel);
    lv_timer_reset(g_vol_timer);
    lv_timer_resume(g_vol_timer);
}
