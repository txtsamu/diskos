/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* splash.c - the Disco! boot splash: a mirror ball.
 *
 * Rainbow lights curl in from the rim on their own spirals and gather; a faceted mirror ball, hanging on its wire,
 * fades in where they met and turns, throwing small spots of light that travel sideways across the screen as it
 * turns (what a real mirror ball does to a room). "Disco!" rises in beneath it in Monoton, the "!" in the accent
 * colour. Then everything fades and Home is there. 2.6 s, once per boot, on lv_layer_top; it swallows taps and
 * deletes itself.
 *
 * Cost on the GPU-less renderer: the ball is one 150 px picture redrawn each frame from lookup tables made once
 * (per pixel: its tile row, its longitude and edge shade; per frame only the ~430 tile shades are worked out), so a
 * frame of it is integer work. The lights and spots are ~90 small plain circles moved from one timeline. Nothing is
 * scaled, masked or blurred. */
#include "screens.h"
#include "theme.h"
#include "theme_fonts.h"
#include "theme_kit.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SP_MS   2600            /* the whole splash */
#define BR      74              /* ball radius */
#define BW      (2 * BR + 2)    /* its picture */
#define BCX     180             /* ball centre */
#define BCY     150
#define ROWS    16              /* rings of tiles */
#define MAXCOL  32              /* tiles round the equator */
#define NSPECK  64              /* lights coming in */
#define NSPOT   48              /* light spots on the walls */

static struct {
    lv_obj_t *root, *ball, *wire, *word, *bang, *speck[NSPECK], *spot[NSPOT], *glint[3];
    int frames;
    uint32_t t0;
} g_sp;

/* the ball: per pixel (made once) its row, its longitude (16-bit turn) and how much its edge darkens */
static uint8_t *g_px;                                   /* the picture, XRGB8888 */
static lv_image_dsc_t g_dsc;
static int8_t  *g_row;                                  /* -1 outside, else the tile row */
static uint16_t *g_lon;                                 /* longitude seen at this pixel, before turning */
static uint8_t *g_edge, *g_rowfrac;                     /* rim darkening 0..255; position within the row 0..255 */
static int g_ncol[ROWS];
static uint8_t g_tint[ROWS][MAXCOL];                    /* each tile catches the light a little differently */
static uint8_t g_shade[ROWS][MAXCOL][3];                /* this frame's colour of every tile */
static float g_spot_lat[NSPOT], g_spot_lon[NSPOT];
static uint32_t g_spot_rgb[NSPOT];
static float g_sk_a[NSPECK], g_sk_r[NSPECK], g_sk_spin[NSPECK], g_sk_d[NSPECK], g_sk_ex[NSPECK], g_sk_ey[NSPECK], g_sk_tw[NSPECK];

static unsigned g_rs = 7;
static float rnd(void){ g_rs = g_rs * 1103515245u + 12345u; return (float)((g_rs >> 8) & 0xFFFF) / 65535.0f; }
static float clampf(float v){ return v < 0 ? 0 : v > 1 ? 1 : v; }
static float seg(float t, float a, float b){ return clampf((t - a) / (b - a)); }
static float ease(float t){ t = clampf(t); float r = 1 - t; return 1 - r * r * r; }
static float eio(float t){ t = clampf(t); return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }
/* a rainbow colour, as the Disco rim's: h 0..1 */
static uint32_t rainbow(float h, float l){
    float r = fabsf(h * 6 - 3) - 1, g = 2 - fabsf(h * 6 - 2), b = 2 - fabsf(h * 6 - 4);
    r = clampf(r); g = clampf(g); b = clampf(b);
    float k = 0.35f + 0.65f * l;
    return ((uint32_t)(255 * clampf(r * k + (1 - k) * 0.95f)) << 16) | ((uint32_t)(255 * clampf(g * k + (1 - k) * 0.95f)) << 8) | (uint32_t)(255 * clampf(b * k + (1 - k) * 0.95f));
}

/* ---- made once ---- */
static int tables(void){
    g_px = malloc((size_t)BW * BW * 4); g_row = malloc((size_t)BW * BW); g_lon = malloc((size_t)BW * BW * 2);
    g_edge = malloc((size_t)BW * BW); g_rowfrac = malloc((size_t)BW * BW);
    if(!g_px || !g_row || !g_lon || !g_edge || !g_rowfrac) return 0;
    for(int r = 0; r < ROWS; r++){
        float lm = -1.5707963f + 3.1415927f * (r + 0.5f) / ROWS;
        int n = (int)lroundf(MAXCOL * cosf(lm)); g_ncol[r] = n < 6 ? 6 : n > MAXCOL ? MAXCOL : n;
        for(int c = 0; c < MAXCOL; c++) g_tint[r][c] = (uint8_t)(rnd() * 255);
    }
    for(int j = 0; j < BW; j++) for(int i = 0; i < BW; i++){
        size_t k = (size_t)j * BW + i;
        float X = (i + 0.5f - BW / 2.0f) / BR, Y = -(j + 0.5f - BW / 2.0f) / BR, r2 = X * X + Y * Y;
        if(r2 >= 1){ g_row[k] = -1; continue; }
        float Z = sqrtf(1 - r2), lat = asinf(Y), lon = atan2f(X, Z);
        float rf = (lat + 1.5707963f) / 3.1415927f * ROWS; int row = (int)rf; if(row >= ROWS) row = ROWS - 1;
        g_row[k] = (int8_t)row; g_rowfrac[k] = (uint8_t)((rf - row) * 255);
        g_lon[k] = (uint16_t)((int32_t)lroundf(lon / 6.2831853f * 65536) & 0xFFFF);
        float cov = BR + 0.5f - sqrtf(r2) * BR; if(cov > 1) cov = 1;           /* antialiased rim */
        g_edge[k] = (uint8_t)(255 * cov * (0.45f + 0.55f * powf(Z, 0.6f)));
    }
    memset(&g_dsc, 0, sizeof g_dsc);
    g_dsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_dsc.header.w = g_dsc.header.h = BW; g_dsc.header.stride = BW * 4;
    g_dsc.data = g_px; g_dsc.data_size = (uint32_t)BW * BW * 4;
    return 1;
}
static void tables_free(void){
    free(g_px); free(g_row); free(g_lon); free(g_edge); free(g_rowfrac);
    g_px = NULL; g_row = NULL; g_lon = NULL; g_edge = NULL; g_rowfrac = NULL;
}

/* ---- one frame of the ball: tile shades for this turn, then the pixels ---- */
static const float LX = -0.45f, LY = 0.55f, LZ = 0.70f;     /* the room's light, up and to the left (normalised below) */
static void ball_draw(float rot, uint32_t bg, int fade){   /* fade 0..256: drawn into the picture, never as object opacity */
    float ln = sqrtf(LX * LX + LY * LY + LZ * LZ), lx = LX / ln, ly = LY / ln, lz = LZ / ln;
    for(int r = 0; r < ROWS; r++){
        float lm = -1.5707963f + 3.1415927f * (r + 0.5f) / ROWS, cl = cosf(lm), sl = sinf(lm);
        for(int c = 0; c < g_ncol[r]; c++){
            float lo = 6.2831853f * (c + 0.5f) / g_ncol[r] + rot;
            float nx = cl * sinf(lo), ny = sl, nz = cl * cosf(lo), tint = g_tint[r][c] / 255.0f;
            float nl = nx * lx + ny * ly + nz * lz + (tint - 0.5f) * 0.15f; if(nl < 0) nl = 0;
            float spec = powf(nl, 24), v = 8 + nl * nl * 62 + tint * 16 + spec * 60; if(v > 97) v = 97;
            float L = v / 100 * 255;
            uint8_t *s = g_shade[r][c];
            if(g_tint[r][c] > 247 && nz > 0.3f){                                /* a few tiles catch the rainbow */
                uint32_t rb = rainbow(tint * 7.3f - floorf(tint * 7.3f), 0.8f);
                s[2] = (uint8_t)((2 * L + ((rb >> 16) & 255)) / 3); s[1] = (uint8_t)((2 * L + ((rb >> 8) & 255)) / 3); s[0] = (uint8_t)((2 * L + (rb & 255)) / 3);
            } else { s[2] = (uint8_t)(L * 0.96f); s[1] = (uint8_t)(L * 0.98f); s[0] = (uint8_t)L; }   /* silvered, faintly cool */
        }
    }
    uint16_t turn = (uint16_t)((int32_t)lroundf(rot / 6.2831853f * 65536) & 0xFFFF);
    uint8_t bb = (uint8_t)(bg & 255), bgg = (uint8_t)((bg >> 8) & 255), br = (uint8_t)((bg >> 16) & 255);
    for(size_t k = 0; k < (size_t)BW * BW; k++){
        uint8_t *o = g_px + k * 4; int row = g_row[k];
        if(row < 0){ o[0] = bb; o[1] = bgg; o[2] = br; o[3] = 255; continue; }
        uint16_t lon = (uint16_t)(g_lon[k] - turn);
        uint32_t cf = (uint32_t)lon * (uint32_t)g_ncol[row];                  /* which tile, and where in it */
        int col = (int)(cf >> 16), fx = (int)((cf >> 8) & 255), fy = g_rowfrac[k];
        int grout = fx < 22 || fy < 26;                                        /* the dark gaps between tiles */
        const uint8_t *s = g_shade[row][col % g_ncol[row]];
        int e = g_edge[k], m = grout ? 70 : 256;
        for(int ch = 0; ch < 3; ch++){                                         /* the tile, darker at the rim; never below the room */
            int v = ((s[ch] * m >> 8) * e >> 8) * fade >> 8, bgc = ch == 0 ? bb : ch == 1 ? bgg : br;
            o[ch] = (uint8_t)(v > bgc ? v : bgc);
        }
        o[3] = 255;
    }
}

/* ---- the timeline ---- */
static void place(lv_obj_t *o, int cx, int cy, int r){ if(r < 1) r = 1; lv_obj_set_pos(o, cx - r, cy - r); lv_obj_set_size(o, 2 * r, 2 * r); }
static void splash_frame(int ms){
    float t = ms / (float)SP_MS;
    float rot = -0.6f + eio(seg(t, 0.2f, 1)) * 5.0f;

    /* 1. the lights curl in */
    float g = seg(t, 0, 0.42f), gone = 1 - seg(t, 0.36f, 0.46f);
    for(int i = 0; i < NSPECK; i++){
        float p = ease(seg(g, g_sk_d[i], 1));
        if(p >= 1 || gone <= 0){ lv_obj_add_flag(g_sp.speck[i], LV_OBJ_FLAG_HIDDEN); continue; }
        float ang = g_sk_a[i] + g_sk_spin[i] * (1 - p) * 1.4f, r = g_sk_r[i] * (1 - p);
        int x = (int)(BCX + cosf(ang) * r + g_sk_ex[i] * BR * 0.85f * p), y = (int)(BCY + sinf(ang) * r + g_sk_ey[i] * BR * 0.85f * p);
        float tw = 0.55f + 0.45f * sinf(g_sk_tw[i] + t * 40);
        lv_obj_remove_flag(g_sp.speck[i], LV_OBJ_FLAG_HIDDEN);
        place(g_sp.speck[i], x, y, 2 + (i % 3));
        lv_obj_set_style_opa(g_sp.speck[i], (lv_opa_t)(255 * gone * tw * clampf(p * 6)), 0);
    }

    /* 2. the ball */
    float b = ease(seg(t, 0.33f, 0.5f)), out = 1 - seg(t, 0.9f, 1);
    if(b > 0){
        /* the fade is drawn into the picture itself: an opaque copy every frame, never a blended one (that blend made
         * the hand-over from the lights to the ball stutter on the Disc) */
        ball_draw(rot, lv_color_to_u32(TC(FIXED_SPLASH_BG)) & 0xFFFFFF, (int)(256 * b));
        lv_image_cache_drop(&g_dsc);
        lv_obj_invalidate(g_sp.ball);
        lv_obj_remove_flag(g_sp.ball, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(g_sp.wire, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(g_sp.wire, (lv_opa_t)(70 * b), 0);
    }

    /* 3. the spots it throws: each from a tile, travelling sideways across the walls as the ball turns */
    float sa = seg(t, 0.48f, 0.6f) * out;
    float ln = sqrtf(LX * LX + LY * LY + LZ * LZ);
    for(int i = 0; i < NSPOT; i++){
        float lo = g_spot_lon[i] + rot, la = g_spot_lat[i];
        float X = cosf(la) * sinf(lo), Y = sinf(la), Z = cosf(la) * cosf(lo);
        float sx = BCX + X * 185, sy = BCY + 20 - Y * 165 + Z * 12, d = hypotf(sx - BCX, sy - BCY);
        float edge = clampf(1.15f - fabsf(X) * 1.1f), lit = 0.5f + 0.5f * Z * Z;
        float br = Z < 0.15f || d < BR + 10 ? 0 : lit * edge * sa * (0.7f + 0.3f * sinf(i * 7.0f + rot * 9));
        (void)ln;
        if(br <= 0.03f){ lv_obj_add_flag(g_sp.spot[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(g_sp.spot[i], LV_OBJ_FLAG_HIDDEN);
        int w = (int)(2 + Z * 3);
        lv_obj_set_pos(g_sp.spot[i], (int)sx - 2 * w, (int)sy - w); lv_obj_set_size(g_sp.spot[i], 4 * w, 2 * w);   /* stretched sideways, along their travel */
        lv_obj_set_style_opa(g_sp.spot[i], (lv_opa_t)(255 * br), 0);
    }

    /* glints: the three brightest tiles facing the light get a star */
    {
        float best[3] = { 0, 0, 0 }; int bx[3] = { 0 }, by[3] = { 0 };
        for(int r = 2; r < ROWS - 2; r++){
            float lm = -1.5707963f + 3.1415927f * (r + 0.5f) / ROWS;
            for(int c = 0; c < g_ncol[r]; c++){
                float lo = 6.2831853f * (c + 0.5f) / g_ncol[r] + rot, nx = cosf(lm) * sinf(lo), ny = sinf(lm), nz = cosf(lm) * cosf(lo);
                if(nz < 0.2f) continue;
                float s = (nx * LX + ny * LY + nz * LZ) / ln;
                for(int q = 0; q < 3; q++) if(s > best[q]){
                    for(int z = 2; z > q; z--){ best[z] = best[z - 1]; bx[z] = bx[z - 1]; by[z] = by[z - 1]; }
                    best[q] = s; bx[q] = BCX + (int)(nx * BR); by[q] = BCY - (int)(ny * BR); break;
                }
            }
        }
        for(int q = 0; q < 3; q++){
            float k = clampf((best[q] - 0.9f) * 10) * b * out;
            if(k <= 0.05f){ lv_obj_add_flag(g_sp.glint[q], LV_OBJ_FLAG_HIDDEN); continue; }
            lv_obj_remove_flag(g_sp.glint[q], LV_OBJ_FLAG_HIDDEN);
            place(g_sp.glint[q], bx[q], by[q], 2 + q % 2);
            lv_obj_set_style_opa(g_sp.glint[q], (lv_opa_t)(255 * k), 0);
        }
    }

    /* 4. the name rises in beneath */
    float w = ease(seg(t, 0.62f, 0.8f));
    lv_obj_set_style_opa(g_sp.word, (lv_opa_t)(255 * w), 0);
    lv_obj_set_y(g_sp.word, 254 + (int)(8 * (1 - w)));

    /* 5. hand-off to Home */
    lv_obj_set_style_opa(g_sp.root, (lv_opa_t)(255 * out), 0);
}

static void splash_anim_cb(void *var, int32_t v){ (void)var; if(g_sp.root) splash_frame((int)v); }
static void splash_drawn_cb(lv_event_t *e){ (void)e; g_sp.frames++; }   /* a frame actually drawn to the screen */
static void splash_done(lv_anim_t *a){
    (void)a;
    lv_display_remove_event_cb_with_user_data(lv_display_get_default(), splash_drawn_cb, NULL);
    fprintf(stderr, "splash: %d frames drawn in %u ms\n", g_sp.frames, (unsigned)lv_tick_elaps(g_sp.t0));   /* how smoothly this Disc drew it */
    if(g_sp.root) lv_obj_delete(g_sp.root);
    memset(&g_sp, 0, sizeof g_sp);
    tables_free();
}
static lv_obj_t *dot(lv_obj_t *parent, lv_color_t c){
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o); kit_keep(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}
static void build(void){
    g_rs = 7;
    g_sp.root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_sp.root); kit_keep(g_sp.root);
    lv_obj_remove_flag(g_sp.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_sp.root, 360, 360);
    lv_obj_set_style_bg_color(g_sp.root, TC(FIXED_SPLASH_BG), 0);
    lv_obj_set_style_bg_opa(g_sp.root, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_sp.root, LV_OBJ_FLAG_CLICKABLE);             /* swallow taps during the splash */

    for(int i = 0; i < NSPOT; i++){                                 /* the spots: behind the ball */
        float rr = rnd(); int row = 3 + (int)(rr * (ROWS - 6));
        g_spot_lat[i] = -1.5707963f + 3.1415927f * (row + 0.5f) / ROWS;
        g_spot_lon[i] = rnd() * 6.2831853f;
        g_spot_rgb[i] = rnd() < 0.3f ? rainbow(rnd(), 0.75f) : (uint32_t)lv_color_to_u32(TC(FIXED_SPLASH_3)) & 0xFFFFFF;
        g_sp.spot[i] = dot(g_sp.root, theme_color_from_rgb(g_spot_rgb[i]));
    }
    g_sp.wire = lv_obj_create(g_sp.root);
    lv_obj_remove_style_all(g_sp.wire); kit_keep(g_sp.wire);
    lv_obj_set_pos(g_sp.wire, BCX, 0); lv_obj_set_size(g_sp.wire, 1, BCY - BR);
    lv_obj_set_style_bg_color(g_sp.wire, TC(FIXED_SPLASH_3), 0); lv_obj_set_style_bg_opa(g_sp.wire, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_sp.wire, LV_OBJ_FLAG_HIDDEN);
    g_sp.ball = lv_image_create(g_sp.root); kit_keep(g_sp.ball);
    lv_image_set_src(g_sp.ball, &g_dsc);
    lv_obj_set_pos(g_sp.ball, BCX - BW / 2, BCY - BW / 2);
    lv_obj_add_flag(g_sp.ball, LV_OBJ_FLAG_HIDDEN);
    for(int q = 0; q < 3; q++) g_sp.glint[q] = dot(g_sp.root, TC(FIXED_SPLASH_3));
    for(int i = 0; i < NSPECK; i++){                                /* the lights, in front */
        g_sk_a[i] = rnd() * 6.2831853f; g_sk_r[i] = 200 + rnd() * 60; g_sk_spin[i] = (rnd() < 0.5f ? -1 : 1) * (0.6f + rnd() * 0.9f);
        g_sk_d[i] = rnd() * 0.12f; g_sk_ex[i] = rnd() - 0.5f; g_sk_ey[i] = rnd() - 0.5f; g_sk_tw[i] = rnd() * 6.2831853f;
        g_sp.speck[i] = dot(g_sp.root, theme_color_from_rgb(rainbow((i * 37 % 360) / 360.0f, 0.9f)));
    }

    /* "Disco!" in Monoton; the "!" in the accent */
    g_sp.word = lv_obj_create(g_sp.root);
    lv_obj_remove_style_all(g_sp.word); kit_keep(g_sp.word);
    lv_obj_set_size(g_sp.word, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_sp.word, LV_FLEX_FLOW_ROW);
    const char *T[2] = { "Disco", "!" };
    for(int i = 0; i < 2; i++){
        lv_obj_t *l = lv_label_create(g_sp.word); kit_keep(l);
        lv_label_set_text(l, T[i]);
        lv_obj_set_style_text_font(l, &diskos_splash_disco_46, 0);
        lv_obj_set_style_text_color(l, i ? ui_current_accent() : TC(FIXED_SPLASH_3), 0);
    }
    lv_obj_update_layout(g_sp.word);
    lv_obj_set_x(g_sp.word, 180 - lv_obj_get_width(g_sp.word) / 2);
}

/* A plain cover in the splash's colour, put up before the first frame is drawn: Home is built and painted during
 * startup, well before the splash can start (the loop isn't running yet), and showed for a moment without it. */
static lv_obj_t *g_cover;
void splash_cover(void){
    if(g_cover || g_sp.root) return;
    g_cover = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_cover); kit_keep(g_cover);
    lv_obj_remove_flag(g_cover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_cover, 360, 360);
    lv_obj_set_style_bg_color(g_cover, TC(FIXED_SPLASH_BG), 0); lv_obj_set_style_bg_opa(g_cover, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_cover, LV_OBJ_FLAG_CLICKABLE);
}
static void cover_off(void){ if(g_cover){ lv_obj_delete(g_cover); g_cover = NULL; } }
void splash_start(void){
    if(g_sp.root){ cover_off(); return; }                           /* one splash at a time */
    if(!tables()){ tables_free(); cover_off(); return; }            /* no memory for the ball: no splash, straight to Home */
    build();
    cover_off();                                                    /* the splash's own black takes over from here */
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
/* host renders: the splash standing at `ms` (no animation, nothing deleted) */
void splash_test_frame(int ms){
    if(!g_sp.root){ if(!tables()){ tables_free(); return; } build(); }
    splash_frame(ms);
}
