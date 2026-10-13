/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Listening Stats (built in, opened from its landing page like Boomshine).
 *
 * Recording: every song that really plays leaves one row in the diskOS-owned PLAY_LOG table (musicdb.c writes it on
 * its history worker): when it started, how long it actually played, and its title / artist / album / album artist
 * as the library has them. A song counts once it has played for 30 seconds, or almost all of a shorter song. Time is
 * measured from the song's own position while it plays (pause and seeking don't add any). Audiobooks are left out.
 *
 * The screen: one list that scrolls like the Library (rows curve with the round screen, position dots on the left
 * rim): the period at a glance, top artists, top albums, the song on repeat and the top songs, and a 24-hour
 * listening clock round the rim. Week / Month / Year at the top, arrows step back through earlier periods. Data is
 * read only when the screen opens (Show me), never while the landing page is merely passed in the menu. */
#include "stats.h"
#include "screens.h"
#include "theme.h"
#include "config.h"
#include "musicdb.h"
#include "artcache.h"
#include "curvelist.h"
#include "art.h"
#include "sqlite3.h"
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#ifndef STATS_DB_PATH
#define STATS_DB_PATH "/usr/data/fiio/db/song.db"
#endif
#define COUNT_MS 30000                     /* a play counts after 30 s ... */

/* ================================ recording ================================ */
static char r_path[256], r_title[160], r_artist[160], r_album[160];
static long r_dur, r_last_pos;
static uint32_t r_ms;
static long long r_last_ms;                          /* monotonic ms at the last position seen (0: none yet) */
static long long r_at;
static int r_active;

static void rec_finish(void){
    if(!r_active) return;
    r_active = 0;
    int counts = r_ms >= COUNT_MS || (r_dur > 0 && r_ms >= 5000 && (long)r_ms >= r_dur * 9 / 10);   /* ... or most of a short one */
    long long now = (long long)time(NULL);
    if(!counts || now < 1600000000LL) return;                                    /* the clock isn't set: nothing to date it by */
    long long at = r_at >= 1600000000LL ? r_at : now - r_ms / 1000;
    mdb_log_play(r_path, r_title, r_artist, r_album, at, (int)r_ms);
}
static void rec_start(const track_state_t *st){
    snprintf(r_path, sizeof r_path, "%s", st->path);
    snprintf(r_title, sizeof r_title, "%s", st->title);
    snprintf(r_artist, sizeof r_artist, "%s", st->artist);
    snprintf(r_album, sizeof r_album, "%s", st->album);
    r_dur = st->duration_ms; r_ms = 0; r_last_ms = 0; r_last_pos = st->position_ms;
    r_at = (long long)time(NULL);
    r_active = !mdb_is_book_path(st->path);                                      /* audiobooks have their own progress */
}
static long long mono_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
/* Listening time is how far the song's position moved while it played, never more than the time that really
 * passed: a seek forward doesn't count, and music that played on while an app had the screen (this loop waits
 * then) still does. */
void stats_tick(const track_state_t *st, int playing){
    int have = st && st->have_track && st->path[0];
    if(!have){ rec_finish(); r_path[0] = 0; return; }
    if(strcmp(st->path, r_path)) { rec_finish(); rec_start(st); }
    else if(playing && r_active && r_ms >= COUNT_MS && st->position_ms < 5000 && st->position_ms + 3000 < r_last_pos){
        rec_finish(); rec_start(st);                                             /* the same song again (Repeat One) */
    }
    long long now = mono_ms();
    if(!playing){ r_last_pos = st->position_ms; r_last_ms = now; }              /* paused: a seek meanwhile never counts */
    else if(st->position_ms != r_last_pos || !r_last_ms){
        long d = st->position_ms - r_last_pos;
        if(playing && r_last_ms && d > 0 && d <= now - r_last_ms + 1500) r_ms += (uint32_t)d;
        r_last_pos = st->position_ms; r_last_ms = now;
    }
    if(st->duration_ms > 0) r_dur = st->duration_ms;
}
void stats_flush(void){ rec_finish(); r_path[0] = 0; }

#ifndef STATS_RECORDER_ONLY                         /* (the recorder alone: its host test) */
/* ================================ periods ================================ */
enum { P_WEEK, P_MONTH, P_YEAR };
static int g_ptype = P_MONTH, g_poff;                /* the period shown: its kind, and how many back from now */
static const char *const MON[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                     "September", "October", "November", "December" };
static long long mk(int y, int m, int d){
    struct tm t; memset(&t, 0, sizeof t);
    t.tm_year = y - 1900; t.tm_mon = m; t.tm_mday = d; t.tm_isdst = -1;
    return (long long)mktime(&t);
}
/* [from, to) of the period `off` steps back (0 = the current one), as local calendar weeks (Monday first), months, years */
static void period_range(int type, int off, long long *from, long long *to, struct tm *first){
    time_t now = time(NULL); struct tm n; localtime_r(&now, &n);
    if(type == P_WEEK){
        int back = (n.tm_wday + 6) % 7;                                         /* days since Monday */
        *from = mk(n.tm_year + 1900, n.tm_mon, n.tm_mday - back + 7 * off);
        *to = mk(n.tm_year + 1900, n.tm_mon, n.tm_mday - back + 7 * (off + 1));
    } else if(type == P_MONTH){
        *from = mk(n.tm_year + 1900, n.tm_mon + off, 1);
        *to = mk(n.tm_year + 1900, n.tm_mon + off + 1, 1);
    } else {
        *from = mk(n.tm_year + 1900 + off, 0, 1);
        *to = mk(n.tm_year + 1900 + off + 1, 0, 1);
    }
    time_t f = (time_t)*from; localtime_r(&f, first);
}
static void period_name(int type, int off, char *out, size_t n, int short_form){
    long long a, b; struct tm f; period_range(type, off, &a, &b, &f);
    time_t now = time(NULL); struct tm t; localtime_r(&now, &t);
    if(type == P_WEEK){
        if(off == 0) snprintf(out, n, short_form ? "this week" : "This week");
        else if(off == -1) snprintf(out, n, short_form ? "last week" : "Last week");
        else snprintf(out, n, "Week of %d %.3s", f.tm_mday, MON[f.tm_mon]);
    } else if(type == P_MONTH){
        if(f.tm_year == t.tm_year) snprintf(out, n, "%s", MON[f.tm_mon]);
        else snprintf(out, n, "%s %d", MON[f.tm_mon], f.tm_year + 1900);
    } else snprintf(out, n, "%d", f.tm_year + 1900);
}

/* ================================ the numbers ================================ */
#define TOPN 10
typedef struct { char name[128], sub[128], path[256]; int plays; long long ms; } item_t;
typedef struct {
    int plays, songs, streak, empty; long long ms, prev_ms; int prev_known;
    long long hour_ms[24];
    item_t art[TOPN], alb[TOPN], song[TOPN]; int nart, nalb, nsong;
} stats_t;
static stats_t g_s;
static long long g_first_at;                         /* the oldest row in the log (0: none) - how far back the arrows go */

static const char *col(sqlite3_stmt *st, int i){ const char *t = (const char *)sqlite3_column_text(st, i); return t ? t : ""; }
static int bind_range(sqlite3 *d, const char *sql, long long a, long long b, sqlite3_stmt **st){
    if(sqlite3_prepare_v2(d, sql, -1, st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(*st, 1, a); sqlite3_bind_int64(*st, 2, b);
    return 1;
}
static void trim(char *s){
    char *p = s; while(*p == ' ' || *p == '\t') p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s); while(n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}
static int item_cmp(const void *x, const void *y){
    const item_t *a = x, *b = y;
    if(a->plays != b->plays) return b->plays - a->plays;
    return (a->ms < b->ms) - (a->ms > b->ms);
}
/* Top artists (rows per artist and song, most played first). By Artist (the Library's setting): each credit of "A, B" counts for both, as the Artists list shows
 * them; by Album Artist: the album artist (else the artist), unsplit. The cover is from the artist's busiest group. */
typedef struct { char name[128], path[256]; int plays, best; long long ms; } acc_t;
static void load_artists(sqlite3 *d, long long a, long long b){
    int by_album = mdb_artist_mode();
    const char *sql = by_album
        ? "SELECT COALESCE(NULLIF(AARTIST,''),ARTIST),COUNT(*),SUM(MS),PATH FROM PLAY_LOG WHERE AT>=?1 AND AT<?2 "
          "AND IFNULL(COALESCE(NULLIF(AARTIST,''),ARTIST),'')<>'' GROUP BY 1,PATH ORDER BY 2 DESC LIMIT 3000;"
        : "SELECT ARTIST,COUNT(*),SUM(MS),PATH FROM PLAY_LOG WHERE AT>=?1 AND AT<?2 AND IFNULL(ARTIST,'')<>'' "
          "GROUP BY 1,PATH ORDER BY 2 DESC LIMIT 3000;";   /* per song: the artist's picture is their most played song's cover */
    sqlite3_stmt *st; if(!bind_range(d, sql, a, b, &st)) return;
    enum { ASLOTS = 1024 };                                                      /* > 2 x cap: the table never fills */
    int cap = 400, n = 0; acc_t *acc = calloc((size_t)cap, sizeof *acc); int *slots = malloc(ASLOTS * sizeof *slots);
    if(!acc || !slots){ free(acc); free(slots); sqlite3_finalize(st); return; }
    for(int i = 0; i < ASLOTS; i++) slots[i] = -1;
    while(sqlite3_step(st) == SQLITE_ROW){
        char raw[256]; snprintf(raw, sizeof raw, "%s", col(st, 0));
        int plays = sqlite3_column_int(st, 1); long long ms = sqlite3_column_int64(st, 2);
        const char *path = col(st, 3);
        for(char *tok = raw, *next; tok; tok = next){
            next = by_album ? NULL : strpbrk(tok, ",;");
            if(next) *next++ = 0;
            char nm[128]; snprintf(nm, sizeof nm, "%.127s", tok); trim(nm);
            if(!nm[0]) continue;
            unsigned h = 2166136261u; for(const char *q = nm; *q; q++){ h ^= (unsigned char)tolower((unsigned char)*q); h *= 16777619u; }
            int slot = (int)(h & (ASLOTS - 1)), k = -1;                          /* case-blind lookup, no list scan */
            while(slots[slot] >= 0){ if(!strcasecmp(acc[slots[slot]].name, nm)){ k = slots[slot]; break; } slot = (slot + 1) & (ASLOTS - 1); }
            if(k < 0){
                if(n == cap) continue;                                           /* very long tails: the top is what shows */
                snprintf(acc[n].name, sizeof acc[n].name, "%s", nm); slots[slot] = n; k = n++;
            }
            acc[k].plays += plays; acc[k].ms += ms;
            if(plays > acc[k].best){ acc[k].best = plays; snprintf(acc[k].path, sizeof acc[k].path, "%s", path); }
        }
    }
    sqlite3_finalize(st);
    item_t *it = calloc((size_t)(n ? n : 1), sizeof *it);
    if(it){
        for(int i = 0; i < n; i++){ snprintf(it[i].name, sizeof it[i].name, "%s", acc[i].name);
            snprintf(it[i].path, sizeof it[i].path, "%s", acc[i].path); it[i].plays = acc[i].plays; it[i].ms = acc[i].ms; }
        qsort(it, (size_t)n, sizeof *it, item_cmp);
        g_s.nart = n < TOPN ? n : TOPN;
        memcpy(g_s.art, it, (size_t)g_s.nart * sizeof *it);
        free(it);
    }
    free(acc); free(slots);
}
static int load_items(sqlite3 *d, const char *sql, long long a, long long b, item_t *out){
    sqlite3_stmt *st; if(!bind_range(d, sql, a, b, &st)) return 0;
    int n = 0;
    while(n < TOPN && sqlite3_step(st) == SQLITE_ROW){
        item_t *it = &out[n++];
        snprintf(it->name, sizeof it->name, "%s", col(st, 0));
        snprintf(it->sub, sizeof it->sub, "%s", col(st, 1));
        it->plays = sqlite3_column_int(st, 2); it->ms = sqlite3_column_int64(st, 3);
        snprintf(it->path, sizeof it->path, "%s", col(st, 4));
        if(!it->name[0]) snprintf(it->name, sizeof it->name, "Untitled");
    }
    sqlite3_finalize(st);
    return n;
}
static long long sum_ms(sqlite3 *d, long long a, long long b, int *plays, int *songs){
    sqlite3_stmt *st; long long ms = 0;
    if(!bind_range(d, "SELECT COUNT(*),IFNULL(SUM(MS),0),COUNT(DISTINCT PATH) FROM PLAY_LOG WHERE AT>=?1 AND AT<?2;", a, b, &st)) return -1;
    if(sqlite3_step(st) == SQLITE_ROW){
        if(plays) *plays = sqlite3_column_int(st, 0);
        ms = sqlite3_column_int64(st, 1);
        if(songs) *songs = sqlite3_column_int(st, 2);
    }
    sqlite3_finalize(st);
    return ms;
}
static void load_days_hours(sqlite3 *d, long long a, long long b){
    sqlite3_stmt *st;
    if(bind_range(d, "SELECT CAST(strftime('%H',AT,'unixepoch','localtime') AS INTEGER),SUM(MS) FROM PLAY_LOG "
                     "WHERE AT>=?1 AND AT<?2 GROUP BY 1;", a, b, &st)){
        while(sqlite3_step(st) == SQLITE_ROW){ int h = sqlite3_column_int(st, 0); if(h >= 0 && h < 24) g_s.hour_ms[h] += sqlite3_column_int64(st, 1); }
        sqlite3_finalize(st);
    }
    if(bind_range(d, "SELECT DISTINCT CAST(julianday(AT,'unixepoch','localtime','start of day') AS INTEGER) FROM PLAY_LOG "
                     "WHERE AT>=?1 AND AT<?2 ORDER BY 1;", a, b, &st)){
        int prev = -100, run = 0;                                               /* the longest run of days in a row */
        while(sqlite3_step(st) == SQLITE_ROW){
            int day = sqlite3_column_int(st, 0);
            run = day == prev + 1 ? run + 1 : 1; prev = day;
            if(run > g_s.streak) g_s.streak = run;
        }
        sqlite3_finalize(st);
    }
}
static void stats_load(void){
    memset(&g_s, 0, sizeof g_s); g_s.empty = 1; g_first_at = 0;
    sqlite3 *d = NULL;
    if(sqlite3_open_v2(STATS_DB_PATH, &d, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK){ if(d) sqlite3_close(d); return; }
    sqlite3_busy_timeout(d, 400);
    long long a, b, pa, pb; struct tm f;
    period_range(g_ptype, g_poff, &a, &b, &f);
    period_range(g_ptype, g_poff - 1, &pa, &pb, &f);
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT IFNULL(MIN(AT),0) FROM PLAY_LOG;", -1, &st, NULL) == SQLITE_OK){
        if(sqlite3_step(st) == SQLITE_ROW) g_first_at = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    g_s.ms = sum_ms(d, a, b, &g_s.plays, &g_s.songs);
    if(g_s.ms < 0){ g_s.ms = 0; sqlite3_close(d); return; }                    /* no log yet (nothing recorded so far) */
    g_s.empty = g_s.plays == 0;
    if(g_poff == 0){ long long in = (long long)time(NULL) - a; if(in > 0 && pa + in < pb) pb = pa + in; }   /* now: the same stretch of the last one */
    g_s.prev_ms = sum_ms(d, pa, pb, NULL, NULL);
    g_s.prev_known = g_first_at && g_first_at < pb && g_s.prev_ms > 0;          /* only compare with a period that was recorded */
    if(!g_s.empty){
        load_artists(d, a, b);
        g_s.nalb = load_items(d, "SELECT ALBUM,COALESCE(NULLIF(AARTIST,''),ARTIST),COUNT(*),SUM(MS),MAX(PATH) FROM PLAY_LOG "
                                 "WHERE AT>=?1 AND AT<?2 AND IFNULL(ALBUM,'')<>'' GROUP BY ALBUM,2 ORDER BY 3 DESC,4 DESC LIMIT 10;",
                              a, b, g_s.alb);
        g_s.nsong = load_items(d, "SELECT MAX(TITLE),MAX(ARTIST),COUNT(*),SUM(MS),PATH FROM PLAY_LOG "
                                  "WHERE AT>=?1 AND AT<?2 GROUP BY PATH ORDER BY 3 DESC,4 DESC LIMIT 10;", a, b, g_s.song);
        load_days_hours(d, a, b);
    }
    sqlite3_close(d);
}

/* ================================ formatting ================================ */
static void fmt_dur(long long ms, char *out, size_t n){
    long long m = ms / 60000;
    if(m >= 60) snprintf(out, n, "%lldh %02lldm", m / 60, m % 60);
    else snprintf(out, n, "%lldm", m);
}
static void fmt_dur_short(long long ms, char *out, size_t n){       /* "4h" / "35m": for the comparison line */
    long long m = (ms + 30000) / 60000;
    if(m >= 90) snprintf(out, n, "%lldh", (m + 30) / 60);
    else snprintf(out, n, "%lldm", m);
}
static void fmt_hour(int h, char *out, size_t n){
    if(cfg_get_int("time_24h", 1)) snprintf(out, n, "%02d:00", h);
    else snprintf(out, n, "%d %s", h % 12 ? h % 12 : 12, h < 12 ? "am" : "pm");
}
static const char *persona(int h){
    if(h >= 5 && h < 9) return "Early bird";
    if(h >= 9 && h < 12) return "Morning listener";
    if(h >= 12 && h < 17) return "Daytime listener";
    if(h >= 17 && h < 21) return "Evening listener";
    return "Night owl";
}

/* ================================ the screen ================================ */
static lv_obj_t *g_root, *g_list;
static curvelist_t g_cl;
/* covers, decoded into RAM once per build (no LVGL file decoding, no per-frame scaling): thumbs at their own 42 px,
 * the two big ones scaled once here to their size */
#define NPIC 16
static struct { uint8_t *px; lv_image_dsc_t dsc; } g_pic[NPIC];
static int g_npic;
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
    lv_obj_set_pos(l, x, y);
    return l;
}
static lv_obj_t *centre(lv_obj_t *p, const char *s, int y, int w, const lv_font_t *f, lv_color_t c){
    return text(p, s, (lv_obj_get_style_width(p, LV_PART_MAIN) - w) / 2, y, w, f, c, LV_TEXT_ALIGN_CENTER);   /* before layout: the set width */
}
static void arc(lv_obj_t *p, int cx, int cy, int r, int w, int a0, int a1, lv_color_t c, lv_opa_t o, int round){
    lv_obj_t *a = lv_arc_create(p); lv_obj_remove_style_all(a);
    lv_obj_set_size(a, 2 * r, 2 * r); lv_obj_set_pos(a, cx - r, cy - r); lv_arc_set_bg_angles(a, a0, a1);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN); lv_obj_set_style_arc_color(a, c, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, o, LV_PART_MAIN); lv_obj_set_style_arc_rounded(a, round, LV_PART_MAIN);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
}
/* a full-width block in the list that doesn't curve (the overview, the heroes, the clock) */
static lv_obj_t *block(int h){
    lv_obj_t *b = lv_obj_create(g_list); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 360, h);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}
static void heading(const char *s){
    lv_obj_t *b = block(44);
    centre(b, s, 18, 240, TF(UI_14), TC(ACCENT_PRIMARY));
}
/* a cache BMP (24-bit, bottom-up, rows padded to 4 bytes: the 42 px thumb's are) into XRGB8888 */
static int read_bmp(const char *path, int n, uint8_t *dst){
    if(!art_bmp_valid(path, n)) return -1;
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    size_t stride = ((size_t)n * 3 + 3) & ~(size_t)3; uint8_t row[148 * 3 + 4]; int ok = n <= 148 && !fseek(f, 54, SEEK_SET);
    for(int y = 0; y < n && ok; y++){
        if(fread(row, 1, stride, f) != stride){ ok = 0; break; }
        uint8_t *d = dst + (size_t)(n - 1 - y) * n * 4;
        for(int x = 0; x < n; x++){ d[x * 4] = row[x * 3]; d[x * 4 + 1] = row[x * 3 + 1]; d[x * 4 + 2] = row[x * 3 + 2]; d[x * 4 + 3] = 0xFF; }
    }
    fclose(f);
    return ok ? 0 : -1;
}
static void pics_forget(void){
    for(int i = 0; i < g_npic; i++){ free(g_pic[i].px); g_pic[i].px = NULL; }
    g_npic = 0;
}
/* the track's cached cover as an image of size x size (42: the thumb as is; else the 148 px cover, box-filtered down);
 * NULL when the cache doesn't have it yet */
static const lv_image_dsc_t *cover_image(const char *path, int size){
    if(!path || !path[0] || g_npic >= NPIC) return NULL;
    int thumb = size == 42, n = thumb ? 42 : 148;
    char tmp[48]; snprintf(tmp, sizeof tmp, "/tmp/stats_cover_%d.bmp", g_npic);
    uint8_t *src = malloc((size_t)n * n * 4), *dst = NULL;
    if(!src) return NULL;
    int ok = (thumb ? artcache_get_thumb(path, tmp) : artcache_get_cover(path, tmp)) == 0 && read_bmp(tmp, n, src) == 0;
    unlink(tmp);
    if(!ok){ free(src); return NULL; }
    if(thumb) dst = src;
    else {
        if(size > n) size = n;
        dst = malloc((size_t)size * size * 4);
        if(!dst){ free(src); return NULL; }
        for(int y = 0; y < size; y++) for(int x = 0; x < size; x++){       /* average the source pixels under each one */
            int x0 = x * n / size, x1 = (x + 1) * n / size, y0 = y * n / size, y1 = (y + 1) * n / size, acc[3] = { 0, 0, 0 }, k = 0;
            for(int yy = y0; yy < y1; yy++) for(int xx = x0; xx < x1; xx++){ const uint8_t *p = src + ((size_t)yy * n + xx) * 4;
                acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; k++; }
            uint8_t *o = dst + ((size_t)y * size + x) * 4;
            o[0] = (uint8_t)(acc[0] / k); o[1] = (uint8_t)(acc[1] / k); o[2] = (uint8_t)(acc[2] / k); o[3] = 0xFF;
        }
        free(src);
    }
    lv_image_dsc_t *d = &g_pic[g_npic].dsc; g_pic[g_npic].px = dst; g_npic++;
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC; d->header.cf = LV_COLOR_FORMAT_XRGB8888;
    d->header.w = d->header.h = (uint32_t)size; d->header.stride = (uint32_t)size * 4;
    d->data = dst; d->data_size = (uint32_t)size * size * 4;
    return d;
}
/* a round or rounded picture: the cover scaled to `size`, else the theme surface with a glyph or a letter */
static lv_obj_t *picture(lv_obj_t *p, int x, int y, int size, int round, const char *path, const char *fallback){
    lv_obj_t *w = box(p, x, y, size, size, round ? LV_RADIUS_CIRCLE : (size > 60 ? 12 : 6), TC(SURFACE_RAISED), LV_OPA_COVER);
    lv_obj_set_style_clip_corner(w, true, 0);
    const lv_image_dsc_t *img = cover_image(path, size);
    if(img){
        lv_obj_t *im = lv_image_create(w);
        lv_image_set_src(im, img);
        lv_obj_center(im);
    } else {
        lv_obj_t *l = lv_label_create(w); lv_label_set_text(l, fallback);
        lv_obj_set_style_text_font(l, size >= 80 ? TF(UI_44) : size >= 40 ? TF(UI_22) : TF(UI_16), 0);
        lv_obj_set_style_text_color(l, TC(ACCENT_PRIMARY), 0); lv_obj_center(l);
    }
    return w;
}
static void initial(const char *name, char *out, size_t n){       /* the first letter (a whole UTF-8 character) */
    unsigned char c = (unsigned char)name[0]; int k = 1;
    if(c >= 0xF0) k = 4; else if(c >= 0xE0) k = 3; else if(c >= 0xC0) k = 2;
    snprintf(out, n, "%.*s", k, name);
    if(k == 1 && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
}
static lv_obj_t *row(int h){
    lv_obj_t *r = lv_obj_create(g_list); lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 268, h);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                                     /* curves like a Library row */
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(r, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return r;
}
static void plays_right(lv_obj_t *r, int plays, int accent){
    char t[16]; snprintf(t, sizeof t, "%d", plays);
    int h = lv_obj_get_style_height(r, LV_PART_MAIN), lh = lv_font_get_line_height(TF(UI_18)), sh = lv_font_get_line_height(TF(UI_10));
    int y = (h - lh - sh) / 2;
    text(r, t, 196, y, 60, TF(UI_18), accent ? TC(ACCENT_PRIMARY) : TC(TEXT_PRIMARY), LV_TEXT_ALIGN_RIGHT);
    text(r, plays == 1 ? "play" : "plays", 196, y + lh, 60, TF(UI_10), TC(TEXT_MUTED), LV_TEXT_ALIGN_RIGHT);
}
static void two_lines(lv_obj_t *r, int x, int w, const char *top, const char *sub){
    const lv_font_t *ft = ui_font_cjk(18), *fs = ui_font_cjk(14);
    int h = lv_obj_get_style_height(r, LV_PART_MAIN), h1 = lv_font_get_line_height(ft), h2 = lv_font_get_line_height(fs);
    int two = sub && sub[0], y = (h - (two ? h1 + h2 : h1)) / 2;
    lv_obj_add_flag(text(r, top, x, y, w, ft, TC(TEXT_PRIMARY), LV_TEXT_ALIGN_LEFT), LV_OBJ_FLAG_USER_2);
    if(two) lv_obj_add_flag(text(r, sub, x, y + h1, w, fs, TC(TEXT_LYRICS), LV_TEXT_ALIGN_LEFT), LV_OBJ_FLAG_USER_2);
}
static void rank(lv_obj_t *r, int n){
    char t[8]; snprintf(t, sizeof t, "%d", n);
    int lh = lv_font_get_line_height(TF(UI_18));
    text(r, t, 6, (lv_obj_get_style_height(r, LV_PART_MAIN) - lh) / 2, 28, TF(UI_18), TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_CENTER);
}

/* after the curve (it narrows rows near the top and bottom and slides the play counts in): the names give way, so
 * they never run under a count. Names carry LV_OBJ_FLAG_USER_2. */
static void names_fit(void){
    uint32_t n = lv_obj_get_child_count(g_list);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(g_list, i);
        if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < -40 || a.y1 > 400) continue;
        int rw = lv_obj_get_style_width(r, LV_PART_MAIN);
        for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){
            lv_obj_t *l = lv_obj_get_child(r, k);
            if(!lv_obj_has_flag(l, LV_OBJ_FLAG_USER_2)) continue;
            int x = lv_obj_get_style_x(l, LV_PART_MAIN), w = rw - x - 50;   /* the count is right-aligned: about 46 px */
            if(w > 152) w = 152;
            if(w < 40) w = 40;
            if(lv_obj_get_style_width(l, LV_PART_MAIN) != w) lv_obj_set_width(l, w);
        }
    }
}
static void fit_cb(lv_event_t *e){ (void)e; names_fit(); }
static void rebuild(void);
static void period_cb(lv_event_t *e){ g_ptype = (int)(intptr_t)lv_event_get_user_data(e); g_poff = 0; stats_load(); rebuild(); }
static void step_cb(lv_event_t *e){ g_poff += (int)(intptr_t)lv_event_get_user_data(e); if(g_poff > 0) g_poff = 0; stats_load(); rebuild(); }
static void song_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= g_s.nsong) return;
    if(ui_play_song_by_path(g_s.song[i].path)) screen_show(SCR_NOWPLAYING);
}
static lv_obj_t *tap(lv_obj_t *o, lv_event_cb_t cb, intptr_t ud){
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, (void *)ud);
    if(!lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1)) lv_obj_add_flag(o, LV_OBJ_FLAG_USER_4);   /* our own look in every theme (the kit leaves it) */
    return o;
}

/* the period at a glance: Week / Month / Year, the period with arrows, the time, the comparison, three counts */
static void build_overview(void){
    lv_obj_t *b = block(360);
    static const char *const P[3] = { "Week", "Month", "Year" };
    lv_obj_t *bar = box(b, 92, 30, 176, 32, 16, TC(SURFACE), LV_OPA_COVER);
    for(int i = 0; i < 3; i++){
        lv_obj_t *seg = box(bar, 2 + i * 58, 2, 56, 28, 14, i == g_ptype ? TC(ACCENT_PRIMARY) : TC(SURFACE), i == g_ptype ? LV_OPA_COVER : LV_OPA_TRANSP);
        lv_obj_set_style_bg_color(seg, TC(SURFACE_PRESSED), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(seg, 6);
        if(i != g_ptype) tap(seg, period_cb, i);
        lv_obj_t *l = text(seg, P[i], 0, (28 - lv_font_get_line_height(TF(UI_14))) / 2, 56, TF(UI_14),
                           i == g_ptype ? TC(ON_ACCENT) : TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }
    char name[48]; period_name(g_ptype, g_poff, name, sizeof name, 0);
    centre(b, name, 76, 190, TF(UI_18), TC(TEXT_SECONDARY));
    long long a, z; struct tm f; period_range(g_ptype, g_poff, &a, &z, &f);
    int can_back = g_first_at && g_first_at < a, can_fwd = g_poff < 0;
    for(int k = 0; k < 2; k++){                                                  /* the arrows: earlier / later */
        int on = k ? can_fwd : can_back;
        lv_obj_t *btn = box(b, k ? 266 : 58, 70, 36, 36, LV_RADIUS_CIRCLE, TC(SURFACE), on ? LV_OPA_COVER : LV_OPA_TRANSP);
        lv_obj_set_style_bg_color(btn, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(btn, 8);
        lv_obj_t *l = lv_label_create(btn); lv_label_set_text(l, k ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(l, TF(UI_14), 0);
        lv_obj_set_style_text_color(l, on ? TC(TEXT_PRIMARY) : TC(TEXT_DISABLED), 0); lv_obj_center(l);
        if(on) tap(btn, step_cb, k ? 1 : -1);
    }
    /* the rim: this period against the one before (now: against the same stretch of it; full when ahead) */
    arc(b, 180, 180, 178, 4, 0, 360, TC(SURFACE), LV_OPA_COVER, 0);
    if(!g_s.empty){
        int deg = 360;
        if(g_s.prev_known && g_s.prev_ms > 0 && g_s.ms < g_s.prev_ms) deg = (int)(360 * g_s.ms / g_s.prev_ms);
        if(deg >= 4) arc(b, 180, 180, 178, 4, 270, 270 + deg, TC(ACCENT_PRIMARY), LV_OPA_COVER, 1);
    }
    char t[48]; fmt_dur(g_s.ms, t, sizeof t);
    centre(b, t, 108, 290, TF(UI_44), TC(TEXT_PRIMARY));
    int y = 108 + lv_font_get_line_height(TF(UI_44)) + 2;
    if(g_s.empty){
        centre(b, g_poff == 0 ? "Nothing played yet" : "Nothing played then", y, 260, TF(UI_16), TC(TEXT_SECONDARY));
        centre(b, "A song counts once it has played\nfor 30 seconds. Your stats\nfill in as you listen.", y + 40, 250, TF(UI_14), TC(TEXT_MUTED));
        return;
    }
    centre(b, "of music", y, 260, TF(UI_16), TC(TEXT_SECONDARY));
    y += lv_font_get_line_height(TF(UI_16)) + 6;
    if(g_s.prev_known){
        char d[24], prev[48], line[96]; long long diff = g_s.ms - g_s.prev_ms;
        fmt_dur_short(diff < 0 ? -diff : diff, d, sizeof d);
        period_name(g_ptype, g_poff - 1, prev, sizeof prev, 1);
        if(llabs(diff) < 60000) snprintf(line, sizeof line, g_poff ? "About the same as %s" : "On a par with %s", prev);
        else if(g_poff) snprintf(line, sizeof line, "%s  %s %s than %s", diff > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, d, diff > 0 ? "more" : "less", prev);
        else snprintf(line, sizeof line, "%s  %s %s %s", diff > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, d, diff > 0 ? "ahead of" : "behind", prev);   /* the same days of it */
        centre(b, line, y, 290, TF(UI_14), TC(ACCENT_PRIMARY));
    }
    y += lv_font_get_line_height(TF(UI_14)) + 16;
    char v[3][16]; const char *cap[3] = { g_s.plays == 1 ? "play" : "plays", g_s.songs == 1 ? "song" : "songs", "day streak" };
    snprintf(v[0], 16, "%d", g_s.plays); snprintf(v[1], 16, "%d", g_s.songs); snprintf(v[2], 16, "%d", g_s.streak);
    for(int i = 0; i < 3; i++){
        lv_obj_t *c = box(b, 46 + i * 92, y, 84, 62, 14, TC(SURFACE), LV_OPA_COVER);
        lv_obj_set_style_border_width(c, 1, 0); lv_obj_set_style_border_color(c, TC(BORDER), 0);
        text(c, v[i], 0, 7, 84, TF(UI_22), TC(TEXT_PRIMARY), LV_TEXT_ALIGN_CENTER);
        text(c, cap[i], 0, 37, 84, TF(UI_12), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    }
    centre(b, LV_SYMBOL_DOWN "  Your favourites", y + 82, 220, TF(UI_12), TC(TEXT_MUTED));
}
static void build_artists(void){
    if(!g_s.nart) return;
    heading("TOP ARTISTS");
    const item_t *top = &g_s.art[0];
    lv_obj_t *h = block(178);
    char ini[8]; initial(top->name, ini, sizeof ini);
    lv_obj_t *pic = picture(h, 128, 4, 104, 1, top->path, ini);
    lv_obj_set_style_border_width(pic, 3, 0); lv_obj_set_style_border_color(pic, TC(ACCENT_PRIMARY), 0);
    lv_obj_t *badge = box(h, 206, 78, 30, 30, LV_RADIUS_CIRCLE, TC(ACCENT_PRIMARY), LV_OPA_COVER);
    text(badge, "1", 0, (30 - lv_font_get_line_height(TF(UI_16))) / 2, 30, TF(UI_16), TC(ON_ACCENT), LV_TEXT_ALIGN_CENTER);
    centre(h, top->name, 116, 290, ui_font_cjk(26), TC(TEXT_PRIMARY));
    char d[24], line[64]; fmt_dur(top->ms, d, sizeof d);
    snprintf(line, sizeof line, "%d %s in %s", top->plays, top->plays == 1 ? "play" : "plays", d);
    centre(h, line, 150, 260, TF(UI_14), TC(TEXT_SECONDARY));
    for(int i = 1; i < g_s.nart; i++){
        lv_obj_t *r = row(46);
        rank(r, i + 1);
        int lh = lv_font_get_line_height(ui_font_cjk(18));
        lv_obj_add_flag(text(r, g_s.art[i].name, 40, (46 - lh) / 2, 136, ui_font_cjk(18), TC(TEXT_PRIMARY), LV_TEXT_ALIGN_LEFT), LV_OBJ_FLAG_USER_2);
        plays_right(r, g_s.art[i].plays, 0);
    }
}
static void build_albums(void){
    if(!g_s.nalb) return;
    heading("TOP ALBUMS");
    for(int i = 0; i < g_s.nalb; i++){
        lv_obj_t *r = row(58);
        if(i == 0){ lv_obj_set_style_border_width(r, 2, 0); lv_obj_set_style_border_color(r, TC(ACCENT_PRIMARY), 0);
                    lv_obj_set_style_bg_color(r, TC(SURFACE), 0); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); }
        char n[12]; snprintf(n, sizeof n, "%d", i + 1);
        picture(r, 8, 8, 42, 0, g_s.alb[i].path, n);
        two_lines(r, 60, 120, g_s.alb[i].name, g_s.alb[i].sub);
        plays_right(r, g_s.alb[i].plays, i == 0);
    }
}
static void build_songs(void){
    if(!g_s.nsong) return;
    heading("ON REPEAT");
    const item_t *top = &g_s.song[0];
    lv_obj_t *h = block(214);
    tap(h, song_cb, 0);
    arc(h, 180, 62, 62, 4, 0, 360, TC(SURFACE), LV_OPA_COVER, 0);
    arc(h, 180, 62, 62, 4, 300, 300 + 290, TC(ACCENT_PRIMARY), LV_OPA_COVER, 1);   /* a loop round the cover */
    picture(h, 136, 18, 88, 0, top->path, LV_SYMBOL_AUDIO);
    lv_obj_t *loop = box(h, 216, 2, 32, 32, LV_RADIUS_CIRCLE, TC(ACCENT_PRIMARY), LV_OPA_COVER);
    text(loop, LV_SYMBOL_LOOP, 0, (32 - lv_font_get_line_height(TF(UI_14))) / 2, 32, TF(UI_14), TC(ON_ACCENT), LV_TEXT_ALIGN_CENTER);
    centre(h, top->name, 132, 290, ui_font_cjk(24), TC(TEXT_PRIMARY));
    centre(h, top->sub, 164, 260, ui_font_cjk(16), TC(TEXT_SECONDARY));
    char line[48]; snprintf(line, sizeof line, "%d %s", top->plays, top->plays == 1 ? "play" : "plays");
    centre(h, line, 188, 200, TF(UI_14), TC(ACCENT_PRIMARY));
    for(int i = 1; i < g_s.nsong; i++){
        lv_obj_t *r = row(54);
        tap(r, song_cb, i);
        rank(r, i + 1);
        two_lines(r, 40, 136, g_s.song[i].name, g_s.song[i].sub);
        plays_right(r, g_s.song[i].plays, 0);
    }
}
/* when you listen: 24 bars round the rim (midnight at the top), the busiest hour in the middle */
static void build_clock(void){
    long long mx = 0; int peak = 0;
    for(int h = 0; h < 24; h++) if(g_s.hour_ms[h] > mx){ mx = g_s.hour_ms[h]; peak = h; }
    if(mx <= 0) return;
    lv_obj_t *b = block(360);
    for(int h = 0; h < 24; h++){
        int a0 = 270 + h * 15 + 2, a1 = a0 + 11;
        arc(b, 180, 180, 160, 36, a0, a1, TC(SURFACE), LV_OPA_COVER, 0);
        int w = (int)(36 * g_s.hour_ms[h] / mx);
        if(g_s.hour_ms[h] > 0 && w < 4) w = 4;
        if(w) arc(b, 180, 180, 160, w, a0, a1, TC(ACCENT_PRIMARY), h == peak ? LV_OPA_COVER : LV_OPA_60, 0);
    }
    const lv_font_t *fs = TF(UI_12); int lh = lv_font_get_line_height(fs);
    text(b, "0", 168, 64, 24, fs, TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
    text(b, "6", 272, 180 - lh / 2, 24, fs, TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
    text(b, "12", 168, 296 - lh, 24, fs, TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
    text(b, "18", 64, 180 - lh / 2, 24, fs, TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
    char hr[16]; fmt_hour(peak, hr, sizeof hr);
    centre(b, "WHEN YOU LISTEN", 112, 170, TF(UI_12), TC(ACCENT_PRIMARY));
    centre(b, "Most of all at", 140, 170, TF(UI_14), TC(TEXT_SECONDARY));
    centre(b, hr, 162, 170, TF(UI_32), TC(TEXT_PRIMARY));
    centre(b, persona(peak), 166 + lv_font_get_line_height(TF(UI_32)), 170, TF(UI_16), TC(ACCENT_PRIMARY));
}
static void rebuild(void){
    if(!g_list) return;
    lv_obj_clean(g_list);
    pics_forget();                                          /* after the clean: no image points at them any more */
    build_overview();
    build_artists();
    build_albums();
    build_songs();
    build_clock();
    lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);
    curvelist_update(&g_cl);
    names_fit();
}
void stats_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_size(g_list, 360, 360);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_list, 0, 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(&g_cl, g_list, root, 268);
    lv_obj_add_event_cb(g_list, fit_cb, LV_EVENT_SCROLL, NULL);   /* after the curve's own handler */
}
lv_obj_t *stats_scroller(void){ return g_list; }
/* the landing page's button: read the numbers now, then show them */
void stats_open(void){
    g_ptype = P_MONTH; g_poff = 0;
    stats_load();
    rebuild();
    screen_show(SCR_STATS);
}
void stats_landing(void){
    applanding_show("Listening Stats", "Your music, in numbers.",
                    "Top artists, albums and songs,\nby week, month or year.", "Show me", APP_BUILTIN_STATS);
}
#endif /* STATS_RECORDER_ONLY */
