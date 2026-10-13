/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SongSphere concept mockups (host_render mode sphere-<n>): a mood circle drawn as a lit sphere. The sphere is a
 * picture computed pixel by pixel (orthographic sphere seen head-on): its colour is the mood map at that 2D spot,
 * the light comes from the chosen point (so it glows there), with a specular highlight, edge falloff, a faint globe
 * grid, and the two great circles through the chosen point: the meridian from Energetic (N) to Calm (S) and the one
 * from Dark (W) to Bright (E). Text uses theme roles; the mood palette is the sphere's own content. */
#include "lvgl/lvgl.h"
#include "theme.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SR 148                                 /* nearly the whole screen: a 30 px ring is left for the poles */                                 /* sphere radius (px) */
#define SW (2 * SR + 4)
static uint8_t g_sp[SW * SW * 4];
static lv_image_dsc_t g_sp_dsc;
static int g_fixed, g_dragging;                                   /* 1: fixed light, the glow is a separate sprite that moves */
#define GW 120
static uint8_t g_glow[GW * GW * 4];
static lv_image_dsc_t g_glow_dsc;
static void glow_make(void){                          /* a soft white light, drawn once */
    for(int j = 0; j < GW; j++) for(int i = 0; i < GW; i++){
        float dx = (i + 0.5f - GW / 2.0f) / (GW / 2.0f), dy = (j + 0.5f - GW / 2.0f) / (GW / 2.0f), r = sqrtf(dx * dx + dy * dy);
        float a = r >= 1 ? 0 : powf(1 - r, 2.0f) * 0.95f + (r < 0.2f ? (0.2f - r) / 0.2f * 0.25f : 0);
        uint8_t *o = g_glow + ((size_t)j * GW + i) * 4; o[0] = o[1] = o[2] = 255; o[3] = (uint8_t)(fminf(1, a) * 255);
    }
    memset(&g_glow_dsc, 0, sizeof g_glow_dsc);
    g_glow_dsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_glow_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_glow_dsc.header.w = g_glow_dsc.header.h = GW; g_glow_dsc.header.stride = GW * 4;
    g_glow_dsc.data = g_glow; g_glow_dsc.data_size = sizeof g_glow;
}
typedef struct { float r, g, b; } rgbf;

/* the mood map: N energetic coral, S calm sky blue, E bright gold, W dark violet; corners blend, centre softens */
static rgbf mood(float x, float y){
    static const rgbf N = { 255, 86, 64 }, S = { 40, 170, 245 }, E = { 255, 206, 40 }, W = { 120, 60, 235 }, C = { 200, 196, 214 };
    rgbf NE = { (N.r + E.r) / 2 + 10, (N.g + E.g) / 2, (N.b + E.b) / 2 }, NW = { (N.r + W.r) / 2 + 20, (N.g + W.g) / 2 - 10, (N.b + W.b) / 2 },
         SE = { 70, 215, 160 }, SW_ = { 60, 70, 200 };
    float u = (x + 1) / 2, v = (y + 1) / 2;                                    /* v: 1 = north */
    rgbf top = { NW.r + (NE.r - NW.r) * u, NW.g + (NE.g - NW.g) * u, NW.b + (NE.b - NW.b) * u };
    rgbf bot = { SW_.r + (SE.r - SW_.r) * u, SW_.g + (SE.g - SW_.g) * u, SW_.b + (SE.b - SW_.b) * u };
    rgbf c = { bot.r + (top.r - bot.r) * v, bot.g + (top.g - bot.g) * v, bot.b + (top.b - bot.b) * v };
    /* pull the axes towards their poles, so N/S/E/W read clearly */
    float ax = fabsf(x) > fabsf(y) ? fabsf(x) - fabsf(y) : 0, ay = fabsf(y) > fabsf(x) ? fabsf(y) - fabsf(x) : 0;
    const rgbf *px = x > 0 ? &E : &W, *py = y > 0 ? &N : &S;
    c.r += (px->r - c.r) * ax * 0.6f + (py->r - c.r) * ay * 0.6f;
    c.g += (px->g - c.g) * ax * 0.6f + (py->g - c.g) * ay * 0.6f;
    c.b += (px->b - c.b) * ax * 0.6f + (py->b - c.b) * ay * 0.6f;
    float r = sqrtf(x * x + y * y), k = (1 - r) * (1 - r) * 0.55f;            /* a calm, greyish middle */
    c.r += (C.r - c.r) * k; c.g += (C.g - c.g) * k; c.b += (C.b - c.b) * k;
    return c;
}
static void plot(float fx, float fy, float a){                                 /* an antialiased white dot (bilinear splat) */
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy); float tx = fx - x0, ty = fy - y0;
    float w[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
    int xs[4] = { x0, x0 + 1, x0, x0 + 1 }, ys[4] = { y0, y0, y0 + 1, y0 + 1 };
    for(int i = 0; i < 4; i++){
        if(xs[i] < 0 || ys[i] < 0 || xs[i] >= SW || ys[i] >= SW) continue;
        uint8_t *p = g_sp + ((size_t)ys[i] * SW + xs[i]) * 4; if(!p[3]) continue;
        float k = a * w[i]; if(k > 1) k = 1;
        for(int c = 0; c < 3; c++) p[c] = (uint8_t)(p[c] + (255 - p[c]) * k);
    }
}
/* a curve on the sphere (unit vectors), front half only; `width` > 1 splats a thicker line */
static void curve(float (*f)(float, float *, float *, float *, void *), void *ud, float a, float width){
    for(int i = 0; i <= 1400; i++){
        float t = (float)i / 1400.0f * 6.2831853f, x, y, z;
        if(!f(t, &x, &y, &z, ud) || z < 0.02f) continue;
        float fade = z < 0.25f ? z / 0.25f : 1;                                 /* lines thin out towards the rim */
        float cx = SW / 2.0f + x * SR, cy = SW / 2.0f - y * SR;
        plot(cx, cy, a * fade);
        if(width > 1){ plot(cx + 0.7f, cy, a * fade * 0.6f); plot(cx, cy + 0.7f, a * fade * 0.6f); }
    }
}
static float meridian(float t, float *x, float *y, float *z, void *ud){         /* through N and S at longitude lon */
    float lon = *(float *)ud; *x = sinf(lon) * cosf(t); *y = sinf(t); *z = cosf(lon) * cosf(t); return 1;
}
static float parallel(float t, float *x, float *y, float *z, void *ud){         /* latitude lat */
    float lat = *(float *)ud; *x = cosf(lat) * sinf(t); *y = sinf(lat); *z = cosf(lat) * cosf(t); return 1;
}
static float ewcircle(float t, float *x, float *y, float *z, void *ud){         /* great circle through E and W, tilted by a */
    float a = *(float *)ud; *x = cosf(t); *y = sinf(t) * sinf(a); *z = sinf(t) * cosf(a); return 1;
}
/* render the sphere lit at (px, py) (disk coordinates, -1..1, y up); grid: 0 none, 1 faint globe grid */
static void sphere_render(float px, float py, int grid){
    float lx = g_fixed ? -0.35f : px, ly = g_fixed ? 0.45f : py;                /* fixed: a studio light up-left */
    float lz = sqrtf(fmaxf(0, 1 - lx * lx - ly * ly));
    float H[3] = { lx, ly, lz };
    float L[3] = { lx, ly, lz + 0.7f }; float ll = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    for(int i = 0; i < 3; i++) L[i] /= ll;
    for(int j = 0; j < SW; j++) for(int i = 0; i < SW; i++){
        float X = (i + 0.5f - SW / 2.0f) / SR, Y = -(j + 0.5f - SW / 2.0f) / SR, r2 = X * X + Y * Y;
        uint8_t *o = g_sp + ((size_t)j * SW + i) * 4;
        float d = sqrtf(r2) * SR, cov = SR + 0.5f - d;                            /* antialiased rim */
        if(cov <= 0){ o[0] = o[1] = o[2] = o[3] = 0; continue; }
        if(cov > 1) cov = 1;
        float Z = sqrtf(fmaxf(0, 1 - r2));
        rgbf c = mood(X, Y);
        float gr = (c.r + c.g + c.b) / 3;                                        /* a little more saturated */
        c.r = fminf(255, fmaxf(0, gr + (c.r - gr) * 1.3f)); c.g = fminf(255, fmaxf(0, gr + (c.g - gr) * 1.3f)); c.b = fminf(255, fmaxf(0, gr + (c.b - gr) * 1.3f));
        float nl = X * L[0] + Y * L[1] + Z * L[2]; if(nl < 0) nl = 0;
        float nh = X * H[0] + Y * H[1] + Z * H[2]; if(nh < 0) nh = 0;
        float diff = 0.38f + 0.70f * nl, spec = g_fixed ? powf(nh, 90) * 0.12f + powf(nh, 12) * 0.05f
                                                     : powf(nh, 70) * 0.75f + powf(nh, 10) * 0.16f;   /* a hot spot and a soft bloom */
        float rim = powf(1 - Z, 3) * 0.35f;                                      /* a thin atmospheric rim */
        float edge = 0.55f + 0.45f * powf(Z, 0.5f);                              /* falloff towards the edge */
        float R = c.r * diff * edge + 255 * spec + c.r * rim, G = c.g * diff * edge + 255 * spec + c.g * rim, B = c.b * diff * edge + 255 * spec + c.b * rim;
        o[2] = (uint8_t)fminf(255, R); o[1] = (uint8_t)fminf(255, G); o[0] = (uint8_t)fminf(255, B);
        o[3] = (uint8_t)(255 * cov);
    }
    if(grid){
        for(int k = -2; k <= 2; k++){ float lon = k * 0.5236f * 1.5f; curve(meridian, &lon, 0.05f, 1); }
        for(int k = -2; k <= 2; k++){ if(!k) continue; float lat = k * 0.5236f; curve(parallel, &lat, 0.05f, 1); }
        float eq = 0; curve(parallel, &eq, 0.08f, 1);                           /* the equator a touch stronger */
        float pm = 0; curve(meridian, &pm, 0.08f, 1);
    }
    /* the two great circles through the chosen point */
    float lat = asinf(fmaxf(-1, fminf(1, py))), cl = cosf(lat);
    float lon = cl > 1e-4f ? asinf(fmaxf(-1, fminf(1, px / cl))) : 0;
    curve(meridian, &lon, 0.42f, 2);
    float s = sqrtf(fmaxf(1e-6f, 1 - px * px)), a = asinf(fmaxf(-1, fminf(1, py / s)));
    curve(ewcircle, &a, 0.42f, 2);
    memset(&g_sp_dsc, 0, sizeof g_sp_dsc);
    g_sp_dsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_sp_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_sp_dsc.header.w = g_sp_dsc.header.h = SW; g_sp_dsc.header.stride = SW * 4;
    g_sp_dsc.data = g_sp; g_sp_dsc.data_size = sizeof g_sp;
}
static lv_obj_t *sp_text(lv_obj_t *p, const char *s, int cx, int y, int w, const lv_font_t *f, lv_color_t c){
    lv_obj_t *l = lv_label_create(p); lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_width(l, w); lv_obj_set_pos(l, cx - w / 2, y);
    return l;
}
static lv_obj_t *sp_dot(lv_obj_t *p, int cx, int cy, int d, lv_color_t c, lv_opa_t o){
    lv_obj_t *b = lv_obj_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, d, d); lv_obj_set_pos(b, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(b, c, 0); lv_obj_set_style_bg_opa(b, o, 0);
    return b;
}
static const char *mood_name(float x, float y){
    float r = sqrtf(x * x + y * y);
    if(r < 0.25f) return "Balanced";
    float a = atan2f(y, x) * 57.29578f;                                          /* 0 = Bright, 90 = Energetic */
    if(a < 0) a += 360;
    static const char *const N8[8] = { "Bright", "Euphoric", "Energetic", "Intense", "Dark", "Melancholic", "Calm", "Relaxed" };
    return N8[((int)((a + 22.5f) / 45)) % 8];
}
/* one screen: tag (an item to place, Save / Remove) or play (Play this mood) */
static void sphere_screen(lv_obj_t *root, float px, float py, int tag, int grid, const char *item, const char *kind, int faded){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    sphere_render(px, py, grid);
    int cx = 180, cy = 180;
    lv_obj_t *im = lv_image_create(root); lv_image_set_src(im, &g_sp_dsc); lv_obj_set_pos(im, cx - SW / 2, cy - SW / 2);
    int mx = cx + (int)lroundf(px * SR), my = cy - (int)lroundf(py * SR);
    if(g_fixed){                                                                /* the light that follows the point */
        glow_make();
        lv_obj_t *g = lv_image_create(root); lv_image_set_src(g, &g_glow_dsc); lv_obj_set_pos(g, mx - GW / 2, my - GW / 2);
    }
    /* the exact point: a dot with a small ring */
    lv_opa_t mo = faded ? LV_OPA_60 : LV_OPA_COVER;
    lv_obj_t *ring = sp_dot(root, mx, my, 20, lv_color_hex(0x000000), LV_OPA_TRANSP);
    lv_obj_set_style_border_width(ring, 2, 0); lv_obj_set_style_border_color(ring, lv_color_hex(0xFFFFFF), 0); lv_obj_set_style_border_opa(ring, mo, 0);
    lv_obj_set_style_outline_width(ring, 1, 0); lv_obj_set_style_outline_color(ring, lv_color_hex(0x000000), 0); lv_obj_set_style_outline_opa(ring, LV_OPA_30, 0);
    lv_obj_t *core = sp_dot(root, mx, my, 8, lv_color_hex(0xFFFFFF), mo);
    lv_obj_set_style_outline_width(core, 1, 0); lv_obj_set_style_outline_color(core, lv_color_hex(0x000000), 0); lv_obj_set_style_outline_opa(core, LV_OPA_30, 0);
    /* the poles, in the ring round the sphere: top and bottom flat, left and right along the rim */
    const lv_font_t *fp = TF(UI_12); int lh = lv_font_get_line_height(fp);
    sp_text(root, "ENERGETIC", cx, 9, 120, fp, TC(TEXT_SECONDARY));
    sp_text(root, "CALM", cx, 351 - lh - 9 + 6, 120, fp, TC(TEXT_SECONDARY));
    lv_obj_t *dk = sp_text(root, "DARK", 16, cy - lh / 2, 80, fp, TC(TEXT_SECONDARY));
    lv_obj_set_style_transform_pivot_x(dk, 40, 0); lv_obj_set_style_transform_pivot_y(dk, lh / 2, 0);
    lv_obj_set_style_transform_rotation(dk, 2700, 0);
    lv_obj_t *br = sp_text(root, "BRIGHT", 344, cy - lh / 2, 80, fp, TC(TEXT_SECONDARY));
    lv_obj_set_style_transform_pivot_x(br, 40, 0); lv_obj_set_style_transform_pivot_y(br, lh / 2, 0);
    lv_obj_set_style_transform_rotation(br, 900, 0);
    /* on the sphere: the item / the mood at the top, the action at the bottom, both on soft glass pills */
    lv_obj_t *top = lv_obj_create(root); lv_obj_remove_style_all(top);
    int tw = tag ? 190 : 150, th = tag ? 40 : 32; lv_obj_set_size(top, tw, th); lv_obj_set_pos(top, cx - tw / 2, 44);
    lv_obj_set_style_radius(top, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(top, TC(CANVAS), 0); lv_obj_set_style_bg_opa(top, LV_OPA_50, 0);
    if(g_dragging) lv_obj_set_style_opa(top, LV_OPA_20, 0);                    /* the pills fade while the marker moves */
    if(tag){
        sp_text(top, item, tw / 2, 3, tw - 20, TF(UI_14), TC(TEXT_PRIMARY));
        sp_text(top, kind, tw / 2, 21, tw - 20, TF(UI_10), faded ? TC(TEXT_SECONDARY) : TC(ACCENT_PRIMARY));
    } else sp_text(top, mood_name(px, py), tw / 2, (th - lv_font_get_line_height(TF(UI_18))) / 2, tw - 16, TF(UI_18), TC(TEXT_PRIMARY));
    lv_obj_t *b = lv_obj_create(root); lv_obj_remove_style_all(b);
    int bw = tag ? 104 : 140; lv_obj_set_size(b, bw, 36); lv_obj_set_pos(b, cx - bw / 2, 278);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(b, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    if(g_dragging) lv_obj_set_style_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(b, 16, 0); lv_obj_set_style_shadow_color(b, lv_color_hex(0x000000), 0); lv_obj_set_style_shadow_opa(b, LV_OPA_40, 0);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, tag ? "Save" : LV_SYMBOL_PLAY "  Play mood");
    lv_obj_set_style_text_font(l, TF(UI_16), 0); lv_obj_set_style_text_color(l, TC(ON_ACCENT), 0); lv_obj_center(l);
}
void sphere_mock(lv_obj_t *root, int which){
    g_fixed = which < 6; g_dragging = which == 2 || which == 5;
    switch(which % 6){
        case 0: sphere_screen(root, 0.0f, 0.0f, 1, 1, "Endless Summer", "ALBUM  -  NOT PLACED YET", 1); break;
        case 1: sphere_screen(root, 0.48f, 0.52f, 1, 1, "Endless Summer", "ALBUM", 0); break;
        case 2: sphere_screen(root, 0.10f, 0.78f, 1, 1, "Endless Summer", "ALBUM", 0); break;
        case 3: sphere_screen(root, -0.55f, -0.45f, 0, 1, "", "", 0); break;
        case 4: sphere_screen(root, 0.55f, -0.50f, 0, 1, "", "", 0); break;
        default: sphere_screen(root, -0.20f, -0.80f, 0, 1, "", "", 0); break;
    }
}
