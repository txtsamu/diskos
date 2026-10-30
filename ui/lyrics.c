/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "sdio.h"
#include "ipc.h"
#include "musicdb.h"
#include "config.h"
#include "lrc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

static lv_obj_t *g_scroll;
static lv_obj_t *g_text;
static lv_obj_t *g_lines_box;

#define LY_MAXL 400
static lrc_line_t g_ll[LY_MAXL];
static char g_ltext[16384];
static char g_lplain[16384];
static int g_nll = 0;
static int g_lcur = -2;
static int g_holding = 0;
static uint32_t g_hold_until = 0;
static int32_t g_scroll_target = -1;
static unsigned g_last_pos_seq = 0;
static long g_pos_base = 0;
static uint32_t g_pos_tick = 0;

static pthread_mutex_t g_ly_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_lbuf[16384];
static int g_lkind = 0;
static int g_lready = 0;
static int g_linflight = 0;
static int g_lphase = 0;
static unsigned g_req = 0;
static unsigned g_done_req = 0;
static char g_q_title[512], g_q_artist[512];
static int g_local_done, g_local_found;
static int g_title_pending;
static uint64_t g_token;
static unsigned g_generation;
static char g_path[256];

typedef struct { unsigned req; int online, online_only; char title[512], artist[512], path[256]; } ly_job_t;


static void urlenc(const char *s, char *out, int cap)
{
    static const char *hex = "0123456789ABCDEF";
    int o = 0;
    for(; *s && o < cap - 4; s++){
        unsigned char c = (unsigned char)*s;
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')
            out[o++] = (char)c;
        else { out[o++]='%'; out[o++]=hex[c>>4]; out[o++]=hex[c&15]; }
    }
    out[o] = 0;
}

static void derive_lrc(const char *audio, char *out, int cap)
{
    snprintf(out, cap, "%s", audio);
    char *slash = strrchr(out, '/');
    char *dot   = strrchr(out, '.');
    if(dot && (!slash || dot > slash)) snprintf(dot, cap - (int)(dot - out), ".lrc");
    else { int n = (int)strlen(out); snprintf(out + n, cap - n, ".lrc"); }
}

static int read_lrc(FILE *f, char *out, int cap)
{
    size_t n = fread(out, 1, (size_t)cap - 1, f);
    out[n] = 0;
    if(n == (size_t)cap - 1){ char *nl = strrchr(out, '\n'); if(nl) nl[1] = 0; }
    return (int)strlen(out);
}

static int hex4(const char *s){
    int v = 0;
    for(int i=0;i<4;i++){
        char c = s[i]; int d;
        if(c>='0'&&c<='9') d = c-'0';
        else if(c>='a'&&c<='f') d = c-'a'+10;
        else if(c>='A'&&c<='F') d = c-'A'+10;
        else return -1;
        v = (v<<4) | d;
    }
    return v;
}
static int utf8_enc(unsigned cp, char *out, int cap){
    if(cp < 0x80)   { if(cap<1) return 0; out[0]=(char)cp; return 1; }
    if(cp < 0x800)  { if(cap<2) return 0; out[0]=(char)(0xC0|(cp>>6));  out[1]=(char)(0x80|(cp&0x3F)); return 2; }
    if(cp < 0x10000){ if(cap<3) return 0; out[0]=(char)(0xE0|(cp>>12)); out[1]=(char)(0x80|((cp>>6)&0x3F)); out[2]=(char)(0x80|(cp&0x3F)); return 3; }
    if(cap<4) return 0;
    out[0]=(char)(0xF0|(cp>>18)); out[1]=(char)(0x80|((cp>>12)&0x3F)); out[2]=(char)(0x80|((cp>>6)&0x3F)); out[3]=(char)(0x80|(cp&0x3F)); return 4;
}

static int json_first_object(const char *resp, const char **b, const char **e)
{
    const char *p = strchr(resp, '{');
    if(!p) return 0;
    int depth = 0, instr = 0;
    for(const char *q = p; *q; q++){
        if(instr){
            if(*q == '\\'){ if(!q[1]) return 0; q++; }
            else if(*q == '"') instr = 0;
        } else if(*q == '"') instr = 1;
        else if(*q == '{') depth++;
        else if(*q == '}' && --depth == 0){ *b = p; *e = q + 1; return 1; }
    }
    return 0;
}

static int json_lyrics(const char *resp, const char *key, char *out, int cap)
{
    char pat[40]; snprintf(pat, sizeof pat, "\"%s\":\"", key);
    out[0] = 0;
    const char *ob, *oe;
    if(!json_first_object(resp, &ob, &oe)) return 0;
    const char *p = strstr(ob, pat);
    if(!p || p + strlen(pat) > oe) return 0;
    p += strlen(pat);
    int len = 0;
    while(*p && len < cap - 2){
        if(*p == '\\'){
            p++; char e = *p;
            if(e=='n') out[len++]='\n';
            else if(e=='t') out[len++]=' ';
            else if(e=='"') out[len++]='"';
            else if(e=='\\') out[len++]='\\';
            else if(e=='/') out[len++]='/';
            else if(e=='r') { }
            else if(e=='u'){
                int u = (p[1]&&p[2]&&p[3]&&p[4]) ? hex4(p+1) : -1;
                if(u >= 0){
                    unsigned cp = (unsigned)u;
                    p += 4;
                    if(cp>=0xD800 && cp<=0xDBFF && p[1]=='\\' && p[2]=='u' &&
                       p[3]&&p[4]&&p[5]&&p[6]){
                        int lo = hex4(p+3);
                        if(lo>=0xDC00 && lo<=0xDFFF){
                            cp = 0x10000u + ((cp-0xD800u)<<10) + (unsigned)(lo-0xDC00);
                            p += 6;
                        }
                    }
                    if(!(cp>=0xD800 && cp<=0xDFFF)) len += utf8_enc(cp, out+len, cap-1-len);
                } else {
                    out[len++]='u';
                }
            }
            else if(e) out[len++]=e;
            if(*p) p++;
        } else if(*p == '"'){
            break;
        } else {
            out[len++] = *p++;
        }
    }
    out[len] = 0;
    return len;
}

static void *lyrics_thread(void *arg)
{
    ly_job_t *job = (ly_job_t *)arg;
    char cmd[1200];
    static char resp[32768];
    resp[0] = 0;
    g_lbuf[0] = 0;
    g_lkind = 0;

    if(!job->online_only && job->path[0] && sd_io_begin()){
        char lrc[320]; derive_lrc(job->path, lrc, sizeof lrc);
        FILE *f = fopen(lrc, "r");
        if(f){ read_lrc(f, g_lbuf, sizeof g_lbuf); fclose(f); }
        sd_io_end();
        g_lkind = 2;
    }

    if(!job->online_only && !g_lbuf[0] && job->path[0] && sd_io_begin()){
        FILE *f = fopen(job->path, "rb");
        if(f){
            int synced = 0;
            if(lyr_embedded_read(f, g_lbuf, sizeof g_lbuf, &synced) > 0) g_lkind = synced ? 2 : 1;
            fclose(f);
        }
        sd_io_end();
    }

    int local_found = g_lbuf[0] != 0;

    if(!g_lbuf[0] && job->title[0] && job->online){
        pthread_mutex_lock(&g_ly_mu); g_lphase = 1; pthread_mutex_unlock(&g_ly_mu);
        if(job->artist[0])
            snprintf(cmd, sizeof cmd,
                "wget -qO- -T 12 'https://lrclib.net/api/search?track_name=%s&artist_name=%s' 2>/dev/null",
                job->title, job->artist);
        else
            snprintf(cmd, sizeof cmd,
                "wget -qO- -T 12 'https://lrclib.net/api/search?track_name=%s' 2>/dev/null", job->title);
        FILE *f = popen(cmd, "r");
        if(f){ int n = fread(resp, 1, sizeof(resp)-1, f); resp[n>0?n:0] = 0; pclose(f); }
        else fprintf(stderr, "lyrics popen failed: %s\n", strerror(errno));
        g_lkind = 2;
        if(!json_lyrics(resp, "syncedLyrics", g_lbuf, sizeof g_lbuf)){
            g_lkind = 1;
            json_lyrics(resp, "plainLyrics", g_lbuf, sizeof g_lbuf);
        }
        if(f && !g_lbuf[0]) fprintf(stderr, "lyrics: lrclib returned no lyrics for '%s'\n", job->title);
    }

    if(!g_lbuf[0]){
        snprintf(g_lbuf, sizeof g_lbuf, "No lyrics found.");
        g_lkind = 0;
    }

    pthread_mutex_lock(&g_ly_mu);
    g_done_req = job->req;
    g_lphase = local_found ? 2 : 3;
    g_linflight = 0;
    g_lready = 1;
    pthread_mutex_unlock(&g_ly_mu);
    free(job);
    return NULL;
}

static void show_text(const char *txt, int message);
static void show_status(const char *txt)
{
    if(g_text && strcmp(lv_label_get_text(g_text), txt)) show_text(txt, 1);
}
static void start_fetch(void)
{
    int start = 0, online_only = 0;
    pthread_mutex_lock(&g_ly_mu);
    if(!g_linflight){
        online_only = g_done_req == g_req && g_lphase == 3;
        g_linflight = 1; g_lready = 0; g_lphase = online_only ? 1 : 0; start = 1;
    }
    pthread_mutex_unlock(&g_ly_mu);
    if(!start) return;
    ly_job_t *job = malloc(sizeof *job);
    if(!job){
        pthread_mutex_lock(&g_ly_mu); g_linflight = 0; pthread_mutex_unlock(&g_ly_mu);
        show_status("Lyrics unavailable.");
        return;
    }
    job->req = g_req;
    job->online_only = online_only;
    job->online = cfg_get_int("online_lyrics", 1);
    snprintf(job->title,  sizeof job->title,  "%s", g_q_title);
    snprintf(job->artist, sizeof job->artist, "%s", g_q_artist);
    snprintf(job->path,   sizeof job->path,   "%s", g_path);
    pthread_t th;
    if(pthread_create(&th, NULL, lyrics_thread, job) == 0) pthread_detach(th);
    else {
        free(job);
        pthread_mutex_lock(&g_ly_mu); g_linflight = 0; pthread_mutex_unlock(&g_ly_mu);
        show_status("Lyrics unavailable.");
    }
}

static void show_text(const char *txt, int message)
{
    g_nll = 0; g_lcur = -2;
    if(g_lines_box){ lv_obj_clean(g_lines_box); lv_obj_add_flag(g_lines_box, LV_OBJ_FLAG_HIDDEN); }
    if(g_text){ lv_obj_remove_flag(g_text, LV_OBJ_FLAG_HIDDEN); lv_label_set_text(g_text, txt);
        lv_obj_set_style_translate_y(g_text, 0, 0);
        if(message) theme_kit()->empty_state(KIT_EMPTY_LYRICS, g_text);
        else {
            lv_obj_set_width(g_text, 280);
            lv_obj_set_style_pad_all(g_text, 0, 0);
            lv_obj_set_style_translate_x(g_text, 0, 0);
            lv_obj_set_style_text_align(g_text, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_font(g_text, TF(USER_16), 0);
            lv_obj_set_style_text_color(g_text, TC(TEXT_LYRICS), 0);
        } }
    if(g_scroll) lv_obj_scroll_to_y(g_scroll, 0, LV_ANIM_OFF);
}
#ifdef DISKOS_HOST
void lyrics_host_show_plain(const char *txt){ show_text(txt, 0); }
#endif

static int show_timed(const char *lrc)
{
    int synced = 0;
    int n = lrc_parse(lrc, g_ll, LY_MAXL, g_ltext, sizeof g_ltext, g_lplain, sizeof g_lplain, &synced);
    if(!synced || !g_lines_box){
        show_text(g_lplain[0] ? g_lplain : "No lyrics found.", !g_lplain[0]);
        return 0;
    }
    lv_obj_clean(g_lines_box);
    for(int i = 0; i < n; i++){
        lv_obj_t *l = lv_label_create(g_lines_box);
        lv_obj_set_width(l, 280);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(l, TF(USER_16), 0);
        lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
        lv_label_set_text(l, g_ltext[g_ll[i].text] ? g_ltext + g_ll[i].text : " ");
    }
    g_nll = n; g_lcur = -2; g_holding = 0; g_scroll_target = -1;
    if(g_text) lv_obj_add_flag(g_text, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_lines_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(g_scroll);
    lv_obj_scroll_to_y(g_scroll, 0, LV_ANIM_OFF);
    return 1;
}

static long lyrics_position(void)
{
    track_state_t st; ipc_get_state(&st);
    uint32_t now = lv_tick_get();
    if(st.pos_seq != g_last_pos_seq || st.position_ms != g_pos_base){
        g_last_pos_seq = st.pos_seq; g_pos_base = st.position_ms; g_pos_tick = now;
    }
    long est = g_pos_base;
    if(ui_is_playing()){ uint32_t d = now - g_pos_tick; est += d > 1500 ? 1500 : (long)d; }
    return est;
}

static void lyrics_follow(void)
{
    if(!g_nll || !g_lines_box) return;
    int idx = lrc_index_at(g_ll, g_nll, lyrics_position());
    if(idx != g_lcur){
        if(g_lcur >= 0 && g_lcur < g_nll) lv_obj_set_style_text_color(lv_obj_get_child(g_lines_box, g_lcur), TC(TEXT_MUTED), 0);
        g_lcur = idx;
        if(idx >= 0) lv_obj_set_style_text_color(lv_obj_get_child(g_lines_box, idx), TC(TEXT_LYRICS), 0);
    }
    if(g_lcur < 0) return;
    if(g_holding && (int32_t)(lv_tick_get() - g_hold_until) < 0) return;
    if(g_holding){ g_holding = 0; g_scroll_target = -1; }
    lv_obj_update_layout(g_scroll);
    lv_obj_t *l = lv_obj_get_child(g_lines_box, g_lcur);
    lv_area_t la, sa;
    lv_obj_get_coords(l, &la); lv_obj_get_coords(g_scroll, &sa);
    int32_t y = lv_obj_get_scroll_y(g_scroll) + (la.y1 + la.y2) / 2 - (sa.y1 + sa.y2) / 2;
    if(y < 0) y = 0;
    if(y != g_scroll_target){ g_scroll_target = y; lv_obj_scroll_to_y(g_scroll, y, LV_ANIM_ON); }
}

static void scroll_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_PRESSED || (c == LV_EVENT_SCROLL_BEGIN && lv_indev_active()))
        { g_holding = 1; g_hold_until = lv_tick_get() + 4000; }
}

static void lyrics_query(const track_state_t *st)
{
    char title[IPC_TITLE_CAPACITY] = "", artist[200] = "";
    int ready = !st->have_track || st->title_ready;
    if(ready) snprintf(title, sizeof title, "%s", st->title);
    snprintf(artist, sizeof artist, "%s", st->artist);
    if(ready && !title[0]){
        mdb_song_t cur; int pm, pl;
        if(mdb_current_play(&cur, &pm, &pl)){
            snprintf(title, sizeof title, "%s", cur.title);
            snprintf(artist, sizeof artist, "%s", cur.artist);
            if(!g_path[0]) mdb_song_path(cur.id, g_path, sizeof g_path);
        }
    }
    urlenc(title, g_q_title, sizeof g_q_title);
    urlenc(artist, g_q_artist, sizeof g_q_artist);
}

void lyrics_open(void)
{
    g_req++;
    track_state_t st; ipc_get_state(&st);
    g_local_done = g_local_found = 0;
    g_title_pending = st.have_track && !st.title_ready;
    g_token = st.playback_token; g_generation = st.player_generation;
    snprintf(g_path, sizeof g_path, "%s", st.path);
    lyrics_query(&st);
    show_text("Checking SD card...", 1);
    screen_show(SCR_LYRICS);
    if(g_q_title[0] || (g_title_pending && g_path[0])) start_fetch();
    else show_text("No track playing.", 1);
}

static void lyrics_commit_title(const track_state_t *st)
{
    if(!st->have_track){ g_title_pending = 0; return; }
    if(st->playback_token != g_token || st->player_generation != g_generation){
        g_token = st->playback_token; g_generation = st->player_generation;
        g_title_pending = !st->title_ready;
    }
    if(!g_title_pending || !st->title_ready || strcmp(st->path, g_path)) return;
    pthread_mutex_lock(&g_ly_mu);
    int waiting = g_linflight || g_lready;
    pthread_mutex_unlock(&g_ly_mu);
    if(waiting || !g_local_done) return;
    g_title_pending = 0;
    if(g_local_found) return;
    lyrics_query(st);
    if(g_q_title[0] && cfg_get_int("online_lyrics", 1)) start_fetch();
}

void lyrics_poll(lv_timer_t *t)
{
    (void)t;
    if(screen_current() == SCR_LYRICS){
        track_state_t st; ipc_get_state(&st);
        if(st.path[0] && strcmp(st.path, g_path)){ lyrics_open(); return; }
        lyrics_commit_title(&st);
    }
    int on = screen_current() == SCR_LYRICS;
    if(t) lv_timer_set_period(t, (on && g_nll) ? 200 : 500);
    if(on) lyrics_follow();
    int ready = 0, inflight, phase; unsigned done = 0;
    pthread_mutex_lock(&g_ly_mu);
    if(g_lready){ g_lready = 0; ready = 1; done = g_done_req; }
    inflight = g_linflight; phase = g_lphase;
    pthread_mutex_unlock(&g_ly_mu);
    if(!ready){
        if(inflight && g_text && !lv_obj_has_flag(g_text, LV_OBJ_FLAG_HIDDEN))
            show_status(phase ? "Searching online..." : "Checking SD card...");
        return;
    }
    if(done == g_req){
        g_local_done = 1; g_local_found = phase == 2;
        if(g_lkind == 2){ if(show_timed(g_lbuf) && on) lyrics_follow(); }
        else show_text(g_lbuf, g_lkind == 0);
    } else if(g_q_title[0] || (g_title_pending && g_path[0])){
        (void)inflight;
        start_fetch();
    }
}

void lyrics_create(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    ui_header(root, "Lyrics");

    g_scroll = lv_obj_create(root);
    lv_obj_remove_style_all(g_scroll);
    lv_obj_set_pos(g_scroll, 40, 76); lv_obj_set_size(g_scroll, 280, 250);
    lv_obj_set_style_pad_bottom(g_scroll, 44, 0);
    lv_obj_set_style_bg_opa(g_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(g_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_scroll, LV_SCROLLBAR_MODE_OFF);

    g_text = lv_label_create(g_scroll);
    lv_obj_set_width(g_text, 280);
    lv_label_set_long_mode(g_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_text, TF(USER_16), 0);
    lv_obj_set_style_text_color(g_text, TC(TEXT_LYRICS), 0);
    lv_label_set_text(g_text, "");

    g_lines_box = lv_obj_create(g_scroll);
    lv_obj_remove_style_all(g_lines_box);
    lv_obj_set_width(g_lines_box, 280);
    lv_obj_set_height(g_lines_box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_lines_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_lines_box, 10, 0);
    lv_obj_set_style_pad_top(g_lines_box, 100, 0);
    lv_obj_set_style_pad_bottom(g_lines_box, 110, 0);
    lv_obj_remove_flag(g_lines_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_lines_box, LV_OBJ_FLAG_HIDDEN);
    g_nll = 0; g_lcur = -2;
    lv_obj_add_event_cb(g_scroll, scroll_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_scroll, scroll_cb, LV_EVENT_SCROLL_BEGIN, NULL);
}
