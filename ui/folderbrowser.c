/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "sdio.h"
#include "folderbrowser.h"
#include "musicdb.h"
#include "scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include "ui_round.h"

#define FB_ROOT        "/tmp/sdcard"
#define FB_MAXPATH     1024
#define FB_NAMELEN     256
#define FB_MAX_ENTRIES 4000

#define FB_K_AUDIO 0
#define FB_K_ISO   1
#define FB_K_CUE   2
typedef struct {
    char name[FB_NAMELEN];
    int  is_dir;
    int  kind;
} fb_entry_t;

static char        g_dir[FB_MAXPATH];
static fb_entry_t *g_ent;
static int         g_nent;
static int         g_opened;
static lv_obj_t   *g_list;
static lv_obj_t   *g_title;
static lv_timer_t *g_fb_fill;
static int         g_fb_i;

static int fb_has_ext(const char *name, const char *ext){
    size_t nl = strlen(name), el = strlen(ext);
    return nl > el && !strcasecmp(name + nl - el, ext);
}
static int fb_is_audio(const char *name){
    return fb_has_ext(name, ".mp3") || fb_has_ext(name, ".flac")
        || fb_has_ext(name, ".wav") || fb_has_ext(name, ".m4a")
        || fb_has_ext(name, ".aac") || fb_has_ext(name, ".ogg") || fb_has_ext(name, ".ape")
        || fb_has_ext(name, ".aif") || fb_has_ext(name, ".aiff") || fb_has_ext(name, ".wma")
        || fb_has_ext(name, ".dsf") || fb_has_ext(name, ".dff") || fb_has_ext(name, ".dts")
        || fb_has_ext(name, ".opus") || fb_has_ext(name, ".oga");
}

static int fb_cmp(const void *a, const void *b){
    const fb_entry_t *x = a, *y = b;
    if(x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    return strcasecmp(x->name, y->name);
}

static void fb_fill_stop(void){ if(g_fb_fill){ lv_timer_del(g_fb_fill); g_fb_fill = NULL; } }

typedef struct {
    fb_entry_t *ent;
    int nent, cap, opened, cancelled;
    unsigned gen;
} fb_result_t;
static pthread_mutex_t g_fb_mu = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_fb_gen;
static int g_fb_running, g_fb_requested, g_fb_ready, g_fb_waiting;
static int g_fb_error_shown;
static char g_fb_path[FB_MAXPATH];
static fb_result_t g_fb_res;
static lv_timer_t *g_fb_poll;

static int fb_current(unsigned gen){
    pthread_mutex_lock(&g_fb_mu);
    int ok = gen == g_fb_gen && g_fb_requested;
    pthread_mutex_unlock(&g_fb_mu);
    return ok && sd_io_allowed();
}

static void fb_scan_leased(const char *dir, fb_result_t *res){
    DIR *d = opendir(dir);
    if(!d){
        if(fb_current(res->gen)){
            d = opendir(dir); res->opened = d != NULL;
            if(d) closedir(d);
        }
        return;
    }
    struct dirent *e;
    for(int pass = 0; pass < 2; pass++){
        for(errno = 0; fb_current(res->gen) && (e = readdir(d)); errno = 0){
            const char *nm = e->d_name;
            if(nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
            if(res->nent >= FB_MAX_ENTRIES) break;
            if(res->nent >= res->cap){
                int nc = res->cap ? res->cap * 2 : 128;
                if(nc > FB_MAX_ENTRIES) nc = FB_MAX_ENTRIES;
                fb_entry_t *ne = realloc(res->ent, (size_t)nc * sizeof *res->ent);
                if(!ne) break;
                res->ent = ne; res->cap = nc;
            }

            int is_dir;
            if(e->d_type == DT_DIR)      is_dir = 1;
            else if(e->d_type == DT_REG) is_dir = 0;
            else {
                char path[FB_MAXPATH];
                int pn = snprintf(path, sizeof path, "%s/%s", dir, nm);
                if(pn <= 0 || pn >= (int)sizeof path) continue;
                struct stat st;
                if(lstat(path, &st) != 0) continue;
                if(S_ISDIR(st.st_mode)) is_dir = 1;
                else if(S_ISREG(st.st_mode)) is_dir = 0;
                else continue;
            }
            if(is_dir != (pass == 0)) continue;
            int kind = FB_K_AUDIO;
            if(!is_dir && !fb_is_audio(nm)){
                if(fb_has_ext(nm, ".cue")) kind = FB_K_CUE;
                else if(fb_has_ext(nm, ".iso")){
                    char ip[FB_MAXPATH];
                    int pn = snprintf(ip, sizeof ip, "%s/%s", dir, nm);
                    if(pn <= 0 || pn >= (int)sizeof ip || mdb_subtrack_count(ip, 1) <= 0) continue;
                    kind = FB_K_ISO;
                } else continue;
            }

            fb_entry_t *slot = &res->ent[res->nent];
            snprintf(slot->name, sizeof slot->name, "%s", nm);
            slot->is_dir = is_dir;
            slot->kind = kind;
            res->nent++;
        }
        if(pass == 0) rewinddir(d);
    }
    closedir(d);
    res->opened = 1;
    if(res->nent == 0 && fb_current(res->gen)){
        d = opendir(dir);
        res->opened = d != NULL;
        if(d) closedir(d);
    }
    if(!fb_current(res->gen)){ res->cancelled = 1; return; }
    if(res->nent > 1) qsort(res->ent, res->nent, sizeof res->ent[0], fb_cmp);
}

static void *fb_worker(void *arg){
    (void)arg;
    for(;;){
        char dir[FB_MAXPATH];
        fb_result_t res = {0};
        pthread_mutex_lock(&g_fb_mu);
        if(!g_fb_requested){
            g_fb_running = 0;
            pthread_mutex_unlock(&g_fb_mu);
            return NULL;
        }
        res.gen = g_fb_gen;
        snprintf(dir, sizeof dir, "%s", g_fb_path);
        pthread_mutex_unlock(&g_fb_mu);
        if(sd_io_begin()){
            fb_scan_leased(dir, &res);
            sd_io_end();
        }
        pthread_mutex_lock(&g_fb_mu);
        if(res.gen == g_fb_gen && g_fb_requested){
            g_fb_res = res;
            g_fb_ready = 1;
            g_fb_running = 0;
            pthread_mutex_unlock(&g_fb_mu);
            return NULL;
        }
        pthread_mutex_unlock(&g_fb_mu);
        free(res.ent);
    }
}

static const char *fb_basename(const char *p);
static void fb_rebuild(void);
static void fb_scan(void);
static void fb_poll_cb(lv_timer_t *t){
    int visible = screen_current() == SCR_FOLDER;
    int admitted = visible && sd_io_begin();
    if(admitted) sd_io_end();
    if(!admitted){
        pthread_mutex_lock(&g_fb_mu);
        if(g_fb_requested){ g_fb_gen++; g_fb_requested = 0; }
        free(g_fb_res.ent); memset(&g_fb_res, 0, sizeof g_fb_res); g_fb_ready = 0;
        pthread_mutex_unlock(&g_fb_mu);
        fb_fill_stop();
        if(visible && !g_fb_error_shown){
            free(g_ent); g_ent = NULL; g_nent = 0; g_opened = 0;
            fb_rebuild(); g_fb_error_shown = 1;
        }
        g_fb_waiting = 1;
        lv_timer_set_period(t, 200);
        return;
    }
    if(g_fb_waiting){ fb_scan(); return; }
    fb_result_t res = {0}; int ready = 0;
    pthread_mutex_lock(&g_fb_mu);
    if(g_fb_ready && g_fb_res.gen == g_fb_gen){
        res = g_fb_res; memset(&g_fb_res, 0, sizeof g_fb_res);
        ready = 1; g_fb_ready = 0; g_fb_requested = 0;
    }
    pthread_mutex_unlock(&g_fb_mu);
    if(!ready) return;
    if(res.cancelled){
        free(res.ent); g_opened = 0; fb_rebuild(); g_fb_waiting = 1; g_fb_error_shown = 1;
        lv_timer_set_period(t, 200);
        return;
    }
    free(g_ent); g_ent = res.ent; g_nent = res.nent; g_opened = res.opened;
    fb_rebuild();
    lv_timer_del(t); g_fb_poll = NULL;
}

static void fb_scan(void){
    fb_fill_stop();
    free(g_ent); g_ent = NULL; g_nent = 0;
    if(g_list) lv_obj_clean(g_list);
    if(g_title) theme_title_text(g_title, strcmp(g_dir, FB_ROOT) == 0 ? "Files" : fb_basename(g_dir));
    pthread_mutex_lock(&g_fb_mu);
    g_fb_gen++;
    free(g_fb_res.ent); memset(&g_fb_res, 0, sizeof g_fb_res); g_fb_ready = 0;
    snprintf(g_fb_path, sizeof g_fb_path, "%s", g_dir);
    g_fb_requested = 1;
    int start = !g_fb_running;
    if(start) g_fb_running = 1;
    pthread_mutex_unlock(&g_fb_mu);
    g_fb_waiting = 0;
    g_fb_error_shown = 0;
    if(!g_fb_poll) g_fb_poll = lv_timer_create(fb_poll_cb, 16, NULL);
    else lv_timer_set_period(g_fb_poll, 16);
    if(start){
        pthread_t th;
        if(pthread_create(&th, NULL, fb_worker, NULL) == 0) pthread_detach(th);
        else {
            pthread_mutex_lock(&g_fb_mu);
            g_fb_running = 0; g_fb_requested = 0;
            pthread_mutex_unlock(&g_fb_mu);
            g_opened = 0; fb_rebuild();
            lv_timer_del(g_fb_poll); g_fb_poll = NULL;
        }
    }
}
static const char *fb_basename(const char *p){
    const char *s = strrchr(p, '/');
    return (s && s[1]) ? s + 1 : p;
}

static void fb_empty_label(const char *msg){
    lv_obj_t *l = lv_label_create(g_list);
    lv_label_set_text(l, msg);
    lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

static void fb_row_cb(lv_event_t *e);

static void fb_add_row(int i){
        fb_entry_t *en = &g_ent[i];
        lv_obj_t *r = lv_button_create(g_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 268, 46);
        lv_obj_set_style_radius(r, 8, 0);
        lv_obj_set_style_bg_color(r, TC(LIST_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(r, LV_OPA_70, LV_STATE_PRESSED);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_ext_click_area(r, 2);
        ui_on(r, fb_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i, "folderbrowser.fb_row", UI_CORE);
        kit_row_layout(r, KIT_ROW_ICON);

        lv_obj_t *ic = lv_label_create(r);
        lv_label_set_text(ic, en->is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO);
        lv_obj_set_pos(ic, 10, 14);
        lv_obj_set_style_text_font(ic, TF(UI_16), 0);
        lv_obj_set_style_text_color(ic, en->is_dir ? TC(FOLDER_ICON) : TC(TEXT_MUTED), 0);

        lv_obj_t *nm = lv_label_create(r);
        lv_label_set_text(nm, en->name);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(nm, 40, 14);
        lv_obj_set_size(nm, en->is_dir ? 194 : 216, 20);
        lv_obj_set_style_text_font(nm, TF(USER_16), 0);
        lv_obj_set_style_text_color(nm, TC(TEXT_PRIMARY), 0);

        if(en->is_dir){
            lv_obj_t *ch = lv_label_create(r);
            lv_label_set_text(ch, LV_SYMBOL_RIGHT);
            lv_obj_set_pos(ch, 244, 15);
            lv_obj_set_style_text_font(ch, TF(UI_16), 0);
            lv_obj_set_style_text_color(ch, TC(TEXT_DISABLED), 0);
        }
        theme_list_row(r);
}

static void fb_fill_cb(lv_timer_t *t){
    (void)t;
    if(screen_current() != SCR_FOLDER) return;
    uint32_t start = lv_tick_get();
    int end = g_fb_i + 40; if(end > g_nent) end = g_nent;
    while(g_fb_i < end){
        fb_add_row(g_fb_i++);
        if(lv_tick_elaps(start) >= 4) break;
    }
    if(g_fb_i >= g_nent) fb_fill_stop();
}

static void fb_rebuild(void){
    fb_fill_stop();
    if(g_title){
        int at_root = (strcmp(g_dir, FB_ROOT) == 0);
        theme_title_text(g_title, at_root ? "Files" : fb_basename(g_dir));
    }
    if(!g_list) return;
    lv_obj_clean(g_list);

    if(g_nent == 0){
        if(!g_opened){
            fb_empty_label(strcmp(g_dir, FB_ROOT) == 0 ? "No SD card" : "Can't open folder");
            return;
        }
        fb_empty_label("Empty folder");
        return;
    }

    g_fb_i = 0;
    int first = g_nent < 20 ? g_nent : 20;
    for(; g_fb_i < first; g_fb_i++) fb_add_row(g_fb_i);
    if(g_fb_i < g_nent) g_fb_fill = lv_timer_create(fb_fill_cb, 16, NULL);
    lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);
}

static void fb_descend(const char *name){
    char next[FB_MAXPATH];
    int n = snprintf(next, sizeof next, "%s/%s", g_dir, name);
    if(n <= 0 || n >= (int)sizeof next){ ui_toast("Path too long"); return; }
    snprintf(g_dir, sizeof g_dir, "%s", next);
    fb_scan();
}

static void fb_ascend(void){
    if(strcmp(g_dir, FB_ROOT) == 0){ screen_back(); return; }
    char *slash = strrchr(g_dir, '/');
    if(!slash || slash == g_dir || (size_t)(slash - g_dir) < strlen(FB_ROOT)){
        snprintf(g_dir, sizeof g_dir, "%s", FB_ROOT);
    } else {
        *slash = 0;
    }
    fb_scan();
}

static void fb_play(const char *name){
    char full[FB_MAXPATH];
    int n = snprintf(full, sizeof full, "%s/%s", g_dir, name);
    if(n <= 0 || n >= (int)sizeof full){ ui_toast("Path too long"); return; }
    if(ui_play_song_by_path(full))
        screen_show(SCR_NOWPLAYING);
}

static void fb_play_tracks(const char *const *paths, int n){
    mdb_plan_t plan;
    if(!mdb_subtrack_plan(paths, n, &plan)){ ui_toast("Not in library"); return; }
    int ok = ui_play_plan(&plan, plan.ids[0]);
    mdb_plan_free(&plan);
    if(ok) screen_show(SCR_NOWPLAYING);
}
#define FB_CUE_FILES 64
static char g_cue_paths[FB_CUE_FILES][FB_MAXPATH];
static int fb_cue_seen(const char *path){ return mdb_subtrack_count(path, 0) > 0; }
static void fb_play_sheet(const char *name){
    char full[FB_MAXPATH];
    int n = snprintf(full, sizeof full, "%s/%s", g_dir, name);
    if(n <= 0 || n >= (int)sizeof full){ ui_toast("Path too long"); return; }
    int np = 0;
    if(sd_io_begin()){ np = scan_cue_files(full, g_cue_paths, FB_CUE_FILES, fb_cue_seen); sd_io_end(); }
    const char *paths[FB_CUE_FILES];
    for(int i = 0; i < np; i++) paths[i] = g_cue_paths[i];
    fb_play_tracks(paths, np);
}
static void fb_play_iso(const char *name){
    char full[FB_MAXPATH];
    int n = snprintf(full, sizeof full, "%s/%s", g_dir, name);
    if(n <= 0 || n >= (int)sizeof full){ ui_toast("Path too long"); return; }
    const char *paths[1] = { full };
    fb_play_tracks(paths, 1);
}

static void fb_row_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= g_nent) return;
    fb_entry_t *en = &g_ent[i];
    if(en->is_dir) fb_descend(en->name);
    else if(en->kind == FB_K_ISO) fb_play_iso(en->name);
    else if(en->kind == FB_K_CUE) fb_play_sheet(en->name);
    else           fb_play(en->name);
}

static void fb_header_back_cb(lv_event_t *e){
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) fb_ascend();
}

void folderbrowser_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    g_title = ui_header_cb(root, "Files", fb_header_back_cb);

    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, 41, UI_LIST_Y);
    lv_obj_set_size(g_list, 290, UI_LIST_H);
    lv_obj_set_style_pad_bottom(g_list, UI_LIST_PAD_BOTTOM, 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_list, 6, 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    snprintf(g_dir, sizeof g_dir, "%s", FB_ROOT);
}

void folderbrowser_open(void){
    snprintf(g_dir, sizeof g_dir, "%s", FB_ROOT);
    fb_scan();
    screen_show(SCR_FOLDER);
}
