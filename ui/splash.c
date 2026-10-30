/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(diskos_brand_disk_32)
LV_FONT_DECLARE(diskos_brand_os_32)

#define SP_MS    2400
#define SP_CX    180
#define SP_R     178
#define SP_SLICE 5
#define SP_GAPS  7
#define SP_GLOW  4

#define K_HUB   460
#define K_B0    500
#define K_B1    560
#define K_LIT0  640
#define K_DATA  985

static struct {
    lv_obj_t *root, *face, *rim, *rim_hl, *lobe[2], *slice[2][SP_SLICE], *streak, *shade[2], *gap[2][(SP_GAPS + 1) / 2],
             *glow[SP_GLOW], *hub, *hub_edge, *word;
    int th, warm, opened, frames;
    uint32_t t0;
} g_sp;

static int clampk(int v){ return v < 0 ? 0 : v > 1024 ? 1024 : v; }
static int win_k(int t, int a, int b){ return clampk((t - a) * 1024 / (b - a)); }
static int ease_out(int k){ int r = 1024 - k; return 1024 - (int)((int64_t)r * r * r >> 20); }
static int ease_io(int k){
    if(k < 512) return (int)(4 * (int64_t)k * k * k >> 20);
    int r = 1024 - k; return 1024 - (int)(4 * (int64_t)r * r * r >> 20);
}
static int scl(int v, int k){ return (int)((int64_t)v * k >> 10); }
static lv_opa_t opa_k(int max, int k){ return (lv_opa_t)(max * clampk(k) >> 10); }

static void sty(lv_obj_t *o, lv_style_prop_t p, int32_t v, lv_style_selector_t sel){
    lv_style_value_t cur;
    if(lv_obj_get_local_style_prop(o, p, &cur, sel) == LV_STYLE_RES_FOUND && cur.num == v) return;
    lv_style_value_t nv = { .num = v };
    lv_obj_set_local_style_prop(o, p, nv, sel);
}

static lv_obj_t *shape(lv_obj_t *parent){
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    kit_keep(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    return o;
}
static lv_obj_t *ring(lv_obj_t *parent, lv_color_t c, lv_opa_t opa){
    lv_obj_t *o = shape(parent);
    lv_obj_set_style_border_color(o, c, 0);
    lv_obj_set_style_border_opa(o, opa, 0);
    return o;
}
static lv_obj_t *arc(lv_obj_t *parent, lv_color_t c, lv_opa_t opa){
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    kit_keep(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_color(a, c, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, opa, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(a, 0, 1);
    return a;
}
static void place_band(lv_obj_t *o, int r0, int r1, int is_arc){
    if(r1 < 1) r1 = 1;
    if(r0 > r1 - 1) r0 = r1 - 1;
    lv_obj_set_pos(o, SP_CX - r1, SP_CX - r1);
    lv_obj_set_size(o, 2 * r1, 2 * r1);
    sty(o, is_arc ? LV_STYLE_ARC_WIDTH : LV_STYLE_BORDER_WIDTH, r1 - r0, 0);
}
static void place_disc(lv_obj_t *o, int r){
    if(r < 1) r = 1;
    lv_obj_set_pos(o, SP_CX - r, SP_CX - r);
    lv_obj_set_size(o, 2 * r, 2 * r);
}
static int ang(int a){ a %= 360; return a < 0 ? a + 360 : a; }
static void span(lv_obj_t *a, int mid, int half){ lv_arc_set_bg_angles(a, ang(mid - half), ang(mid + half)); }
static int gap_r(int i){ return SP_R * (K_B1 + (K_DATA - K_B1) * (i + 1) / 8) / 1000; }

static void sector_box(int r0, int r1, int a0, int a1, lv_area_t *box){
    int x1 = 9999, y1 = 9999, x2 = -9999, y2 = -9999;
    for(int a = a0;; a += 5){
        if(a > a1) a = a1;
        int s = lv_trigo_sin((int16_t)ang(a)), c = lv_trigo_cos((int16_t)ang(a));
        for(int k = 0; k < 2; k++){
            int r = k ? r1 : r0, x = SP_CX + (r * c >> 15), y = SP_CX + (r * s >> 15);
            if(x < x1) x1 = x;
            if(x > x2) x2 = x;
            if(y < y1) y1 = y;
            if(y > y2) y2 = y;
        }
        if(a == a1) break;
    }
    box->x1 = x1 - 4; box->y1 = y1 - 4; box->x2 = x2 + 4; box->y2 = y2 + 4;
}
static void inv_sector(int r0, int r1, int a0, int a1){
    lv_area_t box;
    sector_box(r0, r1, a0, a1, &box);
    lv_obj_invalidate_area(g_sp.root, &box);
}

static void splash_frame(int t){
    int ok = ease_io(win_k(t, 1800, 2350));
    int S = 1024 + scl((SP_CX + 10) * 1024 / (SP_R * K_HUB / 1000) - 1024, ok);
    int R = scl(SP_R, S);
#define RK(k) (R * (k) / 1000)

    int th = 175 + scl(110, ease_io(win_k(t, 150, 1650)));
    int sp = scl(ease_out(win_k(t, 150, 650)), 1024 - ease_io(win_k(t, 1500, 1800)));
    if(t >= 1800 && !g_sp.opened){
        g_sp.opened = 1;
        lv_obj_t *gone[] = { g_sp.rim_hl, g_sp.streak };
        for(unsigned i = 0; i < sizeof gone / sizeof *gone; i++) lv_obj_add_flag(gone[i], LV_OBJ_FLAG_HIDDEN);
        for(int s = 0; s < 2; s++) lv_obj_add_flag(g_sp.lobe[s], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(g_sp.root, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(g_sp.face, LV_OBJ_FLAG_HIDDEN);
    }
    if(g_sp.opened) place_band(g_sp.face, RK(K_HUB), R + 2, 0);

    place_band(g_sp.rim, RK(K_DATA) + 1, R, 1);
    int rim_half = scl(180, ease_out(win_k(t, 150, 1000)));
    if(rim_half >= 180) lv_arc_set_bg_angles(g_sp.rim, 0, 360);
    else span(g_sp.rim, 175, rim_half);

    if(!g_sp.opened){
        int lo = LV_MIN(th, g_sp.th) - 24, hi = LV_MAX(th, g_sp.th) + 24;
        if(g_sp.th < 0) lo = th - 24, hi = th + 24;
        int r0 = RK(K_LIT0) - 8;
        inv_sector(r0, R, lo, hi);
        inv_sector(r0, R, lo + 180, hi + 180);
        g_sp.th = th;
        place_band(g_sp.rim_hl, RK(K_DATA) + 1, R, 1);
        span(g_sp.rim_hl, th, scl(22, sp));
        int half = sp > 0 ? LV_MAX(1, scl(7, sp)) : 0;
        for(int s = 0; s < 2; s++){
            int axis = th + 180 * s;
            lv_area_t box;
            sector_box(r0, R, axis - 24, axis + 24, &box);
            lv_obj_set_pos(g_sp.lobe[s], box.x1, box.y1);
            lv_obj_set_size(g_sp.lobe[s], lv_area_get_width(&box), lv_area_get_height(&box));
            int ox = box.x1, oy = box.y1;
            for(int i = 0; i < SP_SLICE; i++){
                lv_obj_t *a = g_sp.slice[s][i];
                place_band(a, RK(K_LIT0), RK(K_DATA), 1);
                lv_obj_set_pos(a, SP_CX - RK(K_DATA) - ox, SP_CX - RK(K_DATA) - oy);
                span(a, axis + scl((2 * i - (SP_SLICE - 1)) * 7 / 2 * (s ? -1 : 1), sp), half);
            }
            int rs = RK(K_LIT0 + (K_DATA - K_LIT0) * 45 / 100);
            place_band(g_sp.shade[s], RK(K_LIT0), rs, 1);
            lv_obj_set_pos(g_sp.shade[s], SP_CX - rs - ox, SP_CX - rs - oy);
            span(g_sp.shade[s], axis, half > 0 ? 24 : 0);
            for(int k = 0; k < (SP_GAPS + 1) / 2; k++){
                int g = gap_r(2 * k);
                lv_obj_set_pos(g_sp.gap[s][k], SP_CX - g - 1 - ox, SP_CX - g - 1 - oy);
            }
            if(s == 0){
                place_band(g_sp.streak, RK(K_LIT0), RK(K_DATA), 1);
                lv_obj_set_pos(g_sp.streak, SP_CX - RK(K_DATA) - ox, SP_CX - RK(K_DATA) - oy);
                span(g_sp.streak, th, sp > 0 ? 1 : 0);
            }
        }
    }

    int warm = scl(ease_out(win_k(t, 650, 1300)), 1024 - win_k(t, 1850, 2150));
    int hr = RK(K_HUB);
    static const int GW[SP_GLOW] = { 4, 10, 18, 28 };
    static const int GO[SP_GLOW] = { 54, 30, 15, 6 };
    if(!g_sp.opened && warm != g_sp.warm){
        lv_area_t gb = { SP_CX - hr - GW[SP_GLOW - 1] - 1, SP_CX - hr - GW[SP_GLOW - 1] - 1,
                         SP_CX + hr + GW[SP_GLOW - 1] + 1, SP_CX + hr + GW[SP_GLOW - 1] + 1 };
        lv_obj_invalidate_area(g_sp.root, &gb);
    }
    g_sp.warm = warm;
    for(int i = 0; i < SP_GLOW; i++){
        place_band(g_sp.glow[i], hr, hr + scl(GW[i], S), 0);
        sty(g_sp.glow[i], LV_STYLE_BORDER_OPA, opa_k(GO[i], warm), 0);
    }
    place_disc(g_sp.hub, hr);
    sty(g_sp.hub, LV_STYLE_BG_OPA, 255 - scl(255, ease_io(win_k(t, 1850, 2300))), 0);
    place_band(g_sp.hub_edge, hr - 1, hr, 0);
    sty(g_sp.hub_edge, LV_STYLE_BORDER_OPA, opa_k(36, scl(warm, 1024 - win_k(t, 1850, 2100))), 0);

    int wm = scl(ease_out(win_k(t, 950, 1400)), 1024 - win_k(t, 1740, 1900));
    sty(g_sp.word, LV_STYLE_OPA, opa_k(255, wm), 0);
    lv_obj_set_y(g_sp.word, SP_CX - lv_obj_get_height(g_sp.word) / 2 + 2 + scl(6, 1024 - ease_out(win_k(t, 950, 1400))));
#undef RK
}

static void splash_anim_cb(void *var, int32_t v){ (void)var; if(g_sp.root){ splash_frame((int)v); } }
static void splash_drawn_cb(lv_event_t *e){ (void)e; g_sp.frames++; }
static void splash_done(lv_anim_t *a){
    (void)a;
    lv_display_remove_event_cb_with_user_data(lv_display_get_default(), splash_drawn_cb, NULL);
    fprintf(stderr, "splash: %d frames drawn in %u ms\n", g_sp.frames, (unsigned)lv_tick_elaps(g_sp.t0));
    if(g_sp.root) lv_obj_delete(g_sp.root);
    memset(&g_sp, 0, sizeof g_sp);
}

void splash_start(void){
    if(g_sp.root) return;
    static const theme_color_role_t BAND[SP_SLICE] = { THEME_CLR_FIXED_SPLASH_BAND_5, THEME_CLR_FIXED_SPLASH_BAND_4,
        THEME_CLR_FIXED_SPLASH_BAND_3, THEME_CLR_FIXED_SPLASH_BAND_2, THEME_CLR_FIXED_SPLASH_BAND_1 };
    static const lv_opa_t BAND_OPA[SP_SLICE] = { 76, 104, 96, 104, 84 };
    lv_color_t accent = TC(ACCENT_PRIMARY);

    g_sp.root = shape(lv_layer_top());
    lv_obj_set_style_radius(g_sp.root, 0, 0);
    lv_obj_set_size(g_sp.root, 360, 360);
    lv_obj_set_style_bg_color(g_sp.root, TC(FIXED_SPLASH_BG), 0);
    lv_obj_set_style_bg_opa(g_sp.root, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_sp.root, LV_OBJ_FLAG_CLICKABLE);

    g_sp.face = ring(g_sp.root, TC(FIXED_SPLASH_BG), LV_OPA_COVER);
    lv_obj_add_flag(g_sp.face, LV_OBJ_FLAG_HIDDEN);
    for(int s = 0; s < 2; s++){
        g_sp.lobe[s] = shape(g_sp.root);
        lv_obj_set_style_radius(g_sp.lobe[s], 0, 0);
        for(int i = 0; i < SP_SLICE; i++)
            g_sp.slice[s][i] = arc(g_sp.lobe[s], theme_color(BAND[i]), (lv_opa_t)(BAND_OPA[i] * (s ? 2 : 3) / 3));
        if(s == 0) g_sp.streak = arc(g_sp.lobe[s], TC(FIXED_SPLASH_3), 70);
        g_sp.shade[s] = arc(g_sp.lobe[s], TC(FIXED_SPLASH_BG), 130);
        for(int k = 0; k < (SP_GAPS + 1) / 2; k++){
            int g = gap_r(2 * k);
            lv_obj_t *o = ring(g_sp.lobe[s], TC(FIXED_SPLASH_HOLE), 130);
            lv_obj_set_size(o, 2 * (g + 1), 2 * (g + 1));
            lv_obj_set_style_border_width(o, 2, 0);
            if(2 * k + 1 < SP_GAPS){
                lv_obj_set_style_outline_color(o, TC(FIXED_SPLASH_HOLE), 0);
                lv_obj_set_style_outline_opa(o, 130, 0);
                lv_obj_set_style_outline_width(o, 2, 0);
                lv_obj_set_style_outline_pad(o, gap_r(2 * k + 1) - 1 - (g + 1), 0);
            }
            g_sp.gap[s][k] = o;
        }
    }
    g_sp.rim = arc(g_sp.root, TC(FIXED_SPLASH_SPINDLE), LV_OPA_COVER);
    g_sp.rim_hl = arc(g_sp.root, TC(FIXED_SPLASH_HUB_LINE), 150);
    for(int i = SP_GLOW - 1; i >= 0; i--) g_sp.glow[i] = ring(g_sp.root, accent, 0);
    g_sp.hub = shape(g_sp.root);
    lv_obj_set_style_bg_color(g_sp.hub, TC(FIXED_SPLASH_BG), 0);
    lv_obj_set_style_bg_opa(g_sp.hub, LV_OPA_COVER, 0);
    g_sp.hub_edge = ring(g_sp.root, TC(FIXED_SPLASH_3), 0);

    g_sp.word = shape(g_sp.root);
    lv_obj_set_style_radius(g_sp.word, 0, 0);
    lv_obj_set_size(g_sp.word, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_sp.word, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_sp.word, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    const lv_font_t *F[2] = { &diskos_brand_disk_32, &diskos_brand_os_32 };
    const char *T[2] = { "disk", "OS" };
    for(int i = 0; i < 2; i++){
        lv_obj_t *l = lv_label_create(g_sp.word);
        kit_keep(l);
        lv_label_set_text(l, T[i]);
        lv_obj_set_style_text_font(l, F[i], 0);
        lv_obj_set_style_text_letter_space(l, -1, 0);
        lv_obj_set_style_text_color(l, TC(FIXED_SPLASH_3), 0);
    }
    lv_obj_update_layout(g_sp.word);
    lv_obj_set_x(g_sp.word, SP_CX - lv_obj_get_width(g_sp.word) / 2);

    g_sp.th = -1;
    g_sp.t0 = lv_tick_get();
    lv_display_add_event_cb(lv_display_get_default(), splash_drawn_cb, LV_EVENT_RENDER_READY, NULL);
    splash_frame(0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_sp.root);
    lv_anim_set_values(&a, 0, SP_MS);
    lv_anim_set_duration(&a, SP_MS);
    lv_anim_set_exec_cb(&a, splash_anim_cb);
    lv_anim_set_completed_cb(&a, splash_done);
    lv_anim_start(&a);
}
