/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int ui_label_input_changed(lv_obj_t *obj, const char *txt);
int ui_label_set_changed(lv_obj_t *obj, const char *txt);

LV_FONT_DECLARE(font_weather16)
static lv_font_t s_swfont;

static lv_obj_t *g_bg;
static lv_obj_t *g_clock;
static void saver_clock_style_cb(lv_event_t *e);
static void saver_clock_fit(void);
static lv_obj_t *g_date;
static lv_obj_t *g_weather;
static lv_obj_t *g_track;
static lv_obj_t *g_artist;
static int g_playing;

static lv_obj_t *g_face;
static lv_obj_t *g_tick[12];
static lv_obj_t *g_hour;
static lv_obj_t *g_min;
static lv_obj_t *g_sec;
static lv_obj_t *g_hub;
static lv_point_precise_t g_hpts[2], g_mpts[2], g_spts[2];

static lv_obj_t *g_vinyl;
static int g_vspin = 0;
static int g_have_track = 0;

static int g_style = -1;
static int g_song_dirty = 1;
static int blue_saver(void){ return !strcmp(theme_kit()->id, "blueprint"); }
static void blue_song_layout(void){
    if(!blue_saver() || !g_clock || !theme_kit()->saver) return;
    saver_parts_t sp = { lv_obj_get_parent(g_clock), g_clock, g_date, g_weather, g_track, g_artist, g_face, g_style };
    theme_kit()->saver(&sp);
}

#define VIN_W 360
static uint8_t *g_vbuf = NULL;
static int g_vbound;
static int g_vbuf_valid = 0;
static lv_image_dsc_t g_vdsc;

static uint8_t *g_vout = NULL;
static lv_image_dsc_t g_vodsc;
static int32_t g_vangle = 0;
static inline uint32_t vin_px(const uint32_t *src, int x, int y){
    return ((unsigned)x < VIN_W && (unsigned)y < VIN_W) ? src[y * VIN_W + x] : 0;
}
static inline uint32_t vin_lerp(uint32_t a, uint32_t b, uint32_t w){
    uint32_t rb = (((a & 0x00FF00FFu) * (256 - w) + (b & 0x00FF00FFu) * w) >> 8) & 0x00FF00FFu;
    uint32_t ag = ((((a >> 8) & 0x00FF00FFu) * (256 - w) + ((b >> 8) & 0x00FF00FFu) * w)) & 0xFF00FF00u;
    return rb | ag;
}
__attribute__((optimize("O2")))
static void vinyl_rotate(int32_t tenths)
{
    if (!g_vout || !g_vbuf) return;
    const uint32_t *src = (const uint32_t *)g_vbuf;
    uint32_t *out = (uint32_t *)g_vout;
    double a = (double)tenths * 3.14159265358979 / 1800.0;
    int32_t c = (int32_t)lround(cos(a) * 65536.0), sn = (int32_t)lround(sin(a) * 65536.0);
    const int32_t h = VIN_W / 2;
    for (int y = 0; y < VIN_W; y++) {
        int32_t dy = y - h;
        int32_t hw = (int32_t)sqrt((double)(h * h - dy * dy));
        int x0 = h - hw, x1 = h + hw;
        if (x0 < 0) x0 = 0;
        if (x1 > VIN_W) x1 = VIN_W;
        int32_t u = (x0 - h) * c + dy * sn + (h << 16), v = -(x0 - h) * sn + dy * c + (h << 16);
        uint32_t *o = out + y * VIN_W;
        for (int x = x0; x < x1; x++, u += c, v -= sn) {
            int xi = u >> 16, yi = v >> 16;
            uint32_t fx = (u >> 8) & 0xFF, fy = (v >> 8) & 0xFF;
            uint32_t p00, p10, p01, p11;
            if ((unsigned)xi < VIN_W - 1 && (unsigned)yi < VIN_W - 1) {
                const uint32_t *q = src + yi * VIN_W + xi;
                p00 = q[0]; p10 = q[1]; p01 = q[VIN_W]; p11 = q[VIN_W + 1];
            } else {
                p00 = vin_px(src, xi, yi); p10 = vin_px(src, xi + 1, yi);
                p01 = vin_px(src, xi, yi + 1); p11 = vin_px(src, xi + 1, yi + 1);
            }
            o[x] = vin_lerp(vin_lerp(p00, p10, fx), vin_lerp(p01, p11, fx), fy) | 0xFF000000u;
        }
    }
    lv_image_cache_drop(&g_vodsc);
    if (g_vinyl) lv_obj_invalidate(g_vinyl);
}

static void song_visibility(void);
static void vinyl_update_vis(void)
{
    if (!g_vinyl) return;
    if (g_style == 4 && g_vbound && g_vbuf_valid && g_have_track) lv_obj_remove_flag(g_vinyl, LV_OBJ_FLAG_HIDDEN);
    else                                                        lv_obj_add_flag(g_vinyl, LV_OBJ_FLAG_HIDDEN);
    song_visibility();
}

int saver_wants_bright(void) { return g_style == 4 && g_have_track && g_vbuf_valid; }

#include <pthread.h>
#include <fcntl.h>
#include "art.h"
void ui_decode_lock(void); void ui_decode_unlock(void);
static char g_vpath[1024];
static unsigned g_vgen;
static pthread_mutex_t g_vmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_vcv = PTHREAD_COND_INITIALIZER;
static int g_vwork_started, g_vwant;
static char g_vwant_path[1024];
static unsigned g_vwant_gen;
static int64_t g_vwant_deadline;
static int g_vfails;
#define VIN_RETRIES 3
static int64_t vin_now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static uint8_t *g_vres; static unsigned g_vres_gen; static int g_vres_ready;
static unsigned g_vasked_gen = ~0u;
static lv_timer_t *g_vpoll;
static int g_vpending;

static void *vinyl_worker(void *arg)
{
    (void)arg;
    const size_t need = (size_t)VIN_W * VIN_W * 4;
    for (;;) {
        pthread_mutex_lock(&g_vmu);
        while (!g_vwant) pthread_cond_wait(&g_vcv, &g_vmu);
        char path[1024]; snprintf(path, sizeof path, "%s", g_vwant_path);
        unsigned gen = g_vwant_gen; int64_t deadline = g_vwant_deadline; g_vwant = 0;
        pthread_mutex_unlock(&g_vmu);

        char out[64]; snprintf(out, sizeof out, "/tmp/.diskos-vinyl-%u.bgra", gen);
        uint8_t *buf = NULL;
        ui_decode_lock();
        pthread_mutex_lock(&g_vmu);
        int superseded = g_vgen != gen;
        pthread_mutex_unlock(&g_vmu);
        int rc = (superseded || vin_now_ms() >= deadline) ? -1 : art_make_saver(path, out, deadline);
        ui_decode_unlock();
        if (rc == 0) {
            int fd = open(out, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            buf = malloc(need);
            size_t got = 0;
            if (fd >= 0 && buf) {
                ssize_t n;
                while (got < need && (n = read(fd, buf + got, need - got)) > 0) got += (size_t)n;
                char extra; if (got == need && read(fd, &extra, 1) != 0) got = 0;
            }
            if (fd >= 0) close(fd);
            if (got != need) { free(buf); buf = NULL; }
        }
        unlink(out);
        pthread_mutex_lock(&g_vmu);
        if (gen != g_vgen) { free(buf); pthread_mutex_unlock(&g_vmu); continue; }
        free(g_vres);
        g_vres = buf; g_vres_gen = gen; g_vres_ready = 1;
        pthread_mutex_unlock(&g_vmu);
    }
    return NULL;
}

static void vinyl_bind(void)
{
    if (!g_vinyl || !g_vbuf_valid || g_vbound) return;
    if (!g_vout) g_vout = calloc((size_t)VIN_W * VIN_W, 4);
    if (g_vout) {
        g_vodsc = g_vdsc;
        g_vodsc.data = g_vout;
        g_vodsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
        vinyl_rotate(g_vangle);
        lv_image_set_src(g_vinyl, &g_vodsc);
        lv_image_set_rotation(g_vinyl, 0);
    } else {
        lv_image_set_src(g_vinyl, &g_vdsc);
        lv_image_set_pivot(g_vinyl, VIN_W / 2, VIN_W / 2);
    }
    lv_image_set_scale(g_vinyl, 256);
    lv_obj_center(g_vinyl);
    g_vbound = 1;
    vinyl_update_vis();
}

const lv_image_dsc_t *sharp_cover_get(void)
{
    return g_vbuf_valid ? &g_vdsc : NULL;
}

void sharp_cover_invalidate(void)
{
    pthread_mutex_lock(&g_vmu);
    g_vgen++; g_vwant = 0;
    free(g_vres); g_vres = NULL; g_vres_ready = 0;
    pthread_mutex_unlock(&g_vmu);
    g_vpending = 0;
    if (g_vpoll) lv_timer_pause(g_vpoll);
    g_vbuf_valid = g_vbound = 0; g_vfails = 0; g_vasked_gen = ~0u;
    if (g_vinyl) lv_image_set_src(g_vinyl, NULL);
    ui_np_cover_changed();
    lv_image_cache_drop(&g_vdsc); lv_image_cache_drop(&g_vodsc);
    free(g_vbuf); g_vbuf = NULL; free(g_vout); g_vout = NULL;
    g_vdsc.data = g_vodsc.data = NULL;
    vinyl_update_vis();
}

void sharp_cover_set_source(const char *path)
{
    const char *p = path ? path : "";
    if (!strcmp(p, g_vpath)) return;
    sharp_cover_invalidate();
    snprintf(g_vpath, sizeof g_vpath, "%s", p);
}

static int sharp_retained(void)
{
    return !strcmp(theme_kit()->id, "bauhaus") || cfg_get_int("saver_style", 0) == 4;
}

static void vinyl_poll(lv_timer_t *t)
{
    pthread_mutex_lock(&g_vmu);
    if (!g_vres_ready) { pthread_mutex_unlock(&g_vmu); return; }
    uint8_t *buf = g_vres; unsigned gen = g_vres_gen;
    g_vres = NULL; g_vres_ready = 0;
    pthread_mutex_unlock(&g_vmu);
    if (gen != g_vgen) { free(buf); if (!g_vpending) lv_timer_pause(t); return; }
    g_vpending = 0;
    lv_timer_pause(t);
    if (!sharp_retained()) { free(buf); return; }
    if (!buf) {
        if (++g_vfails < VIN_RETRIES) g_vasked_gen = ~0u;
        return;
    }
    g_vbuf = buf; g_vbuf_valid = 1;
    memset(&g_vdsc, 0, sizeof g_vdsc);
    g_vdsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_vdsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_vdsc.header.w = g_vdsc.header.h = VIN_W;
    g_vdsc.header.stride = VIN_W * 4;
    g_vdsc.data = buf; g_vdsc.data_size = (uint32_t)VIN_W * VIN_W * 4;
    ui_np_cover_changed();
    if (g_style == 4 && screen_current() == SCR_SAVER) vinyl_bind();
}

void sharp_cover_request(void)
{
    if (!g_vpath[0] || !sharp_retained() || g_vbuf_valid || g_vasked_gen == g_vgen) return;
    if (!g_vpoll) g_vpoll = lv_timer_create(vinyl_poll, 150, NULL);
    if (!g_vpoll) { if (++g_vfails >= VIN_RETRIES) g_vasked_gen = g_vgen; return; }
    lv_timer_pause(g_vpoll);
    pthread_mutex_lock(&g_vmu);
    if (!g_vwork_started) {
        pthread_t th;
        if (pthread_create(&th, NULL, vinyl_worker, NULL) == 0) { pthread_detach(th); g_vwork_started = 1; }
    }
    if (g_vwork_started) {
        snprintf(g_vwant_path, sizeof g_vwant_path, "%s", g_vpath);
        g_vwant_gen = g_vgen; g_vwant = 1;
        g_vwant_deadline = vin_now_ms() + 15000;
        pthread_cond_signal(&g_vcv);
        g_vasked_gen = g_vgen;
        g_vpending = 1;
        lv_timer_reset(g_vpoll);
        lv_timer_resume(g_vpoll);
    } else if (++g_vfails >= VIN_RETRIES) g_vasked_gen = g_vgen;
    pthread_mutex_unlock(&g_vmu);
}

static void vinyl_load_sharp_cover(void)
{
    if (g_vinyl && g_have_track) { sharp_cover_request(); vinyl_bind(); }
    vinyl_update_vis();
}

#define CX 180
#define CY 180
static int default_saver(void){ return !strcmp(theme_kit()->id, "default"); }
static int something_saver(void){ return !strcmp(theme_kit()->id, "something"); }
static int hifi_saver(void){ return !strcmp(theme_kit()->id, "hi-fi"); }
static int term_saver(void){ return !strcmp(theme_kit()->id, "terminal"); }
static int bauh_saver(void){ return !strcmp(theme_kit()->id, "bauhaus"); }
static int stone_saver(void){ return !strcmp(theme_kit()->id, "stone"); }
static int zine_saver(void){ return !strcmp(theme_kit()->id, "zine"); }
static int analog_y(void){ return default_saver() || something_saver() || hifi_saver() || term_saver() || bauh_saver() || blue_saver() || stone_saver() || zine_saver() ? 138 : CY; }

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *font, lv_color_t c, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_width(l, 320);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *mk_hand(lv_obj_t *p, int w, lv_color_t c, lv_point_precise_t *pts)
{
    lv_obj_t *ln = lv_line_create(p);
    lv_obj_set_style_line_width(ln, w, 0);
    lv_obj_set_style_line_color(ln, c, 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    pts[0].x = CX; pts[0].y = analog_y(); pts[1].x = CX; pts[1].y = analog_y() - 1;
    lv_line_set_points(ln, pts, 2);
    return ln;
}

static void set_hand(lv_obj_t *ln, lv_point_precise_t *pts, double deg, int len)
{
    double r = deg * 3.14159265 / 180.0;
    int y = analog_y();
    pts[0].x = CX; pts[0].y = y;
    pts[1].x = (lv_value_precise_t)(CX + len * sin(r));
    pts[1].y = (lv_value_precise_t)(y - len * cos(r));
    lv_line_set_points(ln, pts, 2);
    lv_obj_invalidate(ln);
}

static int min_hand_len(void)
{
    if(hifi_saver()) return 68;
    return default_saver() || something_saver() || term_saver() || bauh_saver() || blue_saver() || stone_saver()
        || zine_saver() ? 80 : 104;
}
static void saver_anim_cb(lv_timer_t *t)
{
    (void)t;
    if (g_style != 1 || screen_current() != SCR_SAVER) return;
    time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
    int def = default_saver();
    set_hand(g_hour, g_hpts, (lt.tm_hour % 12) * 30.0 + lt.tm_min * 0.5, hifi_saver() ? 60 : 70);
    set_hand(g_min,  g_mpts, lt.tm_min * 6.0, min_hand_len());
    set_hand(g_sec,  g_spts, lt.tm_sec * 6.0, hifi_saver() ? 68 : def || something_saver() || term_saver() || bauh_saver() || blue_saver() || stone_saver() || zine_saver() ? 83 : 118);
}

static void vspin_cb(void *var, int32_t v){
    static uint32_t last = 0;
    uint32_t now = lv_tick_get();
    if (now - last < 33) return;
    last = now;
    g_vangle = v % 3600;
    if (g_vout && lv_image_get_src((lv_obj_t *)var) == (const void *)&g_vodsc) vinyl_rotate(g_vangle);
    else lv_image_set_rotation((lv_obj_t *)var, g_vangle);
}
void saver_vinyl_spin(int want)
{
    want = want && g_style == 4 && g_vinyl &&
           !lv_obj_has_flag(g_vinyl, LV_OBJ_FLAG_HIDDEN);
    if (want == g_vspin) return;
    g_vspin = want;
    if (want) {
        int32_t cur = g_vout ? g_vangle : lv_image_get_rotation(g_vinyl);
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, g_vinyl);
        lv_anim_set_exec_cb(&a, vspin_cb);
        lv_anim_set_values(&a, cur, cur + 3600);
        lv_anim_set_time(&a, 27000);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    } else {
        lv_anim_delete(g_vinyl, vspin_cb);
    }
}

static void song_visibility(void){
    int hifi = hifi_saver();
    int show = default_saver() || something_saver() || hifi || term_saver() || bauh_saver() || blue_saver() || stone_saver() || zine_saver()
               ? g_playing && g_have_track : cfg_get_int("saver_style", 0) == 0;
    if (g_style == 4 && g_vinyl && !lv_obj_has_flag(g_vinyl, LV_OBJ_FLAG_HIDDEN)) show = 0;
    if(g_track){ if(show) lv_obj_remove_flag(g_track, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_track, LV_OBJ_FLAG_HIDDEN); }
    if(g_artist){ if(show) lv_obj_remove_flag(g_artist, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_artist, LV_OBJ_FLAG_HIDDEN); }
    if(zine_saver()){
        extern void kit_zine_song_visibility(int show);
        kit_zine_song_visibility(show);
    }
    if((hifi || bauh_saver()) && g_track && lv_obj_get_parent(g_track) != lv_obj_get_parent(g_clock)){
        lv_obj_t *well = lv_obj_get_parent(g_track);
        if(show) lv_obj_remove_flag(well, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(well, LV_OBJ_FLAG_HIDDEN);
    }
}

void saver_set_playing(int playing){
    if(g_playing == !!playing) return;
    g_playing = !!playing;
    song_visibility();
    if(blue_saver() && screen_current() == SCR_SAVER) blue_song_layout();
    if(zine_saver() && screen_current() == SCR_SAVER && g_clock){
        saver_parts_t sp = { lv_obj_get_parent(g_clock), g_clock, g_date, g_weather, g_track, g_artist, g_face, g_style };
        theme_kit()->saver(&sp);
    }
}

static void default_song_layout(int style)
{
    int analog = style == 1;
    int song_y = analog ? 278 : 254;
    if((style == 0 || style == 3) && g_weather){
        lv_obj_update_layout(g_weather);
        int below = lv_obj_get_y(g_weather) + lv_obj_get_height(g_weather) + 12;
        if(below > song_y) song_y = below;
    }
    if(analog && g_date && !lv_obj_has_flag(g_date, LV_OBJ_FLAG_HIDDEN)){
        lv_obj_update_layout(g_date);
        int below = lv_obj_get_y(g_date) + lv_obj_get_height(g_date) + 8;
        if(below > song_y) song_y = below;
    }
    if(g_track) lv_obj_align(g_track, LV_ALIGN_TOP_MID, 0, song_y);
    if(g_artist) lv_obj_align(g_artist, LV_ALIGN_TOP_MID, 0, song_y + 24);
    int width = analog ? 200 : 230;
    if(analog && g_artist){
        lv_obj_update_layout(g_artist);
        int bottom = lv_obj_get_y(g_artist) + lv_obj_get_height(g_artist);
        int dy = bottom - 180;
        if((width / 2) * (width / 2) + dy * dy > 176 * 176){
            int fit = ui_round_width(bottom, 10);
            if(fit < width) width = fit;
        }
    }
    if(g_track) lv_obj_set_width(g_track, width);
    if(g_artist) lv_obj_set_width(g_artist, width);
}

static void relayout(int style)
{
    int analog = (style == 1);
    int cover  = (style == 0);
    int minim  = (style == 2);
    int vinyl  = (style == 4);
    int theme_dy = (theme_def() && style == 3) ? 24 : 0;

    if (g_bg) { if (cover && g_have_track) lv_obj_remove_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
                else                        lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN); }

    (void)vinyl;
    if (!sharp_retained()) sharp_cover_invalidate();
    else if (style != 4 && g_vout) {
        if (g_vinyl) lv_image_set_src(g_vinyl, NULL);
        g_vbound = 0; lv_image_cache_drop(&g_vodsc);
        free(g_vout); g_vout = NULL; g_vodsc.data = NULL;
    }
    vinyl_update_vis();

    lv_obj_t *aparts[] = { g_face, g_hour, g_min, g_sec, g_hub };
    for (unsigned i = 0; i < sizeof(aparts)/sizeof(aparts[0]); i++)
        if (aparts[i]) { if (analog) lv_obj_remove_flag(aparts[i], LV_OBJ_FLAG_HIDDEN);
                         else        lv_obj_add_flag(aparts[i], LV_OBJ_FLAG_HIDDEN); }
    for (int i = 0; i < 12; i++)
        if (g_tick[i]) { if (analog) lv_obj_remove_flag(g_tick[i], LV_OBJ_FLAG_HIDDEN);
                         else         lv_obj_add_flag(g_tick[i], LV_OBJ_FLAG_HIDDEN); }

    if (g_clock) {
        if (analog || vinyl) lv_obj_add_flag(g_clock, LV_OBJ_FLAG_HIDDEN);
        else { lv_obj_remove_flag(g_clock, LV_OBJ_FLAG_HIDDEN);
               lv_obj_align(g_clock, LV_ALIGN_TOP_MID, 0, minim ? 150 : 110 + theme_dy); }
        saver_clock_fit();
    }
    if (g_date) {
        if (vinyl) lv_obj_add_flag(g_date, LV_OBJ_FLAG_HIDDEN);
        else { lv_obj_remove_flag(g_date, LV_OBJ_FLAG_HIDDEN);
               int lh = lv_font_get_line_height(TF(SAVER_CLOCK));
               int below = minim ? 56 : 60; if (lh + 12 > below) below = lh + 12;
               lv_obj_align(g_date, LV_ALIGN_TOP_MID, 0, analog ? 250 : (minim ? 150 : 110 + theme_dy) + below); }
    }
    if (g_weather) { if (cover || style == 3){ lv_obj_remove_flag(g_weather, LV_OBJ_FLAG_HIDDEN);
                         if (g_date){ lv_obj_update_layout(g_date); lv_obj_align(g_weather, LV_ALIGN_TOP_MID, 0, lv_obj_get_y(g_date) + 30); } }
                     else lv_obj_add_flag(g_weather, LV_OBJ_FLAG_HIDDEN); }
    if (theme_kit()->saver) {
        saver_parts_t sp = { lv_obj_get_parent(g_clock), g_clock, g_date, g_weather, g_track, g_artist, g_face, style };
        theme_kit()->saver(&sp);
    }
    if(default_saver()) default_song_layout(style);
    song_visibility();
    g_song_dirty = 1;
}

void saver_create(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    g_bg = lv_image_create(root);
    lv_obj_set_size(g_bg, 360, 360);
    lv_obj_align(g_bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_image_recolor(g_bg, TC(IMAGE_TINT), 0);
    lv_obj_set_style_image_recolor_opa(g_bg, 185, 0);
    lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 12; i++) {
        lv_obj_t *d = lv_obj_create(root);
        lv_obj_remove_style_all(d);
        int big = (i % 3 == 0);
        lv_obj_set_size(d, big ? 8 : 5, big ? 8 : 5);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, (big ? TC(TEXT_PRIMARY) : TC(TEXT_MUTED)), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        double r = i * 30 * 3.14159265 / 180.0;
        int radius = stone_saver() || zine_saver() ? 99 : default_saver() || something_saver() || hifi_saver() || term_saver() || bauh_saver() || blue_saver() ? 105 : 150;
        int x = (int)(CX + radius * sin(r)), y = (int)(analog_y() - radius * cos(r));
        lv_obj_set_pos(d, x - (big?4:2), y - (big?4:2));
        g_tick[i] = d;
    }
    g_hour = mk_hand(root, 6, TC(TEXT_PRIMARY), g_hpts);
    g_min  = mk_hand(root, 4, TC(TEXT_SECONDARY), g_mpts);
    g_sec  = mk_hand(root, 2, ui_current_accent(), g_spts);
    g_hub  = lv_obj_create(root);
    lv_obj_remove_style_all(g_hub);
    lv_obj_set_size(g_hub, 14, 14);
    lv_obj_set_style_radius(g_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_hub, ui_current_accent(), 0);
    lv_obj_set_style_bg_opa(g_hub, LV_OPA_COVER, 0);
    lv_obj_set_pos(g_hub, CX - 7, analog_y() - 7);

    g_vinyl = lv_image_create(root);
    lv_obj_center(g_vinyl);
    lv_image_set_antialias(g_vinyl, false);
    lv_obj_add_flag(g_vinyl, LV_OBJ_FLAG_HIDDEN);

    s_swfont = *TF(UI_16);
    s_swfont.fallback = &font_weather16;

    g_clock   = mk(root, TF(SAVER_CLOCK), TC(TEXT_PRIMARY), 110);
    g_date    = mk(root, TF(UI_16), TC(TEXT_SECONDARY), 170);
    g_weather = mk(root, &s_swfont,              TC(TEXT_SECONDARY), 200);
    int def = default_saver();
    g_track   = mk(root, def ? TF(UI_18) : TF(UI_16), TC(TEXT_PRIMARY), 254);
    g_artist  = mk(root, def ? TF(UI_16) : TF(UI_14), TC(TEXT_MUTED), 278);
    lv_label_set_text(g_clock, "--:--");
    lv_obj_add_event_cb(g_clock, saver_clock_style_cb, LV_EVENT_STYLE_CHANGED, NULL);

    relayout(cfg_get_int("saver_style", 0));
    g_style = cfg_get_int("saver_style", 0);
    lv_timer_create(saver_anim_cb, 1000, NULL);
}

void saver_set_accent(lv_color_t c)
{
    if (g_sec)    lv_obj_set_style_line_color(g_sec, c, 0);
    if (g_hub)    lv_obj_set_style_bg_color(g_hub, c, 0);
}

void saver_show_sync(void)
{
    int s = cfg_get_int("saver_style", 0);
    g_style = s; relayout(s);
    saver_set_accent(ui_current_accent());
    if (g_style == 4) { if (g_have_track) vinyl_load_sharp_cover(); else vinyl_update_vis(); }
}

static char g_clock_text[32] = "--:--";
static lv_obj_t *g_mer;
static void saver_clock_fit(void)
{
    if (!g_clock) return;
    char digits[32]; snprintf(digits, sizeof digits, "%s", g_clock_text);
    const char *mer = "";
    char *sp = strrchr(digits, ' ');
    if (theme_kit()->saver_mer_apart && sp && (!strcmp(sp + 1, "AM") || !strcmp(sp + 1, "PM"))) { *sp = 0; mer = g_clock_text + (sp - digits) + 1; }
    lv_label_set_text(g_clock, digits);
    lv_point_t sz;
    lv_text_get_size(&sz, digits, lv_obj_get_style_text_font(g_clock, 0),
                     lv_obj_get_style_text_letter_space(g_clock, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int32_t room = 320 - 4;
    int32_t sc = (sz.x > room && sz.x > 0) ? (int32_t)(256L * room / sz.x) : 256;
    lv_obj_set_width(g_clock, sc < 256 ? sz.x + 4 : 320);
    lv_obj_update_layout(g_clock);
    lv_obj_set_style_transform_pivot_x(g_clock, lv_obj_get_width(g_clock) / 2, 0);
    lv_obj_set_style_transform_pivot_y(g_clock, lv_obj_get_height(g_clock) / 2, 0);
    lv_obj_set_style_transform_scale(g_clock, sc, 0);
    if (mer[0] && !g_mer) {
        g_mer = lv_label_create(lv_obj_get_parent(g_clock));
        kit_keep(g_mer);
        lv_obj_set_style_text_font(g_mer, theme_kit()->saver_mer_apart == 2 ? TF(DATE) : TF(UI_14), 0);
    }
    if (g_mer) {
        int show = mer[0] && !lv_obj_has_flag(g_clock, LV_OBJ_FLAG_HIDDEN);
        if (!show) { lv_obj_add_flag(g_mer, LV_OBJ_FLAG_HIDDEN); lv_obj_set_style_translate_x(g_clock, 0, 0); }
        else {
            lv_label_set_text(g_mer, mer);
            lv_obj_set_style_text_color(g_mer, lv_obj_get_style_text_color(g_clock, 0), 0);
            lv_obj_remove_flag(g_mer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_update_layout(g_mer);
            int32_t mw = lv_obj_get_width(g_mer) + 6;
            lv_obj_set_width(g_clock, sz.x + 4);
            lv_obj_set_style_translate_x(g_clock, -mw / 2, 0);
            lv_obj_update_layout(g_clock);
            lv_obj_align_to(g_mer, g_clock, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -lv_obj_get_height(g_clock) / 5);
        }
    }
}
static void saver_clock_style_cb(lv_event_t *e)
{
    (void)e;
    static int busy;
    if (busy) return;
    busy = 1; saver_clock_fit(); busy = 0;
}

#define SONG_GEOM_N 17
static void song_geom(intptr_t g[6][SONG_GEOM_N])
{
    lv_obj_t *o[6] = { g_clock, g_date, g_weather, g_track, g_artist, g_track ? lv_obj_get_parent(g_track) : NULL };
    memset(g, 0, sizeof(intptr_t) * 6 * SONG_GEOM_N);
    for (int i = 0; i < 6; i++) {
        if (!o[i]) continue;
        intptr_t *v = g[i];
        v[0] = (intptr_t)lv_obj_get_parent(o[i]); v[1] = lv_obj_has_flag(o[i], LV_OBJ_FLAG_HIDDEN);
        v[14] = (intptr_t)lv_obj_get_style_text_font(o[i], 0);
        v[15] = lv_obj_get_style_text_letter_space(o[i], 0); v[16] = lv_obj_get_style_text_line_space(o[i], 0);
        if (o[i] == g_clock) continue;
        v[2] = lv_obj_get_style_x(o[i], 0); v[3] = lv_obj_get_style_y(o[i], 0);
        v[4] = lv_obj_get_style_width(o[i], 0); v[5] = lv_obj_get_style_height(o[i], 0);
        v[6] = lv_obj_get_style_align(o[i], 0);
        v[7] = lv_obj_get_style_pad_left(o[i], 0); v[8] = lv_obj_get_style_pad_right(o[i], 0);
        v[9] = lv_obj_get_style_pad_top(o[i], 0); v[10] = lv_obj_get_style_pad_bottom(o[i], 0);
        v[11] = lv_obj_get_style_border_width(o[i], 0);
        v[12] = lv_obj_get_style_translate_x(o[i], 0); v[13] = lv_obj_get_style_translate_y(o[i], 0);
    }
}

void saver_set_clock(const char *t, const char *date)
{
    int s = cfg_get_int("saver_style", 0);
    if (s != g_style) { g_style = s; relayout(s); }

    static int settled;
    static intptr_t after[6][SONG_GEOM_N];
    const char *tt = t ? t : "--:--", *dd = date ? date : "";
    int time_new = strcmp(g_clock_text, tt) != 0;
    int date_new = ui_label_input_changed(g_date, dd);
    intptr_t before[6][SONG_GEOM_N];
    song_geom(before);
    if (time_new || date_new || g_song_dirty || !settled || memcmp(before, after, sizeof before)) {
        if (g_clock && time_new) {
            snprintf(g_clock_text, sizeof g_clock_text, "%s", tt);
            saver_clock_fit();
        }
        if (g_date && date_new) theme_case_text(g_date, dd);
        if(blue_saver()) blue_song_layout();
        if (time_new && theme_kit()->saver_clock) theme_kit()->saver_clock(tt);
        if ((bauh_saver() || hifi_saver() || something_saver() || term_saver() || stone_saver() || zine_saver()) && theme_kit()->saver) {
            saver_parts_t sp = { lv_obj_get_parent(g_clock), g_clock, g_date, g_weather, g_track, g_artist, g_face, s };
            theme_kit()->saver(&sp);
        }
        if(default_saver()) default_song_layout(s);
        g_song_dirty = 0;
        song_geom(after);
        settled = !memcmp(before, after, sizeof before);
    }

    if (g_style == 1) {
        time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
        set_hand(g_hour, g_hpts, (lt.tm_hour % 12) * 30.0 + lt.tm_min * 0.5, hifi_saver() ? 60 : 70);
        set_hand(g_min,  g_mpts, lt.tm_min * 6.0, min_hand_len());
    }
}

void saver_set_weather(const char *text)
{
    if (ui_label_set_changed(g_weather, text ? text : "")) g_song_dirty = 1;
}

void saver_set_track(const char *title, const char *artist, const void *backdrop_src, const char *path)
{
    g_have_track = (title != NULL);
    const char *p = (g_have_track && path) ? path : "";
    sharp_cover_set_source(p);
    if (ui_label_set_changed(g_track, title ? title : "")) g_song_dirty = 1;
    if (ui_label_set_changed(g_artist, artist ? artist : "")) g_song_dirty = 1;
    song_visibility();
    if(blue_saver() && screen_current() == SCR_SAVER) blue_song_layout();
    if(zine_saver() && screen_current() == SCR_SAVER && g_clock){
        saver_parts_t sp = { lv_obj_get_parent(g_clock), g_clock, g_date, g_weather, g_track, g_artist, g_face, g_style };
        theme_kit()->saver(&sp);
    }

    if (g_vinyl) {
        if (g_style == 4 && screen_current() == SCR_SAVER) vinyl_load_sharp_cover();
        else                                               vinyl_update_vis();
    }

    if (g_bg) {
        if (g_have_track && backdrop_src) {
            lv_image_set_src(g_bg, backdrop_src);
            if (g_style == 0) lv_obj_remove_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_background(g_bg);
        } else {
            lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
