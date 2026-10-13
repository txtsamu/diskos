/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The mood sphere. The ball is computed once per run (an orthographic sphere seen head-on, coloured by the mood map,
 * lit by a fixed studio light from the upper left, a faint globe grid), already blended onto the screen colour so it
 * is drawn as a plain opaque copy. Dragging moves only small things over it: a soft glow, the marker (a dot with a
 * ring) and two lines - the great circles through the point from Energetic to Calm and from Dark to Bright. */
#include "sphere.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SR 148                                  /* ball radius */
#define SW (2 * SR + 4)                         /* its picture */
#define GW 120                                  /* the glow sprite */
#define NPT 49                                  /* points per line */
#define GRAB 34                                 /* how near the marker a press must start to pick it up */

static uint8_t *g_ball, *g_glow, *g_mini;
static lv_image_dsc_t g_ball_dsc, g_glow_dsc, g_mini_dsc;
static lv_obj_t *g_box, *g_glow_img, *g_ring, *g_core, *g_line[2];
static lv_point_precise_t g_pts[2][NPT];
static float g_x, g_y; static int g_faded, g_grab;
static sphere_cb_t g_cb; static void *g_ud;

typedef struct { float r, g, b; } rgbf;
/* N Energetic coral, S Calm sky blue, E Bright gold, W Dark violet; corners blend; the middle softens to grey */
static rgbf mood(float x, float y){
    static const rgbf N = { 255, 86, 64 }, S = { 40, 170, 245 }, E = { 255, 206, 40 }, W = { 120, 60, 235 }, C = { 200, 196, 214 };
    const rgbf NE = { 255, 146, 52 }, NW = { 207, 63, 150 }, SE = { 70, 215, 160 }, SWc = { 60, 70, 200 };
    float u = (x + 1) / 2, v = (y + 1) / 2;
    rgbf top = { NW.r + (NE.r - NW.r) * u, NW.g + (NE.g - NW.g) * u, NW.b + (NE.b - NW.b) * u };
    rgbf bot = { SWc.r + (SE.r - SWc.r) * u, SWc.g + (SE.g - SWc.g) * u, SWc.b + (SE.b - SWc.b) * u };
    rgbf c = { bot.r + (top.r - bot.r) * v, bot.g + (top.g - bot.g) * v, bot.b + (top.b - bot.b) * v };
    float ax = fabsf(x) > fabsf(y) ? fabsf(x) - fabsf(y) : 0, ay = fabsf(y) > fabsf(x) ? fabsf(y) - fabsf(x) : 0;
    const rgbf *px = x > 0 ? &E : &W, *py = y > 0 ? &N : &S;
    c.r += (px->r - c.r) * ax * 0.6f + (py->r - c.r) * ay * 0.6f;
    c.g += (px->g - c.g) * ax * 0.6f + (py->g - c.g) * ay * 0.6f;
    c.b += (px->b - c.b) * ax * 0.6f + (py->b - c.b) * ay * 0.6f;
    float r = sqrtf(x * x + y * y), k = (1 - r) * (1 - r) * 0.55f;
    c.r += (C.r - c.r) * k; c.g += (C.g - c.g) * k; c.b += (C.b - c.b) * k;
    float gr = (c.r + c.g + c.b) / 3;
    c.r = fminf(255, fmaxf(0, gr + (c.r - gr) * 1.3f)); c.g = fminf(255, fmaxf(0, gr + (c.g - gr) * 1.3f));
    c.b = fminf(255, fmaxf(0, gr + (c.b - gr) * 1.3f));
    return c;
}
void sphere_mood_rgb(float x, float y, uint8_t rgb[3]){ rgbf c = mood(x, y); rgb[0] = (uint8_t)c.r; rgb[1] = (uint8_t)c.g; rgb[2] = (uint8_t)c.b; }
const char *sphere_mood_name(float x, float y){
    if(sqrtf(x * x + y * y) < 0.25f) return "Balanced";
    float a = atan2f(y, x) * 57.29578f; if(a < 0) a += 360;
    static const char *const N8[8] = { "Bright", "Euphoric", "Energetic", "Intense", "Dark", "Melancholic", "Calm", "Relaxed" };
    return N8[((int)((a + 22.5f) / 45)) % 8];
}

/* ---- the picture, once ---- */
static void grid_plot(uint8_t *buf, int R, int W_, float fx, float fy, float a){                /* a faint white dot, antialiased (bilinear splat) */
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy); float tx = fx - x0, ty = fy - y0;
    float w[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
    for(int i = 0; i < 4; i++){
        int x = x0 + (i & 1), y = y0 + (i >> 1);
        if(x < 0 || y < 0 || x >= W_ || y >= W_) continue;
        float dx = (x + 0.5f - W_ / 2.0f) / R, dy = (y + 0.5f - W_ / 2.0f) / R;
        if(dx * dx + dy * dy > 0.995f) continue;
        uint8_t *p = buf + ((size_t)y * W_ + x) * 4; float k = a * w[i];
        for(int c = 0; c < 3; c++) p[c] = (uint8_t)(p[c] + (255 - p[c]) * k);
    }
}
static void ball_render(uint8_t *buf, int R, lv_color_t canvas, lv_image_dsc_t *dsc){
    const int W_ = 2 * R + 4;
    const float lx = -0.35f, ly = 0.45f, lz = sqrtf(1 - lx * lx - ly * ly);
    float L[3] = { lx, ly, lz + 0.7f }; float ll = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    for(int i = 0; i < 3; i++) L[i] /= ll;
    const float bg[3] = { canvas.blue, canvas.green, canvas.red };
    for(int j = 0; j < W_; j++) for(int i = 0; i < W_; i++){
        float X = (i + 0.5f - W_ / 2.0f) / R, Y = -(j + 0.5f - W_ / 2.0f) / R, r2 = X * X + Y * Y;
        uint8_t *o = buf + ((size_t)j * W_ + i) * 4;
        float cov = R + 0.5f - sqrtf(r2) * R;                         /* antialiased rim */
        if(cov <= 0){ o[0] = (uint8_t)bg[0]; o[1] = (uint8_t)bg[1]; o[2] = (uint8_t)bg[2]; o[3] = 0xFF; continue; }
        if(cov > 1) cov = 1;
        float Z = sqrtf(fmaxf(0, 1 - r2));
        rgbf c = mood(X, Y);
        float nl = X * L[0] + Y * L[1] + Z * L[2]; if(nl < 0) nl = 0;
        float nh = X * lx + Y * ly + Z * lz; if(nh < 0) nh = 0;
        float diff = 0.38f + 0.70f * nl, spec = powf(nh, 90) * 0.12f + powf(nh, 12) * 0.05f;
        float rim = powf(1 - Z, 3) * 0.35f, edge = 0.55f + 0.45f * powf(Z, 0.5f);
        float px[3] = { c.b * diff * edge + 255 * spec + c.b * rim, c.g * diff * edge + 255 * spec + c.g * rim,
                        c.r * diff * edge + 255 * spec + c.r * rim };
        for(int k = 0; k < 3; k++) o[k] = (uint8_t)(bg[k] + (fminf(255, px[k]) - bg[k]) * cov);
        o[3] = 0xFF;
    }
    /* the globe grid, very faint: meridians every 45 degrees, parallels every 30, the equator and middle a touch more */
    for(int k = -2; k <= 2; k++){
        float lon = k * 0.785398f, a = k ? 0.05f : 0.08f;
        for(int i = 0; i <= 700; i++){ float t = -1.5708f + 3.14159f * i / 700, z = cosf(lon) * cosf(t);
            if(z > 0.02f) grid_plot(buf, R, W_, W_ / 2.0f + sinf(lon) * cosf(t) * R, W_ / 2.0f - sinf(t) * R, a * fminf(1, z / 0.25f)); }
    }
    for(int k = -2; k <= 2; k++){
        float lat = k * 0.5236f, a = k ? 0.05f : 0.08f, w = cosf(lat) * R;
        for(int i = 0; i <= 700; i++){ float t = -1.5708f + 3.14159f * i / 700, z = cosf(t);
            if(z > 0.02f) grid_plot(buf, R, W_, W_ / 2.0f + sinf(t) * w, W_ / 2.0f - sinf(lat) * R, a * fminf(1, z / 0.25f)); }
    }
    memset(dsc, 0, sizeof *dsc);
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC; dsc->header.cf = LV_COLOR_FORMAT_XRGB8888;   /* opaque: a plain copy */
    dsc->header.w = dsc->header.h = (uint32_t)W_; dsc->header.stride = (uint32_t)W_ * 4;
    dsc->data = buf; dsc->data_size = (uint32_t)(W_ * W_ * 4);
}
static void glow_render(void){
    for(int j = 0; j < GW; j++) for(int i = 0; i < GW; i++){
        float dx = (i + 0.5f - GW / 2.0f) / (GW / 2.0f), dy = (j + 0.5f - GW / 2.0f) / (GW / 2.0f), r = sqrtf(dx * dx + dy * dy);
        float a = r >= 1 ? 0 : powf(1 - r, 2.0f) * 0.95f + (r < 0.2f ? (0.2f - r) / 0.2f * 0.25f : 0);
        uint8_t *o = g_glow + ((size_t)j * GW + i) * 4; o[0] = o[1] = o[2] = 255; o[3] = (uint8_t)(fminf(1, a) * 255);
    }
    memset(&g_glow_dsc, 0, sizeof g_glow_dsc);
    g_glow_dsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_glow_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_glow_dsc.header.w = g_glow_dsc.header.h = GW; g_glow_dsc.header.stride = GW * 4;
    g_glow_dsc.data = g_glow; g_glow_dsc.data_size = GW * GW * 4;
}

/* ---- the moving pieces ---- */
static void place(void){
    float x = g_x, y = g_y, c = SW / 2.0f;
    int mx = (int)lroundf(c + x * SR), my = (int)lroundf(c - y * SR);
    lv_obj_set_pos(g_glow_img, mx - GW / 2, my - GW / 2);
    lv_obj_set_pos(g_ring, mx - 10, my - 10);
    lv_obj_set_pos(g_core, mx - 4, my - 4);
    lv_opa_t o = g_faded ? LV_OPA_60 : LV_OPA_COVER;
    lv_obj_set_style_border_opa(g_ring, o, 0); lv_obj_set_style_bg_opa(g_core, o, 0);
    lv_obj_set_style_opa(g_glow_img, g_faded ? LV_OPA_50 : LV_OPA_COVER, 0);
    /* the line from Energetic to Calm through the point (its meridian) ... */
    float lat = asinf(fmaxf(-1, fminf(1, y))), cl = cosf(lat);
    float lon = cl > 1e-4f ? asinf(fmaxf(-1, fminf(1, x / cl))) : 0;
    for(int i = 0; i < NPT; i++){ float t = 1.5708f - 3.14159f * i / (NPT - 1);
        g_pts[0][i].x = c + sinf(lon) * cosf(t) * SR; g_pts[0][i].y = c - sinf(t) * SR; }
    /* ... and from Bright to Dark through it (the great circle through E and W) */
    float s = sqrtf(fmaxf(1e-6f, 1 - x * x)), a = asinf(fmaxf(-1, fminf(1, y / s)));
    for(int i = 0; i < NPT; i++){ float t = 3.14159f * i / (NPT - 1);
        g_pts[1][i].x = c + cosf(t) * SR; g_pts[1][i].y = c - sinf(t) * sinf(a) * SR; }
    lv_line_set_points(g_line[0], g_pts[0], NPT);
    lv_line_set_points(g_line[1], g_pts[1], NPT);
}
static void to_unit(lv_indev_t *in, float *x, float *y){
    lv_point_t p; lv_indev_get_point(in, &p);
    lv_area_t a; lv_obj_get_coords(g_box, &a);
    float c = SW / 2.0f, ux = (p.x - a.x1 - c) / SR, uy = -(p.y - a.y1 - c) / SR, r = sqrtf(ux * ux + uy * uy);
    if(r > 0.97f){ ux *= 0.97f / r; uy *= 0.97f / r; }                   /* stays on the ball */
    *x = ux; *y = uy;
}
static void input_cb(lv_event_t *e){
    lv_event_code_t code = lv_event_get_code(e); lv_indev_t *in = lv_indev_active(); if(!in) return;
    if(code == LV_EVENT_PRESSED){
        lv_point_t p; lv_indev_get_point(in, &p); lv_area_t a; lv_obj_get_coords(g_box, &a);
        float c = SW / 2.0f, dx = p.x - (a.x1 + c + g_x * SR), dy = p.y - (a.y1 + c - g_y * SR);
        g_grab = dx * dx + dy * dy <= GRAB * GRAB;                          /* only the marker can be moved */
        if(g_grab && g_cb) g_cb(0, g_x, g_y, g_ud);
    } else if(code == LV_EVENT_PRESSING && g_grab){
        float x, y; to_unit(in, &x, &y);
        if(fabsf(x - g_x) < 0.002f && fabsf(y - g_y) < 0.002f) return;
        g_x = x; g_y = y; g_faded = 0; place();
        if(g_cb) g_cb(1, g_x, g_y, g_ud);
    } else if((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && g_grab){
        g_grab = 0; if(g_cb) g_cb(2, g_x, g_y, g_ud);
    }
}
static lv_obj_t *dot(lv_obj_t *p, int d){
    lv_obj_t *o = lv_obj_create(p); lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d); lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
    return o;
}
lv_obj_t *sphere_create(lv_obj_t *parent, int cx, int cy, lv_color_t canvas){
    if(!sphere_picture(canvas, 0)) return NULL;
    if(!g_glow){ g_glow = malloc((size_t)GW * GW * 4); if(!g_glow) return NULL; glow_render(); }
    g_box = lv_obj_create(parent); lv_obj_remove_style_all(g_box);
    lv_obj_set_size(g_box, SW, SW); lv_obj_set_pos(g_box, cx - SW / 2, cy - SW / 2);
    lv_obj_remove_flag(g_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(g_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ball = lv_image_create(g_box); lv_image_set_src(ball, &g_ball_dsc);
    lv_obj_remove_flag(ball, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(ball, LV_OBJ_FLAG_EVENT_BUBBLE);
    g_glow_img = lv_image_create(g_box); lv_image_set_src(g_glow_img, &g_glow_dsc);
    lv_obj_remove_flag(g_glow_img, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(g_glow_img, LV_OBJ_FLAG_EVENT_BUBBLE);
    for(int k = 0; k < 2; k++){
        g_line[k] = lv_line_create(g_box);
        lv_obj_set_style_line_width(g_line[k], 2, 0); lv_obj_set_style_line_rounded(g_line[k], false, 0);   /* round joints would overlap into beads at 40 % */
        lv_obj_set_style_line_color(g_line[k], lv_color_white(), 0); lv_obj_set_style_line_opa(g_line[k], LV_OPA_40, 0);
        lv_obj_remove_flag(g_line[k], LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(g_line[k], LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    g_ring = dot(g_box, 20);
    lv_obj_set_style_border_width(g_ring, 2, 0); lv_obj_set_style_border_color(g_ring, lv_color_white(), 0);
    lv_obj_set_style_outline_width(g_ring, 1, 0); lv_obj_set_style_outline_color(g_ring, lv_color_black(), 0);
    lv_obj_set_style_outline_opa(g_ring, LV_OPA_30, 0);
    g_core = dot(g_box, 8);
    lv_obj_set_style_bg_color(g_core, lv_color_white(), 0);
    lv_obj_set_style_outline_width(g_core, 1, 0); lv_obj_set_style_outline_color(g_core, lv_color_black(), 0);
    lv_obj_set_style_outline_opa(g_core, LV_OPA_30, 0);
    lv_obj_add_event_cb(g_box, input_cb, LV_EVENT_ALL, NULL);
    g_x = g_y = 0; g_faded = 1; g_grab = 0; place();
    return g_box;
}
void sphere_set(float x, float y, int faded){ g_x = x; g_y = y; g_faded = faded; if(g_box) place(); }
void sphere_get(float *x, float *y){ *x = g_x; *y = g_y; }
void sphere_set_cb(sphere_cb_t cb, void *ud){ g_cb = cb; g_ud = ud; }
void sphere_free(void){ free(g_ball); free(g_glow); free(g_mini); g_ball = g_glow = g_mini = NULL; g_box = NULL; }
const lv_image_dsc_t *sphere_picture(lv_color_t canvas, int small){
    if(small){
        if(!g_mini){ g_mini = malloc((size_t)(2 * 57 + 4) * (2 * 57 + 4) * 4); if(!g_mini) return NULL; ball_render(g_mini, 57, canvas, &g_mini_dsc); }
        return &g_mini_dsc;
    }
    if(!g_ball){ g_ball = malloc((size_t)SW * SW * 4); if(!g_ball) return NULL; ball_render(g_ball, SR, canvas, &g_ball_dsc); }
    return &g_ball_dsc;
}
