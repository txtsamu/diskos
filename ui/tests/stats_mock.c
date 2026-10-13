/* SPDX-License-Identifier: GPL-3.0-or-later */
/* "Listening Stats" concept mockups: the app-style landing page and the stat cards that follow it. Plain LVGL with the
 * theme's own roles (TC/TF), so every theme renders it in its own colours. Included by host_render.c (mode stats-<n>).
 * Numbers and names are sample data. Album covers are placeholders (gradient squares). */
#include "lvgl/lvgl.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *sm_box(lv_obj_t *p, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t o){
    lv_obj_t *b = lv_obj_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, r, 0); lv_obj_set_style_bg_color(b, c, 0); lv_obj_set_style_bg_opa(b, o, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}
static lv_obj_t *sm_text(lv_obj_t *p, const char *s, int x, int y, int w, const lv_font_t *f, lv_color_t c, lv_text_align_t al){
    lv_obj_t *l = lv_label_create(p); lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, al, 0); lv_obj_set_width(l, w);
    if(!strchr(s, '\n')){ lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_height(l, lv_font_get_line_height(f)); }
    lv_obj_set_pos(l, x, y);
    return l;
}
static lv_obj_t *sm_center(lv_obj_t *p, const char *s, int y, int w, const lv_font_t *f, lv_color_t c){
    return sm_text(p, s, 180 - w / 2, y, w, f, c, LV_TEXT_ALIGN_CENTER);
}
static void sm_arc(lv_obj_t *p, int cx, int cy, int r, int w, int a0, int a1, lv_color_t c, lv_opa_t o, int round){
    lv_obj_t *a = lv_arc_create(p); lv_obj_remove_style_all(a);
    lv_obj_set_size(a, 2 * r, 2 * r); lv_obj_set_pos(a, cx - r, cy - r); lv_arc_set_bg_angles(a, a0, a1);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN); lv_obj_set_style_arc_color(a, c, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, o, LV_PART_MAIN); lv_obj_set_style_arc_rounded(a, round, LV_PART_MAIN);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
}
/* page dots on the right rim: n dots, the active one longer */
static void sm_dots(lv_obj_t *root, int n, int active){
    int y = 180 - (n * 12) / 2;
    for(int i = 0; i < n; i++){
        int on = i == active;
        sm_box(root, 338, y, 6, on ? 16 : 6, 3, on ? TC(ACCENT_PRIMARY) : TC(TEXT_SECONDARY), on ? LV_OPA_COVER : LV_OPA_50);
        y += on ? 22 : 12;
    }
}
static void sm_canvas(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
}
/* a placeholder album cover: a rounded gradient square */
static void sm_cover(lv_obj_t *p, int x, int y, int s, uint32_t a, uint32_t b){
    lv_obj_t *c = sm_box(p, x, y, s, s, 6, lv_color_hex(a), LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(c, lv_color_hex(b), 0); lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_VER, 0);
}
static lv_obj_t *sm_chip(lv_obj_t *p, int x, int y, int w, const char *big, const char *small){
    lv_obj_t *c = sm_box(p, x, y, w, 62, 14, TC(SURFACE), LV_OPA_COVER);
    lv_obj_set_style_border_width(c, 1, 0); lv_obj_set_style_border_color(c, TC(BORDER), 0);
    sm_text(c, big, 0, 8, w, TF(UI_22), TC(TEXT_PRIMARY), LV_TEXT_ALIGN_CENTER);
    sm_text(c, small, 0, 36, w, TF(UI_12), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    return c;
}
/* the small period switch at the top: Week / Month / Year, one selected */
static void sm_period(lv_obj_t *root, int sel){
    static const char *P[3] = { "Week", "Month", "Year" };
    lv_obj_t *bar = sm_box(root, 92, 34, 176, 30, 15, TC(SURFACE), LV_OPA_COVER);
    for(int i = 0; i < 3; i++){
        if(i == sel) sm_box(bar, 2 + i * 58, 2, 56, 26, 13, TC(ACCENT_PRIMARY), LV_OPA_COVER);
        sm_text(bar, P[i], 2 + i * 58, 6, 56, TF(UI_14), i == sel ? TC(ON_ACCENT) : TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    }
}

/* 0: landing page, the same layout as Album Roulette and Boomshine */
static void mock_landing(lv_obj_t *root){
    sm_canvas(root);
    lv_obj_t *art = sm_box(root, 116, 38, 128, 128, LV_RADIUS_CIRCLE, TC(SURFACE), LV_OPA_COVER);
    lv_obj_set_style_border_width(art, 2, 0); lv_obj_set_style_border_color(art, TC(TEXT_SECONDARY), 0);
    static const int H[5] = { 22, 38, 30, 52, 64 };                       /* a little rising bar chart */
    for(int i = 0; i < 5; i++)
        sm_box(art, 26 + i * 15, 92 - H[i], 10, H[i], 3, TC(ACCENT_PRIMARY), i == 4 ? LV_OPA_COVER : (lv_opa_t)(110 + i * 30));
    sm_box(art, 22, 96, 80, 2, 1, TC(TEXT_SECONDARY), LV_OPA_60);
    sm_center(root, "Listening Stats", 181, 272, TF(UI_22), TC(TEXT_PRIMARY));
    sm_center(root, "Your music, in numbers.", 215, 260, TF(UI_16), TC(ACCENT_PRIMARY));
    sm_center(root, "Top artists, albums and songs,\nby week, month or year.", 243, 258, TF(UI_16), TC(TEXT_SECONDARY));
    lv_obj_t *b = sm_box(root, 93, 296, 174, 42, LV_RADIUS_CIRCLE, TC(ACCENT_PRIMARY), LV_OPA_COVER);
    sm_text(b, "Show me", 0, 11, 174, TF(UI_18), TC(ON_ACCENT), LV_TEXT_ALIGN_CENTER);
}

/* 1: the month at a glance */
static void stats_overview(lv_obj_t *root){
    sm_canvas(root);
    sm_period(root, 1);
    sm_center(root, "October", 80, 240, TF(UI_16), TC(TEXT_SECONDARY));
    sm_arc(root, 180, 180, 172, 6, 0, 360, TC(SURFACE), LV_OPA_COVER, 0);           /* the rim: this month vs. last */
    sm_arc(root, 180, 180, 172, 6, 270, 270 + 252, TC(ACCENT_PRIMARY), LV_OPA_COVER, 1);
    sm_center(root, "38h 12m", 104, 280, TF(UI_44), TC(TEXT_PRIMARY));
    sm_center(root, "of music this month", 158, 260, TF(UI_16), TC(TEXT_SECONDARY));
    sm_center(root, LV_SYMBOL_UP "  4h more than September", 184, 280, TF(UI_14), TC(ACCENT_PRIMARY));
    sm_chip(root, 46, 222, 84, "612", "plays");
    sm_chip(root, 138, 222, 84, "148", "songs");
    sm_chip(root, 230, 222, 84, "12", "day streak");
    sm_center(root, "Swipe up for your favourites", 300, 220, TF(UI_12), TC(TEXT_MUTED));
    sm_dots(root, 5, 0);
}

/* 2: top artist */
static void stats_artist(lv_obj_t *root){
    sm_canvas(root);
    sm_center(root, "TOP ARTIST  " LV_SYMBOL_BULLET "  OCTOBER", 36, 240, TF(UI_12), TC(ACCENT_PRIMARY));
    lv_obj_t *disc = sm_box(root, 130, 60, 100, 100, LV_RADIUS_CIRCLE, TC(ACCENT_PRIMARY), LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(disc, TC(SURFACE_STRONG), 0); lv_obj_set_style_bg_grad_dir(disc, LV_GRAD_DIR_VER, 0);
    sm_text(disc, "M", 0, 24, 100, TF(UI_44), TC(ON_ACCENT), LV_TEXT_ALIGN_CENTER);
    lv_obj_t *badge = sm_box(root, 206, 134, 30, 30, LV_RADIUS_CIRCLE, TC(CANVAS), LV_OPA_COVER);
    lv_obj_set_style_border_width(badge, 2, 0); lv_obj_set_style_border_color(badge, TC(ACCENT_PRIMARY), 0);
    sm_text(badge, "1", 0, 4, 26, TF(UI_16), TC(TEXT_PRIMARY), LV_TEXT_ALIGN_CENTER);
    sm_center(root, "The Midnight", 168, 280, TF(UI_28), TC(TEXT_PRIMARY));
    sm_center(root, "214 plays in 15h 40m", 204, 260, TF(UI_14), TC(TEXT_SECONDARY));
    sm_box(root, 70, 234, 220, 1, 0, TC(BORDER), LV_OPA_COVER);
    static const char *N[3] = { "Kavinsky", "Timecop1983", "FM-84" };
    static const char *C[3] = { "96", "71", "58" };
    for(int i = 0; i < 3; i++){
        int y = 244 + i * 28, x0 = 86, w = 188;
        char r[4]; snprintf(r, sizeof r, "%d", i + 2);
        sm_text(root, r, x0, y, 20, TF(UI_16), TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_LEFT);
        sm_text(root, N[i], x0 + 24, y, w - 70, TF(UI_16), TC(TEXT_PRIMARY), LV_TEXT_ALIGN_LEFT);
        sm_text(root, C[i], x0 + w - 46, y, 46, TF(UI_14), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_RIGHT);
    }
    sm_dots(root, 5, 1);
}

/* 3: top albums */
static void stats_albums(lv_obj_t *root){
    sm_canvas(root);
    sm_center(root, "TOP ALBUMS  " LV_SYMBOL_BULLET "  OCTOBER", 36, 240, TF(UI_12), TC(ACCENT_PRIMARY));
    static const char *T[5] = { "Endless Summer", "OutRun", "Night Drive", "Atlas", "Days of Thunder" };
    static const char *A[5] = { "The Midnight", "Kavinsky", "Timecop1983", "FM-84", "The Midnight" };
    static const char *P[5] = { "88", "61", "47", "39", "33" };
    static const uint32_t G[5][2] = { { 0xF97316, 0x7C2D12 }, { 0xE11D48, 0x312E81 }, { 0x22D3EE, 0x1E3A8A },
                                      { 0xA855F7, 0x1F2937 }, { 0xFACC15, 0xB45309 } };
    static const int X[5] = { 62, 46, 42, 46, 62 }, W[5] = { 236, 268, 276, 268, 236 };
    for(int i = 0; i < 5; i++){
        int y = 62 + i * 52, x = X[i], w = W[i];
        if(i == 0){ lv_obj_t *hl = sm_box(root, x - 6, y - 4, w + 12, 50, 12, TC(SURFACE), LV_OPA_COVER);
                    lv_obj_set_style_border_width(hl, 1, 0); lv_obj_set_style_border_color(hl, TC(ACCENT_PRIMARY), 0); }
        sm_cover(root, x, y, 42, G[i][0], G[i][1]);
        sm_text(root, T[i], x + 52, y + 2, w - 100, TF(UI_16), TC(TEXT_PRIMARY), LV_TEXT_ALIGN_LEFT);
        sm_text(root, A[i], x + 52, y + 22, w - 100, TF(UI_12), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_LEFT);
        sm_text(root, P[i], x + w - 44, y + 4, 40, TF(UI_18), i == 0 ? TC(ACCENT_PRIMARY) : TC(TEXT_PRIMARY), LV_TEXT_ALIGN_RIGHT);
        sm_text(root, "plays", x + w - 44, y + 24, 40, TF(UI_10), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_RIGHT);
    }
    sm_dots(root, 5, 2);
}

/* 4: on repeat - the most played song */
static void stats_song(lv_obj_t *root){
    sm_canvas(root);
    sm_center(root, "ON REPEAT  " LV_SYMBOL_BULLET "  OCTOBER", 36, 240, TF(UI_12), TC(ACCENT_PRIMARY));
    sm_arc(root, 180, 130, 70, 4, 0, 360, TC(SURFACE_STRONG), LV_OPA_COVER, 0);
    sm_arc(root, 180, 130, 70, 4, 300, 300 + 290, TC(ACCENT_PRIMARY), LV_OPA_COVER, 1);   /* a "loop" around the cover */
    sm_cover(root, 134, 84, 92, 0xF97316, 0x7C2D12);
    lv_obj_t *loop = sm_box(root, 222, 66, 34, 34, LV_RADIUS_CIRCLE, TC(ACCENT_PRIMARY), LV_OPA_COVER);
    sm_text(loop, LV_SYMBOL_LOOP, 0, 8, 34, TF(UI_16), TC(ON_ACCENT), LV_TEXT_ALIGN_CENTER);
    sm_center(root, "Sunset", 212, 260, TF(UI_28), TC(TEXT_PRIMARY));
    sm_center(root, "The Midnight", 248, 240, TF(UI_16), TC(TEXT_SECONDARY));
    sm_center(root, "41", 276, 120, TF(UI_32), TC(ACCENT_PRIMARY));
    sm_center(root, "times this month", 314, 200, TF(UI_12), TC(TEXT_SECONDARY));
    sm_dots(root, 5, 3);
}

/* 5: listening clock - when you listen, as a 24-hour ring around the round screen */
static void stats_clock(lv_obj_t *root){
    sm_canvas(root);
    static const int V[24] = { 4, 2, 0, 0, 0, 0, 2, 10, 22, 14, 6, 6, 9, 12, 8, 7, 10, 20, 24, 18, 26, 30, 20, 9 };
    int peak = 21;
    for(int h = 0; h < 24; h++){
        int a0 = 270 + h * 15 + 2, a1 = a0 + 11;
        sm_arc(root, 180, 180, 168, 40, a0, a1, TC(SURFACE), LV_OPA_COVER, 0);       /* the empty track */
        int w = V[h] ? 4 + V[h] * 6 / 5 : 0;
        if(w) sm_arc(root, 180, 180, 168, w, a0, a1, TC(ACCENT_PRIMARY), h == peak ? LV_OPA_COVER : LV_OPA_70, 0);
    }
    sm_text(root, "0", 172, 106, 16, TF(UI_12), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    sm_text(root, "6", 240, 172, 16, TF(UI_12), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    sm_text(root, "12", 168, 240, 24, TF(UI_12), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    sm_text(root, "18", 100, 172, 24, TF(UI_12), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    sm_center(root, "You listen most at", 138, 160, TF(UI_12), TC(TEXT_SECONDARY));
    sm_center(root, "9 pm", 154, 140, TF(UI_32), TC(TEXT_PRIMARY));
    sm_center(root, "Night owl", 196, 140, TF(UI_14), TC(ACCENT_PRIMARY));
}

void stats_mock(lv_obj_t *root, int which){
    switch(which){
        case 0: mock_landing(root); break;
        case 1: stats_overview(root); break;
        case 2: stats_artist(root); break;
        case 3: stats_albums(root); break;
        case 4: stats_song(root); break;
        default: stats_clock(root); break;
    }
}
