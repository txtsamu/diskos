/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "folderbrowser.h"
#include "books.h"
#include "musicdb.h"
#include "fwcaps.h"
#include "artcache.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"
#include "i18n.h"
#include "ui_round.h"

enum { VIEW_MENU, VIEW_SONGS, VIEW_ALBUMS, VIEW_ARTISTS, VIEW_PLAYLISTS, VIEW_FAVS, VIEW_GENRES, VIEW_GROUP, VIEW_MOSTPLAYED, VIEW_RECENT, VIEW_HISTORY, VIEW_COUNT };
#define CAT_FOLDERS 1000
#define CAT_BOOKS   1001

#define ROW_H 52
#define LIST_Y UI_LIST_Y
#define LIST_H UI_LIST_H

static lv_obj_t *g_list;
static lv_obj_t *g_title;
static lv_obj_t *g_az_btn;
static lv_obj_t *g_grid;
static lv_obj_t *g_lhint = NULL, *g_lhint_lbl = NULL;
static uint32_t  g_lhint_tick = 0;
static lv_timer_t *g_lhint_timer;
static char      g_lhint_ch = 0;
static int g_view = VIEW_MENU;
static int g_drill_kind = 0;
static int  g_scope_lvl = 0;
static int  g_scope_from_albums = 0;
static char g_scope_album[MDB_STR];
static int  g_scope_total = 0;
static int g_deeplink = 0;
static int g_has_header = 0;
static int g_hdr_extra_px = 0;
static char g_drill[MDB_STR];
static library_song_click_cb_t g_song_cb;

static int  g_alloc_n = 0;
static int  g_fav_cap = 0;
static int  g_grp_cap = 0;
static int  g_first_cap = 0;
static char *g_first = NULL;
static int  g_count;

static char (*g_gnames)[MDB_STR]  = NULL;
static char (*g_gartists)[MDB_STR] = NULL;
static int  *g_gcounts = NULL;
static void lib_alloc_buffers(int songs);
static void lib_ensure_first_cap(int need);
static void lib_ensure_group_cap(int need);
static void lib_ensure_fav_cap(int need);
static mdb_song_t *g_favs = NULL;
static char (*g_plnames)[MDB_STR] = NULL;
static long *g_plids = NULL;
static int  *g_plcounts = NULL;
static int   g_plcap = 0;

static const mdb_song_t **g_buf = NULL;
static int g_fill_i, g_fill_n;
static lv_timer_t *g_fill_timer;
static int g_jump_idx = -1;

static void library_reload(void);

static char first_letter(const char *s){
    while(*s==' ') s++;
    char c = toupper((unsigned char)*s);
    return (c>='A'&&c<='Z') ? c : '#';
}
static void fmt_dur(char *b, size_t n, int ms){
    if(ms<=0){ b[0]=0; return; }
    long long t=((long long)ms+500)/1000; snprintf(b,n,"%lld:%02lld", t/60, t%60);
}

static lv_obj_t *base_row(void){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 268, ROW_H);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_bg_color(r, TC(LIST_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    return r;
}
static void row_two(lv_obj_t *r, const char *top, const char *sub, const char *right, kit_value_kind_t kind){
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, top); lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t, 12, sub&&sub[0]?6:16); lv_obj_set_size(t, right&&right[0]?186:242, 21);
    lv_obj_set_style_text_font(t, TF(USER_16), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_t *s = NULL;
    if(sub && sub[0]){
        s = lv_label_create(r);
        lv_label_set_text(s, sub); lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(s, 12, 28); lv_obj_set_size(s, right&&right[0]?186:242, 17);
        lv_obj_set_style_text_font(s, TF(USER_14), 0);
        lv_obj_set_style_text_color(s, TC(TEXT_SECONDARY), 0);
    }
    if(right && right[0]){
        lv_obj_t *rl = lv_label_create(r);
        lv_label_set_text(rl, right);
        lv_obj_set_pos(rl, 204, 17); lv_obj_set_size(rl, 52, 18);
        lv_obj_set_style_text_align(rl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(rl, TF(UI_14), 0);
        lv_obj_set_style_text_color(rl, TC(TEXT_MUTED), 0);
        kit_row_value(r, t, s, rl, kind);
    }
}

static int missing_artist_play(int id){
    mdb_plan_t plan;
    if(!mdb_artist_plan("", &plan)){ ui_toast("Couldn't play that list"); return 0; }
    if(id < 0) id = plan.ids[id == -2 ? rand() % plan.count : 0];
    int ok = ui_play_plan(&plan, id);
    mdb_plan_free(&plan);
    return ok;
}
static void song_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int id=(int)(uintptr_t)lv_event_get_user_data(e);
    if(g_view==VIEW_GROUP && g_drill_kind==2 && !g_drill[0]){
        if(!missing_artist_play(id)) return;
    } else if(!g_song_cb || !g_song_cb(id)) return;
    screen_show(SCR_NOWPLAYING);
}
static void reload_async(void *p){ (void)p; library_reload(); }

static lv_obj_t *g_fav_modal;
static int       g_fav_pending_id;
static void fav_modal_close(void){
    if(g_fav_modal){ lv_obj_delete_async(g_fav_modal); g_fav_modal = NULL; }
}
static void fav_cancel_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) fav_modal_close(); }
static void fav_confirm_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    mdb_unfavorite(g_fav_pending_id);
    fav_modal_close();
    lv_async_call(reload_async, NULL);
}
static void fav_modal_pill(lv_obj_t *card, int x, const char *txt, theme_color_role_t col, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(card);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 108, 42); lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -16);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, "library.cb", UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, theme_color(col), 0);
    lv_obj_center(l);
}
static void fav_confirm(int i){
    g_fav_pending_id = g_favs[i].love_id;
    const char *title = g_favs[i].title[0] ? g_favs[i].title : "this song";
    fav_modal_close();
    g_fav_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_fav_modal);
    lv_obj_set_size(g_fav_modal, 360, 360); lv_obj_center(g_fav_modal);
    lv_obj_set_style_bg_color(g_fav_modal, TC(SCRIM), 0);
    lv_obj_set_style_bg_opa(g_fav_modal, LV_OPA_70, 0);
    lv_obj_clear_flag(g_fav_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_fav_modal, LV_OBJ_FLAG_CLICKABLE);
    ui_on(g_fav_modal, fav_cancel_cb, LV_EVENT_CLICKED, NULL, "library.fav_cancel", UI_CORE);
    lv_obj_t *card = lv_obj_create(g_fav_modal);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 264, 168); lv_obj_center(card);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, "Remove from Favourites?");
    lv_obj_set_style_text_font(t, TF(UI_16), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t *s = lv_label_create(card);
    lv_label_set_text(s, title);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s, 224);
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s, TF(USER_14), 0);
    lv_obj_set_style_text_color(s, TC(TEXT_MUTED), 0);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 52);
    fav_modal_pill(card, -58, "Cancel", THEME_CLR_TEXT_SECONDARY, fav_cancel_cb);
    fav_modal_pill(card,  58, "Remove", THEME_CLR_STATUS_DANGER, fav_confirm_cb);
}

static void fav_row_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    int i = (int)(uintptr_t)lv_event_get_user_data(e);
    if(g_view != VIEW_FAVS || i < 0 || i >= g_count) return;
    if(c == LV_EVENT_SHORT_CLICKED){
        int ok = fw_fav_play_by_love_id() ? ui_play_favorite(g_favs[i].love_id, i + 1)
                                          : (g_song_cb && g_song_cb(g_favs[i].id));
        if(ok) screen_show(SCR_NOWPLAYING);
    } else if(c == LV_EVENT_LONG_PRESSED){
        fav_confirm(i);
    }
}
static void playlist_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int i=(int)(intptr_t)lv_event_get_user_data(e);
    plview_open(g_plids[i], g_plnames[i]);
}
static void pl_new_done(const char *name){
    if(name && name[0] && mdb_playlist_create(name) > 0) library_reload();
}
static void pl_new_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    kbinput_open("Playlist name", "", pl_new_done);
}
static void group_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED) return;
    int gi=(int)(uintptr_t)lv_event_get_user_data(e);
    g_drill_kind = (g_view==VIEW_ALBUMS)?1:(g_view==VIEW_GENRES)?3:2;
    snprintf(g_drill, MDB_STR, "%s", g_gnames[gi]);
    g_scope_from_albums = 0;
    g_scope_lvl = (g_drill_kind != 1 && mdb_scope_albums(g_drill_kind==3 ? MDB_SCOPE_GENRE : MDB_SCOPE_ARTIST, g_drill, NULL, NULL, NULL, 0) > 0) ? 1 : 0;
    int keep=g_view; g_view=VIEW_GROUP; (void)keep;
    g_deeplink=0;
    library_reload();
}
static void group_play_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_LONG_PRESSED) return;
    int gi=(int)(uintptr_t)lv_event_get_user_data(e);
    int kind = (g_view==VIEW_ALBUMS)?1:(g_view==VIEW_GENRES)?3:2;
    int lt   = (kind==1)?3:(kind==3)?10:2;
    ui_set_workmode(0);
    if(lt == 2 && !g_gnames[gi][0]){
        if(!missing_artist_play(-1)) return;
    } else if(lt == 2 && mdb_artist_mode()){
        mdb_plan_t plan; int ok = 0;
        if(!mdb_artist_plan(g_gnames[gi], &plan)){ ui_toast("Couldn't play that list"); return; }
        ok = ui_play_plan(&plan, plan.ids[0]); mdb_plan_free(&plan);
        if(!ok) return;
    } else if(!ui_play_list(lt, g_gnames[gi], 1)) return;
    screen_show(SCR_NOWPLAYING);
}
static void menu_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int v=(int)(uintptr_t)lv_event_get_user_data(e);
    if(v==CAT_FOLDERS){ folderbrowser_open(); return; }
    if(v==CAT_BOOKS){ books_open(); return; }
    if(v==VIEW_ALBUMS && cfg_get_int("album_view", 0)==1){ screen_show(SCR_ALBUMWALL); return; }
    g_view=v;
    g_drill_kind=0; library_reload();
}
int library_back(void){
    if(g_view==VIEW_GROUP){
        if(g_drill_kind!=1 && g_scope_lvl==2){ g_scope_lvl=1; library_reload(); return 1; }
        if(g_drill_kind!=1 && g_scope_lvl==0 && g_scope_from_albums){ g_scope_from_albums=0; g_scope_lvl=1; library_reload(); return 1; }
        if(g_deeplink){
            g_deeplink=0; g_view=VIEW_MENU; g_drill_kind=0; library_reload(); return 0;
        }
        g_view=(g_drill_kind==1)?VIEW_ALBUMS:(g_drill_kind==3)?VIEW_GENRES:VIEW_ARTISTS;
        g_drill_kind=0; library_reload(); return 1;
    }
    if(g_deeplink && g_view==VIEW_ARTISTS){
        g_deeplink=0; g_view=VIEW_MENU; g_drill_kind=0; library_reload(); return 0;
    }
    if(g_view==VIEW_MOSTPLAYED || g_view==VIEW_RECENT){ g_view=VIEW_HISTORY; library_reload(); return 1; }
    if(g_view!=VIEW_MENU){ g_view=VIEW_MENU; library_reload(); return 1; }
    return 0;
}
static void back_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(!library_back()) screen_back();
}

static int scope_kind(void){ return g_drill_kind==3 ? MDB_SCOPE_GENRE : MDB_SCOPE_ARTIST; }
static void scope_all_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED) return;
    g_scope_lvl = 0; g_scope_from_albums = 1; library_reload();
}
static void scope_album_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED) return;
    int ai=(int)(uintptr_t)lv_event_get_user_data(e);
    if(g_view!=VIEW_GROUP || g_scope_lvl!=1 || ai<0 || ai>=g_count-1) return;
    snprintf(g_scope_album, MDB_STR, "%s", g_gnames[ai]);
    g_scope_lvl = 2; library_reload();
}
static int scope_play_plan(mdb_plan_t *plan, int song_id){
    if(plan->list_type == 8){
        int pos = mdb_plan_pos(plan, song_id);
        if(pos < 1){ ui_toast("Couldn't find that song"); return 0; }
        return ui_play_list(8, plan->name, pos);
    }
    return ui_play_plan(plan, song_id);
}
static int scope_play_album(const char *album, int song_id){
    mdb_plan_t plan;
    if(!mdb_scope_album_plan(scope_kind(), g_drill, album, &plan)){ ui_toast("Couldn't play that list"); return 0; }
    if(song_id == -2 && plan.count > 1) song_id = plan.ids[rand() % plan.count];
    else if(song_id < 0) song_id = plan.ids[0];
    int ok = scope_play_plan(&plan, song_id);
    mdb_plan_free(&plan);
    return ok;
}
static void scope_song_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int id=(int)(uintptr_t)lv_event_get_user_data(e);
    if(!scope_play_album(g_scope_album, id)) return;
    screen_show(SCR_NOWPLAYING);
}
static void scope_album_play_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_LONG_PRESSED) return;
    int ai=(int)(uintptr_t)lv_event_get_user_data(e);
    if(g_view!=VIEW_GROUP || g_scope_lvl!=1 || ai<0 || ai>=g_count-1) return;
    ui_set_workmode(0);
    if(!scope_play_album(g_gnames[ai], -1)) return;
    screen_show(SCR_NOWPLAYING);
}

static void add_row(int i){
    char dur[12];
    lv_obj_t *r = base_row();
    if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==1){
        char cnt[12];
        if(i==0){
            snprintf(cnt, sizeof cnt, "%d", g_scope_total);
            ui_on(r, scope_all_cb, LV_EVENT_SHORT_CLICKED, NULL, "library.scope_all", UI_CORE);
            row_two(r, tr("All Songs"), NULL, cnt, KIT_VALUE_COUNT);
        } else {
            snprintf(cnt, sizeof cnt, "%d", g_gcounts[i-1]);
            ui_on(r, scope_album_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)(i-1), "library.scope_album", UI_CORE);
            ui_on(r, scope_album_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)(i-1), "library.scope_album_play.long", UI_CORE);
            row_two(r, g_gnames[i-1], g_drill_kind==3 ? g_gartists[i-1] : NULL, cnt, KIT_VALUE_COUNT);
        }
        theme_list_row(r);
        return;
    }
    switch(g_view){
        case VIEW_SONGS: case VIEW_GROUP:
            if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==2)
                ui_on(r, scope_song_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)g_buf[i]->id, "library.scope_song", UI_CORE);
            else
                ui_on(r, song_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)g_buf[i]->id, "library.song", UI_CORE);
            fmt_dur(dur,sizeof dur,g_buf[i]->dur_ms);
            if(g_view==VIEW_GROUP && (g_drill_kind==1 || g_scope_lvl==2) && g_buf[i]->track > 0 && cfg_get_int("track_numbers", 0)){
                char num[MDB_STR + 16];
                snprintf(num, sizeof num, "%d %s", g_buf[i]->track, g_buf[i]->title[0]?g_buf[i]->title:"Untitled");
                row_two(r, num, g_buf[i]->artist, dur, KIT_VALUE_DURATION);
            } else
            row_two(r, g_buf[i]->title[0]?g_buf[i]->title:"Untitled", g_buf[i]->artist, dur, KIT_VALUE_DURATION);
            break;
        case VIEW_ALBUMS: {
            ui_on(r, group_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.group", UI_CORE);
            ui_on(r, group_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.group_play.long", UI_CORE);
            char cnt[12]; snprintf(cnt,sizeof cnt,"%d",g_gcounts[i]);
            row_two(r, g_gnames[i], mdb_artist_missing(g_gartists[i]) ? "Unknown artist" : g_gartists[i], cnt, KIT_VALUE_COUNT);
            break; }
        case VIEW_ARTISTS:
            ui_on(r, group_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.group", UI_CORE);
            ui_on(r, group_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.group_play.long", UI_CORE);
            row_two(r, g_gnames[i][0] ? g_gnames[i] : "Unknown artist", NULL, NULL, KIT_VALUE_COUNT);
            break;
        case VIEW_GENRES: {
            ui_on(r, group_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.group", UI_CORE);
            ui_on(r, group_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.group_play.long", UI_CORE);
            char cnt[12]; snprintf(cnt,sizeof cnt,"%d",g_gcounts[i]);
            row_two(r, g_gnames[i], NULL, cnt, KIT_VALUE_COUNT);
            break; }
        case VIEW_FAVS:
            ui_on(r, fav_row_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.fav_row", UI_CORE);
            ui_on(r, fav_row_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.fav_row.long", UI_CORE);
            fmt_dur(dur,sizeof dur,g_favs[i].dur_ms);
            row_two(r, g_favs[i].title, g_favs[i].artist, dur, KIT_VALUE_DURATION);
            break;
        case VIEW_MOSTPLAYED: case VIEW_RECENT:
            ui_on(r, song_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)g_favs[i].id, "library.song", UI_CORE);
            fmt_dur(dur,sizeof dur,g_favs[i].dur_ms);
            row_two(r, g_favs[i].title[0]?g_favs[i].title:"Untitled", g_favs[i].artist, dur, KIT_VALUE_DURATION);
            break;
    }
    theme_list_row(r);
}
static void fill_stop(void){
    if(g_fill_timer){ lv_timer_del(g_fill_timer); g_fill_timer=NULL; }
    g_jump_idx = -1;
}
static void jump_finish(void){
    if(g_jump_idx < 0) return;
    int need = g_jump_idx + 1 + (LIST_H + ROW_H + 3)/(ROW_H+4);
    if(g_fill_i < need && g_fill_i < g_fill_n) return;
    int idx = g_jump_idx;
    g_jump_idx = -1;
    lv_obj_scroll_to_y(g_list, (idx + g_has_header)*(ROW_H+4), LV_ANIM_OFF);
}
void library_jump_cancel(void){ g_jump_idx = -1; }
static void list_scroll_cb(lv_event_t *e){ (void)e; library_jump_cancel(); }
static void fill_cb(lv_timer_t *t){
    if(screen_current() != SCR_LIBRARY){
        g_jump_idx = -1;
        lv_timer_pause(t);
        return;
    }
    uint32_t start = lv_tick_get();
    int end = g_fill_i + 40; if(end > g_fill_n) end = g_fill_n;
    do { add_row(g_fill_i++); }
    while(g_fill_i < end && lv_tick_elaps(start) < 4);
    jump_finish();
    if(g_fill_i >= g_fill_n) fill_stop();
    (void)t;
}
static void fill_start(int n){
    fill_stop();
    g_fill_n = n; g_fill_i = 0;
    int first = n < 18 ? n : 18;
    for(; g_fill_i < first; g_fill_i++) add_row(g_fill_i);
    if(g_fill_i < g_fill_n){
        g_fill_timer = lv_timer_create(fill_cb, 16, NULL);
        if(screen_current() != SCR_LIBRARY) lv_timer_pause(g_fill_timer);
    }
}

static void jump_to_letter(char L){
    if(g_count<=0) return;
    int idx=-1;
    for(int i=0;i<g_count;i++) if(g_first[i]==L){ idx=i; break; }
    if(idx<0) for(int i=0;i<g_count;i++) if(g_first[i]>=L){ idx=i; break; }
    if(idx<0) idx=g_count-1;
    g_jump_idx = idx;
    jump_finish();
}
static void letter_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    jump_to_letter((char)(intptr_t)lv_event_get_user_data(e));
    lv_obj_add_flag(g_grid, LV_OBJ_FLAG_HIDDEN);
}
static void grid_bg_cb(lv_event_t *e){
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) lv_obj_add_flag(g_grid, LV_OBJ_FLAG_HIDDEN);
}
static void az_btn_cb(lv_event_t *e){
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) lv_obj_clear_flag(g_grid, LV_OBJ_FLAG_HIDDEN);
}
static void az_show(int on){
    if(on) lv_obj_clear_flag(g_az_btn, LV_OBJ_FLAG_HIDDEN);
    else { lv_obj_add_flag(g_az_btn, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_grid, LV_OBJ_FLAG_HIDDEN); }
    if(g_list){
        lv_obj_set_x(g_list, on ? 30 : 37);
        lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, on ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
}

static lv_obj_t *empty_label(const char *txt){
    lv_obj_t *l = lv_label_create(g_list);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, 268);
    theme_kit()->empty_state(KIT_EMPTY_LIBRARY, l);
    return l;
}
void kit_stone_empty_scan(lv_obj_t *message, lv_obj_t *button);
static void scan_action_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    ui_rescan_library();
}
static void empty_scan(const char *txt){
    lv_obj_t *message = empty_label(txt);
    lv_obj_t *b = lv_button_create(g_list);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 200, 46);
    lv_obj_set_style_radius(b, 23, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    ui_on(b, scan_action_cb, LV_EVENT_CLICKED, NULL, "library.scan_action", UI_CORE);
    if(!strcmp(theme_kit()->id, "default")){
        lv_obj_update_layout(b);
        lv_area_t a; lv_obj_get_coords(b, &a);
        lv_obj_set_style_translate_y(b, 216 - (a.y1 + a.y2) / 2, 0);
    }
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, tr_sym(LV_SYMBOL_REFRESH, "Scan Library"));
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_update_layout(l);
    int lw = lv_obj_get_width(l);
    if(lw > 250){
        lv_obj_set_style_text_font(l, TF(UI_14), 0);
        lv_obj_update_layout(l);
        lw = lv_obj_get_width(l);
    }
    if(lw > 250){
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_size(l, 250, lv_font_get_line_height(TF(UI_14)));
        lw = 250;
    }
    if(lw + 36 > 200) lv_obj_set_width(b, lw + 36);
    lv_obj_center(l);
    kit_stone_empty_scan(message, b);
}

static int current_list_context(int *lt, char *name, int cap){
    if(g_view==VIEW_SONGS){ if(lt)*lt=1; if(name&&cap>0)name[0]=0; return 1; }
    if(g_view==VIEW_FAVS){  if(lt)*lt=6; if(name&&cap>0)name[0]=0; return 1; }
    if(g_view==VIEW_GROUP)  return library_drill_context(lt, name, cap);
    return 0;
}
static void play_list_mode(int mode){
    if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==2){
        cfg_set_int("work_mode", mode); ui_set_workmode(mode);
        if(scope_play_album(g_scope_album, mode == 1 ? -2 : -1)) screen_show(SCR_NOWPLAYING);
        return;
    }
    int lt; char nm[160];
    if(!current_list_context(&lt, nm, sizeof nm)) return;
    cfg_set_int("work_mode", mode); ui_set_workmode(mode);
    int pos = 1;
    if(mode == 1 && g_count > 1 && lt != 2) pos = 1 + (rand() % g_count);
    if(lt == 6 && fw_fav_play_by_love_id()){ if(pos > g_count || !ui_play_favorite(g_favs[pos-1].love_id, pos)) return; }
    else if(lt == 2 && !nm[0]){
        if(!missing_artist_play(mode == 1 ? -2 : -1)) return;
    }
    else if(lt == 2 && mdb_artist_mode()){
        mdb_plan_t plan; int ok = 0;
        if(!mdb_artist_plan(nm, &plan)){ ui_toast("Couldn't play that list"); return; }
        int i = (mode == 1 && plan.count > 1) ? rand() % plan.count : 0;
        ok = ui_play_plan(&plan, plan.ids[i]);
        mdb_plan_free(&plan);
        if(!ok) return;
    }
    else if(!ui_play_list(lt, nm, pos)) return;
    screen_show(SCR_NOWPLAYING);
}
static void play_all_cb(lv_event_t *e){    if(lv_event_get_code(e)==LV_EVENT_CLICKED) play_list_mode(0); }
static void shuffle_all_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) play_list_mode(1); }

static void hdr_btn(lv_obj_t *row, int x, const char *txt, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 128, 44); lv_obj_set_pos(b, x, 4);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, "library.cb", UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_14), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
}
static int g_hdr_cover_ctr;
static void add_album_cover_header(const char *album, const char *artist){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    artist = mdb_artist_missing(artist) ? "Unknown artist" : artist;
    int has_artist = 1;
    int hdr_h = has_artist ? 196 : 172;
    lv_obj_set_size(r, 286, hdr_h);
    g_hdr_extra_px = hdr_h + 4;
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    const int CX = 7;

    char src[64]; src[0] = 0;
    int ids[6]; int n = mdb_album_track_ids(album, ids, 6);
    for(int i=0;i<n;i++){
        char track[512]; track[0]=0;
        char tmp[48]; snprintf(tmp, sizeof tmp, "/tmp/album_hdr%d.bmp", g_hdr_cover_ctr & 3);
        if(mdb_song_path(ids[i], track, sizeof track) && track[0] && artcache_get_cover(track, tmp)==0){
            snprintf(src, sizeof src, "A:%s", tmp); g_hdr_cover_ctr++;
            break;
        }
    }
    if(src[0]){
        lv_obj_t *img = lv_image_create(r);
        lv_image_set_src(img, src);
        lv_obj_align(img, LV_ALIGN_TOP_MID, CX, 10);
    } else {
        lv_obj_t *ph = lv_obj_create(r);
        lv_obj_remove_style_all(ph);
        lv_obj_set_size(ph, 148, 148); lv_obj_align(ph, LV_ALIGN_TOP_MID, CX, 10);
        lv_obj_set_style_bg_color(ph, TC(SURFACE_RAISED), 0);
        lv_obj_set_style_bg_opa(ph, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(ph, 12, 0);
        lv_obj_t *in = lv_label_create(ph);
        char init[8]={0};
        if(album && album[0]){ unsigned char c0=(unsigned char)album[0];
            int len=(c0<0x80)?1:((c0>>5)==0x6)?2:((c0>>4)==0xE)?3:((c0>>3)==0x1E)?4:1;
            for(int i=0;i<len && album[i];i++) init[i]=album[i];
            if(len==1 && init[0]>='a'&&init[0]<='z') init[0]-=32;
        } else init[0]='?';
        lv_label_set_text(in, init);
        lv_obj_set_style_text_font(in, (unsigned char)init[0] < 0x80 ? TF(UI_28) : TF(USER_20), 0);
        lv_obj_set_style_text_color(in, TC(TEXT_MUTED), 0);
        lv_obj_center(in);
    }
    if(has_artist){
        lv_obj_t *a = lv_label_create(r);
        lv_label_set_long_mode(a, LV_LABEL_LONG_DOT); lv_obj_set_width(a, 240);
        lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(a, artist);
        lv_obj_set_style_text_font(a, TF(USER_16), 0);
        lv_obj_set_style_text_color(a, TC(TEXT_MUTED), 0);
        lv_obj_align(a, LV_ALIGN_TOP_MID, CX, 166);
    }
}

static void add_play_header(int centered){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, centered ? 286 : 268, ROW_H);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    int base = centered ? 16 : 0;
    hdr_btn(r, base,       tr_sym(LV_SYMBOL_PLAY, "Play All"), play_all_cb);
    hdr_btn(r, base + 140, tr_sym(LV_SYMBOL_SHUFFLE, "Shuffle"), shuffle_all_cb);
    g_has_header = 1;
}

static void add_plist_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(!g_buf || g_count <= 0 || (g_view!=VIEW_GROUP)){ ui_toast("Nothing to add"); return; }
    int *ids = malloc((size_t)g_count * sizeof *ids);
    if(!ids){ ui_toast("Out of memory"); return; }
    for(int i=0;i<g_count;i++) ids[i] = g_buf[i]->id;
    plpick_set_ids(ids, g_count);
    free(ids);
    screen_show(SCR_PLPICK);
}
static void add_plist_row(int centered){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, centered ? 286 : 268, ROW_H);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    hdr_btn(r, (centered ? 16 : 0) + 70, tr_sym(LV_SYMBOL_PLUS, "Playlist"), add_plist_cb);
    g_has_header = 2;
}

static const char *VIEW_TITLE[] = { "Library","Songs","Albums","Artists","Playlists","Favourites","Genres","","Most Played","Recently Played","History" };
_Static_assert(sizeof(VIEW_TITLE)/sizeof(VIEW_TITLE[0]) == VIEW_COUNT, "VIEW_TITLE must have one entry per view");

static void library_reload(void){
    if(!g_list) return;
    if(g_lhint){ lv_obj_add_flag(g_lhint, LV_OBJ_FLAG_HIDDEN); g_lhint_ch = 0; if(g_lhint_timer) lv_timer_pause(g_lhint_timer); }
    fill_stop();
    lv_obj_clean(g_list);
    g_count = 0;
    g_has_header = 0;
    g_hdr_extra_px = 0;
    lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);

    const char *ttl = (g_view==VIEW_GROUP) ? ((g_drill_kind!=1 && g_scope_lvl==2) ? g_scope_album : g_drill) : VIEW_TITLE[g_view];
    if(g_view==VIEW_GROUP && g_drill_kind==2 && g_scope_lvl!=2 && !g_drill[0]) ttl = "Unknown artist";
    theme_title_text(g_title, (g_view==VIEW_GROUP) ? ttl : tr(ttl));
    az_show(0);

    static int s_group_hold_hint=0, s_fav_hold_hint=0;
    if((g_view==VIEW_ALBUMS || g_view==VIEW_ARTISTS || g_view==VIEW_GENRES) && !s_group_hold_hint){
        s_group_hold_hint=1; ui_toast("Hold an item to play all");
    } else if(g_view==VIEW_FAVS && !s_fav_hold_hint){
        s_fav_hold_hint=1; ui_toast("Hold to remove");
    }

    char dur[12];

    if(g_view==VIEW_MENU){
        static const char *CATS[] = { "Songs","Albums","Artists","Genres","Playlists","Favourites","Folders","Books","History" };
        static const int   CATV[] = { VIEW_SONGS,VIEW_ALBUMS,VIEW_ARTISTS,VIEW_GENRES,VIEW_PLAYLISTS,VIEW_FAVS,CAT_FOLDERS,CAT_BOOKS,VIEW_HISTORY };
        for(int i=0;i<(int)(sizeof CATS/sizeof CATS[0]);i++){
            if(!DISKOS_AUDIOBOOKS && CATV[i]==CAT_BOOKS) continue;
            lv_obj_t *r = base_row();
            ui_on(r, menu_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)CATV[i], "library.menu", UI_CORE);
            row_two(r, tr(CATS[i]), NULL, NULL, KIT_VALUE_COUNT);
            lv_obj_t *ch = lv_label_create(r);
            lv_label_set_text(ch, LV_SYMBOL_RIGHT);
            lv_obj_set_pos(ch, 240, 17);
            lv_obj_set_style_text_color(ch, TC(TEXT_DISABLED), 0);
            theme_list_row(r);
        }
        return;
    }
    if(g_view==VIEW_HISTORY){
        static const char *HCATS[] = { "Most Played","Recently Played" };
        static const int   HCATV[] = { VIEW_MOSTPLAYED, VIEW_RECENT };
        for(int i=0;i<2;i++){
            lv_obj_t *r = base_row();
            ui_on(r, menu_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)HCATV[i], "library.menu", UI_CORE);
            row_two(r, tr(HCATS[i]), NULL, NULL, KIT_VALUE_COUNT);
            lv_obj_t *ch = lv_label_create(r);
            lv_label_set_text(ch, LV_SYMBOL_RIGHT);
            lv_obj_set_pos(ch, 240, 17);
            lv_obj_set_style_text_color(ch, TC(TEXT_DISABLED), 0);
            theme_list_row(r);
        }
        return;
    }

    (void)dur;
    if(g_view==VIEW_SONGS || g_view==VIEW_GROUP){
        lib_alloc_buffers(mdb_song_count());
    }
    if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==1){
        lib_ensure_group_cap(mdb_scope_albums(scope_kind(), g_drill, NULL, NULL, NULL, 0) + 1);
        int n = mdb_scope_albums(scope_kind(), g_drill, g_gnames, g_gartists, g_gcounts, g_grp_cap);
        int cap = g_grp_cap < g_first_cap ? g_grp_cap : g_first_cap;
        if(n > cap - 1) n = cap - 1;
        if(n <= 0) g_scope_lvl = 0;
        else {
            g_scope_total = (g_drill_kind==3) ? mdb_genre_songs(g_drill, g_buf, g_alloc_n) : mdb_artist_songs(g_drill, g_buf, g_alloc_n);
            g_first[0] = '#';
            for(int i=0;i<n;i++) g_first[i+1]=first_letter(g_gnames[i]);
            g_count = n + 1; fill_start(n + 1);
            if(n > 12) az_show(1);
            return;
        }
    }
    if(g_view==VIEW_SONGS || g_view==VIEW_GROUP){
        lib_ensure_first_cap(g_alloc_n);
        int cap = g_alloc_n < g_first_cap ? g_alloc_n : g_first_cap;
        int n;
        if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==2) n=mdb_scope_album_songs(scope_kind(),g_drill,g_scope_album,g_buf,cap);
        else if(g_view==VIEW_GROUP) n=(g_drill_kind==1)?mdb_album_songs(g_drill,g_buf,cap):(g_drill_kind==3)?mdb_genre_songs(g_drill,g_buf,cap):mdb_artist_songs(g_drill,g_buf,cap);
        else { n=mdb_song_count(); if(n>cap) n=cap; for(int i=0;i<n;i++) g_buf[i]=mdb_song(i); }
        if(n<=0){ empty_scan(tr("No songs found")); return; }
        int is_album = (g_view==VIEW_GROUP && (g_drill_kind==1 || g_scope_lvl==2));
        if(is_album) add_album_cover_header(g_scope_lvl==2 && g_drill_kind!=1 ? g_scope_album : g_drill, n>0 ? g_buf[0]->artist : NULL);
        add_play_header(is_album);
        if(g_view==VIEW_GROUP) add_plist_row(is_album);
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_buf[i]->title);
        g_count=n; fill_start(n);
        if(g_view==VIEW_SONGS) az_show(1);
    } else if(g_view==VIEW_ALBUMS){
        lib_ensure_group_cap(mdb_album_count());
        int cap = g_grp_cap < g_first_cap ? g_grp_cap : g_first_cap;
        int n=mdb_albums(g_gnames,g_gartists,g_gcounts,cap);
        if(n<=0){ empty_scan(tr("No albums found")); return; }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_gnames[i]);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_ARTISTS){
        lib_ensure_group_cap(mdb_artist_count());
        int cap = g_grp_cap < g_first_cap ? g_grp_cap : g_first_cap;
        int n=mdb_artists(g_gnames,cap);
        if(n<=0){ empty_scan(tr("No artists found")); return; }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_gnames[i]);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_GENRES){
        lib_ensure_group_cap(mdb_genre_count());
        int cap = g_grp_cap < g_first_cap ? g_grp_cap : g_first_cap;
        int n=mdb_genres(g_gnames,g_gcounts,cap);
        if(n<=0){ empty_scan(tr("No genres found")); return; }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_gnames[i]);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_FAVS){
        int need = mdb_favorite_count();
        if(need < 0) need = mdb_total_song_count();
        if(need < 0) need = mdb_song_count() > 256 ? mdb_song_count() : 256;
        lib_ensure_fav_cap(need);
        lib_ensure_first_cap(need);
        int n=mdb_favorites(g_favs, g_fav_cap < g_first_cap ? g_fav_cap : g_first_cap, fw_fav_play_by_love_id());
        if(n<=0){ empty_label(tr("No Favourites yet")); return; }
        add_play_header(0);
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_favs[i].title);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_MOSTPLAYED || g_view==VIEW_RECENT){
        int need = mdb_stats_count(g_view==VIEW_RECENT);
        if(need < 0) need = mdb_total_song_count();
        if(need < 0) need = mdb_song_count() > 256 ? mdb_song_count() : 256;
        lib_ensure_fav_cap(need);
        int n = (g_view==VIEW_MOSTPLAYED) ? mdb_mostplayed(g_favs, g_fav_cap) : mdb_recent(g_favs, g_fav_cap);
        if(n<=0){ empty_label(tr("Nothing played yet")); return; }
        g_count=n; fill_start(n);
    } else {
        int num = mdb_playlist_num();
        if(num > g_plcap && (size_t)num <= ((size_t)-1) / MDB_STR){
            char (*nn)[MDB_STR] = realloc(g_plnames, (size_t)num*MDB_STR);
            long  *ni           = realloc(g_plids,   (size_t)num*sizeof(long));
            int   *nc           = realloc(g_plcounts, (size_t)num*sizeof(int));
            if(nc) g_plcounts = nc;
            if(nn) g_plnames = nn;
            if(ni) g_plids   = ni;
            if(nn && ni && nc) g_plcap = num;
        }
        int n = (g_plcap > 0 && g_plnames && g_plids && g_plcounts) ? mdb_playlists_counted(g_plnames, g_plids, g_plcounts, g_plcap) : 0;
        lv_obj_t *nr=base_row();
        ui_on(nr, pl_new_cb, LV_EVENT_CLICKED, NULL, "library.pl_new", UI_CORE);
        kit_row_layout(nr, KIT_ROW_SYMBOL);
        row_two(nr, tr_sym(LV_SYMBOL_PLUS, "New Playlist"), NULL, NULL, KIT_VALUE_COUNT);
        theme_list_row(nr);
        for(int i=0;i<n;i++){
            lv_obj_t *r=base_row();
            ui_on(r, playlist_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i, "library.playlist", UI_CORE);
            char cnt[16]; snprintf(cnt,sizeof cnt,"%d", g_plcounts[i]);
            row_two(r, g_plnames[i], NULL, cnt, KIT_VALUE_COUNT);
            theme_list_row(r);
        }
    }
}

void library_set_song_click_cb(library_song_click_cb_t cb){ g_song_cb=cb; }

void library_refresh(void){ if(g_list) library_reload(); }
void library_enter(void){
    g_jump_idx = -1;
    if(g_view == VIEW_GROUP || g_view == VIEW_PLAYLISTS || g_view == VIEW_FAVS ||
       g_view == VIEW_MOSTPLAYED || g_view == VIEW_RECENT) library_refresh();
    else {
        if(g_lhint){ lv_obj_add_flag(g_lhint, LV_OBJ_FLAG_HIDDEN); g_lhint_ch = 0; if(g_lhint_timer) lv_timer_pause(g_lhint_timer); }
        if(g_list) lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);
    }
    if(g_fill_timer) lv_timer_resume(g_fill_timer);
}
void library_artist_mode_changed(void){
    if(g_view == VIEW_GROUP && g_drill_kind == 2){ g_view = VIEW_ARTISTS; g_drill_kind = 0; }
    if(g_list) library_reload();
}
lv_obj_t *library_scroller(void){ return g_list; }

int library_drill_context(int *list_type, char *name, int cap){
    if(g_view != VIEW_GROUP) return 0;
    int lt = (g_drill_kind==1)?3 : (g_drill_kind==3)?10 : 2;
    if(list_type) *list_type = lt;
    if(name && cap>0) snprintf(name, cap, "%s", g_drill);
    return 1;
}

void library_open_album(const char *name){
    g_view=VIEW_GROUP; g_drill_kind=1; g_scope_lvl=0; g_scope_from_albums=0; g_deeplink=1; snprintf(g_drill, MDB_STR, "%s", name); library_reload();
}
void library_open_artist(const char *name){
    g_view=VIEW_GROUP; g_drill_kind=2; g_scope_lvl=0; g_scope_from_albums=0; g_deeplink=1; snprintf(g_drill, MDB_STR, "%s", name); library_reload();
}

static void lhint_timer_cb(lv_timer_t *t){
    if(!g_lhint || lv_obj_has_flag(g_lhint, LV_OBJ_FLAG_HIDDEN)){ lv_timer_pause(t); return; }
    if(lv_tick_elaps(g_lhint_tick) > 650){
        lv_obj_add_flag(g_lhint, LV_OBJ_FLAG_HIDDEN);
        lv_timer_pause(t);
    }
}
void library_scroll_letter_tick(void){
    if(!g_list || !g_lhint || g_count <= 0) return;
    if(!(g_view==VIEW_SONGS || g_view==VIEW_GROUP || g_view==VIEW_ALBUMS ||
         g_view==VIEW_ARTISTS || g_view==VIEW_GENRES || g_view==VIEW_FAVS)) return;
    int pitch = ROW_H + 4;
    int idx = (lv_obj_get_scroll_y(g_list) - g_hdr_extra_px)/pitch - g_has_header;
    if(idx < 0) idx = 0; else if(idx >= g_count) idx = g_count - 1;
    char ch = g_first[idx];
    if(ch && ch != g_lhint_ch){ g_lhint_ch = ch; char b[2]={ch,0}; lv_label_set_text(g_lhint_lbl, b); }
    if(g_lhint_timer) lv_timer_resume(g_lhint_timer);
    g_lhint_tick = lv_tick_get();
    lv_obj_clear_flag(g_lhint, LV_OBJ_FLAG_HIDDEN);
}

static void lib_alloc_buffers(int songs){
    if(songs <= g_alloc_n) return;
    const mdb_song_t **buf = realloc(g_buf, (size_t)songs * sizeof *buf);
    if(buf){ g_buf = buf; g_alloc_n = songs; }
}
void library_ensure_capacity(void){
}
static void lib_ensure_first_cap(int need){
    if(need <= g_first_cap) return;
    char *f = realloc(g_first, (size_t)need);
    if(f){ g_first = f; g_first_cap = need; }
}
static void lib_ensure_group_cap(int need){
    if(need > g_grp_cap){
        char (*a)[MDB_STR] = realloc(g_gnames, (size_t)need * sizeof *a);
        if(!a) return;
        g_gnames = a;
        char (*b)[MDB_STR] = realloc(g_gartists, (size_t)need * sizeof *b);
        if(!b) return;
        g_gartists = b;
        int *c = realloc(g_gcounts, (size_t)need * sizeof *c);
        if(!c) return;
        g_gcounts = c;
        g_grp_cap = need;
    }
    lib_ensure_first_cap(need);
}
static void lib_ensure_fav_cap(int need){
    if(need > g_fav_cap){
        mdb_song_t *f = realloc(g_favs, (size_t)need * sizeof *f);
        if(f){ g_favs = f; g_fav_cap = need; }
    }
}

void library_create(lv_obj_t *root){
    if(theme_screen_plain(THEME_PLAIN_LIBRARY)) theme_screen_solid(root);
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    g_title = ui_header_cb(root, "Library", back_cb);

    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, UI_LIST_X, LIST_Y); lv_obj_set_size(g_list, 286, LIST_H);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_bottom(g_list, UI_LIST_PAD_BOTTOM, 0);
    lv_obj_set_style_pad_row(g_list, 4, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_event_cb(g_list, list_scroll_cb, LV_EVENT_SCROLL_BEGIN, NULL);

    g_az_btn = lv_button_create(root);
    lv_obj_remove_style_all(g_az_btn);
    lv_obj_set_pos(g_az_btn, 302, 158); lv_obj_set_size(g_az_btn, 44, 44);
    lv_obj_set_style_radius(g_az_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_az_btn, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(g_az_btn, LV_OPA_90, 0);
    ui_on(g_az_btn, az_btn_cb, LV_EVENT_CLICKED, NULL, "library.az_btn", UI_CORE);
    lv_obj_t *azl=lv_label_create(g_az_btn); lv_label_set_text(azl,"A-Z");
    lv_obj_set_style_text_font(azl,TF(UI_14),0);
    lv_obj_set_style_text_color(azl,TC(TEXT_PRIMARY),0); lv_obj_center(azl);
    lv_obj_add_flag(g_az_btn, LV_OBJ_FLAG_HIDDEN);

    g_grid = lv_obj_create(root);
    lv_obj_remove_style_all(g_grid);
    kit_keep(g_grid);
    lv_obj_set_size(g_grid, 360, 360); lv_obj_set_pos(g_grid, 0, 0);
    lv_obj_set_style_bg_color(g_grid, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_grid, LV_OPA_80, 0);
    lv_obj_add_flag(g_grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_grid, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(g_grid, grid_bg_cb, LV_EVENT_CLICKED, NULL, "library.grid_bg", UI_CORE);
    lv_obj_add_flag(g_grid, LV_OBJ_FLAG_HIDDEN);
    {
        lv_obj_t *gt = lv_label_create(g_grid);
        lv_label_set_text(gt, tr("Jump to"));
        lv_obj_align(gt, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_text_font(gt, TF(UI_14), 0);
        lv_obj_set_style_text_color(gt, TC(TEXT_MUTED), 0);
    }
    {
        static const char *AZ="ABCDEFGHIJKLMNOPQRSTUVWXYZ#";
        int cols=5, cw=52, ch=48, n=27;
        int gw=cols*cw, x0=(360-gw)/2;
        int rows=(n+cols-1)/cols;
        int y0=48;
        for(int i=0;i<n;i++){
            int r=i/cols, c=i%cols;
            int cells_in_row = (r==rows-1) ? (n - r*cols) : cols;
            int row_x0 = x0 + ((cols - cells_in_row)*cw)/2;
            lv_obj_t *cell=lv_button_create(g_grid);
            lv_obj_remove_style_all(cell);
            lv_obj_set_pos(cell, row_x0+c*cw, y0+r*ch); lv_obj_set_size(cell, cw-4, ch-4);
            lv_obj_set_style_radius(cell, 8, 0);
            lv_obj_set_style_bg_color(cell, TC(ACCENT_PRIMARY), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_set_style_text_color(cell, TC(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_color(cell, TC(ON_ACCENT), LV_STATE_PRESSED);
            ui_on(cell, letter_cb, LV_EVENT_CLICKED, (void*)(intptr_t)AZ[i], "library.letter", UI_CORE);
            lv_obj_t *l=lv_label_create(cell);
            char b[2]={AZ[i],0}; lv_label_set_text(l,b);
            lv_obj_set_style_text_font(l,TF(UI_20),0);
            lv_obj_center(l);
        }
    }

    g_lhint = lv_obj_create(root);
    lv_obj_remove_style_all(g_lhint);
    lv_obj_set_size(g_lhint, 96, 96);
    lv_obj_center(g_lhint);
    lv_obj_set_style_radius(g_lhint, 22, 0);
    lv_obj_set_style_bg_color(g_lhint, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(g_lhint, LV_OPA_80, 0);
    lv_obj_clear_flag(g_lhint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_lhint, LV_OBJ_FLAG_HIDDEN);
    g_lhint_lbl = lv_label_create(g_lhint);
    lv_obj_set_style_text_font(g_lhint_lbl, TF(UI_40), 0);
    lv_obj_set_style_text_color(g_lhint_lbl, TC(TEXT_PRIMARY), 0);
    lv_label_set_text(g_lhint_lbl, "A");
    lv_obj_center(g_lhint_lbl);
    g_lhint_timer = lv_timer_create(lhint_timer_cb, 150, NULL);
    if(g_lhint_timer) lv_timer_pause(g_lhint_timer);

    mdb_load();
    library_reload();
}
