/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SongSphere, built in (2.2.0): music by mood. The same app as the add-on (ui/songsphere: the sphere and its data are
 * shared), as one screen of mq_ui with four pages:
 *   first page  - Play Music / Place Music, how much is placed
 *   Place Music - albums or artists (most played first), curved list; a tap opens the tag page
 *   tag page    - drag the marker to where it sits (Energetic..Calm, Dark..Bright), Save / Remove
 *   Play a mood - drag to a spot, Play: up to 40 songs near it, played in Shuffle (and kept as a playlist, if asked)
 * The tag page also opens straight from the Library's album / artist hold menu and from the track popup on Music
 * (a song: its own spot, else its album's, else its artist's). Back goes up one page, then leaves.
 * Placements live in /usr/data/songsphere.db; the add-on's file is taken over the first time. Settings > Playback >
 * SongSphere turns the whole thing off (the menu actions, the mood pill, the app in Apps). */
#include "screens.h"
#include "theme.h"
#include "config.h"
#include "musicdb.h"
#include "songsphere/sphere.h"
#include "songsphere/ssdb.h"
#include "sqlite3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SS_DB_NEW "/usr/data/songsphere.db"
#define SS_DB_OLD "/usr/data/apps/songsphere/sphere.db"
LV_FONT_DECLARE(font_inter_medium_12)
LV_FONT_DECLARE(font_inter_medium_14)
LV_FONT_DECLARE(font_inter_medium_16)
LV_FONT_DECLARE(font_inter_medium_20)
LV_FONT_DECLARE(font_inter_bold_16)
LV_FONT_DECLARE(font_inter_bold_18)
LV_FONT_DECLARE(font_inter_bold_22)
static lv_font_t F12, F14, F16, F20, B16, B18, B22;   /* Inter, with the theme's symbols behind it */

enum { P_LAND, P_LIST, P_TAG, P_PLAY };
static lv_obj_t *g_root, *g_page, *g_list, *g_dot[11], *g_mood_lbl, *g_sheet, *g_fade[3];
static int g_pg, g_kind = SS_ALBUM, g_shown, g_cur = -1, g_nfade, g_dot_cur = -2, g_ext, g_ok;
static ss_item_t *g_items; static int g_nitems;
static ss_item_t g_one; static int g_one_kind; static char g_one_key[300];   /* the tag page opened from a menu */
static float g_play_x, g_play_y;
static int *g_pick, g_npick; static float g_pick_r;

static lv_color_t ACC(void){ return ui_current_accent(); }
int sphere_ui_enabled(void){ return cfg_get_int("songsphere", 1); }

/* ---- data ---- */
/* The add-on's placements, moved once with SQLite's own backup (never a raw byte copy): written to a temporary file,
 * checked, then renamed into place. Any failure leaves no new file behind, so the next start simply tries again; until
 * then SongSphere works on the add-on's file itself, so nothing ever looks lost. 1 = the new file holds them. */
static int db_migrate(void){
    sqlite3 *src = NULL, *dst = NULL; int ok = 0;
    unlink(SS_DB_NEW ".tmp");
    if(sqlite3_open_v2(SS_DB_OLD, &src, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK &&
       sqlite3_open_v2(SS_DB_NEW ".tmp", &dst, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(src, 1500);
        sqlite3_backup *bk = sqlite3_backup_init(dst, "main", src, "main");
        if(bk){ int rc = sqlite3_backup_step(bk, -1); ok = sqlite3_backup_finish(bk) == SQLITE_OK && rc == SQLITE_DONE; }
        if(ok){                                                                 /* the copy reads back whole */
            sqlite3_stmt *st; ok = 0;
            if(sqlite3_prepare_v2(dst, "PRAGMA quick_check;", -1, &st, NULL) == SQLITE_OK){
                ok = sqlite3_step(st) == SQLITE_ROW && !strcmp((const char *)sqlite3_column_text(st, 0), "ok");
                sqlite3_finalize(st);
            }
        }
    }
    if(src) sqlite3_close(src);
    if(dst && sqlite3_close(dst) != SQLITE_OK) ok = 0;
    if(ok && rename(SS_DB_NEW ".tmp", SS_DB_NEW) != 0) ok = 0;
    if(!ok) unlink(SS_DB_NEW ".tmp");
    return ok;
}
static int db_ready(void){
    if(g_ok) return 1;
    const char *path = SS_DB_NEW;
    if(access(SS_DB_NEW, F_OK) && !access(SS_DB_OLD, R_OK) && !db_migrate())
        path = SS_DB_OLD;                                                       /* not moved yet: use it where it is */
    ssdb_paths(NULL, path);
    g_ok = ssdb_open();
    return g_ok;
}

/* ---- small builders ---- */
static void fonts_init(void){
    static int done; if(done) return; done = 1;
    F12 = font_inter_medium_12; F12.fallback = TF(UI_12);
    F14 = font_inter_medium_14; F14.fallback = TF(UI_14);
    F16 = font_inter_medium_16; F16.fallback = TF(UI_16);
    F20 = font_inter_medium_20; F20.fallback = TF(UI_20);
    B16 = font_inter_bold_16;   B16.fallback = TF(UI_16);
    B18 = font_inter_bold_18;   B18.fallback = TF(UI_18);
    B22 = font_inter_bold_22;   B22.fallback = TF(UI_22);
}
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t o){
    lv_obj_t *b = lv_obj_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, r, 0); lv_obj_set_style_bg_color(b, c, 0); lv_obj_set_style_bg_opa(b, o, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}
static lv_obj_t *text(lv_obj_t *p, const char *s, int x, int y, int w, const lv_font_t *f, lv_color_t c, lv_text_align_t al){
    lv_obj_t *l = lv_label_create(p); lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, al, 0); lv_obj_set_width(l, w);
    if(!strchr(s, '\n')){ lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_height(l, lv_font_get_line_height(f)); }
    lv_obj_set_pos(l, x, y); lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
static lv_obj_t *ctext(lv_obj_t *p, const char *s, int y, int w, const lv_font_t *f, lv_color_t c){
    return text(p, s, (lv_obj_get_style_width(p, LV_PART_MAIN) - w) / 2, y, w, f, c, LV_TEXT_ALIGN_CENTER);
}
static lv_obj_t *button(lv_obj_t *p, const char *label, int cx, int y, int w, int h, int primary, lv_event_cb_t cb, void *ud){
    lv_obj_t *b = box(p, cx - w / 2, y, w, h, LV_RADIUS_CIRCLE, primary ? ACC() : TC(SURFACE_RAISED), LV_OPA_COVER);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(b, h >= 40 ? 10 : 7);
    lv_obj_set_style_bg_color(b, primary ? ACC() : TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, primary ? LV_OPA_80 : LV_OPA_COVER, LV_STATE_PRESSED);
    if(!primary){ lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, TC(BORDER), 0); }
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, h >= 44 ? &B18 : (h >= 34 ? &B16 : &F14), 0);
    lv_obj_set_style_text_color(l, primary ? theme_on_color(ACC()) : TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}
static lv_obj_t *glass(lv_obj_t *s, int y, int w, int h){ return box(s, 180 - w / 2, y, w, h, LV_RADIUS_CIRCLE, TC(CANVAS), LV_OPA_50); }
static lv_color_t mood_col(float x, float y){ uint8_t c[3]; sphere_mood_rgb(x, y, c); return theme_color_from_rgb(((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2]); }
lv_color_t sphere_rgb(uint32_t rgb){ return theme_color_from_rgb(rgb); }   /* the sphere's marker / lines, in mq_ui */

static void show(int page);

/* ---- the first page ---- */
static void goto_cb(lv_event_t *e){ show((int)(intptr_t)lv_event_get_user_data(e)); }
static void build_land(lv_obj_t *s){
    lv_obj_t *art = lv_image_create(s);
    const lv_image_dsc_t *pic = sphere_picture(TC(CANVAS), 0);
    if(pic){ lv_image_set_src(art, pic); lv_obj_set_pos(art, 180 - (int)pic->header.w / 2, 180 - (int)pic->header.h / 2); }
    lv_obj_set_style_opa(art, LV_OPA_20, 0);
    lv_obj_remove_flag(art, LV_OBJ_FLAG_CLICKABLE);
    static const struct { float x, y; int px, py, d; } M[5] = {                /* a few placed songs, in the margins */
        { -0.72f, 0.40f, 40, 170, 10 }, { 0.70f, 0.52f, 320, 160, 10 }, { -0.50f, -0.60f, 34, 266, 12 },
        { 0.46f, -0.66f, 326, 266, 12 }, { 0.10f, 0.90f, 232, 24, 8 } };
    for(int i = 0; i < 5; i++){
        lv_color_t c = mood_col(M[i].x, M[i].y);
        lv_obj_t *o = box(s, M[i].px - M[i].d / 2, M[i].py - M[i].d / 2, M[i].d, M[i].d, LV_RADIUS_CIRCLE, c, LV_OPA_80);
        lv_obj_set_style_shadow_width(o, M[i].d + 8, 0); lv_obj_set_style_shadow_color(o, c, 0); lv_obj_set_style_shadow_opa(o, LV_OPA_30, 0);
    }
    ctext(s, "SongSphere", 44, 220, &B22, TC(TEXT_PRIMARY));
    ctext(s, "Place your music by mood.", 78, 270, &F16, TC(TEXT_PRIMARY));
    ctext(s, "Play what fits the moment.", 100, 270, &F16, TC(TEXT_SECONDARY));
    int placed = 0, albums = 0; ssdb_coverage(&placed, &albums);
    lv_obj_t *chip = glass(s, 132, 220, 30);
    char cov[64];
    if(albums == 0) snprintf(cov, sizeof cov, "No library found");
    else snprintf(cov, sizeof cov, "%d of %d albums placed", placed, albums);
    text(chip, cov, 10, (30 - lv_font_get_line_height(&F14)) / 2, 200, &F14, placed ? ACC() : TC(TEXT_PRIMARY), LV_TEXT_ALIGN_CENTER);
    button(s, LV_SYMBOL_PLAY "  Play Music", 180, 182, 252, 60, 1, goto_cb, (void *)(intptr_t)P_PLAY);
    button(s, "Place Music", 180, 254, 252, 54, 0, goto_cb, (void *)(intptr_t)P_LIST);
}

/* ---- Place Music: the curved list ---- */
#define PAGE 120
static void list_dots(void){
    if(!g_list) return;
    int32_t sy = lv_obj_get_scroll_y(g_list), sb = lv_obj_get_scroll_bottom(g_list), tot = sy + sb;
    int idx = (tot <= 24) ? -1 : (int)((sy * 10 + tot / 2) / tot);
    if(idx > 10) idx = 10;
    if(idx == g_dot_cur) return;
    g_dot_cur = idx;
    for(int k = 0; k < 11; k++){
        lv_obj_t *d = g_dot[k]; if(!d) continue;
        if(idx < 0){ lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(d, LV_OBJ_FLAG_HIDDEN);
        int on = k == idx, sz = on ? 8 : 5;
        float yy = 126.0f + 10.8f * k, dy = yy - 180.0f, xx = 180.0f - sqrtf(fmaxf(0.0f, 168.0f * 168.0f - dy * dy));
        lv_obj_set_size(d, sz, sz); lv_obj_set_pos(d, (int)lroundf(xx - sz / 2.0f), (int)lroundf(yy - sz / 2.0f));
        lv_obj_set_style_bg_color(d, on ? ACC() : TC(TEXT_MUTED), 0);
        lv_obj_set_style_bg_opa(d, on ? LV_OPA_COVER : LV_OPA_50, 0);
    }
}
static void list_curve(void){
    if(!g_list) return;
    lv_obj_update_layout(g_list);
    uint32_t n = lv_obj_get_child_count(g_list);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(g_list, i);
        if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < 60 || a.y1 > 380) continue;
        int dy = abs((a.y1 + a.y2) / 2 - 180) + lv_area_get_height(&a) / 2;
        int half = dy < 174 ? (int)sqrtf((float)(174 * 174 - dy * dy)) - 8 : 0;
        int w = 2 * half; if(w > 300) w = 300; if(w < 196) w = 196;
        if(lv_obj_get_width(r) != w){ lv_obj_set_width(r, w);
            for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){ lv_obj_t *l = lv_obj_get_child(r, k);
                if(lv_obj_has_flag(l, LV_OBJ_FLAG_USER_2)) lv_obj_set_width(l, w - 46 - 18); } }
    }
    list_dots();
}
static void list_scroll_cb(lv_event_t *e){ (void)e; list_curve(); }
static void row_cb(lv_event_t *e){ g_cur = (int)(intptr_t)lv_event_get_user_data(e); g_ext = 0; show(P_TAG); }
static void more_cb(lv_event_t *e);
static void list_fill(int from){
    for(int i = from; i < g_nitems && i < from + PAGE; i++){
        ss_item_t *it = &g_items[i];
        lv_obj_t *r = box(g_list, 0, 0, 300, 58, 24, TC(SURFACE_RAISED), it->placed ? LV_OPA_20 : LV_OPA_TRANSP);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_USER_1);
        lv_obj_set_style_bg_color(r, TC(SURFACE_PRESSED), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0); lv_obj_set_style_border_width(r, 1, 0);
        lv_obj_set_style_border_color(r, it->placed ? ACC() : TC(BORDER), 0); lv_obj_set_style_border_opa(r, it->placed ? LV_OPA_20 : LV_OPA_COVER, 0);
        lv_obj_add_event_cb(r, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *d = box(r, 14, 19, 20, 20, LV_RADIUS_CIRCLE, TC(SURFACE), LV_OPA_TRANSP);
        if(it->placed){ lv_obj_set_style_bg_color(d, mood_col(it->x, it->y), 0); lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0); }
        else { lv_obj_set_style_border_width(d, 2, 0); lv_obj_set_style_border_color(d, TC(TEXT_MUTED), 0); }
        char sub[160];
        if(g_kind == SS_ALBUM) snprintf(sub, sizeof sub, "%s", it->sub);
        else snprintf(sub, sizeof sub, "%d song%s", it->songs, it->songs == 1 ? "" : "s");
        lv_obj_add_flag(text(r, it->name, 46, 6, 230, &B18, TC(TEXT_PRIMARY), LV_TEXT_ALIGN_LEFT), LV_OBJ_FLAG_USER_2);
        lv_obj_add_flag(text(r, sub, 46, 30, 230, &F14, TC(TEXT_SECONDARY), LV_TEXT_ALIGN_LEFT), LV_OBJ_FLAG_USER_2);
    }
    g_shown = from + PAGE < g_nitems ? from + PAGE : g_nitems;
    if(g_shown < g_nitems){ lv_obj_t *m = button(g_list, "Show more", 0, 0, 176, 40, 0, more_cb, NULL); lv_obj_set_style_margin_top(m, 4, 0); }
}
static void more_cb(lv_event_t *e){
    lv_obj_delete_async(lv_event_get_target(e));
    list_fill(g_shown); lv_obj_update_layout(g_list); list_curve();
}
static void kind_cb(lv_event_t *e){ int k = (int)(intptr_t)lv_event_get_user_data(e); if(k != g_kind){ g_kind = k; show(P_LIST); } }
static void back_cb(lv_event_t *e){ (void)e; screen_back(); }
static void build_list(lv_obj_t *s){
    free(g_items); g_items = NULL;
    g_nitems = ssdb_items(g_kind, &g_items); if(g_nitems < 0) g_nitems = 0;
    g_dot_cur = -2;
    g_list = lv_obj_create(s); lv_obj_remove_style_all(g_list);
    lv_obj_set_size(g_list, 360, 262); lv_obj_set_pos(g_list, 0, 98);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_list, 4, 0); lv_obj_set_style_pad_top(g_list, 6, 0); lv_obj_set_style_pad_bottom(g_list, 64, 0);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_event_cb(g_list, list_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(g_list, list_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    if(!g_nitems) ctext(s, g_kind == SS_ALBUM ? "No albums in the library" : "No artists in the library", 184, 280, &F16, TC(TEXT_SECONDARY));
    else list_fill(0);
    lv_obj_t *hd = box(s, 0, 0, 360, 104, 0, TC(CANVAS), LV_OPA_COVER);         /* the header; the list fades in under it */
    lv_obj_set_style_bg_grad_color(hd, TC(CANVAS), 0); lv_obj_set_style_bg_main_opa(hd, LV_OPA_COVER, 0); lv_obj_set_style_bg_grad_opa(hd, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_main_stop(hd, 200, 0); lv_obj_set_style_bg_grad_stop(hd, 255, 0); lv_obj_set_style_bg_grad_dir(hd, LV_GRAD_DIR_VER, 0);
    lv_obj_t *b = box(s, 62, 30, 42, 42, LV_RADIUS_CIRCLE, TC(SURFACE_RAISED), LV_OPA_COVER);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(b, 8);
    lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, TC(BORDER), 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_t *bl = lv_label_create(b); lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(bl, &B22, 0); lv_obj_set_style_text_color(bl, TC(TEXT_PRIMARY), 0); lv_obj_center(bl);
    lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *seg = box(s, 114, 30, 176, 42, 21, TC(SURFACE_RAISED), LV_OPA_COVER);
    lv_obj_set_style_border_width(seg, 1, 0); lv_obj_set_style_border_color(seg, TC(BORDER), 0);
    static const char *K[2] = { "Albums", "Artists" };
    for(int i = 0; i < 2; i++){
        int on = (i ? SS_ARTIST : SS_ALBUM) == g_kind;
        lv_obj_t *t = box(seg, 2 + i * 86, 2, 86, 38, 19, on ? ACC() : TC(SURFACE_RAISED), on ? LV_OPA_COVER : LV_OPA_TRANSP);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(t, 7);
        lv_obj_add_event_cb(t, kind_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i ? SS_ARTIST : SS_ALBUM));
        text(t, K[i], 0, (38 - lv_font_get_line_height(&B16)) / 2, 86, &B16, on ? theme_on_color(ACC()) : TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    }
    int placed = 0; for(int i = 0; i < g_nitems; i++) placed += g_items[i].placed;
    char cov[64]; snprintf(cov, sizeof cov, "%d placed  \xE2\x80\xA2  %d total", placed, g_nitems);
    ctext(s, cov, 78, 220, &F14, TC(TEXT_SECONDARY));
    for(int k = 0; k < 11; k++){
        g_dot[k] = box(s, 0, 0, 5, 5, LV_RADIUS_CIRCLE, TC(TEXT_MUTED), LV_OPA_50);
        lv_obj_add_flag(g_dot[k], LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    }
    list_curve();
}

/* ---- the sphere pages ---- */
static void fade_set(void *o, int32_t v){ lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }
static void fade_to(lv_opa_t to){
    for(int i = 0; i < g_nfade; i++){
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_fade[i]); lv_anim_set_exec_cb(&a, fade_set);
        lv_anim_set_values(&a, lv_obj_get_style_opa(g_fade[i], 0), to); lv_anim_set_duration(&a, 140); lv_anim_start(&a);
    }
}
static void drag_cb(int phase, float x, float y, void *ud){
    (void)ud;
    if(phase == 0) fade_to(LV_OPA_10); else if(phase == 2) fade_to(LV_OPA_COVER);
    if(g_mood_lbl && phase){ const char *m = sphere_mood_name(x, y); if(strcmp(lv_label_get_text(g_mood_lbl), m)) lv_label_set_text(g_mood_lbl, m); }
}
static void pole(lv_obj_t *s, const char *t, int cx, int cy){                /* a direction, just inside the ball's rim */
    int lh = lv_font_get_line_height(&B16);
    lv_obj_t *l = text(s, t, cx - 60, cy - lh / 2, 120, &B16, TC(FIXED_MEDIA_WHITE), LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_opa(l, LV_OPA_80, 0); lv_obj_set_style_text_letter_space(l, 2, 0);
}
static void poles(lv_obj_t *s){ pole(s, "ENERGETIC", 180, 30); pole(s, "CALM", 180, 330); pole(s, "DARK", 62, 180); pole(s, "BRIGHT", 298, 180); }
static ss_item_t *tag_item(int *kind, const char **key){
    if(g_ext){ *kind = g_one_kind; *key = g_one_key; return &g_one; }
    *kind = g_kind; *key = g_items[g_cur].name; return &g_items[g_cur];
}
static void tag_done(void){ if(g_ext) screen_back(); else show(P_LIST); }    /* from a menu: back where it came from */
static void save_cb(lv_event_t *e){
    (void)e; float x, y; sphere_get(&x, &y); int k; const char *key; ss_item_t *it = tag_item(&k, &key);
    if(ssdb_put(k, key, x, y)){ it->placed = 1; it->x = x; it->y = y; ui_toast_icon(LV_SYMBOL_OK, mood_col(x, y), sphere_mood_name(x, y)); tag_done(); }
    else ui_toast("Couldn't save. Is the card full?");
}
static void remove_cb(lv_event_t *e){
    (void)e; int k; const char *key; ss_item_t *it = tag_item(&k, &key);
    if(ssdb_del(k, key)){ it->placed = 0; tag_done(); } else ui_toast("Couldn't remove it");
}
static void build_tag(lv_obj_t *s){
    int kind; const char *key; ss_item_t *it = tag_item(&kind, &key);
    sphere_create(s, 180, 180, TC(CANVAS));
    poles(s);
    float x = it->placed ? it->x : 0, y = it->placed ? it->y : 0;
    int inherited = 0;
    if(!it->placed && kind == SS_SONG && (it->x || it->y)){ x = it->x; y = it->y; inherited = 1; }   /* its album's / artist's spot */
    sphere_set(x, y, !it->placed);
    sphere_set_cb(drag_cb, NULL);
    g_mood_lbl = NULL; g_nfade = 0;
    lv_obj_t *top = glass(s, 54, 236, 48);
    text(top, it->name, 14, 4, 208, &B18, TC(TEXT_PRIMARY), LV_TEXT_ALIGN_CENTER);
    static const char *const KN[3] = { "SONG", "ALBUM", "ARTIST" };
    char k[64]; snprintf(k, sizeof k, "%s%s", KN[kind], it->placed ? "" : inherited ? "  \xE2\x80\xA2  FROM ITS ALBUM / ARTIST" : "  \xE2\x80\xA2  NOT PLACED");
    text(top, k, 14, 27, 208, &F12, it->placed ? ACC() : TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    g_fade[g_nfade++] = top;
    lv_obj_t *bot = lv_obj_create(s); lv_obj_remove_style_all(bot); lv_obj_set_size(bot, 360, 86); lv_obj_set_pos(bot, 0, 232);
    lv_obj_remove_flag(bot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    button(bot, "Save", 180, 0, 150, 48, 1, save_cb, NULL);
    if(it->placed){ lv_obj_t *r = button(bot, "Remove", 180, 54, 108, 30, 0, remove_cb, NULL);
        lv_obj_set_style_bg_opa(r, LV_OPA_70, 0); lv_obj_set_style_text_font(lv_obj_get_child(r, 0), &F14, 0); }
    g_fade[g_nfade++] = bot;
}
static void sheet_close(lv_event_t *e){ (void)e; if(g_sheet){ lv_obj_delete(g_sheet); g_sheet = NULL; } }
static void go_cb(lv_event_t *e){
    int save = (int)(intptr_t)lv_event_get_user_data(e);
    char name[96] = "";
    if(save){ char day[24]; time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm); strftime(day, sizeof day, "%d %b %H:%M", &tm);
        snprintf(name, sizeof name, "SongSphere: %s, %s", sphere_mood_name(g_play_x, g_play_y), day); }   /* two saves never share a name */
    int *ids = g_pick, n = g_npick; g_pick = NULL; g_npick = 0;
    sheet_close(NULL);
    ui_app_play_ids(ids, n, 1, name, 1, save);                                    /* it opens Now Playing */
}
static void play_cb(lv_event_t *e){
    (void)e; sphere_get(&g_play_x, &g_play_y);
    free(g_pick); g_pick = NULL;
    g_npick = ssdb_pick(g_play_x, g_play_y, 40, &g_pick, &g_pick_r);
    if(g_npick <= 0){ ui_toast("Place some music first"); return; }
    g_sheet = box(lv_layer_top(), 0, 0, 360, 360, 0, TC(CANVAS), LV_OPA_80);
    lv_obj_add_flag(g_sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *card = box(g_sheet, 34, 64, 292, 222, 32, TC(SURFACE), LV_OPA_COVER);
    lv_obj_set_style_border_width(card, 1, 0); lv_obj_set_style_border_color(card, TC(BORDER), 0);
    char t[64]; snprintf(t, sizeof t, "%d songs", g_npick);
    ctext(card, t, 18, 252, &B22, TC(TEXT_PRIMARY));
    ctext(card, sphere_mood_name(g_play_x, g_play_y), 50, 252, &F16, ACC());
    if(g_pick_r > 0.7f) ctext(card, "Few songs right here: some are\nfrom a little further away.", 72, 252, &F14, TC(TEXT_SECONDARY));
    else ctext(card, "Played in Shuffle.", 76, 240, &F14, TC(TEXT_SECONDARY));
    button(card, LV_SYMBOL_PLAY "  Play", 146, 116, 200, 48, 1, go_cb, (void *)0);
    button(card, "Play and save", 146, 172, 200, 40, 0, go_cb, (void *)1);
    lv_obj_t *x = text(g_sheet, "Cancel", 130, 300, 100, &B16, TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(x, 12);
    lv_obj_add_event_cb(x, sheet_close, LV_EVENT_CLICKED, NULL);
}
static void build_play(lv_obj_t *s){
    sphere_create(s, 180, 180, TC(CANVAS));
    poles(s);
    sphere_set(g_play_x, g_play_y, 0);
    sphere_set_cb(drag_cb, NULL);
    g_nfade = 0;
    lv_obj_t *top = glass(s, 54, 180, 44);
    g_mood_lbl = text(top, sphere_mood_name(g_play_x, g_play_y), 8, (44 - lv_font_get_line_height(&B22)) / 2, 164, &B22, TC(TEXT_PRIMARY), LV_TEXT_ALIGN_CENTER);
    g_fade[g_nfade++] = top;
    lv_obj_t *b = button(s, LV_SYMBOL_PLAY "  Play mood", 180, 248, 188, 52, 1, play_cb, NULL);
    lv_obj_set_style_shadow_width(b, 16, 0); lv_obj_set_style_shadow_opa(b, LV_OPA_40, 0);
    g_fade[g_nfade++] = b;
}

/* ---- pages ---- */
static void show(int page){
    if(!g_root) return;
    if(g_sheet){ lv_obj_delete(g_sheet); g_sheet = NULL; }
    sphere_set_cb(NULL, NULL);
    if(g_page) lv_obj_delete(g_page);
    g_list = NULL; g_mood_lbl = NULL; g_nfade = 0; memset(g_dot, 0, sizeof g_dot);
    g_page = lv_obj_create(g_root); lv_obj_remove_style_all(g_page);
    lv_obj_set_size(g_page, 360, 360); lv_obj_remove_flag(g_page, LV_OBJ_FLAG_SCROLLABLE);
    g_pg = page;
    switch(page){
        case P_LIST: build_list(g_page); break;
        case P_TAG:  build_tag(g_page); break;
        case P_PLAY: build_play(g_page); break;
        default:     build_land(g_page); break;
    }
}
/* screen_back: up one page (a tag page opened from a menu goes straight back there) */
int sphere_ui_back_consumed(void){
    if(g_sheet){ sheet_close(NULL); return 1; }
    if(g_pg == P_TAG && !g_ext){ show(P_LIST); return 1; }
    if(g_pg == P_LIST || g_pg == P_PLAY){ show(P_LAND); return 1; }
    return 0;
}
/* a press on the marker belongs to the sphere, never to the back swipe */
int sphere_ui_owns_point(int x, int y){
    if(g_pg != P_TAG && g_pg != P_PLAY) return 0;
    float mx, my; sphere_get(&mx, &my);
    float dx = x - (180 + mx * 164), dy = y - (180 - my * 164);
    return dx * dx + dy * dy <= 40 * 40;
}
void sphere_ui_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
}
static int ready(void){
    fonts_init();
    if(!db_ready()){ ui_toast("SongSphere can't read the library"); return 0; }
    return 1;
}
void sphere_ui_open(void){
    if(!sphere_ui_enabled()){ ui_toast("SongSphere is off: Settings > Playback"); return; }
    if(!ready()) return;
    g_ext = 0; show(P_LAND); screen_show(SCR_SPHERE);
}
/* the tag page for one album / artist (the Library's hold menu) or song (the track popup) */
void sphere_ui_tag(int kind, const char *key, const char *name){
    if(!key || !key[0] || !ready()) return;
    memset(&g_one, 0, sizeof g_one);
    g_one_kind = kind; snprintf(g_one_key, sizeof g_one_key, "%s", key);
    snprintf(g_one.name, sizeof g_one.name, "%s", name && name[0] ? name : key);
    g_one.placed = ssdb_get(kind, key, &g_one.x, &g_one.y);
    if(!g_one.placed && kind == SS_SONG){ float x, y; if(sphere_ui_song_spot(key, NULL, NULL, &x, &y)){ g_one.x = x; g_one.y = y; } }
    g_ext = 1; show(P_TAG); screen_show(SCR_SPHERE);
}
/* a song's spot: its own (1), its album's (2) or its artist's (3); 0 = none */
int sphere_ui_song_spot(const char *path, const char *album, const char *artist, float *x, float *y){
    if(!path || !path[0] || !db_ready()) return 0;
    if(ssdb_get(SS_SONG, path, x, y)) return 1;
    mdb_song_t s; int have = 0;
    if(!album || !artist){ have = mdb_song_by_path(path, &s); if(have){ if(!album) album = s.album; if(!artist) artist = s.artist_group[0] ? s.artist_group : s.artist; } }
    if(album && album[0] && ssdb_get(SS_ALBUM, album, x, y)) return 2;
    if(artist && artist[0] && ssdb_get(SS_ARTIST, artist, x, y)) return 3;
    return 0;
}
const char *sphere_ui_mood_name(float x, float y){ return sphere_mood_name(x, y); }
lv_color_t sphere_ui_mood_colour(float x, float y){ return mood_col(x, y); }
void sphere_ui_test(int page, float x, float y){ g_play_x = x; g_play_y = y; if(!ready()) return; g_ext = 0; show(page); screen_show(SCR_SPHERE); }
