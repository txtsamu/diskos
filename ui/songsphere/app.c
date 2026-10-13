/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SongSphere: music by mood. An add-on app for diskOS (its own program in /usr/data/apps/songsphere).
 *
 * Place music: pick an album or an artist (most played first), drag the marker to where it sits between Energetic
 * and Calm, Dark and Bright, Save. Play a mood: drag the marker to a spot, Play: SongSphere picks up to 40 songs
 * near it and hands them to diskOS to play in Shuffle (and keep as a playlist, if asked), then closes.
 *
 * The marker moves only by dragging it (a tap elsewhere does nothing), and the labels on the sphere fade while it
 * moves. Swipe right, or the round back button, goes back; Exit is on the first page. */
#include "platform.h"
#include "sphere.h"
#include "ssdb.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

LV_FONT_DECLARE(font_inter_medium_12)
LV_FONT_DECLARE(font_inter_medium_14)
LV_FONT_DECLARE(font_inter_medium_16)
LV_FONT_DECLARE(font_inter_medium_20)
LV_FONT_DECLARE(font_inter_bold_16)
LV_FONT_DECLARE(font_inter_bold_18)
LV_FONT_DECLARE(font_inter_bold_22)
static lv_font_t F12, F14, F16, F20, B16, B18, B22;     /* Inter, with LVGL's symbols as the fallback */
static void fonts_init(void){
    F12 = font_inter_medium_12; F12.fallback = &lv_font_montserrat_12;
    F14 = font_inter_medium_14; F14.fallback = &lv_font_montserrat_14;
    F16 = font_inter_medium_16; F16.fallback = &lv_font_montserrat_16;
    F20 = font_inter_medium_20; F20.fallback = &lv_font_montserrat_20;
    B16 = font_inter_bold_16;   B16.fallback = &lv_font_montserrat_16;
    B18 = font_inter_bold_18;   B18.fallback = &lv_font_montserrat_18;
    B22 = font_inter_bold_22;   B22.fallback = &lv_font_montserrat_22;
}
#define C(name) look(g_look.name)

enum { S_LAND, S_LIST, S_TAG, S_PLAY };
static int g_screen, g_kind = SS_ALBUM, g_shown, g_cur = -1, g_quit;
static ss_item_t *g_items; static int g_nitems;
static lv_obj_t *g_fade[3]; static int g_nfade;           /* what fades while the marker moves */
static lv_obj_t *g_mood_lbl, *g_toast;
static float g_play_x, g_play_y;
static int *g_pick; static int g_npick; static float g_pick_r;

static void show(int screen);

/* ---- small builders ---- */
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r, uint32_t c, lv_opa_t o){
    lv_obj_t *b = lv_obj_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, r, 0); lv_obj_set_style_bg_color(b, look(c), 0); lv_obj_set_style_bg_opa(b, o, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}
static lv_obj_t *text(lv_obj_t *p, const char *s, int x, int y, int w, const lv_font_t *f, uint32_t c, lv_text_align_t al){
    lv_obj_t *l = lv_label_create(p); lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, look(c), 0);
    lv_obj_set_style_text_align(l, al, 0); lv_obj_set_width(l, w);
    if(!strchr(s, '\n')){ lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_height(l, lv_font_get_line_height(f)); }
    lv_obj_set_pos(l, x, y);
    return l;
}
static lv_obj_t *ctext(lv_obj_t *p, const char *s, int cy, int w, const lv_font_t *f, uint32_t c){
    return text(p, s, (lv_obj_get_style_width(p, LV_PART_MAIN) - w) / 2, cy, w, f, c, LV_TEXT_ALIGN_CENTER);
}
static lv_obj_t *button(lv_obj_t *p, const char *label, int cx, int y, int w, int h, int primary, lv_event_cb_t cb, void *ud){
    lv_obj_t *b = box(p, cx - w / 2, y, w, h, LV_RADIUS_CIRCLE, primary ? g_look.accent : g_look.surface_raised, LV_OPA_COVER);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(b, 6);
    lv_obj_set_style_bg_color(b, look(primary ? g_look.accent : g_look.surface_pressed), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, primary ? LV_OPA_80 : LV_OPA_COVER, LV_STATE_PRESSED);
    if(!primary){ lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, look(g_look.border), 0); }
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &F16, 0); lv_obj_set_style_text_color(l, look(primary ? g_look.on_accent : g_look.text), 0);
    lv_obj_center(l);
    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}
static void toast_del(lv_timer_t *t){ (void)t; if(g_toast){ lv_obj_delete(g_toast); g_toast = NULL; } }
static void toast(const char *s){
    if(g_toast) lv_obj_delete(g_toast);
    g_toast = box(lv_layer_top(), 50, 296, 260, 36, LV_RADIUS_CIRCLE, g_look.surface_raised, LV_OPA_COVER);
    lv_obj_set_style_border_width(g_toast, 1, 0); lv_obj_set_style_border_color(g_toast, look(g_look.border), 0);
    text(g_toast, s, 10, (36 - lv_font_get_line_height(&F14)) / 2, 240, &F14, g_look.text, LV_TEXT_ALIGN_CENTER);
    lv_timer_t *t = lv_timer_create(toast_del, 2200, NULL); lv_timer_set_repeat_count(t, 1);
}
static void back_cb(lv_event_t *e){ (void)e; show(g_screen == S_TAG ? S_LIST : S_LAND); }
static lv_obj_t *back_button(lv_obj_t *root){
    lv_obj_t *b = box(root, 40, 40, 36, 36, LV_RADIUS_CIRCLE, g_look.surface_raised, LV_OPA_90);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(b, 8);
    lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, look(g_look.border), 0);
    lv_obj_set_style_bg_color(b, look(g_look.surface_pressed), LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(l, &F14, 0); lv_obj_set_style_text_color(l, look(g_look.text), 0); lv_obj_center(l);
    lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, NULL);
    return b;
}
static void gesture_cb(lv_event_t *e){
    (void)e; lv_indev_t *in = lv_indev_active();
    if(in && lv_indev_get_gesture_dir(in) == LV_DIR_RIGHT && g_screen != S_LAND){ lv_indev_wait_release(in); back_cb(NULL); }
}
static lv_obj_t *screen_new(void){
    lv_obj_t *s = lv_obj_create(NULL); lv_obj_remove_style_all(s);
    lv_obj_set_size(s, 360, 360);
    lv_obj_set_style_bg_color(s, C(canvas), 0); lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s, gesture_cb, LV_EVENT_GESTURE, NULL);
    return s;
}

/* ---- leaving: hand the songs to diskOS ---- */
static int request_write(int save){
    FILE *f = fopen("/tmp/diskos-app-request.tmp", "w"); if(!f) return 0;
    char day[24]; time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm); strftime(day, sizeof day, "%d %b %H:%M", &tm);   /* two saves never share a name */
    fprintf(f, "diskos-app-request-v1\naction=%s\nshuffle=1\n", save ? "playsave" : "play");
    if(save) fprintf(f, "name=SongSphere: %s, %s\n", sphere_mood_name(g_play_x, g_play_y), day);
    for(int i = 0; i < g_npick; i++) fprintf(f, "id=%d\n", g_pick[i]);
    int ok = fclose(f) == 0;
    return ok && rename("/tmp/diskos-app-request.tmp", "/tmp/diskos-app-request") == 0;
}

/* ---- the first page ---- */
static void exit_cb(lv_event_t *e){ (void)e; g_quit = 1; }
static void goto_cb(lv_event_t *e){ show((int)(intptr_t)lv_event_get_user_data(e)); }
static void build_land(lv_obj_t *s){
    lv_obj_t *art = lv_image_create(s);                     /* the sphere itself, small (a picture of its own) */
    const lv_image_dsc_t *pic = sphere_picture(C(canvas), 1);
    if(pic) lv_image_set_src(art, pic);
    lv_obj_set_pos(art, 180 - 59, 30);
    lv_obj_remove_flag(art, LV_OBJ_FLAG_CLICKABLE);
    ctext(s, "SongSphere", 162, 260, &B22, g_look.text);
    ctext(s, "Music by mood.", 192, 260, &F16, g_look.accent);
    int placed, albums; ssdb_coverage(&placed, &albums);
    char cov[64];
    if(albums == 0) snprintf(cov, sizeof cov, "No library found");
    else snprintf(cov, sizeof cov, "%d of %d albums placed", placed, albums);
    ctext(s, cov, 216, 240, &F12, g_look.text_secondary);
    button(s, LV_SYMBOL_PLAY "  Play a mood", 180, 244, 190, 40, 1, goto_cb, (void *)(intptr_t)S_PLAY);
    button(s, "Place music", 180, 292, 190, 36, 0, goto_cb, (void *)(intptr_t)S_LIST);
    lv_obj_t *x = text(s, "Exit", 140, 334, 80, &F12, g_look.text_muted, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(x, 10);
    lv_obj_add_event_cb(x, exit_cb, LV_EVENT_CLICKED, NULL);
}

/* ---- Place music: the list ---- */
#define PAGE 60
static lv_obj_t *g_list;
static void row_cb(lv_event_t *e){ g_cur = (int)(intptr_t)lv_event_get_user_data(e); show(S_TAG); }
static void more_cb(lv_event_t *e);
static void list_fill(int from){
    for(int i = from; i < g_nitems && i < from + PAGE; i++){
        ss_item_t *it = &g_items[i];
        lv_obj_t *r = box(g_list, 0, 0, 252, 52, 12, g_look.surface, LV_OPA_TRANSP);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(r, C(surface_pressed), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_event_cb(r, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *d = box(r, 10, 19, 14, 14, LV_RADIUS_CIRCLE, g_look.surface, LV_OPA_TRANSP);
        if(it->placed){ uint8_t c[3]; sphere_mood_rgb(it->x, it->y, c);
            lv_obj_set_style_bg_color(d, lv_color_make(c[0], c[1], c[2]), 0); lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0); }
        else { lv_obj_set_style_border_width(d, 2, 0); lv_obj_set_style_border_color(d, C(text_muted), 0); }
        char sub[160];
        if(g_kind == SS_ALBUM) snprintf(sub, sizeof sub, "%s", it->sub);
        else snprintf(sub, sizeof sub, "%d song%s", it->songs, it->songs == 1 ? "" : "s");
        text(r, it->name, 36, 7, 206, &F16, g_look.text, LV_TEXT_ALIGN_LEFT);
        text(r, sub, 36, 28, 206, &F12, g_look.text_secondary, LV_TEXT_ALIGN_LEFT);
    }
    g_shown = from + PAGE < g_nitems ? from + PAGE : g_nitems;
    if(g_shown < g_nitems){
        lv_obj_t *m = button(g_list, "Show more", 0, 0, 160, 38, 0, more_cb, NULL);
        lv_obj_set_style_margin_top(m, 6, 0);
    }
    lv_obj_t *pad = box(g_list, 0, 0, 10, 70, 0, g_look.canvas, LV_OPA_TRANSP); (void)pad;   /* room at the round bottom */
}
static void more_cb(lv_event_t *e){
    lv_obj_t *b = lv_event_get_target(e);
    lv_obj_delete(lv_obj_get_child(g_list, -1));            /* the bottom pad */
    lv_obj_delete_async(b);
    list_fill(g_shown);
}
static void kind_cb(lv_event_t *e){ int k = (int)(intptr_t)lv_event_get_user_data(e); if(k != g_kind){ g_kind = k; show(S_LIST); } }
static void build_list(lv_obj_t *s){
    free(g_items); g_items = NULL;
    g_nitems = ssdb_items(g_kind, &g_items); if(g_nitems < 0) g_nitems = 0;
    g_list = lv_obj_create(s); lv_obj_remove_style_all(g_list);
    lv_obj_set_size(g_list, 360, 360 - 90); lv_obj_set_pos(g_list, 0, 90);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_list, 2, 0); lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    if(!g_nitems) ctext(s, g_kind == SS_ALBUM ? "No albums in the library" : "No artists in the library", 170, 260, &F14, g_look.text_secondary);
    else list_fill(0);
    /* top: Albums | Artists and the coverage, over a band of the screen colour */
    box(s, 0, 0, 360, 90, 0, g_look.canvas, LV_OPA_COVER);
    lv_obj_t *seg = box(s, 104, 20, 152, 32, 16, g_look.surface, LV_OPA_COVER);
    static const char *K[2] = { "Albums", "Artists" };
    for(int i = 0; i < 2; i++){
        int on = (i ? SS_ARTIST : SS_ALBUM) == g_kind;
        lv_obj_t *b = box(seg, 2 + i * 74, 2, 74, 28, 14, on ? g_look.accent : g_look.surface, on ? LV_OPA_COVER : LV_OPA_TRANSP);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(b, 6);
        lv_obj_add_event_cb(b, kind_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i ? SS_ARTIST : SS_ALBUM));
        text(b, K[i], 0, (28 - lv_font_get_line_height(&F14)) / 2, 74, &F14, on ? g_look.on_accent : g_look.text_secondary, LV_TEXT_ALIGN_CENTER);
    }
    int placed = 0; for(int i = 0; i < g_nitems; i++) placed += g_items[i].placed;
    char cov[64]; snprintf(cov, sizeof cov, "%d of %d placed", placed, g_nitems);
    ctext(s, cov, 60, 200, &F12, g_look.text_secondary);
    back_button(s);
}

/* ---- the sphere pages ---- */
static void fade_set(void *o, int32_t v){ lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }
static void fade_to(lv_opa_t to){
    for(int i = 0; i < g_nfade; i++){
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_fade[i]); lv_anim_set_exec_cb(&a, fade_set);
        lv_anim_set_values(&a, lv_obj_get_style_opa(g_fade[i], 0), to); lv_anim_set_duration(&a, 140);
        lv_anim_start(&a);
    }
}
static void drag_cb(int phase, float x, float y, void *ud){
    (void)ud;
    if(phase == 0) fade_to(LV_OPA_10);
    else if(phase == 2) fade_to(LV_OPA_COVER);
    if(g_mood_lbl && phase){ const char *m = sphere_mood_name(x, y);
        if(strcmp(lv_label_get_text(g_mood_lbl), m)) lv_label_set_text(g_mood_lbl, m); }
}
static void poles(lv_obj_t *s){
    int lh = lv_font_get_line_height(&F12);
    ctext(s, "ENERGETIC", 9, 120, &F12, g_look.text_secondary);
    ctext(s, "CALM", 360 - 9 - lh, 120, &F12, g_look.text_secondary);
    lv_obj_t *d = text(s, "DARK", 16 - 40 + 1, 180 - lh / 2, 80, &F12, g_look.text_secondary, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_transform_pivot_x(d, 40, 0); lv_obj_set_style_transform_pivot_y(d, lh / 2, 0); lv_obj_set_style_transform_rotation(d, 2700, 0);
    lv_obj_t *b = text(s, "BRIGHT", 344 - 40 - 1, 180 - lh / 2, 80, &F12, g_look.text_secondary, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_transform_pivot_x(b, 40, 0); lv_obj_set_style_transform_pivot_y(b, lh / 2, 0); lv_obj_set_style_transform_rotation(b, 900, 0);
}
static lv_obj_t *glass(lv_obj_t *s, int y, int w, int h){
    lv_obj_t *p = box(s, 180 - w / 2, y, w, h, LV_RADIUS_CIRCLE, g_look.canvas, LV_OPA_50);
    return p;
}
static void save_cb(lv_event_t *e){
    (void)e; float x, y; sphere_get(&x, &y);
    ss_item_t *it = &g_items[g_cur];
    if(ssdb_put(g_kind, it->name, x, y)){ it->placed = 1; it->x = x; it->y = y; show(S_LIST); }
    else toast("Couldn't save. Is the card full?");
}
static void remove_cb(lv_event_t *e){
    (void)e; ss_item_t *it = &g_items[g_cur];
    if(ssdb_del(g_kind, it->name)){ it->placed = 0; show(S_LIST); } else toast("Couldn't remove it");
}
static void build_tag(lv_obj_t *s){
    ss_item_t *it = &g_items[g_cur];
    poles(s);
    sphere_create(s, 180, 180, C(canvas));
    sphere_set(it->placed ? it->x : 0, it->placed ? it->y : 0, !it->placed);
    sphere_set_cb(drag_cb, NULL);
    g_mood_lbl = NULL; g_nfade = 0;
    lv_obj_t *top = glass(s, 44, 200, 42);
    text(top, it->name, 10, 4, 180, &F14, g_look.text, LV_TEXT_ALIGN_CENTER);
    char k[64]; snprintf(k, sizeof k, "%s%s", g_kind == SS_ALBUM ? "ALBUM" : "ARTIST", it->placed ? "" : "  -  NOT PLACED YET");
    text(top, k, 10, 23, 180, &F12, it->placed ? g_look.accent : g_look.text_secondary, LV_TEXT_ALIGN_CENTER);
    g_fade[g_nfade++] = top;
    lv_obj_t *bot = lv_obj_create(s); lv_obj_remove_style_all(bot); lv_obj_set_size(bot, 360, 90); lv_obj_set_pos(bot, 0, 262);
    lv_obj_remove_flag(bot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    button(bot, "Save", 180, 0, 110, 38, 1, save_cb, NULL);
    if(it->placed){ lv_obj_t *r = button(bot, "Remove", 180, 46, 96, 28, 0, remove_cb, NULL);
        lv_obj_set_style_bg_opa(r, LV_OPA_70, 0); lv_obj_set_style_text_font(lv_obj_get_child(r, 0), &F12, 0); }
    g_fade[g_nfade++] = bot;
    back_button(s);
}
static lv_obj_t *g_sheet;
static void sheet_close(lv_event_t *e){ (void)e; if(g_sheet){ lv_obj_delete(g_sheet); g_sheet = NULL; } }
static void go_cb(lv_event_t *e){
    int save = (int)(intptr_t)lv_event_get_user_data(e);
    if(request_write(save)) g_quit = 1; else toast("Couldn't hand the list over");
}
static void play_cb(lv_event_t *e){
    (void)e; sphere_get(&g_play_x, &g_play_y);
    free(g_pick); g_pick = NULL;
    g_npick = ssdb_pick(g_play_x, g_play_y, 40, &g_pick, &g_pick_r);
    if(g_npick <= 0){ toast("Place some music first"); return; }
    g_sheet = box(lv_layer_top(), 0, 0, 360, 360, 0, g_look.canvas, LV_OPA_80);
    lv_obj_add_flag(g_sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *card = box(g_sheet, 50, 86, 260, 188, 28, g_look.surface, LV_OPA_COVER);
    lv_obj_set_style_border_width(card, 1, 0); lv_obj_set_style_border_color(card, C(border), 0);
    char t[64]; snprintf(t, sizeof t, "%d songs", g_npick);
    ctext(card, t, 16, 220, &B22, g_look.text);
    ctext(card, sphere_mood_name(g_play_x, g_play_y), 46, 220, &F16, g_look.accent);
    if(g_pick_r > 0.7f) ctext(card, "Few songs right here: some are\nfrom a little further away.", 70, 230, &F12, g_look.text_secondary);
    else ctext(card, "Played in Shuffle.", 74, 220, &F12, g_look.text_secondary);
    button(card, LV_SYMBOL_PLAY "  Play", 130, 104, 150, 36, 1, go_cb, (void *)0);
    button(card, "Play and save", 130, 146, 150, 30, 0, go_cb, (void *)1);
    lv_obj_t *x = text(g_sheet, "Cancel", 140, 290, 80, &F14, g_look.text_secondary, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(x, 12);
    lv_obj_add_event_cb(x, sheet_close, LV_EVENT_CLICKED, NULL);
}
static void build_play(lv_obj_t *s){
    poles(s);
    sphere_create(s, 180, 180, C(canvas));
    sphere_set(g_play_x, g_play_y, 0);
    sphere_set_cb(drag_cb, NULL);
    g_nfade = 0;
    lv_obj_t *top = glass(s, 44, 160, 34);
    g_mood_lbl = text(top, sphere_mood_name(g_play_x, g_play_y), 8, (34 - lv_font_get_line_height(&B18)) / 2, 144, &B18, g_look.text, LV_TEXT_ALIGN_CENTER);
    g_fade[g_nfade++] = top;
    lv_obj_t *b = button(s, LV_SYMBOL_PLAY "  Play mood", 180, 270, 150, 40, 1, play_cb, NULL);
    lv_obj_set_style_shadow_width(b, 16, 0); lv_obj_set_style_shadow_opa(b, LV_OPA_40, 0);
    g_fade[g_nfade++] = b;
    back_button(s);
}

static void show(int screen){
    lv_obj_t *old = lv_screen_active(), *s = screen_new();
    g_screen = screen; g_mood_lbl = NULL; g_nfade = 0;
    if(g_sheet){ lv_obj_delete(g_sheet); g_sheet = NULL; }
    sphere_set_cb(NULL, NULL);
    switch(screen){
        case S_LIST: build_list(s); break;
        case S_TAG:  build_tag(s); break;
        case S_PLAY: build_play(s); break;
        default:     build_land(s); break;
    }
    lv_screen_load(s);
    if(old && old != s) lv_obj_delete(old);
}

/* ---- host pictures and timing (SS_SHOT / SS_BENCH; never on the Disc) ---- */
static uint32_t *g_frame;
static void host_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px){
    int w = a->x2 - a->x1 + 1;
    for(int y = a->y1; y <= a->y2; y++) memcpy(&g_frame[y * 360 + a->x1], px + (size_t)(y - a->y1) * w * 4, (size_t)w * 4);
    lv_display_flush_ready(d);
}
static void host_display(void){
    g_frame = calloc(360 * 360, 4);
    static uint8_t b1[360 * 120 * 4], b2[360 * 120 * 4];             /* the Disc's banded buffers, for honest timing */
    lv_display_t *d = lv_display_create(360, 360);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_flush_cb(d, host_flush);
    lv_display_set_buffers(d, b1, b2, sizeof b1, LV_DISPLAY_RENDER_MODE_PARTIAL);
}
static void host_shot(const char *path){
    for(int i = 0; i < 30; i++){ lv_tick_inc(10); lv_timer_handler(); }
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL);
    FILE *f = fopen(path, "wb"); if(!f) return;
    fprintf(f, "P6\n360 360\n255\n");
    for(int i = 0; i < 360 * 360; i++){ uint32_t p = g_frame[i]; uint8_t c[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p }; fwrite(c, 1, 3, f); }
    fclose(f);
}
/* SS_TEST: a scripted finger (a real LVGL pointer) and the whole flow, checked step by step (host only) */
static int t_x, t_y, t_down;
static void t_read(lv_indev_t *in, lv_indev_data_t *d){ (void)in; d->point.x = t_x; d->point.y = t_y; d->state = t_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED; }
static void t_run(int ms){ for(int i = 0; i < ms / 10; i++){ lv_tick_inc(10); lv_timer_handler(); } }
static void t_move(int x0, int y0, int x1, int y1){
    t_x = x0; t_y = y0; t_down = 1; t_run(60);
    for(int i = 1; i <= 20; i++){ t_x = x0 + (x1 - x0) * i / 20; t_y = y0 + (y1 - y0) * i / 20; t_run(20); }
    t_down = 0; t_run(200);
}
#define T_CHECK(c, m) do{ if(!(c)){ printf("FAIL: %s\n", m); return 1; } else printf("ok: %s\n", m); }while(0)
static int self_test(void){
    lv_indev_t *in = lv_indev_create(); lv_indev_set_type(in, LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(in, t_read);
    unlink("/tmp/diskos-app-request");
    show(S_LIST); t_run(100);
    T_CHECK(g_nitems > 0, "albums listed");
    g_cur = 0; char key[128]; snprintf(key, sizeof key, "%s", g_items[0].name); ssdb_del(SS_ALBUM, key);
    show(S_LIST); g_cur = 0; show(S_TAG); t_run(100);
    float x, y; sphere_get(&x, &y);
    T_CHECK(fabsf(x) < 0.01f && fabsf(y) < 0.01f, "unplaced album starts in the middle");
    t_move(250, 250, 300, 120); sphere_get(&x, &y);                 /* a press away from the marker: nothing moves */
    T_CHECK(fabsf(x) < 0.01f && fabsf(y) < 0.01f, "a press away from the marker doesn't move it");
    t_move(180, 180, 270, 90); sphere_get(&x, &y);                 /* drag the marker up-right */
    T_CHECK(x > 0.5f && y > 0.5f, "dragging the marker moves it");
    T_CHECK(lv_obj_get_style_opa(g_fade[0], 0) == LV_OPA_COVER, "labels back after the drag");
    t_move(270, 90, 400, -50); sphere_get(&x, &y);                 /* past the edge: stays on the ball */
    T_CHECK(sqrtf(x * x + y * y) <= 0.971f, "the marker stays on the ball");
    save_cb(NULL); t_run(50);
    float sx, sy; T_CHECK(ssdb_get(SS_ALBUM, key, &sx, &sy) && fabsf(sx - x) < 0.002f, "Save stores the spot");
    T_CHECK(g_screen == S_LIST && g_items[0].placed, "back on the list, shown as placed");
    g_cur = 0; show(S_TAG); remove_cb(NULL);
    T_CHECK(!ssdb_get(SS_ALBUM, key, &sx, &sy), "Remove deletes it");
    ssdb_put(SS_ALBUM, key, sx, sy);
    show(S_PLAY); t_run(50);
    t_move(180, 180, 120, 260); sphere_get(&x, &y);
    T_CHECK(x < -0.2f && y < -0.3f, "Play a mood: drag works");
    play_cb(NULL);
    T_CHECK(g_npick > 0 && g_npick <= 40 && g_sheet, "a list is picked and the card shows");
    int dup = 0; for(int i = 0; i < g_npick; i++) for(int j = i + 1; j < g_npick; j++) dup |= g_pick[i] == g_pick[j];
    T_CHECK(!dup, "no song twice");
    if(request_write(1)) g_quit = 1;                           /* what Play and save does */
    FILE *f = fopen("/tmp/diskos-app-request", "r"); int lines = 0, ids = 0; char ln[256];
    while(f && fgets(ln, sizeof ln, f)){ lines++; if(!strncmp(ln, "id=", 3)) ids++; }
    if(f) fclose(f);
    T_CHECK(ids == g_npick && g_quit, "Play and save hands the list over and closes");
    g_quit = 0; show(S_LAND); t_move(20, 180, 200, 180);
    T_CHECK(g_screen == S_LAND, "swipe on the first page does nothing");
    show(S_LIST); t_move(20, 200, 300, 200);
    T_CHECK(g_screen == S_LAND, "swipe right goes back");
    for(int i = 0; i < 30; i++){ show(i % 2 ? S_PLAY : S_LIST); if(i % 2 == 0 && g_nitems){ g_cur = i % g_nitems; show(S_TAG); } t_run(20); }
    T_CHECK(1, "30 screen changes");
    return 0;
}
static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

int main(int argc, char **argv){
    (void)argc; (void)argv;
    const char *shot = getenv("SS_SHOT"), *bench = getenv("SS_BENCH") ? getenv("SS_BENCH") : getenv("SS_TEST");
    look_load();
    lv_init();
    fonts_init();
    if(shot || bench){ lv_tick_set_cb(NULL); host_display(); }
    else if(!platform_init()){ fprintf(stderr, "songsphere: no screen\n"); return 1; }
    if(!ssdb_open()) fprintf(stderr, "songsphere: library not readable\n");
    if(getenv("SS_X")) g_play_x = (float)atof(getenv("SS_X"));
    if(getenv("SS_Y")) g_play_y = (float)atof(getenv("SS_Y"));
    const char *scr = getenv("SS_SCREEN");
    if(scr && !strcmp(scr, "artists")) g_kind = SS_ARTIST;
    if(scr && (!strcmp(scr, "list") || !strcmp(scr, "artists"))) show(S_LIST);
    else if(scr && !strcmp(scr, "tag")){ g_kind = SS_ALBUM; show(S_LIST); g_cur = getenv("SS_ITEM") ? atoi(getenv("SS_ITEM")) : 0; show(S_TAG); }
    else if(scr && (!strcmp(scr, "play") || !strcmp(scr, "pick"))){ show(S_PLAY); if(!strcmp(scr, "pick")) play_cb(NULL); }
    else show(S_LAND);
    if(getenv("SS_DRAG")){ for(int i = 0; i < g_nfade; i++) lv_obj_set_style_opa(g_fade[i], LV_OPA_10, 0); }
    if(shot){ host_shot(shot); return 0; }
    if(getenv("SS_TEST")) return self_test();
    if(bench){
        /* Drag timing: the marker swept round a circle, one LVGL refresh per step (what a finger does), against a
         * reference of the same banded display redrawing the whole screen (a full list scroll frame). */
        if(g_screen != S_PLAY) show(S_PLAY);
        for(int i = 0; i < 10; i++){ lv_tick_inc(10); lv_timer_handler(); }
        int steps = 600; double t0 = now_ms();
        for(int i = 0; i < steps; i++){
            float a = i * 0.05f, r = 0.2f + 0.7f * (0.5f + 0.5f * sinf(i * 0.013f));
            sphere_set(r * cosf(a), r * sinf(a), 0); drag_cb(1, r * cosf(a), r * sinf(a), NULL);
            lv_refr_now(NULL);
        }
        double drag = (now_ms() - t0) / steps;
        t0 = now_ms();
        for(int i = 0; i < 100; i++){ lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL); }
        double full = (now_ms() - t0) / 100;
        printf("drag frame: %.3f ms   full-screen frame: %.3f ms   ratio %.2f\n", drag, full, drag / full);
        t0 = now_ms(); int *ids = NULL, got = 0; float r;
        for(int i = 0; i < 10; i++){ free(ids); ids = NULL; got = ssdb_pick(0.3f, -0.4f, 40, &ids, &r); }
        double pick = (now_ms() - t0) / 10; free(ids);
        ss_item_t *it = NULL; t0 = now_ms(); int na = ssdb_items(SS_ALBUM, &it); double list = now_ms() - t0; free(it);
        printf("pick 40 songs: %.1f ms (%d found)   album list: %.1f ms (%d albums)\n", pick, got, list, na);
        return 0;
    }
    while(!g_quit){
        uint32_t idle = lv_timer_handler();
        usleep((idle > 5 ? 5 : idle) * 1000);
    }
    ssdb_close();
    return 0;
}
