/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "sdio.h"
#include "musicdb.h"
#include "sqlite3.h"
#define JSMN_HEADER
#include "jsmn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <strings.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define DB_PATH "/usr/data/fiio/db/song.db"

static mdb_song_t *g_songs = NULL;
static int g_n = 0;
static int g_cap = 0;
static int g_load_err = 0;

static char (*g_cart)[MDB_STR];                      static int  g_cart_n = -1;
static char (*g_calb)[MDB_STR], (*g_calb_ar)[MDB_STR]; static int *g_calb_ct; static int g_calb_n = -1;
static int *g_calb_rep;
static char (*g_cgen)[MDB_STR];                      static int *g_cgen_ct;  static int g_cgen_n = -1;
static int g_artist_mode;
static void groups_free(void){
    free(g_cart);    g_cart=NULL;    g_cart_n=-1;
    free(g_calb);    free(g_calb_ar); free(g_calb_ct); free(g_calb_rep);
    g_calb=NULL; g_calb_ar=NULL; g_calb_ct=NULL; g_calb_rep=NULL; g_calb_n=-1;
    free(g_cgen);    free(g_cgen_ct); g_cgen=NULL; g_cgen_ct=NULL; g_cgen_n=-1;
}

static int mdb_ensure_cap(int need){
    if(need <= g_cap) return 1;
    if((size_t)need + 16 > ((size_t)-1) / sizeof *g_songs) return 0;
    int nc = need + 16;
    mdb_song_t *p = realloc(g_songs, (size_t)nc * sizeof *g_songs);
    if(!p) return 0;
    g_songs = p; g_cap = nc; return 1;
}

static void trim(char *s){
    char *p = s; while(*p==' '||*p=='\t') p++;
    if(p!=s) memmove(s, p, strlen(p)+1);
    int n = (int)strlen(s);
    while(n>0 && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) s[--n]=0;
}

static sqlite3 *g_db;
static pthread_mutex_t g_db_mu = PTHREAD_MUTEX_INITIALIZER;
static sqlite3 *db(void){
    pthread_mutex_lock(&g_db_mu);
    if(!g_db){
        sqlite3 *tmp = NULL;
        if(sqlite3_open_v2(DB_PATH, &tmp, SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX, NULL) == SQLITE_OK){
            sqlite3_busy_timeout(tmp, 4000);
            sqlite3_exec(tmp, "ALTER TABLE SONG ADD COLUMN ACCENT INTEGER DEFAULT 0;", 0, 0, 0);
            sqlite3_exec(tmp, "ALTER TABLE SONG ADD COLUMN ACCENT_KEY TEXT;", 0, 0, 0);
            sqlite3_exec(tmp, "CREATE TABLE IF NOT EXISTS PLAY_STATS(PATH TEXT PRIMARY KEY, "
                              "PLAYS INTEGER DEFAULT 0, LAST_PLAYED INTEGER DEFAULT 0);", 0, 0, 0);
            sqlite3_exec(tmp, "CREATE TABLE IF NOT EXISTS BOOK_PROGRESS(BOOK_KEY TEXT PRIMARY KEY, "
                              "MEMBER_PATH TEXT NOT NULL, POSITION_MS INTEGER NOT NULL DEFAULT 0, "
                              "UPDATED_AT INTEGER NOT NULL DEFAULT 0, COMPLETED INTEGER NOT NULL DEFAULT 0);", 0, 0, 0);
            sqlite3_exec(tmp, "CREATE TABLE IF NOT EXISTS DISKOS_META(K TEXT PRIMARY KEY, V INTEGER NOT NULL);", 0, 0, 0);
            sqlite3_exec(tmp, "CREATE TABLE IF NOT EXISTS DISKOS_PL_EXPORT(PID INTEGER PRIMARY KEY, UID INTEGER NOT NULL);", 0, 0, 0);
            g_db = tmp;
        } else if(tmp){ sqlite3_close(tmp); }
    }
    sqlite3 *ret = g_db;
    pthread_mutex_unlock(&g_db_mu);
    return ret;
}
static sqlite3 *g_db_w;
static pthread_mutex_t g_db_w_mu = PTHREAD_MUTEX_INITIALIZER;
static sqlite3 *db_w(void){
    db();
    pthread_mutex_lock(&g_db_w_mu);
    if(!g_db_w){
        sqlite3 *tmp = NULL;
        if(sqlite3_open_v2(DB_PATH, &tmp, SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX, NULL) == SQLITE_OK){
            sqlite3_busy_timeout(tmp, 500);
            g_db_w = tmp;
        } else if(tmp){ sqlite3_close(tmp); }
    }
    sqlite3 *ret = g_db_w;
    pthread_mutex_unlock(&g_db_w_mu);
    return ret;
}
int mdb_song_accent(const char *path){
    sqlite3 *d = db(); if(!d || !path) return 0;
    sqlite3_stmt *st; int rgb = 0;
    if(sqlite3_prepare_v2(d, "SELECT ACCENT FROM SONG WHERE PATH=? LIMIT 1;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
        if(sqlite3_step(st) == SQLITE_ROW) rgb = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return rgb;
}
void mdb_set_song_accent(const char *path, int rgb, const char *cover_key){
    sqlite3 *d = db_w(); if(!d || !path) return;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "UPDATE SONG SET ACCENT=?, ACCENT_KEY=? WHERE PATH=?;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_int(st, 1, rgb);
        if(cover_key && cover_key[0]) sqlite3_bind_text(st, 2, cover_key, -1, SQLITE_STATIC); else sqlite3_bind_null(st, 2);
        sqlite3_bind_text(st, 3, path, -1, SQLITE_STATIC);
        sqlite3_step(st); sqlite3_finalize(st);
    }
}
int mdb_song_accent_fresh(const char *path, const char *cover_key){
    sqlite3 *d = db_w(); if(!d || !path || !cover_key || !cover_key[0]) return 0;
    sqlite3_stmt *st; int rgb = 0, fresh = 0, legacy = 0;
    if(sqlite3_prepare_v2(d, "SELECT ACCENT, ACCENT_KEY FROM SONG WHERE PATH=? LIMIT 1;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
        if(sqlite3_step(st) == SQLITE_ROW){
            rgb = sqlite3_column_int(st, 0);
            const unsigned char *k = sqlite3_column_text(st, 1);
            if(rgb && !k) legacy = 1;
            else if(rgb && !strcmp((const char *)k, cover_key)) fresh = 1;
        }
        sqlite3_finalize(st);
    }
    if(legacy && sqlite3_prepare_v2(d, "UPDATE SONG SET ACCENT_KEY=? WHERE PATH=? AND ACCENT_KEY IS NULL;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_text(st, 1, cover_key, -1, SQLITE_STATIC); sqlite3_bind_text(st, 2, path, -1, SQLITE_STATIC);
        sqlite3_step(st); sqlite3_finalize(st);
        fresh = 1;
    }
    return fresh;
}
int mdb_prewarm_next(int after_id, int *id, char *path, int cap){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st; int found = 0;
    if(sqlite3_prepare_v2(d, "SELECT ID, PATH FROM SONG WHERE ID > ? ORDER BY ID LIMIT 1;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_int(st, 1, after_id);
        if(sqlite3_step(st) == SQLITE_ROW){
            if(id) *id = sqlite3_column_int(st, 0);
            const char *p = (const char*)sqlite3_column_text(st, 1);
            if(path && cap>0) snprintf(path, cap, "%s", p ? p : "");
            found = 1;
        }
        sqlite3_finalize(st);
    }
    return found;
}
static unsigned album_hash(const char *v){ unsigned h = 2166136261u; for(const unsigned char *q = (const unsigned char *)v; *q; q++) h = (h ^ *q) * 16777619u; return h; }
static const char *colt(sqlite3_stmt *st, int i){
    const char *t = (const char*)sqlite3_column_text(st, i);
    return t ? t : "";
}
static int table_has_col(sqlite3 *d, const char *table, const char *col){
    char q[96]; snprintf(q, sizeof q, "PRAGMA table_info(%s);", table);
    sqlite3_stmt *st; int has = 0;
    if(sqlite3_prepare_v2(d, q, -1, &st, NULL) != SQLITE_OK) return 0;
    while(!has && sqlite3_step(st) == SQLITE_ROW){ const unsigned char *n = sqlite3_column_text(st, 1); has = n && !strcasecmp((const char *)n, col); }
    sqlite3_finalize(st);
    return has;
}
static int playlist_has_m3u_cols(sqlite3 *d){ return table_has_col(d, "CUSTOM_PLAYLIST", "IS_M3U") && table_has_col(d, "CUSTOM_PLAYLIST", "M3U_PATH"); }

#define MDB_SONG_ORDER \
    "CASE WHEN IS_CUE=0 AND IS_ISO=0 THEN 1 WHEN IS_CUE=1 OR IS_ISO=1 THEN 2 END," \
    "CASE WHEN (IS_CUE=0 AND IS_ISO=0) THEN CASE WHEN TITLE IS NOT NULL THEN TITLE_CODE ELSE NAME_CODE END END," \
    "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN NAME_CODE END,CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN ID END," \
    "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN TRACK END,ID"
#define MDB_GROUP_CODE_EXPR "CASE WHEN NULLIF(ALBUM_ARTIST,'') IS NOT NULL AND ALBUM_ARTIST_CODE IS NOT NULL THEN ALBUM_ARTIST_CODE ELSE ARTIST_CODE END"

int mdb_load(void){
    g_n = 0; g_load_err = 0;
    groups_free();
    sqlite3 *d = db(); if(!d){ g_load_err = 1; return 0; }
    sqlite3_stmt *cst;
    int count = 0, count_ok = 0;
    if(sqlite3_prepare_v2(d, "SELECT COUNT(*) FROM SONG;", -1, &cst, NULL) == SQLITE_OK){
        if(sqlite3_step(cst) == SQLITE_ROW){ count = sqlite3_column_int(cst, 0); count_ok = 1; }
        sqlite3_finalize(cst);
    }
    if(!count_ok){ g_load_err = 1; return 0; }
    if(count > 0 && !mdb_ensure_cap(count)) g_load_err = 1;
    if(g_cap == 0){ if(count > 0) g_load_err = 1; return 0; }
    sqlite3_stmt *st;
    int has_aa = table_has_col(d, "SONG", "ALBUM_ARTIST");
    int has_codes = table_has_col(d, "SONG", "TITLE_CODE") && table_has_col(d, "SONG", "NAME_CODE")
                 && table_has_col(d, "SONG", "ARTIST_CODE") && table_has_col(d, "SONG", "ALBUM_CODE")
                 && table_has_col(d, "SONG", "GENRE_CODE") && table_has_col(d, "SONG", "IS_CUE") && table_has_col(d, "SONG", "IS_ISO");
    char codes[400], sql[1400];
    if(has_codes)
        snprintf(codes, sizeof codes, "IFNULL(ARTIST_CODE,0),IFNULL(ALBUM_CODE,0),IFNULL(GENRE_CODE,0),IFNULL(%s,0)",
                 has_aa && table_has_col(d, "SONG", "ALBUM_ARTIST_CODE") ? MDB_GROUP_CODE_EXPR : "ARTIST_CODE");
    else snprintf(codes, sizeof codes, "0,0,0,0");
    snprintf(sql, sizeof sql,
        "SELECT IFNULL(TITLE,IFNULL(NAME,'Untitled')),IFNULL(ARTIST,''),IFNULL(ALBUM,''),"
        "IFNULL(DURATION,0),ID,IFNULL(GENRE,''),%s,IFNULL(TRACK,0),%s FROM SONG "
        "WHERE lower(PATH) NOT LIKE '%%.m4b' "
        "ORDER BY %s;",
        has_aa ? "IFNULL(COALESCE(NULLIF(ALBUM_ARTIST,''),ARTIST),'')" : "IFNULL(ARTIST,'')",
        codes,
        has_codes ? MDB_SONG_ORDER : "1 COLLATE NOCASE");
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK){ g_load_err = 1; return 0; }
    int rc = SQLITE_DONE;
    while(g_n < g_cap && (rc = sqlite3_step(st)) == SQLITE_ROW){
        mdb_song_t *s = &g_songs[g_n++];
        snprintf(s->title,  MDB_STR, "%s", colt(st,0));
        snprintf(s->artist, MDB_STR, "%s", colt(st,1));
        snprintf(s->album,  MDB_STR, "%s", colt(st,2));
        s->album_h = album_hash(colt(st,2));
        s->dur_ms = sqlite3_column_int(st,3);
        s->id     = sqlite3_column_int(st,4);
        snprintf(s->genre,  MDB_STR, "%s", colt(st,5));
        snprintf(s->artist_group, MDB_STR, "%s", colt(st,6));
        s->track  = sqlite3_column_int(st,7);
        s->code_artist = sqlite3_column_int(st,8); s->code_album = sqlite3_column_int(st,9);
        s->code_genre  = sqlite3_column_int(st,10); s->code_group = sqlite3_column_int(st,11);
        trim(s->title); trim(s->artist); trim(s->album); trim(s->genre);
    }
    if(rc != SQLITE_ROW && rc != SQLITE_DONE) g_load_err = 1;
    sqlite3_finalize(st);
    return g_n;
}

int mdb_song_count(void){ return g_n; }
int mdb_total_song_count(void){
    sqlite3 *d = db(); if(!d) return -1;
    sqlite3_stmt *st; int total = -1;
    if(sqlite3_prepare_v2(d, "SELECT COUNT(*) FROM SONG;", -1, &st, NULL) == SQLITE_OK){
        if(sqlite3_step(st) == SQLITE_ROW) total = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if(total >= 0 && sqlite3_prepare_v2(d, "SELECT COUNT(*) FROM BOOKS;", -1, &st, NULL) == SQLITE_OK){
        if(sqlite3_step(st) == SQLITE_ROW) total += sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return total;
}
int mdb_load_failed(void){ return g_load_err; }
const mdb_song_t *mdb_song(int i){ return (i>=0 && i<g_n) ? &g_songs[i] : NULL; }

int mdb_song_path(int id, char *out, int cap){
    if(!out || cap<=0) return 0;
    out[0]=0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT PATH FROM SONG WHERE ID=? LIMIT 1;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, id);
    if(sqlite3_step(st) == SQLITE_ROW) snprintf(out, cap, "%s", colt(st,0));
    sqlite3_finalize(st);
    return out[0] ? 1 : 0;
}

int mdb_song_id_by_path(const char *path){
    if(!path || !*path) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT ID FROM SONG WHERE PATH=? LIMIT 1;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
    int id = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    return id;
}

int mdb_song_album(int id, char *out, int cap){
    if(!out || cap<=0) return 0;
    out[0]=0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT IFNULL(ALBUM,'') FROM SONG WHERE ID=? LIMIT 1;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, id);
    if(sqlite3_step(st) == SQLITE_ROW) snprintf(out, cap, "%s", colt(st,0));
    sqlite3_finalize(st);
    return out[0] ? 1 : 0;
}

int mdb_song_meta_by_path(const char *path, char *album, int acap, char *artist, int arcap){
    if(album && acap>0) album[0]=0;
    if(artist && arcap>0) artist[0]=0;
    if(!path || !path[0]) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT IFNULL(ALBUM,''),IFNULL(ARTIST,'') FROM SONG WHERE PATH=? LIMIT 1;",
                          -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
    int found = 0;
    if(sqlite3_step(st) == SQLITE_ROW){
        if(album  && acap>0)  snprintf(album,  acap,  "%s", colt(st,0));
        if(artist && arcap>0) snprintf(artist, arcap, "%s", colt(st,1));
        found = 1;
    }
    sqlite3_finalize(st);
    return found;
}

int mdb_current_play(mdb_song_t *out, int *pos_ms, int *is_playing){
    if(out) memset(out, 0, sizeof *out);
    if(pos_ms) *pos_ms = 0;
    if(is_playing) *is_playing = 0;
    sqlite3 *d = db(); if(!d) return 0;
    int mid = 0, pos = 0, play = 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT MUSIC_ID,IFNULL(POSITION,0),IFNULL(IS_PLAYING,0) "
                             "FROM MEMORY_PLAY ORDER BY ID DESC LIMIT 1;", -1, &st, NULL) == SQLITE_OK){
        if(sqlite3_step(st) == SQLITE_ROW){
            mid  = sqlite3_column_int(st,0);
            pos  = sqlite3_column_int(st,1);
            play = sqlite3_column_int(st,2);
        }
        sqlite3_finalize(st);
    }
    if(mid <= 0) return 0;
    if(sqlite3_prepare_v2(d, "SELECT IFNULL(TITLE,IFNULL(NAME,'Untitled')),IFNULL(ARTIST,''),"
                             "IFNULL(ALBUM,''),IFNULL(DURATION,0),ID FROM SONG WHERE ID=? LIMIT 1;",
                             -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, mid);
    int got = 0;
    if(sqlite3_step(st) == SQLITE_ROW){
        if(out){
            snprintf(out->title,  MDB_STR, "%s", colt(st,0));
            snprintf(out->artist, MDB_STR, "%s", colt(st,1));
            snprintf(out->album,  MDB_STR, "%s", colt(st,2));
            out->dur_ms = sqlite3_column_int(st,3);
            out->id     = sqlite3_column_int(st,4);
        }
        if(pos_ms) *pos_ms = pos;
        if(is_playing) *is_playing = play;
        got = 1;
    }
    sqlite3_finalize(st);
    return got;
}

#ifndef MDB_SYSCONFIG_PATH
#define MDB_SYSCONFIG_PATH "/usr/data/fiio/db/sysconfig.db"
#endif
int mdb_artist_class_type(void){
    sqlite3 *c = NULL; int v = -1;
    if(sqlite3_open_v2(MDB_SYSCONFIG_PATH, &c, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, 200);
        sqlite3_stmt *st;
        if(sqlite3_prepare_v2(c, "SELECT ARTIST_CLASS_TYPE FROM SYSCONFIG WHERE ID=1;", -1, &st, NULL) == SQLITE_OK){
            if(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) == SQLITE_INTEGER){
                int x = sqlite3_column_int(st, 0);
                if(x == 0 || x == 1) v = x;
            }
            sqlite3_finalize(st);
        }
    }
    if(c) sqlite3_close(c);
    return v;
}

int mdb_play_pos(int id, int list_type, const char *name){
    static const char *ORDER_TAIL =
        "CASE WHEN IS_CUE=0 AND IS_ISO=0 THEN 1 WHEN IS_CUE=1 OR IS_ISO=1 THEN 2 END,"
        "CASE WHEN (IS_CUE=0 AND IS_ISO=0) THEN CASE WHEN TITLE IS NOT NULL THEN TITLE_CODE ELSE NAME_CODE END END,"
        "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN NAME_CODE END,"
        "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN ID END,"
        "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN TRACK END";
    static const char *ORDER_ALBUM =
        "CASE WHEN DISC=0 THEN 1 ELSE 0 END,DISC,"
        "CASE WHEN TRACK=0 THEN 1 ELSE 0 END,TRACK,"
        "CASE WHEN TITLE IS NOT NULL THEN TITLE_CODE ELSE NAME_CODE END";

    sqlite3 *d = db(); if(!d) return 0;
    const char *order = (list_type==3) ? ORDER_ALBUM : ORDER_TAIL;
    const char *col   = (list_type==3) ? "ALBUM" : (list_type==2) ? "ARTIST" : (list_type==10) ? "GENRE" : NULL;
    if(list_type == 2){
        int cls = mdb_artist_class_type();
        if(cls < 0) return 0;
        if(cls == 1) col = "COALESCE(ALBUM_ARTIST,ARTIST)";
    }
    char sql[1400];
    if(col)
        snprintf(sql, sizeof sql,
            "SELECT pos FROM (SELECT ID,ROW_NUMBER() OVER (ORDER BY %s) pos FROM SONG WHERE %s=? AND lower(PATH) NOT LIKE '%%.m4b') WHERE ID=?;",
            order, col);
    else
        snprintf(sql, sizeof sql,
            "SELECT pos FROM (SELECT ID,ROW_NUMBER() OVER (ORDER BY %s) pos FROM SONG WHERE lower(PATH) NOT LIKE '%%.m4b') WHERE ID=?;", order);
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    if(col){ sqlite3_bind_text(st,1,name?name:"",-1,SQLITE_STATIC); sqlite3_bind_int(st,2,id); }
    else   { sqlite3_bind_int(st,1,id); }
    int pos = 0;
    if(sqlite3_step(st) == SQLITE_ROW){ int v=sqlite3_column_int(st,0); if(v>=1) pos=v; }
    sqlite3_finalize(st);
    return pos;
}

#define MDB_ALBUM_ORDER "CASE WHEN DISC=0 THEN 1 ELSE 0 END,DISC,CASE WHEN TRACK=0 THEN 1 ELSE 0 END,TRACK," \
                        "CASE WHEN TITLE IS NOT NULL THEN TITLE_CODE ELSE NAME_CODE END"
#define MDB_TYPE7_MAX 1000
#define MDB_KEY_WHERE "((COALESCE(ALBUM_ARTIST,ARTIST)=?1) OR (?1 IS NULL AND COALESCE(ALBUM_ARTIST,ARTIST) IS NULL)) AND ALBUM=?2"
int mdb_album_keys(mdb_album_key_t *keys, int *counts, int *rep_ids, int cap){
    sqlite3 *d = db(); if(!d) return -1;
    sqlite3_stmt *st;
    static const char *SQL_CODE =
        "SELECT O, ALBUM, N, ID FROM (SELECT COALESCE(ALBUM_ARTIST,ARTIST) O, ALBUM, ID, COUNT(*) OVER w N, ALBUM_CODE C,"
        " ROW_NUMBER() OVER (w ORDER BY " MDB_ALBUM_ORDER ") RN FROM SONG"
        " WHERE ALBUM IS NOT NULL AND ALBUM<>'' AND lower(PATH) NOT LIKE '%.m4b'"
        " WINDOW w AS (PARTITION BY COALESCE(ALBUM_ARTIST,ARTIST), ALBUM)) WHERE RN=1"
        " ORDER BY IFNULL(C,0), ALBUM COLLATE NOCASE, ALBUM, O COLLATE NOCASE, O;";
    static const char *SQL_NAME =
        "SELECT O, ALBUM, N, ID FROM (SELECT COALESCE(ALBUM_ARTIST,ARTIST) O, ALBUM, ID, COUNT(*) OVER w N,"
        " ROW_NUMBER() OVER (w ORDER BY CASE WHEN DISC=0 THEN 1 ELSE 0 END,DISC,CASE WHEN TRACK=0 THEN 1 ELSE 0 END,TRACK,ID) RN FROM SONG"
        " WHERE ALBUM IS NOT NULL AND ALBUM<>'' AND lower(PATH) NOT LIKE '%.m4b'"
        " WINDOW w AS (PARTITION BY COALESCE(ALBUM_ARTIST,ARTIST), ALBUM)) WHERE RN=1"
        " ORDER BY ALBUM COLLATE NOCASE, ALBUM, O COLLATE NOCASE, O;";
    if(sqlite3_prepare_v2(d, SQL_CODE, -1, &st, NULL) != SQLITE_OK && sqlite3_prepare_v2(d, SQL_NAME, -1, &st, NULL) != SQLITE_OK) return -1;
    int n = 0, rc;
    while((rc = sqlite3_step(st)) == SQLITE_ROW){
        if(n < cap){
            const unsigned char *o = sqlite3_column_text(st, 0), *al = sqlite3_column_text(st, 1);
            if((o && strlen((const char *)o) >= MDB_STR) || strlen((const char *)al) >= MDB_STR){ continue; }
            keys[n].owner_null = (sqlite3_column_type(st, 0) == SQLITE_NULL);
            snprintf(keys[n].owner, MDB_STR, "%s", o ? (const char *)o : "");
            snprintf(keys[n].album, MDB_STR, "%s", (const char *)al);
            if(counts)  counts[n]  = sqlite3_column_int(st, 2);
            if(rep_ids) rep_ids[n] = sqlite3_column_int(st, 3);
        }
        n++;
    }
    sqlite3_finalize(st);
    if(rc != SQLITE_DONE) return -1;
    return n < cap ? n : cap;
}
static int ids_ordered(const char *where, const char *p1, int p1null, const char *p2, int no_books, int *ids, int cap){
    sqlite3 *d = db(); if(!d) return -1;
    char sql[700];
    snprintf(sql, sizeof sql, "SELECT ID FROM SONG WHERE %s%s ORDER BY " MDB_ALBUM_ORDER ";", where,
             no_books ? " AND lower(PATH) NOT LIKE '%.m4b'" : "");
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    if(p1null) sqlite3_bind_null(st, 1); else sqlite3_bind_text(st, 1, p1 ? p1 : "", -1, SQLITE_STATIC);
    if(p2) sqlite3_bind_text(st, 2, p2, -1, SQLITE_STATIC);
    int n = 0, rc;
    while((rc = sqlite3_step(st)) == SQLITE_ROW){ if(n < cap) ids[n] = sqlite3_column_int(st, 0); n++; }
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? n : -1;
}
int mdb_album_key_song_ids(const mdb_album_key_t *k, int *ids, int cap){
    if(!k || !k->album[0]) return -1;
    return ids_ordered(MDB_KEY_WHERE, k->owner, k->owner_null, k->album, 1, ids, cap);
}
static int same_seq(const int *a, int na, const int *b, int nb){ return na == nb && (na == 0 || !memcmp(a, b, (size_t)na * sizeof *a)); }
int mdb_album_plan(const mdb_album_key_t *k, mdb_plan_t *plan){
    if(!k || !plan || !k->album[0]) return 0;
    memset(plan, 0, sizeof *plan);
    int n = mdb_album_key_song_ids(k, NULL, 0);
    if(n <= 0) return 0;
    int *a = malloc((size_t)n * sizeof *a), *b = malloc((size_t)(n + 1) * sizeof *b);
    if(!a || !b){ free(a); free(b); return 0; }
    if(mdb_album_key_song_ids(k, a, n) != n){ free(a); free(b); return 0; }
    int m = ids_ordered("ALBUM=?1", k->album, 0, NULL, 0, b, n + 1);
    if(same_seq(a, n, b, m)){
        plan->list_type = 3; snprintf(plan->name, sizeof plan->name, "%s", k->album);
    } else {
        int cls = mdb_artist_class_type();
        int quotable = !k->owner_null && !strchr(k->owner, '"') && !strchr(k->album, '"')
                    && strlen(k->owner) < MDB_TYPE7_MAX && strlen(k->album) < MDB_TYPE7_MAX;
        m = -1;
        if(cls >= 0 && quotable)
            m = ids_ordered(cls == 1 ? "COALESCE(ALBUM_ARTIST,ARTIST)=?1 AND ALBUM=?2" : "ARTIST=?1 AND ALBUM=?2",
                            k->owner, 0, k->album, 0, b, n + 1);
        if(m >= 0 && same_seq(a, n, b, m)){
            plan->list_type = 7;
            int w = snprintf(plan->name, sizeof plan->name, "{\"artist\":\"%s\", \"album\":\"%s\"}", k->owner, k->album);
            if(w < 0 || w >= (int)sizeof plan->name) plan->list_type = 5;
        } else plan->list_type = 5;
        if(plan->list_type == 5) plan->name[0] = 0;
    }
    free(b);
    plan->ids = a; plan->count = n;
    return 1;
}
int mdb_plan_materialize(mdb_plan_t *plan){
    if(!plan || plan->list_type != 5 || !plan->ids || plan->count <= 0) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    if(sqlite3_exec(d, "BEGIN IMMEDIATE;", 0, 0, 0) != SQLITE_OK) return 0;
    int ok = sqlite3_exec(d, "INSERT OR IGNORE INTO CUSTOM_PLAYLIST_INDEX (LIST_ID,LIST_NAME,M3U_PATH) VALUES ("
                             XSTR(DISKOS_RSV_LISTID) ",'diskos-book','');", 0, 0, 0) == SQLITE_OK
          && sqlite3_exec(d, "DELETE FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=" XSTR(DISKOS_RSV_LISTID) ";", 0, 0, 0) == SQLITE_OK;
    sqlite3_stmt *ins = NULL;
    int m3u = ok && playlist_has_m3u_cols(d);
    if(ok && sqlite3_prepare_v2(d, m3u ?
        "INSERT INTO CUSTOM_PLAYLIST (PLAYLIST_ID,PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,DURATION,"
        "ALBUM_ARTIST,IS_M3U,M3U_PATH) "
        "SELECT " XSTR(DISKOS_RSV_LISTID) ",PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,"
        "(CASE WHEN DURATION>0 THEN DURATION ELSE 86400000 END),ALBUM_ARTIST,IFNULL(IS_M3U,0),IFNULL(M3U_PATH,'') "
        "FROM SONG WHERE ID=?1;" :
        "INSERT INTO CUSTOM_PLAYLIST (PLAYLIST_ID,PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,DURATION,"
        "ALBUM_ARTIST) "
        "SELECT " XSTR(DISKOS_RSV_LISTID) ",PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,"
        "(CASE WHEN DURATION>0 THEN DURATION ELSE 86400000 END),ALBUM_ARTIST "
        "FROM SONG WHERE ID=?1;", -1, &ins, NULL) != SQLITE_OK) ok = 0;
    for(int i = 0; ok && i < plan->count; i++){
        sqlite3_reset(ins); sqlite3_bind_int(ins, 1, plan->ids[i]);
        if(sqlite3_step(ins) != SQLITE_DONE || sqlite3_changes(d) != 1) ok = 0;
    }
    if(ins) sqlite3_finalize(ins);
    if(!ok || sqlite3_exec(d, "COMMIT;", 0, 0, 0) != SQLITE_OK){ sqlite3_exec(d, "ROLLBACK;", 0, 0, 0); return 0; }
    int *order = malloc((size_t)plan->count * sizeof *order);
    if(!order) return 0;
    int *sorted = malloc((size_t)plan->count * sizeof *sorted);
    if(sorted){
        memcpy(sorted, plan->ids, (size_t)plan->count * sizeof *sorted);
        qsort(sorted, (size_t)plan->count, sizeof *sorted, mdb_id_cmp);
    }
    sqlite3_stmt *rd, *map;
    int n = 0; ok = 1;
    if(sqlite3_prepare_v2(d, "SELECT PATH,TRACK,IS_CUE,IS_ISO FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=" XSTR(DISKOS_RSV_LISTID) ";",
                          -1, &rd, NULL) != SQLITE_OK){ free(sorted); free(order); return 0; }
    if(sqlite3_prepare_v2(d, "SELECT ID FROM SONG WHERE PATH=?1 AND TRACK IS ?2 AND IS_CUE IS ?3 AND IS_ISO IS ?4;", -1, &map, NULL) != SQLITE_OK){
        sqlite3_finalize(rd); free(sorted); free(order); return 0;
    }
    int rrc;
    while(ok && (rrc = sqlite3_step(rd)) == SQLITE_ROW){
        sqlite3_reset(map);
        for(int c = 0; c < 4; c++) sqlite3_bind_value(map, c + 1, sqlite3_column_value(rd, c));
        int id = 0, hits = 0, mrc;
        while((mrc = sqlite3_step(map)) == SQLITE_ROW){
            int cand = sqlite3_column_int(map, 0);
            int hit = 0;
            if(sorted) hit = bsearch(&cand, sorted, (size_t)plan->count, sizeof *sorted, mdb_id_cmp) != NULL;
            else for(int j = 0; j < plan->count && !hit; j++) hit = (plan->ids[j] == cand);
            if(hit){ id = cand; hits++; }
        }
        if(mrc != SQLITE_DONE || hits != 1 || n >= plan->count) ok = 0; else order[n++] = id;
    }
    if(ok && rrc != SQLITE_DONE) ok = 0;
    sqlite3_finalize(rd); sqlite3_finalize(map);
    if(!ok || n != plan->count){ free(sorted); free(order); return 0; }
    int dup = 0;
    if(sorted){
        memcpy(sorted, order, (size_t)n * sizeof *sorted);
        qsort(sorted, (size_t)n, sizeof *sorted, mdb_id_cmp);
        for(int i = 1; i < n && !dup; i++) dup = (sorted[i-1] == sorted[i]);
    } else for(int i = 0; i < n && !dup; i++) for(int j = i + 1; j < n && !dup; j++) dup = (order[i] == order[j]);
    free(sorted);
    if(dup){ free(order); return 0; }
    free(plan->ids); plan->ids = order;
    return 1;
}
int mdb_plan_pos(const mdb_plan_t *plan, int song_id){
    if(!plan || !plan->ids) return 0;
    for(int i = 0; i < plan->count; i++) if(plan->ids[i] == song_id) return i + 1;
    return 0;
}
void mdb_plan_free(mdb_plan_t *plan){ if(plan){ free(plan->ids); plan->ids = NULL; plan->count = 0; } }

int mdb_subtrack_count(const char *path, int iso_only){
    sqlite3 *d = db(); if(!d || !path) return 0;
    sqlite3_stmt *st; int n = 0;
    if(sqlite3_prepare_v2(d, iso_only ? "SELECT COUNT(*) FROM SONG WHERE PATH=?1 AND COALESCE(IS_ISO,0)=1;"
                                      : "SELECT COUNT(*) FROM SONG WHERE PATH=?1 AND (COALESCE(IS_CUE,0)=1 OR COALESCE(IS_ISO,0)=1);", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
    if(sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}
int mdb_subtrack_plan(const char *const *paths, int npaths, mdb_plan_t *plan){
    if(!plan || !paths || npaths <= 0) return 0;
    memset(plan, 0, sizeof *plan);
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT ID FROM SONG WHERE PATH=?1 AND (COALESCE(IS_CUE,0)=1 OR COALESCE(IS_ISO,0)=1) ORDER BY TRACK,ID;", -1, &st, NULL) != SQLITE_OK) return 0;
    int cap = 0, n = 0; int *ids = NULL;
    for(int i = 0; i < npaths; i++){
        if(!paths[i]) continue;
        sqlite3_reset(st); sqlite3_bind_text(st, 1, paths[i], -1, SQLITE_STATIC);
        while(sqlite3_step(st) == SQLITE_ROW){
            if(n == cap){
                int nc = cap ? cap * 2 : 32; int *ni = realloc(ids, (size_t)nc * sizeof *ids);
                if(!ni){ free(ids); sqlite3_finalize(st); return 0; }
                ids = ni; cap = nc;
            }
            ids[n++] = sqlite3_column_int(st, 0);
        }
    }
    sqlite3_finalize(st);
    if(n <= 0){ free(ids); return 0; }
    plan->list_type = 5; plan->ids = ids; plan->count = n;
    return 1;
}


int mdb_split_artists(const char *raw, char toks[][MDB_STR], int cap){
    int n = 0;
    char buf[MDB_STR]; snprintf(buf, MDB_STR, "%s", raw);
    char *p = buf, *start = buf;
    while(n < cap){
        if(*p==',' || *p==';' || *p==0){
            char c = *p; *p = 0;
            char tok[MDB_STR]; snprintf(tok, MDB_STR, "%s", start); trim(tok);
            if(tok[0]) snprintf(toks[n++], MDB_STR, "%s", tok);
            if(c==0) break;
            start = p+1;
        }
        p++;
    }
    return n;
}


static int mdb_code4(const char *s){
    unsigned v = 0; int j = 0;
    for(const unsigned char *p = (const unsigned char *)(s ? s : ""); *p && j < 4; p++, j++) v = (v << 8) | (unsigned)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
    for(; j < 4; j++) v <<= 8;
    return (int)(v & 0x7fffffffu);
}
static int mdb_code_name_cmp(int ca, const char *a, int cb, const char *b){
    if(ca != cb) return ca < cb ? -1 : 1;
    int c = strcasecmp(a, b);
    return c ? c : strcmp(a, b);
}
typedef struct { const char *name; int code; } mdb_nc_t;
static int mdb_nc_cmp(const void *a, const void *b){ return strcmp(((const mdb_nc_t*)a)->name, ((const mdb_nc_t*)b)->name); }
static mdb_nc_t *mdb_nc_build(int group, int *n){
    *n = 0;
    mdb_nc_t *t = malloc((size_t)(g_n > 0 ? g_n : 1) * sizeof *t);
    if(!t) return NULL;
    int m = 0;
    for(int i = 0; i < g_n; i++){
        const char *nm = group ? g_songs[i].artist_group : g_songs[i].artist;
        if(!nm[0]) continue;
        t[m].name = nm; t[m].code = group ? g_songs[i].code_group : g_songs[i].code_artist; m++;
    }
    qsort(t, (size_t)m, sizeof *t, mdb_nc_cmp);
    int w = 0;
    for(int i = 0; i < m; i++){
        if(w && !strcmp(t[w-1].name, t[i].name)){ if(t[i].code < t[w-1].code) t[w-1].code = t[i].code; }
        else t[w++] = t[i];
    }
    *n = w;
    return t;
}
static int mdb_nc_code(const mdb_nc_t *t, int n, const char *name){
    mdb_nc_t key = { name, 0 };
    const mdb_nc_t *hit = t ? bsearch(&key, t, (size_t)n, sizeof *t, mdb_nc_cmp) : NULL;
    return hit ? hit->code : mdb_code4(name);
}

typedef struct { const char *al; const char *ar; int idx; int cnt; int first; int code; unsigned h; } mdb_ai_t;
static int mdb_ai_cmp(const void *a, const void *b){
    const mdb_ai_t *x = a, *y = b;
    int c = strcasecmp(x->al, y->al);
    if(!c) c = strcmp(x->al, y->al);
    if(!c) c = (x->h > y->h) - (x->h < y->h);
    return c ? c : (x->idx > y->idx) - (x->idx < y->idx);
}
static int mdb_ai_code_cmp(const void *a, const void *b){
    const mdb_ai_t *x = a, *y = b;
    return mdb_code_name_cmp(x->code, x->al, y->code, y->al);
}
int mdb_albums(char names[][MDB_STR], char artists[][MDB_STR], int *counts, int cap){
    if(g_calb_n < 0){
        mdb_ai_t *tmp = malloc((size_t)(g_n>0?g_n:1) * sizeof *tmp);
        if(!tmp) return 0;
        int m = 0;
        for(int i=0;i<g_n;i++) if(g_songs[i].album[0]){
            tmp[m].al=g_songs[i].album; tmp[m].ar=g_songs[i].artist; tmp[m].idx=i; tmp[m].cnt=1; tmp[m].first=i; tmp[m].code=g_songs[i].code_album; tmp[m].h=g_songs[i].album_h; m++; }
        qsort(tmp, (size_t)m, sizeof *tmp, mdb_ai_cmp);
        int n = 0;
        for(int i=0;i<m;i++){
            if(n>0 && !strcmp(tmp[i].al, tmp[n-1].al) && tmp[i].h == tmp[n-1].h){
                tmp[n-1].cnt++;
                if(tmp[i].idx < tmp[n-1].first) tmp[n-1].first = tmp[i].idx;
                if(tmp[i].code < tmp[n-1].code) tmp[n-1].code = tmp[i].code;
            } else tmp[n++] = tmp[i];
        }
        qsort(tmp, (size_t)n, sizeof *tmp, mdb_ai_code_cmp);
        g_calb=malloc((size_t)(n>0?n:1)*MDB_STR); g_calb_ar=malloc((size_t)(n>0?n:1)*MDB_STR); g_calb_ct=malloc((size_t)(n>0?n:1)*sizeof(int));
        g_calb_rep=malloc((size_t)(n>0?n:1)*sizeof(int));
        if(g_calb && g_calb_ar && g_calb_ct && g_calb_rep){
            for(int i=0;i<n;i++){ snprintf(g_calb[i],MDB_STR,"%s",tmp[i].al); snprintf(g_calb_ar[i],MDB_STR,"%s",tmp[i].ar); g_calb_ct[i]=tmp[i].cnt;
                                  g_calb_rep[i]=g_songs[tmp[i].first].id; }
            g_calb_n=n;
        } else { free(g_calb); free(g_calb_ar); free(g_calb_ct); free(g_calb_rep); g_calb=NULL; g_calb_ar=NULL; g_calb_ct=NULL; g_calb_rep=NULL; free(tmp); return 0; }
        free(tmp);
    }
    int n = g_calb_n < cap ? g_calb_n : cap;
    if(n>0){ memcpy(names,g_calb,(size_t)n*MDB_STR); memcpy(artists,g_calb_ar,(size_t)n*MDB_STR); memcpy(counts,g_calb_ct,(size_t)n*sizeof(int)); }
    return n;
}

int mdb_album_rep_ids(int *out, int cap){
    if(g_calb_n < 0) mdb_albums(NULL, NULL, NULL, 0);
    if(g_calb_n <= 0 || !g_calb_rep || !out) return 0;
    int n = g_calb_n < cap ? g_calb_n : cap;
    memcpy(out, g_calb_rep, (size_t)n * sizeof *out);
    return n;
}

int mdb_album_count(void){
    if(g_calb_n < 0) mdb_albums(NULL, NULL, NULL, 0);
    return g_calb_n;
}

static int mdb_name_ci_cmp(const void *a, const void *b){ return strcasecmp((const char*)a, (const char*)b); }

static int mdb_name_ci_exact_cmp(const void *a, const void *b){
    int c = strcasecmp((const char*)a, (const char*)b);
    return c ? c : strcmp((const char*)a, (const char*)b);
}
static int mdb_named_code_cmp(const void *a, const void *b){
    const mdb_nc_t *x = a, *y = b;
    return mdb_code_name_cmp(x->code, x->name, y->code, y->name);
}
static void mdb_order_names_by_code(char (*buf)[MDB_STR], int n, int group){
    if(n < 2) return;
    int tn = 0; mdb_nc_t *tbl = mdb_nc_build(group, &tn);
    mdb_nc_t *arr = malloc((size_t)n * sizeof *arr);
    char (*out)[MDB_STR] = malloc((size_t)n * MDB_STR);
    if(arr && out){
        for(int i = 0; i < n; i++){ arr[i].name = buf[i]; arr[i].code = mdb_nc_code(tbl, tn, buf[i]); }
        qsort(arr, (size_t)n, sizeof *arr, mdb_named_code_cmp);
        for(int i = 0; i < n; i++) memcpy(out[i], arr[i].name, MDB_STR);
        memcpy(buf, out, (size_t)n * MDB_STR);
    }
    free(arr); free(out); free(tbl);
}
void mdb_set_artist_mode(int album_artist){
    album_artist = album_artist ? 1 : 0;
    if(album_artist == g_artist_mode) return;
    g_artist_mode = album_artist;
    free(g_cart); g_cart = NULL; g_cart_n = -1;
}
int mdb_artist_mode(void){ return g_artist_mode; }

static int artist_credited(const char *raw, const char *artist);
int mdb_artist_missing(const char *key){
    return !key || key[strspn(key, " \t\r\n\v\f")] == 0;
}
static int artist_has(const mdb_song_t *s, const char *key){
    const char *raw = g_artist_mode ? s->artist_group : s->artist;
    if(!key[0]) return mdb_artist_missing(raw);
    return g_artist_mode ? !strcmp(raw, key) : artist_credited(raw, key);
}

int mdb_artists(char names[][MDB_STR], int cap){
    if(g_cart_n < 0 && g_artist_mode){
        char (*buf)[MDB_STR] = malloc((size_t)(g_n > 0 ? g_n : 1) * MDB_STR);
        if(!buf) return 0;
        int n = 0;
        for(int i = 0; i < g_n; i++){
            if(mdb_artist_missing(g_songs[i].artist_group)) memset(buf[n++], 0, MDB_STR);
            else memcpy(buf[n++], g_songs[i].artist_group, MDB_STR);
        }
        qsort(buf, (size_t)n, MDB_STR, mdb_name_ci_exact_cmp);
        int w = 0;
        for(int i = 0; i < n; i++)
            if(w == 0 || strcmp(buf[w-1], buf[i]) != 0){ if(w != i) memcpy(buf[w], buf[i], MDB_STR); w++; }
        mdb_order_names_by_code(buf, w, 1);
        g_cart = malloc((size_t)(w > 0 ? w : 1) * MDB_STR);
        if(g_cart){ if(w) memcpy(g_cart, buf, (size_t)w * MDB_STR); g_cart_n = w; }
        free(buf);
        if(g_cart_n < 0) return 0;
    }
    if(g_cart_n < 0){
        long maxtok = 0;
        for(int i=0;i<g_n;i++){
            int c=1; for(const char *p=g_songs[i].artist; *p; p++) if(*p==','||*p==';') c++;
            maxtok += c;
        }
        char (*buf)[MDB_STR] = malloc((size_t)(maxtok>0?maxtok:1) * MDB_STR);
        if(!buf) return 0;
        int n = 0;
        for(int i=0;i<g_n && n<maxtok;i++){
            if(mdb_artist_missing(g_songs[i].artist)) memset(buf[n++], 0, MDB_STR);
            else n += mdb_split_artists(g_songs[i].artist, buf + n, (int)(maxtok - n));
        }
        qsort(buf, (size_t)n, MDB_STR, mdb_name_ci_cmp);
        int w = 0;
        for(int i=0;i<n;i++)
            if(w==0 || strcasecmp(buf[w-1], buf[i]) != 0){
                if(w != i) memcpy(buf[w], buf[i], MDB_STR);
                w++;
            }
        mdb_order_names_by_code(buf, w, 0);
        g_cart = malloc((size_t)(w>0?w:1) * MDB_STR);
        if(g_cart){ if(w) memcpy(g_cart, buf, (size_t)w * MDB_STR); g_cart_n = w; }
        free(buf);
        if(g_cart_n < 0) return 0;
    }
    int n = g_cart_n < cap ? g_cart_n : cap;
    if(n>0) memcpy(names, g_cart, (size_t)n * MDB_STR);
    return n;
}

int mdb_artist_count(void){
    if(g_cart_n < 0){ char dummy[MDB_STR]; mdb_artists((char (*)[MDB_STR])dummy, 0); }
    return g_cart_n < 0 ? 0 : g_cart_n;
}

typedef struct { const char *name; int code; int count; } mdb_gn_t;
static int mdb_gn_ci_cmp(const void *a, const void *b){ return strcasecmp(((const mdb_gn_t*)a)->name, ((const mdb_gn_t*)b)->name); }
static int mdb_gn_code_cmp(const void *a, const void *b){
    const mdb_gn_t *x = a, *y = b;
    return mdb_code_name_cmp(x->code, x->name, y->code, y->name);
}
int mdb_genres(char names[][MDB_STR], int *counts, int cap){
    if(g_cgen_n < 0){
        mdb_gn_t *tmp = malloc((size_t)(g_n>0?g_n:1) * sizeof *tmp);
        if(!tmp) return 0;
        int m=0;
        for(int i=0;i<g_n;i++) if(g_songs[i].genre[0]){ tmp[m].name=g_songs[i].genre; tmp[m].code=g_songs[i].code_genre; tmp[m].count=1; m++; }
        qsort(tmp, (size_t)m, sizeof *tmp, mdb_gn_ci_cmp);
        int n=0;
        for(int i=0;i<m;i++){
            if(n>0 && !strcasecmp(tmp[i].name, tmp[n-1].name)){ tmp[n-1].count++; if(tmp[i].code < tmp[n-1].code) tmp[n-1].code = tmp[i].code; }
            else tmp[n++] = tmp[i];
        }
        qsort(tmp, (size_t)n, sizeof *tmp, mdb_gn_code_cmp);
        g_cgen=malloc((size_t)(n>0?n:1)*MDB_STR); g_cgen_ct=malloc((size_t)(n>0?n:1)*sizeof(int));
        if(g_cgen && g_cgen_ct){
            for(int i=0;i<n;i++){ snprintf(g_cgen[i],MDB_STR,"%s",tmp[i].name); g_cgen_ct[i]=tmp[i].count; }
            g_cgen_n=n;
        }
        else { free(g_cgen); free(g_cgen_ct); g_cgen=NULL; g_cgen_ct=NULL; free(tmp); return 0; }
        free(tmp);
    }
    int n = g_cgen_n < cap ? g_cgen_n : cap;
    if(n>0){ memcpy(names,g_cgen,(size_t)n*MDB_STR); memcpy(counts,g_cgen_ct,(size_t)n*sizeof(int)); }
    return n;
}

int mdb_genre_count(void){
    if(g_cgen_n < 0) mdb_genres(NULL, NULL, 0);
    return g_cgen_n > 0 ? g_cgen_n : 0;
}

int mdb_genre_songs(const char *genre, const mdb_song_t **out, int cap){
    int n = 0;
    for(int i=0;i<g_n && n<cap;i++)
        if(!strcasecmp(g_songs[i].genre, genre)) out[n++] = &g_songs[i];
    return n;
}

typedef struct { int disc, track, ord; const mdb_song_t *s; } alb_key_t;
static int alb_cmp(const void *a, const void *b){
    const alb_key_t *x = a, *y = b;
    if(x->disc  != y->disc ) return (x->disc  < y->disc ) ? -1 : 1;
    if(x->track != y->track) return (x->track < y->track) ? -1 : 1;
    return x->ord - y->ord;
}

int mdb_album_track_ids(const char *album, int *ids, int cap){
    int n = 0;
    for(int i=0;i<g_n && n<cap;i++) if(!strcmp(g_songs[i].album, album)) ids[n++] = g_songs[i].id;
    return n;
}

static int alb_id_cmp(const void *a, const void *b){
    const alb_key_t *x = a, *y = b;
    return (x->s->id > y->s->id) - (x->s->id < y->s->id);
}
static int alb_id_find(const void *a, const void *b){
    int id = *(const int *)a;
    const alb_key_t *k = b;
    return (id > k->s->id) - (id < k->s->id);
}
static void alb_read_each(sqlite3 *d, alb_key_t *k, int n){
    sqlite3_stmt *st = NULL;
    if(sqlite3_prepare_v2(d, "SELECT DISC,TRACK FROM SONG WHERE ID=?;", -1, &st, NULL) != SQLITE_OK) st = NULL;
    for(int i=0;i<n;i++){
        if(k[i].disc) continue;
        int disc = 0, track = 0;
        if(st){
            sqlite3_reset(st);
            sqlite3_bind_int(st, 1, k[i].s->id);
            if(sqlite3_step(st) == SQLITE_ROW){ disc = sqlite3_column_int(st,0); track = sqlite3_column_int(st,1); }
        }
        k[i].disc  = (disc  > 0) ? disc  : INT_MAX;
        k[i].track = (track > 0) ? track : INT_MAX;
    }
    if(st) sqlite3_finalize(st);
}
int mdb_album_songs(const char *album, const mdb_song_t **out, int cap){
    if(cap <= 0) return 0;
    int n = 0;
    for(int i=0;i<g_n && n<cap;i++)
        if(!strcmp(g_songs[i].album, album)) out[n++] = &g_songs[i];
    if(n <= 1) return n;
    sqlite3 *d = db(); if(!d) return n;
    alb_key_t *k = malloc((size_t)n * sizeof *k);
    if(!k) return n;
    for(int i=0;i<n;i++){ k[i].disc = 0; k[i].track = INT_MAX; k[i].ord = i; k[i].s = out[i]; }
    size_t size = (size_t)n > ((size_t)-1 - 64) / 12 ? 0 : (size_t)n * 12 + 64;
    char *sql = size ? malloc(size) : NULL;
    sqlite3_stmt *st = NULL;
    int rc = SQLITE_ERROR;
    if(sql){
        size_t used = (size_t)snprintf(sql, size, "SELECT ID,DISC,TRACK FROM SONG WHERE ID IN (");
        for(int i=0;i<n;i++) used += (size_t)snprintf(sql + used, size - used, "%s%d", i ? "," : "", out[i]->id);
        snprintf(sql + used, size - used, ");");
        if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) st = NULL;
        free(sql);
    }
    if(st){
        qsort(k, (size_t)n, sizeof *k, alb_id_cmp);
        while((rc = sqlite3_step(st)) == SQLITE_ROW){
            int id = sqlite3_column_int(st, 0);
            alb_key_t *hit = bsearch(&id, k, (size_t)n, sizeof *k, alb_id_find);
            if(hit){
                int disc = sqlite3_column_int(st, 1), track = sqlite3_column_int(st, 2);
                hit->disc = disc > 0 ? disc : INT_MAX;
                hit->track = track > 0 ? track : INT_MAX;
            }
        }
        sqlite3_finalize(st);
    }
    if(rc == SQLITE_DONE){ for(int i=0;i<n;i++) if(!k[i].disc) k[i].disc = INT_MAX; }
    else alb_read_each(d, k, n);
    qsort(k, (size_t)n, sizeof *k, alb_cmp);
    for(int i=0;i<n;i++) out[i] = k[i].s;
    free(k);
    return n;
}

static int artist_credited(const char *raw, const char *artist){
    size_t alen = strlen(artist);
    for(const char *s = raw; *s; ){
        while(*s==' '||*s=='\t') s++;
        const char *start = s;
        while(*s && *s!=',' && *s!=';') s++;
        const char *end = s;
        while(end>start && (end[-1]==' '||end[-1]=='\t'||end[-1]=='\r'||end[-1]=='\n')) end--;
        if((size_t)(end-start)==alen && alen && strncasecmp(start, artist, alen)==0) return 1;
        if(*s) s++;
    }
    return 0;
}

int mdb_artist_songs(const char *artist, const mdb_song_t **out, int cap){
    int n = 0;
    for(int i=0;i<g_n && n<cap;i++)
        if(artist && artist_has(&g_songs[i], artist))
            out[n++] = &g_songs[i];
    return n;
}
int mdb_artist_plan(const char *key, mdb_plan_t *plan){
    if(!key || !plan) return 0;
    memset(plan, 0, sizeof *plan);
    if(!key[0]){
        int n = 0;
        for(int i = 0; i < g_n; i++) if(artist_has(&g_songs[i], key)) n++;
        if(!n) return 0;
        plan->ids = malloc((size_t)n * sizeof *plan->ids);
        if(!plan->ids) return 0;
        for(int i = 0; i < g_n; i++) if(artist_has(&g_songs[i], key)) plan->ids[plan->count++] = g_songs[i].id;
        plan->list_type = 5;
        return 1;
    }
    int n = 0;
    for(int i = 0; i < g_n; i++) if(!strcmp(g_songs[i].artist_group, key)) n++;
    if(n <= 0) return 0;
    int *a = malloc((size_t)n * sizeof *a), *b = malloc((size_t)(n + 1) * sizeof *b);
    if(!a || !b){ free(a); free(b); return 0; }
    int k = 0;
    for(int i = 0; i < g_n && k < n; i++) if(!strcmp(g_songs[i].artist_group, key)) a[k++] = g_songs[i].id;
    int m = -1;
    sqlite3 *d = db();
    sqlite3_stmt *st;
    if(d && sqlite3_prepare_v2(d, "SELECT ID FROM SONG WHERE COALESCE(ALBUM_ARTIST,ARTIST)=?1 ORDER BY "
        "CASE WHEN IS_CUE=0 AND IS_ISO=0 THEN 1 WHEN IS_CUE=1 OR IS_ISO=1 THEN 2 END,"
        "CASE WHEN (IS_CUE=0 AND IS_ISO=0) THEN CASE WHEN TITLE IS NOT NULL THEN TITLE_CODE ELSE NAME_CODE END END,"
        "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN NAME_CODE END,"
        "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN ID END,"
        "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN TRACK END;",
                               -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
        int rc; m = 0;
        while((rc = sqlite3_step(st)) == SQLITE_ROW){ if(m < n + 1) b[m] = sqlite3_column_int(st, 0); m++; }
        sqlite3_finalize(st);
        if(rc != SQLITE_DONE) m = -1;
    }
    int same = m == n;
    if(same){
        qsort(a, (size_t)n, sizeof *a, mdb_id_cmp);
        for(int i = 0; same && i < n; i++)
            same = bsearch(&b[i], a, (size_t)n, sizeof *a, mdb_id_cmp) != NULL;
    }
    if(same && mdb_artist_class_type() == 1 && strlen(key) < MDB_STR - 1 && strlen(key) < sizeof plan->name){
        plan->list_type = 2; snprintf(plan->name, sizeof plan->name, "%s", key);
        memcpy(a, b, (size_t)n * sizeof *a);
    } else {
        plan->list_type = 5; plan->name[0] = 0;
        if(m == n){
            k = 0;
            for(int i = 0; i < g_n && k < n; i++) if(!strcmp(g_songs[i].artist_group, key)) a[k++] = g_songs[i].id;
        }
    }
    free(b);
    plan->ids = a; plan->count = n;
    return 1;
}

static int scope_has(int kind, const mdb_song_t *s, const char *key){
    if(kind == MDB_SCOPE_GENRE) return !strcasecmp(s->genre, key);
    return artist_has(s, key);
}
static int scope_song_cmp(const void *a, const void *b){
    const mdb_song_t *x = *(const mdb_song_t *const *)a, *y = *(const mdb_song_t *const *)b;
    int c = (x->code_album == 0 && y->code_album == 0) ? strcmp(x->album, y->album)
          : mdb_code_name_cmp(x->code_album, x->album, y->code_album, y->album);
    return c ? c : (x < y ? -1 : x > y);
}
static int scope_id_cmp(const void *a, const void *b){
    int x = (*(const mdb_song_t *const *)a)->id, y = (*(const mdb_song_t *const *)b)->id;
    return (x > y) - (x < y);
}
int mdb_scope_albums(int kind, const char *key, char names[][MDB_STR], char artists[][MDB_STR], int *counts, int cap){
    if(!key || (!key[0] && kind != MDB_SCOPE_ARTIST)) return 0;
    const mdb_song_t **tmp = malloc((size_t)(g_n > 0 ? g_n : 1) * sizeof *tmp);
    if(!tmp) return 0;
    int m = 0;
    for(int i = 0; i < g_n; i++) if(g_songs[i].album[0] && scope_has(kind, &g_songs[i], key)) tmp[m++] = &g_songs[i];
    qsort(tmp, (size_t)m, sizeof *tmp, scope_song_cmp);
    int n = 0;
    for(int i = 0; i < m; ){
        int j = i + 1;
        while(j < m && !strcmp(tmp[j]->album, tmp[i]->album)) j++;
        if(names && n < cap){
            snprintf(names[n], MDB_STR, "%s", tmp[i]->album);
            if(artists) snprintf(artists[n], MDB_STR, "%s", tmp[i]->artist);
            if(counts)  counts[n] = j - i;
        }
        n++; i = j;
    }
    free(tmp);
    return n;
}
static int scope_album_rows(int kind, const char *key, const char *album, const mdb_song_t **out, int cap, int *exact){
    if(exact) *exact = 0;
    if(!key || (!key[0] && kind != MDB_SCOPE_ARTIST) || !album || !album[0]) return 0;
    const mdb_song_t **cand = malloc((size_t)(g_n > 0 ? g_n : 1) * sizeof *cand);
    if(!cand) return 0;
    int n = 0;
    for(int i = 0; i < g_n; i++) if(!strcmp(g_songs[i].album, album) && scope_has(kind, &g_songs[i], key)) cand[n++] = &g_songs[i];
    if(n == 0){ free(cand); return 0; }
    const mdb_song_t **srt = malloc((size_t)n * sizeof *srt), **ord = malloc((size_t)n * sizeof *ord);
    const mdb_song_t **use = cand;
    if(srt && ord){
        memcpy(srt, cand, (size_t)n * sizeof *srt);
        qsort(srt, (size_t)n, sizeof *srt, scope_id_cmp);
        sqlite3 *d = db();
        sqlite3_stmt *st;
        if(d && sqlite3_prepare_v2(d, "SELECT ID FROM SONG WHERE trim(ALBUM, ' ' || char(9,10,13)) = ?1 ORDER BY "
                                      MDB_ALBUM_ORDER ";", -1, &st, NULL) == SQLITE_OK){
            sqlite3_bind_text(st, 1, album, -1, SQLITE_STATIC);
            int k = 0, rc;
            while((rc = sqlite3_step(st)) == SQLITE_ROW){
                mdb_song_t probe; probe.id = sqlite3_column_int(st, 0);
                const mdb_song_t *pp = &probe;
                const mdb_song_t **hit = bsearch(&pp, srt, (size_t)n, sizeof *srt, scope_id_cmp);
                if(hit && k < n) ord[k++] = *hit;
            }
            sqlite3_finalize(st);
            if(rc == SQLITE_DONE && k == n){ use = ord; if(exact) *exact = 1; }
        }
    }
    int w = n < cap ? n : cap;
    if(out) for(int i = 0; i < w; i++) out[i] = use[i];
    free(cand); free(srt); free(ord);
    return out ? w : n;
}
int mdb_scope_album_songs(int kind, const char *key, const char *album, const mdb_song_t **out, int cap){
    return scope_album_rows(kind, key, album, out, cap, NULL);
}
int mdb_scope_album_plan(int kind, const char *key, const char *album, mdb_plan_t *plan){
    if(!plan) return 0;
    memset(plan, 0, sizeof *plan);
    if(!key || (!key[0] && kind != MDB_SCOPE_ARTIST) || !album || !album[0]) return 0;
    int n = scope_album_rows(kind, key, album, NULL, 0, NULL);
    if(n <= 0) return 0;
    const mdb_song_t **rows = malloc((size_t)n * sizeof *rows);
    int *a = malloc((size_t)n * sizeof *a), *b = malloc((size_t)(n + 1) * sizeof *b);
    int exact = 0;
    if(!rows || !a || !b || scope_album_rows(kind, key, album, rows, n, &exact) != n){ free(rows); free(a); free(b); return 0; }
    for(int i = 0; i < n; i++) a[i] = rows[i]->id;
    free(rows);
    plan->list_type = 5;
    int quotable = key[0] && !strchr(key, '"') && !strchr(album, '"') && strlen(key) < MDB_TYPE7_MAX && strlen(album) < MDB_TYPE7_MAX;
    const char *where = NULL, *fmt = NULL; int lt = 0;
    if(kind == MDB_SCOPE_GENRE){
        where = "GENRE=?1 AND ALBUM=?2"; lt = 8; fmt = "{\"style\":\"%s\", \"album\":\"%s\"}";
    } else {
        int cls = mdb_artist_class_type();
        if(cls >= 0){
            where = cls == 1 ? "COALESCE(ALBUM_ARTIST,ARTIST)=?1 AND ALBUM=?2" : "ARTIST=?1 AND ALBUM=?2";
            lt = 7; fmt = "{\"artist\":\"%s\", \"album\":\"%s\"}";
        }
    }
    if(exact && quotable && where){
        int m = ids_ordered(where, key, 0, album, 0, b, n + 1);
        if(m >= 0 && same_seq(a, n, b, m)){
            int w = snprintf(plan->name, sizeof plan->name, fmt, key, album);
            if(w > 0 && w < (int)sizeof plan->name) plan->list_type = lt;
        }
    }
    if(plan->list_type == 5) plan->name[0] = 0;
    free(b);
    plan->ids = a; plan->count = n;
    return 1;
}

#define MDB_FAV_ORDER \
    "CASE WHEN IS_CUE=0 AND IS_ISO=0 THEN 1 WHEN IS_CUE=1 OR IS_ISO=1 THEN 2 END," \
    "CASE WHEN (IS_CUE=0 AND IS_ISO=0) THEN CASE WHEN TITLE IS NOT NULL THEN TITLE_CODE ELSE NAME_CODE END END," \
    "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN NAME_CODE END," \
    "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN ID END," \
    "CASE WHEN (IS_CUE=1 OR IS_ISO=1) THEN TRACK END"
int mdb_favorites(mdb_song_t *out, int cap, int stock_order){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    char sql[1400];
    snprintf(sql, sizeof sql,
        "SELECT IFNULL(TITLE,IFNULL(NAME,'Untitled')),IFNULL(ARTIST,''),IFNULL(ALBUM,''),"
        "IFNULL(DURATION,0),COALESCE("
        "(SELECT S.ID FROM SONG S WHERE S.PATH=MY_LOVE.PATH AND COALESCE(S.IS_CUE,0)=COALESCE(MY_LOVE.IS_CUE,0) "
        "AND COALESCE(S.IS_ISO,0)=COALESCE(MY_LOVE.IS_ISO,0) AND COALESCE(S.TRACK,0)=COALESCE(MY_LOVE.TRACK,0) LIMIT 1),"
        "(SELECT S.ID FROM SONG S WHERE S.PATH=MY_LOVE.PATH AND COALESCE(S.IS_CUE,0)=0 AND COALESCE(S.IS_ISO,0)=0 LIMIT 1),"
        "(SELECT S.ID FROM SONG S WHERE S.PATH=MY_LOVE.PATH ORDER BY S.TRACK, S.ID LIMIT 1)),ID "
        "FROM MY_LOVE WHERE lower(PATH) NOT LIKE '%%.m4b' ORDER BY %s;",
        stock_order ? MDB_FAV_ORDER : "1 COLLATE NOCASE");
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    int n=0;
    while(n<cap && sqlite3_step(st) == SQLITE_ROW){
        mdb_song_t *s = &out[n++];
        snprintf(s->title,  MDB_STR, "%s", colt(st,0));
        snprintf(s->artist, MDB_STR, "%s", colt(st,1));
        snprintf(s->album,  MDB_STR, "%s", colt(st,2));
        s->album_h = album_hash(colt(st,2));
        s->dur_ms = sqlite3_column_int(st,3);
        s->id     = sqlite3_column_int(st,4);
        s->love_id = sqlite3_column_int(st,5);
        s->genre[0] = 0;
        trim(s->title); trim(s->artist); trim(s->album);
    }
    sqlite3_finalize(st); return n;
}

int mdb_favorite_count(void){
    sqlite3 *d = db(); if(!d) return -1;
    sqlite3_stmt *st; int n = -1;
    if(sqlite3_prepare_v2(d, "SELECT COUNT(*) FROM MY_LOVE WHERE lower(PATH) NOT LIKE '%.m4b';", -1, &st, NULL) != SQLITE_OK) return -1;
    if(sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st); return n;
}
int mdb_love_path(int love_id, char *out, int cap){
    if(cap > 0) out[0] = 0;
    sqlite3 *d = db(); if(!d || love_id <= 0 || cap <= 0) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT PATH FROM MY_LOVE WHERE ID=?1;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, love_id);
    int ok = 0;
    if(sqlite3_step(st) == SQLITE_ROW){ snprintf(out, cap, "%s", colt(st,0)); ok = out[0] != 0; }
    sqlite3_finalize(st); return ok;
}

void mdb_record_play(const char *path){
    if(!path || !path[0]) return;
    sqlite3 *d = db(); if(!d) return;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d,
        "INSERT INTO PLAY_STATS(PATH,PLAYS,LAST_PLAYED) VALUES(?1,1,?2) "
        "ON CONFLICT(PATH) DO UPDATE SET PLAYS=PLAYS+1, LAST_PLAYED=?2;", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_text (st, 1, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)time(NULL));
    sqlite3_step(st); sqlite3_finalize(st);
}
static int mdb_stats_list(mdb_song_t *out, int cap, int by_recent){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    const char *sql = by_recent
      ? "SELECT IFNULL(S.TITLE,IFNULL(S.NAME,'Untitled')),IFNULL(S.ARTIST,''),IFNULL(S.ALBUM,''),"
        "IFNULL(S.DURATION,0),S.ID FROM PLAY_STATS P JOIN SONG S ON S.PATH=P.PATH "
        "WHERE lower(S.PATH) NOT LIKE '%.m4b' ORDER BY P.LAST_PLAYED DESC LIMIT ?1;"
      : "SELECT IFNULL(S.TITLE,IFNULL(S.NAME,'Untitled')),IFNULL(S.ARTIST,''),IFNULL(S.ALBUM,''),"
        "IFNULL(S.DURATION,0),S.ID FROM PLAY_STATS P JOIN SONG S ON S.PATH=P.PATH "
        "WHERE P.PLAYS>0 AND lower(S.PATH) NOT LIKE '%.m4b' ORDER BY P.PLAYS DESC, P.LAST_PLAYED DESC LIMIT ?1;";
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, cap);
    int n=0;
    while(n<cap && sqlite3_step(st) == SQLITE_ROW){
        mdb_song_t *s = &out[n++];
        snprintf(s->title,  MDB_STR, "%s", colt(st,0));
        snprintf(s->artist, MDB_STR, "%s", colt(st,1));
        snprintf(s->album,  MDB_STR, "%s", colt(st,2));
        s->album_h = album_hash(colt(st,2));
        s->dur_ms = sqlite3_column_int(st,3);
        s->id     = sqlite3_column_int(st,4);
        s->genre[0] = 0;
        trim(s->title); trim(s->artist); trim(s->album);
    }
    sqlite3_finalize(st); return n;
}
int mdb_stats_count(int by_recent){
    sqlite3 *d = db(); if(!d) return -1;
    sqlite3_stmt *st;
    const char *sql = by_recent
        ? "SELECT COUNT(*) FROM PLAY_STATS P JOIN SONG S ON S.PATH=P.PATH WHERE lower(S.PATH) NOT LIKE '%.m4b';"
        : "SELECT COUNT(*) FROM PLAY_STATS P JOIN SONG S ON S.PATH=P.PATH WHERE P.PLAYS>0 AND lower(S.PATH) NOT LIKE '%.m4b';";
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    int n = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : -1;
    sqlite3_finalize(st);
    return n;
}
int mdb_mostplayed(mdb_song_t *out, int cap){ return mdb_stats_list(out, cap, 0); }
int mdb_recent(mdb_song_t *out, int cap){ return mdb_stats_list(out, cap, 1); }

int mdb_song_genre(const char *path, int track, char *out, int cap){
    if(cap > 0) out[0] = 0;
    sqlite3 *d = db(); if(!d || !path || !path[0] || cap <= 0) return 0;
    sqlite3_stmt *st; int found = 0;
    if(sqlite3_prepare_v2(d, track > 0 ? "SELECT IFNULL(GENRE,'') FROM SONG WHERE PATH=?1 AND TRACK=?2 AND (IS_CUE=1 OR IS_ISO=1) ORDER BY ID LIMIT 1;"
                                       : "SELECT IFNULL(GENRE,'') FROM SONG WHERE PATH=?1 ORDER BY ID LIMIT 1;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
    if(track > 0) sqlite3_bind_int(st, 2, track);
    if(sqlite3_step(st) == SQLITE_ROW){ snprintf(out, (size_t)cap, "%s", colt(st, 0)); found = 1; }
    sqlite3_finalize(st);
    return found;
}

int mdb_is_book_path(const char *path){
    if(!DISKOS_AUDIOBOOKS) return 0;
    if(!path) return 0;
    size_t n = strlen(path);
    return n > 4 && !strcasecmp(path + n - 4, ".m4b");
}

int mdb_books(book_t *out, int max){
    if(max <= 0) return 0;
    sqlite3 *d = db(); if(!d) return -1;
    sqlite3_stmt *st;
    const char *sql =
        "SELECT S.ID, IFNULL(S.TITLE,IFNULL(S.NAME,'Untitled')), IFNULL(S.ARTIST,''), S.PATH, "
        "IFNULL(B.POSITION_MS,0), IFNULL(B.COMPLETED,0) "
        "FROM BOOKS S LEFT JOIN BOOK_PROGRESS B ON B.BOOK_KEY = S.PATH "
        "ORDER BY IFNULL(B.UPDATED_AT,0) DESC, IFNULL(S.ADD_TIME,0) DESC, S.ID DESC;";
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    int n = 0, rc;
    while(n < max && (rc = sqlite3_step(st)) == SQLITE_ROW){
        book_t *b = &out[n++];
        b->id = sqlite3_column_int(st, 0);
        snprintf(b->title,  MDB_STR, "%s", colt(st, 1));
        snprintf(b->author, MDB_STR, "%s", colt(st, 2));
        snprintf(b->path, sizeof b->path, "%s", colt(st, 3));
        b->position_ms = (long)sqlite3_column_int64(st, 4);
        b->completed   = sqlite3_column_int(st, 5);
        trim(b->title); trim(b->author);
    }
    int err = (n == 0 && rc != SQLITE_DONE && rc != SQLITE_ROW);
    sqlite3_finalize(st);
    return err ? -1 : n;
}

int mdb_book_progress(const char *key, char *member_out, int member_cap, long *position_ms, int *completed_out, long *updated_out){
    if(completed_out) *completed_out = 0;
    if(updated_out) *updated_out = 0;
    if(!key) return -1;
    sqlite3 *d = db(); if(!d) return -1;
    sqlite3_stmt *st; int found = 0;
    if(sqlite3_prepare_v2(d, "SELECT MEMBER_PATH,POSITION_MS,COMPLETED,UPDATED_AT FROM BOOK_PROGRESS WHERE BOOK_KEY=? LIMIT 1;", -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    if(rc == SQLITE_ROW){
        if(member_out && member_cap > 0) snprintf(member_out, (size_t)member_cap, "%s", colt(st, 0));
        if(position_ms) *position_ms = (long)sqlite3_column_int64(st, 1);
        if(completed_out) *completed_out = sqlite3_column_int(st, 2);
        if(updated_out) *updated_out = (long)sqlite3_column_int64(st, 3);
        found = 1;
    } else if(rc != SQLITE_DONE){
        found = -1;
    }
    sqlite3_finalize(st);
    return found;
}

int mdb_book_save(const char *key, const char *member_path, long position_ms, int completed){
    if(!key || !member_path) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    const char *sql =
        "INSERT INTO BOOK_PROGRESS(BOOK_KEY,MEMBER_PATH,POSITION_MS,UPDATED_AT,COMPLETED) VALUES(?1,?2,?3,?4,?5) "
        "ON CONFLICT(BOOK_KEY) DO UPDATE SET MEMBER_PATH=?2,POSITION_MS=?3,UPDATED_AT=?4,COMPLETED=?5;";
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, member_path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, position_ms < 0 ? 0 : position_ms);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)time(NULL));
    sqlite3_bind_int(st, 5, completed ? 1 : 0);
    int ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

int mdb_reserved_slot_set(const char *path){
    sqlite3 *d = db(); if(!d || !path || !*path) return 0;
    if(sqlite3_exec(d, "BEGIN IMMEDIATE;", 0, 0, 0) != SQLITE_OK) return 0;
    int ok = 1;
    if(sqlite3_exec(d, "INSERT OR IGNORE INTO CUSTOM_PLAYLIST_INDEX (LIST_ID,LIST_NAME,M3U_PATH) VALUES ("
                       XSTR(DISKOS_RSV_LISTID) ",'diskos-book','');", 0, 0, 0) != SQLITE_OK) ok = 0;
    if(ok && sqlite3_exec(d, "DELETE FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=" XSTR(DISKOS_RSV_LISTID) ";", 0, 0, 0) != SQLITE_OK) ok = 0;
    if(ok){
        sqlite3_stmt *st;
        const char *sql =
            "INSERT INTO CUSTOM_PLAYLIST (PLAYLIST_ID,PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DURATION,IS_CUE,IS_ISO,IS_DSD,OFFSET) "
            "SELECT " XSTR(DISKOS_RSV_LISTID) ",PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,"
            "(CASE WHEN DURATION>0 THEN DURATION ELSE 86400000 END),0,0,0,0 FROM BOOKS WHERE PATH=?1;";
        if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) == SQLITE_OK){
            sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
            if(sqlite3_step(st) != SQLITE_DONE) ok = 0;
            sqlite3_finalize(st);
        } else ok = 0;
        if(ok && sqlite3_changes(d) == 0){
            sqlite3_stmt *s2;
            if(sqlite3_prepare_v2(d, "INSERT INTO CUSTOM_PLAYLIST (PLAYLIST_ID,PATH,NAME,TITLE,DURATION) VALUES ("
                                     XSTR(DISKOS_RSV_LISTID) ",?1,?1,?1,86400000);", -1, &s2, NULL) == SQLITE_OK){
                sqlite3_bind_text(s2, 1, path, -1, SQLITE_STATIC);
                if(sqlite3_step(s2) != SQLITE_DONE) ok = 0;
                sqlite3_finalize(s2);
            } else ok = 0;
        }
    }
    if(ok && sqlite3_exec(d, "COMMIT;", 0, 0, 0) == SQLITE_OK) return 1;
    sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    return 0;
}

int mdb_reserved_slot_set_playlist(long pid){
    sqlite3 *d = db(); if(!d) return 0;
    if(sqlite3_exec(d, "BEGIN IMMEDIATE;", 0, 0, 0) != SQLITE_OK) return 0;
    int ok = 1, n = 0;
    if(sqlite3_exec(d, "INSERT OR IGNORE INTO CUSTOM_PLAYLIST_INDEX (LIST_ID,LIST_NAME,M3U_PATH) VALUES ("
                       XSTR(DISKOS_RSV_LISTID) ",'diskos-book','');", 0, 0, 0) != SQLITE_OK) ok = 0;
    if(ok && sqlite3_exec(d, "DELETE FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=" XSTR(DISKOS_RSV_LISTID) ";", 0, 0, 0) != SQLITE_OK) ok = 0;
    if(ok){
        sqlite3_stmt *st;
        const char *sql = playlist_has_m3u_cols(d) ?
            "INSERT INTO CUSTOM_PLAYLIST (PLAYLIST_ID,PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,DURATION,ALBUM_ARTIST,"
            "IS_M3U,M3U_PATH) "
            "SELECT " XSTR(DISKOS_RSV_LISTID) ",PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,"
            "(CASE WHEN DURATION>0 THEN DURATION ELSE 86400000 END),ALBUM_ARTIST,IFNULL(IS_M3U,0),IFNULL(M3U_PATH,'') "
            "FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?1 ORDER BY ID;" :
            "INSERT INTO CUSTOM_PLAYLIST (PLAYLIST_ID,PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,DURATION,ALBUM_ARTIST) "
            "SELECT " XSTR(DISKOS_RSV_LISTID) ",PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,"
            "(CASE WHEN DURATION>0 THEN DURATION ELSE 86400000 END),ALBUM_ARTIST "
            "FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?1 "
            "ORDER BY ID;";
        if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) == SQLITE_OK){
            sqlite3_bind_int64(st, 1, (sqlite3_int64)pid);
            if(sqlite3_step(st) != SQLITE_DONE) ok = 0; else n = sqlite3_changes(d);
            sqlite3_finalize(st);
        } else ok = 0;
    }
    if(ok && n > 0 && sqlite3_exec(d, "COMMIT;", 0, 0, 0) == SQLITE_OK) return n;
    sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    return 0;
}
int mdb_reserved_slot_player_pos(long pid, int row){
    sqlite3 *d = db(); if(!d || pid <= 0 || row < 1) return 0;
    sqlite3_stmt *st; char *path = NULL; sqlite3_int64 track = 0; int occ = 0;
    if(sqlite3_prepare_v2(d, "SELECT PATH, IFNULL(TRACK,0), (SELECT COUNT(*) FROM CUSTOM_PLAYLIST c2 WHERE c2.PLAYLIST_ID=?1 "
                             "AND c2.PATH=c.PATH AND IFNULL(c2.TRACK,0)=IFNULL(c.TRACK,0) AND c2.ID<=c.ID) "
                             "FROM CUSTOM_PLAYLIST c WHERE PLAYLIST_ID=?1 ORDER BY ID LIMIT 1 OFFSET ?2;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, pid); sqlite3_bind_int(st, 2, row - 1);
    if(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0)){
        path = strdup((const char *)sqlite3_column_text(st, 0));
        track = sqlite3_column_int64(st, 1); occ = sqlite3_column_int(st, 2);
    }
    sqlite3_finalize(st);
    if(!path || occ < 1){ free(path); return 0; }
    int pos = 0, n = 0, rc = SQLITE_ERROR;
    if(sqlite3_prepare_v2(d, "SELECT PATH,TRACK,IS_CUE,IS_ISO FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=" XSTR(DISKOS_RSV_LISTID) ";",
                          -1, &st, NULL) == SQLITE_OK){
        while((rc = sqlite3_step(st)) == SQLITE_ROW){
            n++;
            const unsigned char *p = sqlite3_column_text(st, 0);
            if(p && !strcmp((const char *)p, path) && sqlite3_column_int64(st, 1) == track && --occ == 0){ pos = n; break; }
        }
        sqlite3_finalize(st);
    }
    free(path);
    return (rc == SQLITE_ROW || rc == SQLITE_DONE) ? pos : 0;
}

int mdb_migrate_books(void){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_exec(d, "CREATE TABLE IF NOT EXISTS BOOKS (ID INTEGER PRIMARY KEY autoincrement,"
        "PATH TEXT UNIQUE, NAME TEXT, TITLE TEXT, ARTIST TEXT, ALBUM TEXT, GENRE TEXT,"
        "DURATION BIGINT, ADD_TIME INT8);", 0, 0, 0);
    sqlite3_stmt *c; int have = 0, detect_ok = 0;
    if(sqlite3_prepare_v2(d,
        "SELECT 1 FROM SONG WHERE lower(PATH) LIKE '%.m4b' "
        "UNION ALL SELECT 1 FROM MY_LOVE WHERE lower(PATH) LIKE '%.m4b' "
        "UNION ALL SELECT 1 FROM CUSTOM_PLAYLIST WHERE lower(PATH) LIKE '%.m4b' AND PLAYLIST_ID<>" XSTR(DISKOS_RSV_LISTID)
        " LIMIT 1;", -1, &c, NULL) == SQLITE_OK){
        int rc = sqlite3_step(c);
        if(rc == SQLITE_ROW){ have = 1; detect_ok = 1; }
        else if(rc == SQLITE_DONE){ have = 0; detect_ok = 1; }
        sqlite3_finalize(c);
    }
    if(!detect_ok) return 0;
    if(!have) return 1;
    if(sqlite3_exec(d, "BEGIN IMMEDIATE;", 0, 0, 0) != SQLITE_OK) return 0;
    int ok = sqlite3_exec(d,
        "INSERT OR IGNORE INTO BOOKS(PATH,NAME,TITLE,ARTIST,ALBUM,GENRE,DURATION,ADD_TIME) "
        "SELECT PATH,NAME,TITLE,ARTIST,ALBUM,GENRE,DURATION,ADD_TIME FROM SONG WHERE lower(PATH) LIKE '%.m4b';",0,0,0)==SQLITE_OK
      && sqlite3_exec(d, "DELETE FROM SONG WHERE lower(PATH) LIKE '%.m4b';",0,0,0)==SQLITE_OK
      && sqlite3_exec(d, "CREATE TABLE IF NOT EXISTS DISKOS_BOOK_MOVED(KIND TEXT NOT NULL, LIST_ID INTEGER, PATH TEXT NOT NULL, "
                         "TRACK INTEGER, MOVED_AT INTEGER, UNIQUE(KIND, LIST_ID, PATH, TRACK));",0,0,0)==SQLITE_OK
      && sqlite3_exec(d, "UPDATE OR IGNORE DISKOS_BOOK_MOVED SET LIST_ID=0 WHERE LIST_ID IS NULL;",0,0,0)==SQLITE_OK
      && sqlite3_exec(d, "DELETE FROM DISKOS_BOOK_MOVED WHERE LIST_ID IS NULL;",0,0,0)==SQLITE_OK
      && sqlite3_exec(d, "INSERT OR IGNORE INTO DISKOS_BOOK_MOVED(KIND,LIST_ID,PATH,TRACK,MOVED_AT) "
                         "SELECT 'favourite',0,PATH,IFNULL(TRACK,0),strftime('%s','now') FROM MY_LOVE WHERE lower(PATH) LIKE '%.m4b';",0,0,0)==SQLITE_OK
      && sqlite3_exec(d, "INSERT OR IGNORE INTO DISKOS_BOOK_MOVED(KIND,LIST_ID,PATH,TRACK,MOVED_AT) "
                         "SELECT 'playlist',PLAYLIST_ID,PATH,IFNULL(TRACK,0),strftime('%s','now') FROM CUSTOM_PLAYLIST "
                         "WHERE lower(PATH) LIKE '%.m4b' AND PLAYLIST_ID<>" XSTR(DISKOS_RSV_LISTID) ";",0,0,0)==SQLITE_OK
      && sqlite3_exec(d, "DELETE FROM MY_LOVE WHERE lower(PATH) LIKE '%.m4b';",0,0,0)==SQLITE_OK
      && sqlite3_exec(d, "DELETE FROM CUSTOM_PLAYLIST WHERE lower(PATH) LIKE '%.m4b' AND PLAYLIST_ID<>" XSTR(DISKOS_RSV_LISTID) ";",0,0,0)==SQLITE_OK;
    if(ok && sqlite3_exec(d, "COMMIT;", 0, 0, 0) == SQLITE_OK) return 1;
    sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    return 0;
}

int mdb_unfavorite(int love_id){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "DELETE FROM MY_LOVE WHERE ID=?1;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, love_id);
    int rc = sqlite3_step(st);
    int changed = sqlite3_changes(d);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE && changed > 0;
}

#define PL_COLS "PLAYLIST_ID,PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,DURATION,NAME_CODE,TITLE_CODE,ALBUM_CODE,ARTIST_CODE,GENRE_CODE,ADD_TIME,SAMPLE_RATE,BIT_PER_SAMPLE,CHANNELS,BIT_RATE,SONG_MIMETYPE,SONG_PRODUCTION_YEAR,IS_SELECT,ALBUM_ARTIST,ALBUM_ARTIST_CODE"
#define PL_SRC  "PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,DURATION,NAME_CODE,TITLE_CODE,ALBUM_CODE,ARTIST_CODE,GENRE_CODE,ADD_TIME,SAMPLE_RATE,BIT_PER_SAMPLE,CHANNELS,BIT_RATE,SONG_MIMETYPE,SONG_PRODUCTION_YEAR,IS_SELECT,ALBUM_ARTIST,ALBUM_ARTIST_CODE"

#define PL_NODUP "AND NOT EXISTS (SELECT 1 FROM CUSTOM_PLAYLIST c WHERE c.PLAYLIST_ID=?1 AND c.PATH=S.PATH AND IFNULL(c.TRACK,0)=IFNULL(S.TRACK,0))"
#define M3U_TRACK_TAG "#DISKOS-TRACK:"

static sqlite3_int64 mdb_scalar_i64(sqlite3 *d, const char *sql, int *ok){
    sqlite3_stmt *st; sqlite3_int64 v = 0; *ok = 0;
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    if(sqlite3_step(st) == SQLITE_ROW){ v = sqlite3_column_int64(st, 0); *ok = 1; }
    sqlite3_finalize(st);
    return v;
}

static long long diskos_next_export_uid(sqlite3 *d){
    if(sqlite3_exec(d, "CREATE TABLE IF NOT EXISTS DISKOS_META(K TEXT PRIMARY KEY, V INTEGER NOT NULL);", 0, 0, 0) != SQLITE_OK) return 0;
    if(sqlite3_exec(d, "CREATE TABLE IF NOT EXISTS DISKOS_PL_EXPORT(PID INTEGER PRIMARY KEY, UID INTEGER NOT NULL);", 0, 0, 0) != SQLITE_OK) return 0;
    int ok = 0;
    sqlite3_int64 next = mdb_scalar_i64(d,
        "SELECT COALESCE((SELECT V FROM DISKOS_META WHERE K='pl_export_seq'),1);", &ok);
    if(!ok || next < 1 || next >= 0x7fffffffffffffffLL) return 0;
    char sql[96];
    snprintf(sql, sizeof sql, "INSERT OR REPLACE INTO DISKOS_META(K,V) VALUES('pl_export_seq',%lld);", (long long)next + 1);
    if(sqlite3_exec(d, sql, 0, 0, 0) != SQLITE_OK) return 0;
    return (long long)next;
}
static int diskos_assign_export_uid_val(sqlite3 *d, sqlite3_int64 pid, long long uid){
    if(uid <= 0) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "INSERT OR REPLACE INTO DISKOS_PL_EXPORT(PID,UID) VALUES(?,?);", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, pid);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)uid);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}
static int diskos_assign_export_uid(sqlite3 *d, sqlite3_int64 pid){
    return diskos_assign_export_uid_val(d, pid, diskos_next_export_uid(d));
}

static int stock_index_present(sqlite3 *d){
    int ok; sqlite3_int64 n = mdb_scalar_i64(d,
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='CUSTOM_PLAYLIST_INDEX' COLLATE NOCASE;", &ok);
    return ok && n > 0;
}
static int hidden_playlist_name(const char *name){ return name && name[0] == 0x01; }
static void utf8_clip(char *s){
    size_t n = strlen(s), i = n;
    while(i > 0 && ((unsigned char)s[i-1] & 0xC0) == 0x80) i--;
    if(i == 0) return;
    unsigned char lead = (unsigned char)s[i-1];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if(lead >= 0xC0 && n - (i - 1) < need) s[i-1] = 0;
}
static int stock_index_put(sqlite3 *d, sqlite3_int64 pid, const char *name){
    if(pid <= 0 || hidden_playlist_name(name) || !stock_index_present(d)) return 1;
    sqlite3_stmt *st; int ok = 0, changed = 0;
    if(sqlite3_prepare_v2(d, "UPDATE CUSTOM_PLAYLIST_INDEX SET LIST_NAME=?2 WHERE LIST_ID=?1;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_int64(st, 1, pid);
        sqlite3_bind_text(st, 2, name ? name : "", -1, SQLITE_STATIC);
        ok = sqlite3_step(st) == SQLITE_DONE; changed = sqlite3_changes(d);
        sqlite3_finalize(st);
    }
    if(ok && changed == 0){
        ok = 0;
        if(sqlite3_prepare_v2(d, "INSERT INTO CUSTOM_PLAYLIST_INDEX (LIST_ID,LIST_NAME) VALUES (?1,?2);", -1, &st, NULL) == SQLITE_OK){
            sqlite3_bind_int64(st, 1, pid);
            sqlite3_bind_text(st, 2, name ? name : "", -1, SQLITE_STATIC);
            ok = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st);
        }
    }
    if(ok){
        char q[200];
        snprintf(q, sizeof q, "INSERT OR REPLACE INTO DISKOS_PL_SYNCED(PID, MEMBERS) VALUES(%lld, "
                              "(SELECT COUNT(*) FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=%lld));", (long long)pid, (long long)pid);
        ok = sqlite3_exec(d, "CREATE TABLE IF NOT EXISTS DISKOS_PL_SYNCED(PID INTEGER PRIMARY KEY, MEMBERS INTEGER NOT NULL DEFAULT 0);",
                          0, 0, 0) == SQLITE_OK;
        if(ok) sqlite3_exec(d, "ALTER TABLE DISKOS_PL_SYNCED ADD COLUMN MEMBERS INTEGER NOT NULL DEFAULT 0;", 0, 0, 0);
        ok = ok && sqlite3_exec(d, q, 0, 0, 0) == SQLITE_OK;
    }
    return ok;
}
static int stock_index_drop(sqlite3 *d, sqlite3_int64 pid){
    if(pid <= 0 || !stock_index_present(d)) return 1;
    sqlite3_stmt *st; int ok = 0;
    if(sqlite3_prepare_v2(d, "DELETE FROM CUSTOM_PLAYLIST_INDEX WHERE LIST_ID=?1;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_int64(st, 1, pid); ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st);
    }
    return ok;
}
int mdb_playlist_sync_registry(void){
    sqlite3 *d = db(); if(!d) return 0;
    if(!stock_index_present(d)) return 1;
    sqlite3_exec(d, "CREATE TABLE IF NOT EXISTS PLAYLIST_INFO (ID INTEGER PRIMARY KEY AUTOINCREMENT, NAME TEXT, ADD_TIME INT8);", 0, 0, 0);
    int owns_txn = sqlite3_get_autocommit(d);
    if(sqlite3_exec(d, "SAVEPOINT plsync;", 0, 0, 0) != SQLITE_OK) return 0;
    int ok = 1;
    if(sqlite3_exec(d, "CREATE TABLE IF NOT EXISTS DISKOS_PL_SYNCED(PID INTEGER PRIMARY KEY, MEMBERS INTEGER NOT NULL DEFAULT 0);",
                    0, 0, 0) != SQLITE_OK) ok = 0;
    sqlite3_exec(d, "ALTER TABLE DISKOS_PL_SYNCED ADD COLUMN MEMBERS INTEGER NOT NULL DEFAULT 0;", 0, 0, 0);
    const char *gone = "FROM DISKOS_PL_SYNCED y WHERE y.MEMBERS>0 "
        "AND NOT EXISTS (SELECT 1 FROM CUSTOM_PLAYLIST_INDEX i WHERE i.LIST_ID=y.PID) "
        "AND NOT EXISTS (SELECT 1 FROM CUSTOM_PLAYLIST m WHERE m.PLAYLIST_ID=y.PID)";
    char q[600];
    snprintf(q, sizeof q, "DELETE FROM PLAYLIST_INFO WHERE ID IN (SELECT PID %s);", gone);
    if(ok && sqlite3_exec(d, q, 0, 0, 0) != SQLITE_OK) ok = 0;
    snprintf(q, sizeof q, "DELETE FROM DISKOS_PL_SYNCED WHERE PID IN (SELECT PID %s);", gone);
    if(ok && sqlite3_exec(d, q, 0, 0, 0) != SQLITE_OK) ok = 0;
    if(sqlite3_exec(d,
        "INSERT INTO CUSTOM_PLAYLIST_INDEX (LIST_ID,LIST_NAME) "
        "SELECT m.PLAYLIST_ID, IFNULL((SELECT NAME FROM PLAYLIST_INFO p WHERE p.ID=m.PLAYLIST_ID AND substr(p.NAME,1,1)<>char(1)), "
        "'custom list '||m.PLAYLIST_ID) FROM (SELECT DISTINCT PLAYLIST_ID FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID>0) m "
        "WHERE NOT EXISTS (SELECT 1 FROM CUSTOM_PLAYLIST_INDEX i WHERE i.LIST_ID=m.PLAYLIST_ID) "
        "AND NOT EXISTS (SELECT 1 FROM PLAYLIST_INFO p WHERE p.ID=m.PLAYLIST_ID AND substr(p.NAME,1,1)=char(1));", 0, 0, 0) != SQLITE_OK) ok = 0;
    sqlite3_stmt *sel = NULL;
    if(sqlite3_prepare_v2(d, "SELECT LIST_ID, IFNULL(LIST_NAME,'') FROM CUSTOM_PLAYLIST_INDEX i WHERE LIST_ID>0 "
                             "AND NOT EXISTS (SELECT 1 FROM PLAYLIST_INFO p WHERE p.ID=i.LIST_ID) ORDER BY LIST_ID;",
                          -1, &sel, NULL) != SQLITE_OK) ok = 0;
    int rc = ok ? SQLITE_DONE : SQLITE_ERROR;
    while(ok && (rc = sqlite3_step(sel)) == SQLITE_ROW){
        sqlite3_int64 id = sqlite3_column_int64(sel, 0);
        char nm[MDB_STR]; snprintf(nm, sizeof nm, "%s", colt(sel, 1)); utf8_clip(nm); trim(nm);
        if(!nm[0] || hidden_playlist_name(nm)) snprintf(nm, sizeof nm, "custom list %lld", (long long)id);
        sqlite3_stmt *ins;
        if(sqlite3_prepare_v2(d, "INSERT INTO PLAYLIST_INFO (ID,NAME,ADD_TIME) VALUES (?1,?2,strftime('%s','now'));",
                              -1, &ins, NULL) != SQLITE_OK){ ok = 0; break; }
        sqlite3_bind_int64(ins, 1, id); sqlite3_bind_text(ins, 2, nm, -1, SQLITE_TRANSIENT);
        if(sqlite3_step(ins) != SQLITE_DONE) ok = 0;
        sqlite3_finalize(ins);
        if(ok && !diskos_assign_export_uid(d, id)) ok = 0;
    }
    if(ok && rc != SQLITE_DONE) ok = 0;
    if(sel) sqlite3_finalize(sel);
    if(ok && sqlite3_exec(d,
        "INSERT INTO CUSTOM_PLAYLIST_INDEX (LIST_ID,LIST_NAME) "
        "SELECT ID, NAME FROM PLAYLIST_INFO p WHERE ID>0 AND substr(NAME,1,1)<>char(1) "
        "AND NOT EXISTS (SELECT 1 FROM CUSTOM_PLAYLIST_INDEX i WHERE i.LIST_ID=p.ID);", 0, 0, 0) != SQLITE_OK) ok = 0;
    if(ok && sqlite3_exec(d,
        "UPDATE CUSTOM_PLAYLIST_INDEX SET LIST_NAME=(SELECT NAME FROM PLAYLIST_INFO p WHERE p.ID=LIST_ID) "
        "WHERE LIST_ID>0 AND LIST_NAME='custom list '||LIST_ID AND EXISTS (SELECT 1 FROM PLAYLIST_INFO p "
        "WHERE p.ID=LIST_ID AND p.NAME<>LIST_NAME AND substr(p.NAME,1,1)<>char(1));", 0, 0, 0) != SQLITE_OK) ok = 0;
    if(ok && sqlite3_exec(d, "INSERT OR REPLACE INTO DISKOS_PL_SYNCED(PID, MEMBERS) "
                             "SELECT c.ID,c.MEMBERS FROM (SELECT p.ID,COUNT(m.PLAYLIST_ID) AS MEMBERS "
                             "FROM PLAYLIST_INFO p LEFT JOIN CUSTOM_PLAYLIST m ON m.PLAYLIST_ID=p.ID "
                             "WHERE p.ID>0 AND EXISTS (SELECT 1 FROM CUSTOM_PLAYLIST_INDEX i WHERE i.LIST_ID=p.ID) "
                             "GROUP BY p.ID) c WHERE NOT EXISTS (SELECT 1 FROM DISKOS_PL_SYNCED y "
                             "WHERE y.PID=c.ID AND y.MEMBERS=c.MEMBERS);", 0, 0, 0) != SQLITE_OK) ok = 0;
    if(ok && sqlite3_exec(d, "RELEASE plsync;", 0, 0, 0) == SQLITE_OK) return 2;
    sqlite3_exec(d, "ROLLBACK TO plsync;", 0, 0, 0);
    sqlite3_exec(d, "RELEASE plsync;", 0, 0, 0);
    if(owns_txn && !sqlite3_get_autocommit(d)) sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    return 0;
}

long mdb_playlist_create(const char *name){
    sqlite3 *d = db(); if(!d) return 0;
    char nm[MDB_STR]; snprintf(nm, sizeof nm, "%s", name ? name : ""); trim(nm); name = nm;
    sqlite3_exec(d, "CREATE TABLE IF NOT EXISTS PLAYLIST_INFO "
                    "(ID INTEGER PRIMARY KEY AUTOINCREMENT, NAME TEXT, ADD_TIME INT8);", 0, 0, 0);
    int owns_txn = sqlite3_get_autocommit(d);
    if(sqlite3_exec(d, "SAVEPOINT plcreate;", 0, 0, 0) != SQLITE_OK) return 0;
    sqlite3_stmt *st;
    sqlite3_int64 id = 0;
    int ok = 1;

    if(sqlite3_prepare_v2(d, "INSERT INTO PLAYLIST_INFO (NAME,ADD_TIME) VALUES (?, strftime('%s','now'));",
                          -1, &st, NULL) == SQLITE_OK){
        if(sqlite3_bind_text(st, 1, name?name:"", -1, SQLITE_STATIC) != SQLITE_OK) ok = 0;
        else if(sqlite3_step(st) != SQLITE_DONE) ok = 0;
        sqlite3_finalize(st);
    } else ok = 0;

    if(ok){
        id = sqlite3_last_insert_rowid(d);
        int q;
        sqlite3_int64 hi = mdb_scalar_i64(d, "SELECT COALESCE(MAX(PLAYLIST_ID),0) FROM CUSTOM_PLAYLIST;", &q);
        if(!q) ok = 0;
        if(ok){
            char qs[96]; snprintf(qs, sizeof qs, "SELECT COALESCE(MAX(ID),0) FROM PLAYLIST_INFO WHERE ID<>%lld;", (long long)id);
            sqlite3_int64 v = mdb_scalar_i64(d, qs, &q);
            if(!q) ok = 0; else if(v > hi) hi = v;
        }
        if(ok){
            int has;
            sqlite3_int64 t = mdb_scalar_i64(d,
                "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='CUSTOM_PLAYLIST_INDEX' COLLATE NOCASE;", &has);
            if(!has) ok = 0;
            else if(t > 0){
                int q2; sqlite3_int64 v = mdb_scalar_i64(d, "SELECT COALESCE(MAX(LIST_ID),0) FROM CUSTOM_PLAYLIST_INDEX;", &q2);
                if(!q2) ok = 0; else if(v > hi) hi = v;
            }
        }
        if(ok && id <= hi){
            if(hi >= 0x7fffffffffffffffLL) ok = 0;
            else {
                sqlite3_int64 safe = hi + 1;
                if(sqlite3_prepare_v2(d, "UPDATE PLAYLIST_INFO SET ID=? WHERE ID=?;", -1, &st, NULL) == SQLITE_OK){
                    sqlite3_bind_int64(st, 1, safe); sqlite3_bind_int64(st, 2, id);
                    if(sqlite3_step(st) != SQLITE_DONE || sqlite3_changes(d) != 1) ok = 0;
                    sqlite3_finalize(st);
                } else ok = 0;
                if(ok) id = safe;
            }
        }
        if(ok && id > 0x7fffffffLL) ok = 0;
        if(ok && !diskos_assign_export_uid(d, id)) ok = 0;
        if(ok && !stock_index_put(d, id, name)) ok = 0;
    }

    if(ok && sqlite3_exec(d, "RELEASE plcreate;", 0, 0, 0) == SQLITE_OK) return (long)id;
    sqlite3_exec(d, "ROLLBACK TO plcreate;", 0, 0, 0);
    sqlite3_exec(d, "RELEASE plcreate;", 0, 0, 0);
    if(owns_txn && !sqlite3_get_autocommit(d)) sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    return 0;
}
int mdb_playlist_add_song_ex(long pid, const char *path, int *hard_err){
    if(hard_err) *hard_err = 0;
    if(pid<=0 || !path || !path[0]) return 0;
    sqlite3 *d = db(); if(!d){ if(hard_err) *hard_err = 1; return 0; }
    sqlite3_stmt *st;
    const char *sql = "INSERT OR IGNORE INTO CUSTOM_PLAYLIST (" PL_COLS ") "
                      "SELECT ?1," PL_SRC " FROM SONG S WHERE S.PATH=?2 " PL_NODUP " GROUP BY S.PATH,IFNULL(S.TRACK,0) ORDER BY MIN(S.ID);";
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK){ if(hard_err) *hard_err = 1; return 0; }
    sqlite3_bind_int64(st, 1, pid);
    sqlite3_bind_text(st, 2, path, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if(rc != SQLITE_DONE){ if(hard_err) *hard_err = 1; return 0; }
    return (sqlite3_changes(d) > 0) ? 1 : 0;
}
int mdb_playlist_add_song(long pid, const char *path){
    return mdb_playlist_add_song_ex(pid, path, NULL);
}
static int song_subtrack_core(const char *path, int pos_id, const char *title, int need_title, int *track, int *is_cue, int *is_iso){
    if(!path || !path[0] || pos_id <= 0) return 0;
    if(need_title && (!title || !title[0])) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st; int r = 0;
    if(sqlite3_prepare_v2(d, "SELECT IFNULL(TRACK,0),IFNULL(IS_CUE,0),IFNULL(IS_ISO,0) FROM LIST_SONG_0 WHERE ID=?1 AND PATH=?2 "
                          "AND (?4=0 OR TITLE=?3 OR NAME=?3);",
                          -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, pos_id); sqlite3_bind_text(st, 2, path, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, title ? title : "", -1, SQLITE_STATIC); sqlite3_bind_int(st, 4, need_title ? 1 : 0);
    if(sqlite3_step(st) == SQLITE_ROW && (sqlite3_column_int(st, 1) || sqlite3_column_int(st, 2))){
        if(track) *track = sqlite3_column_int(st, 0);
        if(is_cue) *is_cue = sqlite3_column_int(st, 1) != 0;
        if(is_iso) *is_iso = sqlite3_column_int(st, 2) != 0;
        r = 1;
    }
    sqlite3_finalize(st);
    return r;
}
int mdb_song_subtrack(const char *path, int pos_id, int *track, int *is_cue, int *is_iso){
    return song_subtrack_core(path, pos_id, NULL, 0, track, is_cue, is_iso);
}
int mdb_song_subtrack_verified(const char *path, int pos_id, const char *title, int *track, int *is_cue, int *is_iso){
    return song_subtrack_core(path, pos_id, title, 1, track, is_cue, is_iso);
}
int mdb_song_multi(const char *path){
    if(!path || !path[0]) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st; int r = 0;
    if(sqlite3_prepare_v2(d, "SELECT COUNT(DISTINCT IFNULL(TRACK,0)||'/'||IFNULL(IS_CUE,0)||'/'||IFNULL(IS_ISO,0)) FROM SONG WHERE PATH=?1;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
    if(sqlite3_step(st) == SQLITE_ROW) r = sqlite3_column_int(st, 0) > 1;
    sqlite3_finalize(st);
    return r;
}
int mdb_playlist_add_subtrack(long pid, const char *path, int track, int *hard_err){
    if(hard_err) *hard_err = 0;
    if(pid<=0 || !path || !path[0]) return 0;
    sqlite3 *d = db(); if(!d){ if(hard_err) *hard_err = 1; return 0; }
    sqlite3_stmt *st;
    const char *sql = "INSERT OR IGNORE INTO CUSTOM_PLAYLIST (" PL_COLS ") "
                      "SELECT ?1," PL_SRC " FROM SONG S WHERE S.PATH=?2 AND IFNULL(S.TRACK,0)=?3 "
                      "AND (IFNULL(S.IS_CUE,0)<>0 OR IFNULL(S.IS_ISO,0)<>0) " PL_NODUP " ORDER BY S.ID LIMIT 1;";
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK){ if(hard_err) *hard_err = 1; return 0; }
    sqlite3_bind_int64(st, 1, pid); sqlite3_bind_text(st, 2, path, -1, SQLITE_STATIC); sqlite3_bind_int(st, 3, track);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if(rc != SQLITE_DONE){ if(hard_err) *hard_err = 1; return 0; }
    return (sqlite3_changes(d) > 0) ? 1 : 0;
}
int mdb_playlist_has_subtrack(long pid, const char *path, int track){
    if(pid<=0 || !path || !path[0]) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT 1 FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=? AND PATH=? AND IFNULL(TRACK,0)=? LIMIT 1;",
                          -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, pid); sqlite3_bind_text(st, 2, path, -1, SQLITE_STATIC); sqlite3_bind_int(st, 3, track);
    int yes = (sqlite3_step(st) == SQLITE_ROW);
    sqlite3_finalize(st);
    return yes;
}
#define BOOK_SCOPE_NAME "\x01""diskos-book"
static long book_scope_pid(void){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st; long pid = 0;
    if(sqlite3_prepare_v2(d, "SELECT ID FROM PLAYLIST_INFO WHERE NAME=? LIMIT 1;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_text(st, 1, BOOK_SCOPE_NAME, -1, SQLITE_STATIC);
        if(sqlite3_step(st) == SQLITE_ROW) pid = (long)sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    if(pid <= 0) pid = mdb_playlist_create(BOOK_SCOPE_NAME);
    return pid;
}
long mdb_book_scope_set(const char *path){
    if(!path || !path[0]) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    long pid = book_scope_pid();
    if(pid <= 0) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "DELETE FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_int64(st, 1, pid); sqlite3_step(st); sqlite3_finalize(st);
    }
    if(!mdb_playlist_add_song(pid, path)) return 0;
    return pid;
}

int mdb_playlist_add_group(long pid, const char *col, const char *val){
    if(pid<=0 || !col || !val || !val[0]) return 0;
    if(strcmp(col,"ALBUM") && strcmp(col,"ARTIST") && strcmp(col,"GENRE")) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    char sql[1600];
    snprintf(sql, sizeof sql,
             "INSERT OR IGNORE INTO CUSTOM_PLAYLIST (" PL_COLS ") SELECT ?1," PL_SRC " FROM SONG S WHERE S.%s=?2 " PL_NODUP
             " GROUP BY S.PATH,IFNULL(S.TRACK,0) ORDER BY MIN(S.ID);", col);
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, pid);
    sqlite3_bind_text(st, 2, val, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    return (rc == SQLITE_DONE) ? sqlite3_changes(d) : 0;
}
int mdb_playlist_add_ids(long pid, const int *ids, int n){
    if(pid <= 0 || !ids || n <= 0) return 0;
    sqlite3 *d = db(); if(!d) return -1;
    sqlite3_stmt *st;
    int owns = sqlite3_get_autocommit(d);
    if(sqlite3_exec(d, "SAVEPOINT pladdids;", 0, 0, 0) != SQLITE_OK) return -1;
    if(sqlite3_prepare_v2(d, "INSERT OR IGNORE INTO CUSTOM_PLAYLIST (" PL_COLS ") SELECT ?1," PL_SRC " FROM SONG S WHERE S.ID=?2 " PL_NODUP ";",
                          -1, &st, NULL) != SQLITE_OK){ sqlite3_exec(d, "ROLLBACK TO pladdids;", 0, 0, 0); sqlite3_exec(d, "RELEASE pladdids;", 0, 0, 0); return -1; }
    int added = 0, ok = 1;
    for(int i = 0; i < n && ok; i++){
        sqlite3_reset(st); sqlite3_bind_int64(st, 1, pid); sqlite3_bind_int(st, 2, ids[i]);
        if(sqlite3_step(st) != SQLITE_DONE) ok = 0; else if(sqlite3_changes(d) > 0) added++;
    }
    sqlite3_finalize(st);
    if(ok && sqlite3_exec(d, "RELEASE pladdids;", 0, 0, 0) == SQLITE_OK) return added;
    sqlite3_exec(d, "ROLLBACK TO pladdids;", 0, 0, 0);
    sqlite3_exec(d, "RELEASE pladdids;", 0, 0, 0);
    if(owns && !sqlite3_get_autocommit(d)) sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    return -1;
}
int mdb_playlist_has_song(long pid, const char *path){
    if(pid<=0 || !path || !path[0]) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT 1 FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=? AND PATH=? LIMIT 1;",
                          -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, pid);
    sqlite3_bind_text(st, 2, path, -1, SQLITE_STATIC);
    int yes = (sqlite3_step(st) == SQLITE_ROW);
    sqlite3_finalize(st);
    return yes;
}
int mdb_playlist_remove_at(long pid, int position){
    if(pid<=0 || position<=0) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    const char *sql =
        "DELETE FROM CUSTOM_PLAYLIST WHERE ID = ("
        "SELECT ID FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?1 ORDER BY ID LIMIT 1 OFFSET ?2);";
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, pid);
    sqlite3_bind_int(st, 2, position-1);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    return (rc == SQLITE_DONE && sqlite3_changes(d) > 0) ? 1 : 0;
}
int sd_write_begin(void); void sd_write_end(void);
static void pl_sanitize(const char *name, char *out, int cap){
    int j=0;
    for(const char *s = (name && name[0]) ? name : "playlist"; *s && j < cap - 1; s++){
        char c = *s;
        out[j++] = ((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c==' '||c=='-'||c=='_') ? c : '_';
    }
    out[j] = 0;
    if(!out[0]){ out[0]='p'; out[1]=0; }
}
static int pl_export_is_ours(const char *path, const char *name){
    FILE *f = fopen(path, "r"); if(!f) return 0;
    char line[256]; int ok = 0, i = 0;
    while(i < 8 && fgets(line, sizeof line, f)){
        if(i == 0 && strncmp(line, "#EXTM3U", 7) != 0) break;
        if(strncmp(line, "#PLAYLIST:", 10) == 0){
            char *v = line + 10; size_t L = strlen(v);
            while(L && (v[L-1]=='\n' || v[L-1]=='\r')) v[--L] = 0;
            if(name && strcmp(v, name) == 0) ok = 1;
            break;
        }
        i++;
    }
    fclose(f);
    return ok;
}

static int mdb_playlist_export_leased(long pid, const char *name, char *outname, int cap){
    if(pid<=0) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    char safe[96]; pl_sanitize(name, safe, sizeof safe);
    long long uid = 0; int have_uid = 0;
    { sqlite3_stmt *st;
      if(sqlite3_prepare_v2(d, "SELECT UID FROM DISKOS_PL_EXPORT WHERE PID=?;", -1, &st, NULL) == SQLITE_OK){
          sqlite3_bind_int64(st, 1, pid);
          if(sqlite3_step(st) == SQLITE_ROW){ uid = sqlite3_column_int64(st, 0); have_uid = 1; }
          sqlite3_finalize(st);
      } }
    if(!have_uid && sqlite3_exec(d, "BEGIN IMMEDIATE;", 0, 0, 0) == SQLITE_OK){
        long long u = diskos_next_export_uid(d);
        int okk = (u > 0) && diskos_assign_export_uid_val(d, pid, u);
        if(okk && sqlite3_exec(d, "COMMIT;", 0, 0, 0) == SQLITE_OK){ uid = u; have_uid = 1; }
        else sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    }
    if(!have_uid) return 0;
    char base[160];
    snprintf(base, sizeof base, "%s-%lld", safe, uid);
    char path[200]; snprintf(path, sizeof path, "/tmp/sdcard/%s.m3u", base);
    if(access(path, F_OK) == 0 && !pl_export_is_ours(path, name)){
        int found = 0;
        for(int nsfx = 2; nsfx <= 999; nsfx++){
            char cand[200]; snprintf(cand, sizeof cand, "/tmp/sdcard/%s_%d.m3u", base, nsfx);
            if(access(cand, F_OK) != 0 || pl_export_is_ours(cand, name)){
                char nb[176]; snprintf(nb, sizeof nb, "%s_%d", base, nsfx);
                snprintf(base, sizeof base, "%s", nb);
                snprintf(path, sizeof path, "%s", cand);
                found = 1; break;
            }
        }
        if(!found) return 0;
    }
    char tmp[64];  snprintf(tmp, sizeof tmp, "/tmp/sdcard/.diskos-export-XXXXXX");
    if(!sd_write_begin()) return 0;
    int tfd = mkstemp(tmp);
    if(tfd < 0){ sd_write_end(); return 0; }
    FILE *f = fdopen(tfd, "w");
    if(!f){ close(tfd); unlink(tmp); sd_write_end(); return 0; }
    int ok = 1;
    if(fprintf(f, "#EXTM3U\n") < 0) ok = 0;
    if(name && name[0]){
        char nm1[160]; int k=0;
        for(const char *s=name; *s && k<(int)sizeof nm1-1; s++){ char c=*s; if(c!='\r'&&c!='\n') nm1[k++]=c; }
        nm1[k]=0;
        if(nm1[0] && fprintf(f, "#PLAYLIST:%s\n", nm1) < 0) ok = 0;
    }
    sqlite3_stmt *st = NULL;
    if(sqlite3_prepare_v2(d, "SELECT PATH,IFNULL(TRACK,0),(IFNULL(IS_CUE,0)<>0 OR IFNULL(IS_ISO,0)<>0) FROM CUSTOM_PLAYLIST "
                             "WHERE PLAYLIST_ID=? ORDER BY ID;", -1, &st, NULL) == SQLITE_OK){
        if(sqlite3_bind_int64(st, 1, pid) == SQLITE_OK){
            int rc;
            while((rc = sqlite3_step(st)) == SQLITE_ROW){
                const char *p = colt(st,0);
                if(p && p[0] && sqlite3_column_int(st,2) && fprintf(f, "%s%d\n", M3U_TRACK_TAG, sqlite3_column_int(st,1)) < 0) ok = 0;
                if(p && p[0] && fprintf(f, "%s\n", p) < 0) ok = 0;
            }
            if(rc != SQLITE_DONE) ok = 0;
        } else { ok = 0; }
        sqlite3_finalize(st);
    } else { ok = 0; }
    if(ferror(f)) ok = 0;
    if(fflush(f) != 0) ok = 0;
    if(ok){
        int fd = fileno(f);
        if(fd >= 0 && fsync(fd) != 0 && errno != EINVAL && errno != ENOSYS && errno != EOPNOTSUPP) ok = 0;
    }
    if(fclose(f) != 0) ok = 0;
    if(ok && rename(tmp, path) != 0) ok = 0;
    if(ok){
        int dfd = open("/tmp/sdcard", O_RDONLY | O_DIRECTORY);
        if(dfd >= 0){ fsync(dfd); close(dfd); }
    }
    if(!ok) unlink(tmp);
    sd_write_end();
    if(ok && outname && cap > 0) snprintf(outname, cap, "%s.m3u", base);
    return ok;
}
int mdb_set_peq(int style_preset, double master_gain, const char *params_json){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "UPDATE PEQ SET MASTER_GAIN=?, PARAMS_JSON=? WHERE STYLE_PRESET=?;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_double(st, 1, master_gain);
    sqlite3_bind_text(st, 2, params_json?params_json:"", -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, style_preset);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if(rc != SQLITE_DONE) return 0;
    if(sqlite3_changes(d) == 0){
        char nm[16] = "Custom";
        if(style_preset >= 11 && style_preset <= 20) snprintf(nm, sizeof nm, "USER%d", style_preset - 10);
        if(sqlite3_prepare_v2(d, "INSERT INTO PEQ (STYLE_NAME, MASTER_GAIN, PARAMS_JSON, STYLE_PRESET) VALUES (?,?,?,?);", -1, &st, NULL) != SQLITE_OK) return 0;
        sqlite3_bind_text(st, 1, nm, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(st, 2, master_gain);
        sqlite3_bind_text(st, 3, params_json?params_json:"", -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 4, style_preset);
        rc = sqlite3_step(st); sqlite3_finalize(st);
        if(rc != SQLITE_DONE) return 0;
    }
    return 1;
}
int mdb_get_peq(int style_preset, int *master_out, int *gains_out){
    if(gains_out) for(int i=0;i<10;i++) gains_out[i]=0;
    if(master_out) *master_out=0;
    sqlite3 *d = db(); if(!d) return -1;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT MASTER_GAIN, PARAMS_JSON FROM PEQ WHERE STYLE_PRESET=?;", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, style_preset);
    int found = 0, rc = sqlite3_step(st);
    if(rc == SQLITE_ROW){
        found = 1;
        if(master_out) *master_out = sqlite3_column_int(st, 0);
        const char *js = (const char*)sqlite3_column_text(st, 1);
        if(gains_out && js){
            int n=0; const char *p=js;
            while(n<10 && (p=strstr(p, "\"gain\"")) != NULL){
                p += 6;
                while(*p==':'||*p==' '||*p=='"') p++;
                int neg=0; if(*p=='-'){ neg=1; p++; }
                int g=0; while(*p>='0'&&*p<='9' && g<100000){ g=g*10+(*p-'0'); p++; }
                gains_out[n++] = neg ? -g : g;
            }
        }
    }
    sqlite3_finalize(st);
    if(!found && rc != SQLITE_DONE) return -1;
    return found;
}

static int peq_tok_int(const char *js, const jsmntok_t *t, long *out){
    const char *p = js + t->start, *end = js + t->end;
    int neg = 0; if(p < end && *p == '-'){ neg = 1; p++; }
    if(p >= end || !(*p>='0'&&*p<='9')) return 0;
    if(*p == '0' && p+1 < end && p[1]>='0'&&p[1]<='9') return 0;
    long v = 0; while(p < end && *p>='0'&&*p<='9'){ if(v > 10000000) return 0; v = v*10 + (*p-'0'); p++; }
    if(p < end && *p == '.'){ p++; if(p >= end || !(*p>='0'&&*p<='9')) return 0;
                              while(p < end && *p>='0'&&*p<='9'){ if(*p != '0') return 0; p++; } }
    if(p != end) return 0;
    *out = neg ? -v : v; return 1;
}
static int peq_key_is(const char *js, const jsmntok_t *t, const char *s){
    int n = t->end - t->start;
    return n == (int)strlen(s) && strncmp(js + t->start, s, (size_t)n) == 0;
}

int mdb_peq_is_graphic(int style_preset){
    static const int GFREQ[10] = {32,64,125,250,500,1000,2000,4000,8000,16000};
    sqlite3 *d = db(); if(!d) return -1;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT MASTER_GAIN, PARAMS_JSON FROM PEQ WHERE STYLE_PRESET=?;", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, style_preset);
    int rc = sqlite3_step(st), graphic = 1;
    if(rc == SQLITE_ROW){
        const char *ms = (const char*)sqlite3_column_text(st, 0);
        if(ms && ms[0]){
            const char *p = ms; int neg = 0, ok = 1; long mv = 0;
            if(*p == '-'){ neg = 1; p++; }
            if(!(*p>='0'&&*p<='9')) ok = 0;
            while(*p>='0'&&*p<='9'){ mv = (mv < 100000) ? mv*10 + (*p-'0') : 100000; p++; }
            if(*p == '.'){ p++; while(*p>='0'&&*p<='9'){ if(*p != '0') ok = 0; p++; } }
            if(*p) ok = 0;
            if(neg) mv = -mv;
            if(!ok || mv < -12 || mv > 12) graphic = 0;
        }
        const char *js = (const char*)sqlite3_column_text(st, 1);
        int jlen = js ? sqlite3_column_bytes(st, 1) : 0;
        if(graphic && js){
            jsmn_parser jp; jsmntok_t tok[256]; jsmn_init(&jp);
            int nt = jsmn_parse(&jp, js, jlen, tok, (unsigned)(sizeof tok / sizeof tok[0]));
            if(nt < 1 || tok[0].type != JSMN_ARRAY || tok[0].size != 10) graphic = 0;
            else {
                int re = tok[0].end;
                while(re < jlen && (js[re]==' '||js[re]=='\t'||js[re]=='\n'||js[re]=='\r')) re++;
                if(re != jlen) graphic = 0;
                int ti = 1;
                for(int band = 0; graphic && band < 10; band++){
                    if(ti >= nt || tok[ti].type != JSMN_OBJECT){ graphic = 0; break; }
                    int nfields = tok[ti].size; ti++;
                    int have_ft = 0, have_fr = 0, have_q = 0, have_g = 0;
                    for(int f = 0; graphic && f < nfields; f++){
                        if(ti + 1 >= nt || tok[ti].type != JSMN_STRING){ graphic = 0; break; }
                        const jsmntok_t *k = &tok[ti], *val = &tok[ti + 1];
                        if(val->type == JSMN_OBJECT || val->type == JSMN_ARRAY){ graphic = 0; break; }
                        long v;
                        if(peq_key_is(js, k, "filterType")){ if(have_ft || val->type != JSMN_PRIMITIVE || !peq_tok_int(js, val, &v) || v != 0) graphic = 0; else have_ft = 1; }
                        else if(peq_key_is(js, k, "frequency")){ if(have_fr || val->type != JSMN_PRIMITIVE || !peq_tok_int(js, val, &v) || v != GFREQ[band]) graphic = 0; else have_fr = 1; }
                        else if(peq_key_is(js, k, "qValue")){ if(have_q || val->type != JSMN_STRING || !(val->end - val->start == 3 && strncmp(js + val->start, "0.7", 3) == 0)) graphic = 0; else have_q = 1; }
                        else if(peq_key_is(js, k, "gain")){ if(have_g || val->type != JSMN_STRING || !peq_tok_int(js, val, &v) || v < -12 || v > 12) graphic = 0; else have_g = 1; }
                        ti += 2;
                    }
                    if(graphic && !(have_ft && have_fr && have_q && have_g)) graphic = 0;
                }
            }
        }
    }
    sqlite3_finalize(st);
    if(rc != SQLITE_ROW && rc != SQLITE_DONE) return -1;
    return graphic;
}

int mdb_playlist_rename(long pid, const char *name){
    if(pid<=0) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    int owns_txn = sqlite3_get_autocommit(d);
    if(sqlite3_exec(d, "SAVEPOINT plrename;", 0, 0, 0) != SQLITE_OK) return 0;
    sqlite3_stmt *st; int ok = 0;
    if(sqlite3_prepare_v2(d, "UPDATE PLAYLIST_INFO SET NAME=? WHERE ID=?;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_text(st, 1, name?name:"", -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 2, pid);
        ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(d) > 0;
        sqlite3_finalize(st);
    }
    if(ok && !stock_index_put(d, pid, name)) ok = 0;
    if(ok && sqlite3_exec(d, "RELEASE plrename;", 0, 0, 0) == SQLITE_OK) return 1;
    sqlite3_exec(d, "ROLLBACK TO plrename;", 0, 0, 0);
    sqlite3_exec(d, "RELEASE plrename;", 0, 0, 0);
    if(owns_txn && !sqlite3_get_autocommit(d)) sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    return 0;
}
int mdb_playlist_delete(long pid){
    if(pid<=0) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    if(sqlite3_exec(d, "BEGIN;", 0, 0, 0) != SQLITE_OK) return 0;
    sqlite3_stmt *st;
    int memb_ok = 0, info_ok = 0, changed = 0;
    char qs[80]; snprintf(qs, sizeof qs, "SELECT COUNT(*) FROM PLAYLIST_INFO WHERE ID=%ld;", pid);
    int q; if(mdb_scalar_i64(d, qs, &q) != 1 || !q){ sqlite3_exec(d, "ROLLBACK;", 0, 0, 0); return 0; }
    if(sqlite3_prepare_v2(d, "DELETE FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_int64(st, 1, pid); memb_ok = (sqlite3_step(st) == SQLITE_DONE); sqlite3_finalize(st);
    }
    if(memb_ok && sqlite3_prepare_v2(d, "DELETE FROM PLAYLIST_INFO WHERE ID=?;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_int64(st, 1, pid);
        if(sqlite3_step(st) == SQLITE_DONE){ info_ok = 1; changed = (sqlite3_changes(d) > 0); }
        sqlite3_finalize(st);
    }
    if(memb_ok && info_ok && !stock_index_drop(d, pid)) info_ok = 0;
    if(memb_ok && info_ok){
        char fq[80]; snprintf(fq, sizeof fq, "DELETE FROM DISKOS_PL_SYNCED WHERE PID=%ld;", pid);
        sqlite3_exec(d, fq, 0, 0, 0);
    }
    if(memb_ok && info_ok && sqlite3_exec(d, "COMMIT;", 0, 0, 0) == SQLITE_OK) return changed;
    sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    return 0;
}
int mdb_playlist_songs(long pid, mdb_song_t *out, int cap){
    if(pid<=0) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    const char *sql =
        "SELECT IFNULL(TITLE,IFNULL(NAME,'Untitled')),IFNULL(ARTIST,''),IFNULL(DURATION,0) "
        "FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=? ORDER BY ID;";
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, pid);
    int n=0;
    while(n<cap && sqlite3_step(st) == SQLITE_ROW){
        snprintf(out[n].title,  MDB_STR, "%s", colt(st,0));
        snprintf(out[n].artist, MDB_STR, "%s", colt(st,1));
        out[n].album[0]=0; out[n].album_h=0; out[n].genre[0]=0;
        out[n].dur_ms = sqlite3_column_int(st,2); out[n].id = 0;
        trim(out[n].title); trim(out[n].artist);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

int mdb_playlist_count(long pid){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT COUNT(*) FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?;", -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, pid);
    int n=0; if(sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st,0);
    sqlite3_finalize(st);
    return n;
}

static long playlist_id_by_name(const char *name){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st; long id = 0;
    if(sqlite3_prepare_v2(d, "SELECT ID FROM PLAYLIST_INFO WHERE NAME=? LIMIT 1;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        if(sqlite3_step(st) == SQLITE_ROW) id = (long)sqlite3_column_int64(st,0);
        sqlite3_finalize(st);
    }
    return id;
}
static long playlist_id_by_file_name(const char *stem){
    if(!strchr(stem, '_')) return 0;
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st; long id = 0; int hits = 0;
    if(sqlite3_prepare_v2(d, "SELECT ID, NAME FROM PLAYLIST_INFO;", -1, &st, NULL) != SQLITE_OK) return 0;
    while(sqlite3_step(st) == SQLITE_ROW){
        const char *nm = colt(st, 1); size_t k = 0;
        for(; nm[k] && stem[k]; k++){
            char c = nm[k] && strchr("\"*/:<>?\\|", nm[k]) ? '_' : nm[k];
            if(c != stem[k]) break;
        }
        if(!nm[k] && !stem[k]){ id = (long)sqlite3_column_int64(st, 0); hits++; }
    }
    sqlite3_finalize(st);
    return hits == 1 ? id : 0;
}
static int like_escape(const char *s, char *out, int cap){
    int j=0;
    for(; *s; s++){
        int need = (*s=='%'||*s=='_'||*s=='\\') ? 2 : 1;
        if(j + need > cap - 1) return 0;
        if(need==2) out[j++]='\\';
        out[j++]=*s;
    }
    out[j]=0; return 1;
}
static int resolve_like(sqlite3 *d, const char *like, char *out, int cap, int *herr){
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT PATH FROM SONG WHERE PATH LIKE ? ESCAPE '\\' LIMIT 2;", -1, &st, NULL)!=SQLITE_OK){ if(herr)*herr=1; return 0; }
    if(sqlite3_bind_text(st, 1, like, -1, SQLITE_STATIC)!=SQLITE_OK){ sqlite3_finalize(st); if(herr)*herr=1; return 0; }
    int r1 = sqlite3_step(st);
    if(r1 != SQLITE_ROW){ sqlite3_finalize(st); if(r1 != SQLITE_DONE && herr)*herr=1; return 0; }
    const char *p = (const char*)sqlite3_column_text(st, 0);
    if(!p || !p[0]){ sqlite3_finalize(st); if(herr)*herr=1; return 0; }
    int plen = (int)strlen(p);
    int fits = (plen < cap);
    if(fits) memcpy(out, p, (size_t)plen + 1);
    int r2 = sqlite3_step(st);
    int fz = sqlite3_finalize(st);
    if(r2 != SQLITE_DONE || !fits){
        if((r2 != SQLITE_DONE && r2 != SQLITE_ROW) && herr)*herr=1;
        if(cap>0) out[0]=0; return 0;
    }
    if(fz != SQLITE_OK){ if(herr)*herr=1; if(cap>0) out[0]=0; return 0; }
    return 1;
}
static int song_resolve(const char *entry, char *out, int cap, int *herr){
    sqlite3 *d = db(); if(!d){ if(herr)*herr=1; return 0; }
    sqlite3_stmt *st; int got = 0;
    if(sqlite3_prepare_v2(d, "SELECT PATH FROM SONG WHERE PATH=? LIMIT 1;", -1, &st, NULL) == SQLITE_OK){
        int rc;
        if(sqlite3_bind_text(st, 1, entry, -1, SQLITE_STATIC) != SQLITE_OK){ if(herr)*herr=1; rc = SQLITE_ERROR; }
        else rc = sqlite3_step(st);
        if(rc == SQLITE_ROW){
            const char *pth = (const char*)sqlite3_column_text(st, 0);
            if(pth && pth[0]){ snprintf(out, cap, "%s", pth); got = 1; }
            else if(herr)*herr=1;
        } else if(rc != SQLITE_DONE && herr)*herr=1;
        if(sqlite3_finalize(st) != SQLITE_OK && herr)*herr=1;
    } else if(herr)*herr=1;
    if(got) return 1;
    if(herr && *herr) return 0;
    const char *rel = entry; while(rel[0]=='/' || (rel[0]=='.' && rel[1]=='/')) rel += (rel[0]=='/')?1:2;
    if(!rel[0]) return 0;
    char esc[600], like[608];
    if(strchr(rel, '/') && like_escape(rel, esc, sizeof esc)){
        snprintf(like, sizeof like, "%%/%s", esc);
        if(resolve_like(d, like, out, cap, herr)) return 1;
        if(herr && *herr) return 0;
    }
    const char *base = strrchr(rel, '/'); base = base ? base+1 : rel;
    if(!base[0]) return 0;
    if(like_escape(base, esc, sizeof esc)){
        snprintf(like, sizeof like, "%%/%s", esc);
        if(resolve_like(d, like, out, cap, herr)) return 1;
    }
    if(!(herr && *herr))
        fprintf(stderr,"m3u resolve: '%s' unresolved (no match, ambiguous, or too long)\n", entry);
    return 0;
}
#define M3U_CLASH_MAX 100
static int playlist_same_members(sqlite3 *d, long a, long b){
    static const char *sql =
        "SELECT (SELECT COUNT(*) FROM (SELECT PATH,IFNULL(TRACK,0) FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?1 "
        "EXCEPT SELECT PATH,IFNULL(TRACK,0) FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?2)) + "
        "(SELECT COUNT(*) FROM (SELECT PATH,IFNULL(TRACK,0) FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?2 "
        "EXCEPT SELECT PATH,IFNULL(TRACK,0) FROM CUSTOM_PLAYLIST WHERE PLAYLIST_ID=?1));";
    sqlite3_stmt *st; int same = -1;
    if(sqlite3_prepare_v2(d, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, a); sqlite3_bind_int64(st, 2, b);
    if(sqlite3_step(st) == SQLITE_ROW) same = (sqlite3_column_int(st, 0) == 0) ? 1 : 0;
    sqlite3_finalize(st);
    return same;
}
static void utf8_fix(char *s){
    size_t n = strlen(s), i = n;
    while(i > 0 && n - i < 4 && ((unsigned char)s[i-1] & 0xC0) == 0x80) i--;
    if(i == 0) return;
    unsigned char lead = (unsigned char)s[i-1];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if(need > 1 && n - (i-1) < need) s[i-1] = 0;
    else if(need == 1 && lead >= 0x80) s[i-1] = 0;
}
static int song_subtrack_exists(sqlite3 *d, const char *path, int track){
    sqlite3_stmt *st; int yes = 0;
    if(sqlite3_prepare_v2(d, "SELECT 1 FROM SONG WHERE PATH=?1 AND IFNULL(TRACK,0)=?2 AND (IFNULL(IS_CUE,0)<>0 OR IFNULL(IS_ISO,0)<>0) LIMIT 1;",
                          -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC); sqlite3_bind_int(st, 2, track);
    yes = (sqlite3_step(st) == SQLITE_ROW);
    sqlite3_finalize(st);
    return yes;
}
#define M3U_MAX_BYTES (4L * 1024 * 1024)
static int import_m3u_file(const char *m3u_path){
    FILE *fp = fopen(m3u_path, "r"); if(!fp) return 0;
    struct stat fst;
    if(fstat(fileno(fp), &fst) != 0 || fst.st_size > M3U_MAX_BYTES){ fclose(fp); return 0; }
    int from_directive = 0;
    const char *b = strrchr(m3u_path, '/'); b = b ? b+1 : m3u_path;
    char name[512]; snprintf(name, sizeof name, "%s", b);
    char *dot = strrchr(name, '.'); if(dot) *dot = 0;
    { char hl[192];
      for(;;){
          int n = 0, ch, overlong = 0, binary = 0;
          while((ch = fgetc(fp)) != EOF && ch != '\n'){
              if(ch == 0) binary = 1;
              if(n < (int)sizeof hl - 1) hl[n++] = (char)ch; else overlong = 1;
          }
          if(ch == EOF && n == 0) break;
          if(binary || overlong) break;
          if(n > 0 && hl[n-1] == '\r') n--;
          hl[n] = 0;
          char *q = hl;
          if((unsigned char)q[0]==0xEF && (unsigned char)q[1]==0xBB && (unsigned char)q[2]==0xBF) q += 3;
          while(*q==' '||*q=='\t') q++;
          if(!*q) continue;
          if(!strncmp(q, "#PLAYLIST:", 10)){
              char v[192]; snprintf(v, sizeof v, "%s", q+10);
              trim(v);
              if(v[0]){ snprintf(name, sizeof name, "%s", v); from_directive = 1; break; }
              continue;
          }
          if(*q != '#') break;
      }
      rewind(fp);
    }
    utf8_fix(name);
    trim(name);
    if(strlen(name) > 139){
        unsigned h = 2166136261u; for(const unsigned char *q = (const unsigned char *)name; *q; q++){ h ^= *q; h *= 16777619u; }
        int cut = 130; while(cut > 0 && ((unsigned char)name[cut] & 0xC0) == 0x80) cut--;
        snprintf(name + cut, sizeof name - (size_t)cut, "~%08x", h);
    }
    char dir[600];
    if(snprintf(dir, sizeof dir, "%s", m3u_path) >= (int)sizeof dir){ fclose(fp); return 0; }
    char *sl = strrchr(dir, '/'); if(sl) *sl = 0; else dir[0] = 0;
    sqlite3 *d = db(); if(!d){ fclose(fp); return 0; }
    if(sqlite3_exec(d, "BEGIN IMMEDIATE;", 0, 0, 0) != SQLITE_OK){ fclose(fp); return 0; }
    long clash[M3U_CLASH_MAX]; int nclash = 0;
    char cname[MDB_STR];
    snprintf(cname, sizeof cname, "%s", name);
    { unsigned char used[M3U_CLASH_MAX]; memset(used, 0, sizeof used);
      char nb[MDB_STR]; snprintf(nb, sizeof nb, "%.140s", name); utf8_fix(nb);
      size_t bl = strlen(nb), nl = strlen(name);
      sqlite3_stmt *ns; int rc = SQLITE_ERROR;
      if(sqlite3_prepare_v2(d, "SELECT ID,NAME FROM PLAYLIST_INFO;", -1, &ns, NULL) == SQLITE_OK){
          while((rc = sqlite3_step(ns)) == SQLITE_ROW){
              const char *nm = colt(ns, 1); long id = (long)sqlite3_column_int64(ns, 0); int k = 0;
              if(!strcmp(nm, name)) k = 1;
              else if(!strncmp(nm, nb, bl) && nm[bl] == ' ' && nm[bl+1] == '('){
                  char *e; long v = strtol(nm + bl + 2, &e, 10);
                  if(v >= 2 && v < M3U_CLASH_MAX && !strcmp(e, ")")) k = (int)v;
              }
              if(k){ used[k] = 1; if(nclash < M3U_CLASH_MAX) clash[nclash++] = id; }
          }
          sqlite3_finalize(ns);
      }
      if(rc != SQLITE_DONE){ sqlite3_exec(d, "ROLLBACK;", 0, 0, 0); fclose(fp); return 0; }
      (void)nl;
      if(!used[1] && !from_directive){
          long id0 = playlist_id_by_file_name(name);
          if(id0 > 0 && nclash < M3U_CLASH_MAX){ used[1] = 1; clash[nclash++] = id0; }
      }
      if(used[1]){
          int k = 2; while(k < M3U_CLASH_MAX && used[k]) k++;
          if(k >= M3U_CLASH_MAX){ sqlite3_exec(d, "ROLLBACK;", 0, 0, 0); fclose(fp); return 0; }
          snprintf(cname, sizeof cname, "%s (%d)", nb, k);
      }
    }
    long pid = 0; int added = 0; int db_err = 0; char line[700];
    int pend_track = 0, has_pend = 0;
    while(fgets(line, sizeof line, fp)){
        size_t ll = strlen(line);
        if(ll == sizeof line - 1 && line[ll-1] != '\n'){
            int ch = fgetc(fp);
            if(ch != EOF && ch != '\n' && ch != '\r'){
                while((ch = fgetc(fp)) != '\n' && ch != EOF){ }
                has_pend = 0;
                continue;
            }
        }
        char *nl = strpbrk(line, "\r\n"); if(nl) *nl = 0;
        char *p = line; while(*p==' '||*p=='\t') p++;
        if((unsigned char)p[0]==0xEF && (unsigned char)p[1]==0xBB && (unsigned char)p[2]==0xBF) p += 3;
        while(*p==' '||*p=='\t') p++;
        for(char *q=p; *q; q++) if(*q=='\\') *q='/';
        if(!strncmp(p, M3U_TRACK_TAG, sizeof M3U_TRACK_TAG - 1)){
            char *endp; long tv = strtol(p + sizeof M3U_TRACK_TAG - 1, &endp, 10);
            has_pend = (endp != p + sizeof M3U_TRACK_TAG - 1 && tv >= 0 && tv < 100000);
            pend_track = has_pend ? (int)tv : 0;
            continue;
        }
        if(!*p || *p=='#') continue;
        int want_track = has_pend, track_no = pend_track; has_pend = 0;
        char entry[700]; int en;
        if(*p=='/') en = snprintf(entry, sizeof entry, "%s", p);
        else        en = snprintf(entry, sizeof entry, "%s/%s", dir, p);
        int entry_ok = (en > 0 && en < (int)sizeof entry);
        char songpath[700];
        int rherr = 0;
        int found = (entry_ok && song_resolve(entry, songpath, sizeof songpath, &rherr));
        if(!found && !rherr) found = song_resolve(p, songpath, sizeof songpath, &rherr);
        if(rherr){ db_err = 1; break; }
        if(!found) continue;
        if(want_track && !song_subtrack_exists(d, songpath, track_no)){
            fprintf(stderr, "m3u import: '%s' track %d not in the library - import incomplete, rolled back\n", songpath, track_no);
            db_err = 1; break;
        }
        if(!pid){ pid = mdb_playlist_create(cname); if(pid<=0){ db_err = 1; break; } }
        int aerr = 0;
        if(want_track ? mdb_playlist_add_subtrack(pid, songpath, track_no, &aerr) : mdb_playlist_add_song_ex(pid, songpath, &aerr)) added++;
        if(aerr){ db_err = 1; break; }
    }
    int read_err = ferror(fp);
    fclose(fp);
    if(pid && added > 0 && !read_err && !db_err){
        for(int i = 0; i < nclash; i++)
        { int sm = playlist_same_members(d, pid, clash[i]);
          if(sm < 0){ fprintf(stderr, "m3u import: could not compare with playlist %ld - import incomplete, rolled back\n", clash[i]);
                      sqlite3_exec(d, "ROLLBACK;", 0, 0, 0); return 0; }
          if(sm){ sqlite3_exec(d, "ROLLBACK;", 0, 0, 0); return 0; } }
        if(sqlite3_exec(d, "COMMIT;", 0, 0, 0) == SQLITE_OK) return added;
    }
    sqlite3_exec(d, "ROLLBACK;", 0, 0, 0);
    return 0;
}
static int import_m3u_dir_budget(const char *dir, int *budget){
    DIR *d = opendir(dir); if(!d) return 0;
    int total = 0; struct dirent *e;
    while((!budget || (budget[0] > 0 && budget[1] > 0)) && (e = readdir(d))){
        if(budget) budget[1]--;
        const char *n = e->d_name; int L = (int)strlen(n);
        int ism3u = (L>4 && !strcasecmp(n+L-4, ".m3u")) || (L>5 && !strcasecmp(n+L-5, ".m3u8"));
        if(!ism3u) continue;
        char path[600];
        if(snprintf(path, sizeof path, "%s/%s", dir, n) >= (int)sizeof path) continue;
        if(budget) budget[0]--;
        if(import_m3u_file(path) > 0) total++;
    }
    closedir(d);
    return total;
}
static int mdb_import_m3u_dir_leased(const char *dir){ return import_m3u_dir_budget(dir, NULL); }

#define M3U_WALK_DEPTH   8
#define M3U_WALK_DIRS    4096
#define M3U_WALK_FILES   512
#define M3U_WALK_ENTRIES 65536
static void import_m3u_walk(const char *dir, int depth, int *budget, int *total){
    if(budget[0] <= 0 || budget[1] <= 0 || budget[2] <= 0) return;
    budget[0]--;
    *total += import_m3u_dir_budget(dir, budget + 1);
    if(depth >= M3U_WALK_DEPTH) return;
    DIR *d = opendir(dir); if(!d) return;
    struct dirent *e;
    while(budget[0] > 0 && budget[2] > 0 && (e = readdir(d))){
        budget[2]--;
        const char *nm = e->d_name;
        if(nm[0] == '.' || !strcasecmp(nm, "System Volume Information")) continue;
        char path[600];
        if(snprintf(path, sizeof path, "%s/%s", dir, nm) >= (int)sizeof path) continue;
        int isdir = e->d_type == DT_DIR;
        if(e->d_type == DT_UNKNOWN){ struct stat st; isdir = lstat(path, &st) == 0 && S_ISDIR(st.st_mode); }
        if(isdir) import_m3u_walk(path, depth + 1, budget, total);
    }
    closedir(d);
}
static int mdb_import_m3u_sd_leased(const char *root){
    int budget[3] = { M3U_WALK_DIRS, M3U_WALK_FILES, M3U_WALK_ENTRIES }, total = 0;
    import_m3u_walk(root, 0, budget, &total);
    return total;
}

int mdb_playlist_num(void){
    sqlite3 *d = db(); if(!d) return 0;
    mdb_playlist_sync_registry();
    sqlite3_stmt *st; int n=0;
    if(sqlite3_prepare_v2(d, "SELECT COUNT(*) FROM PLAYLIST_INFO;", -1, &st, NULL) == SQLITE_OK){
        if(sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st,0);
        sqlite3_finalize(st);
    }
    return n;
}

int mdb_playlists_counted(char names[][MDB_STR], long *ids, int *counts, int cap){
    sqlite3 *d = db(); if(!d) return 0;
    sqlite3_stmt *st;
    const char *q = counts
        ? "SELECT p.NAME,p.ID,COUNT(m.PLAYLIST_ID) FROM PLAYLIST_INFO p LEFT JOIN CUSTOM_PLAYLIST m "
          "ON m.PLAYLIST_ID=p.ID WHERE p.NAME<>? GROUP BY p.ID ORDER BY p.ADD_TIME,p.ID;"
        : "SELECT NAME,ID FROM PLAYLIST_INFO WHERE NAME<>? ORDER BY ADD_TIME,ID;";
    if(sqlite3_prepare_v2(d, q, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, BOOK_SCOPE_NAME, -1, SQLITE_STATIC);
    int n=0;
    while(n<cap && sqlite3_step(st) == SQLITE_ROW){
        snprintf(names[n], MDB_STR, "%s", colt(st,0)); trim(names[n]);
        ids[n] = (long)sqlite3_column_int64(st,1);
        if(counts) counts[n] = sqlite3_column_int(st,2);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

int mdb_playlists(char names[][MDB_STR], long *ids, int cap){
    return mdb_playlists_counted(names, ids, NULL, cap);
}

int mdb_search(const char *q, const mdb_song_t **out, int cap){
    if(!q || !q[0]) return 0;
    int n = 0;
    for(int i=0;i<g_n && n<cap;i++){
        const mdb_song_t *s = &g_songs[i];
        if(strcasestr(s->title, q) || strcasestr(s->artist, q) || strcasestr(s->album, q))
            out[n++] = s;
    }
    return n;
}

int mdb_import_m3u_dir(const char *dir){
    if(!sd_io_begin()) return 0;
    int result = mdb_import_m3u_dir_leased(dir);
    sd_io_end();
    return result;
}

int mdb_import_m3u_sd(const char *root){
    if(!sd_io_begin()) return 0;
    int result = mdb_import_m3u_sd_leased(root);
    sd_io_end();
    return result;
}

int mdb_playlist_export(long pid, const char *name, char *outname, int cap){
    if(!sd_io_begin()) return 0;
    int result = mdb_playlist_export_leased(pid, name, outname, cap);
    sd_io_end();
    return result;
}

static sqlite3 *g_qdb;
static sqlite3 *qdb(void){
    if(!g_qdb){
        sqlite3 *tmp = NULL;
        if(sqlite3_open_v2(DB_PATH, &tmp, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK){
            sqlite3_busy_timeout(tmp, 200);
            g_qdb = tmp;
        } else if(tmp) sqlite3_close(tmp);
    }
    return g_qdb;
}
static int q_busy(int rc){ rc &= 0xFF; return rc == SQLITE_BUSY || rc == SQLITE_LOCKED; }

int mdb_upnext(int shuffle, int cur_pos_id, const char *cur_path, mdb_qrow_t *out, int cap, int *more){
    if(more) *more = 0;
    if(!out || cap < 1) return MDB_UPNEXT_ERROR;
    sqlite3 *d = qdb(); if(!d) return MDB_UPNEXT_ERROR;
    const char *sql = shuffle
        ? "WITH B AS (SELECT ID,ROW_NUMBER() OVER (ORDER BY ID) AS ORD,TITLE,NAME,ARTIST,PATH,DURATION FROM LIST_SONG_0),"
          "C AS (SELECT (SELECT COUNT(*) FROM LIST_SONG_0) AS N0,(SELECT COUNT(*) FROM LIST_SONG_3) AS N3,"
          "(SELECT COUNT(DISTINCT POS_ID) FROM LIST_SONG_3) AS ND,"
          "CASE WHEN ?1>0 THEN ?1 ELSE (SELECT CASE WHEN COUNT(*)=1 THEN MAX(ID) END FROM LIST_SONG_0 WHERE PATH=?2) END AS TID),"
          "X AS (SELECT R.ID AS RID,B.ID AS BID,B.ORD,B.TITLE,B.NAME,B.ARTIST,B.PATH,B.DURATION FROM LIST_SONG_3 R LEFT JOIN B ON B.ID=R.POS_ID) "
          "SELECT C.N0,C.N3,C.ND,C.TID,X.RID,X.BID,X.ORD,X.TITLE,X.NAME,X.ARTIST,X.PATH,X.DURATION FROM C LEFT JOIN X ON 1 ORDER BY X.RID;"
        : "WITH B AS (SELECT ID,ROW_NUMBER() OVER (ORDER BY ID) AS ORD,TITLE,NAME,ARTIST,PATH,DURATION FROM LIST_SONG_0),"
          "C AS (SELECT (SELECT COUNT(*) FROM LIST_SONG_0) AS N0,"
          "CASE WHEN ?1>0 THEN ?1 ELSE (SELECT CASE WHEN COUNT(*)=1 THEN MAX(ID) END FROM LIST_SONG_0 WHERE PATH=?2) END AS TID) "
          "SELECT C.N0,C.N0,C.N0,C.TID,B.ID,B.ID,B.ORD,B.TITLE,B.NAME,B.ARTIST,B.PATH,B.DURATION FROM C LEFT JOIN B ON 1 ORDER BY B.ID;";
    sqlite3_stmt *q = NULL;
    int rc = sqlite3_prepare_v2(d, sql, -1, &q, NULL);
    if(rc != SQLITE_OK){ int b = q_busy(sqlite3_extended_errcode(d)); if(q) sqlite3_finalize(q); return b ? MDB_UPNEXT_BUSY : MDB_UPNEXT_ERROR; }
    sqlite3_bind_int(q, 1, cur_pos_id > 0 ? cur_pos_id : 0);
    if(cur_path && cur_path[0]) sqlite3_bind_text(q, 2, cur_path, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(q, 2);
    int n = 0, found = 0, first = 1, status = 0, have_status = 0, target = 0, extra = 0;
    while((rc = sqlite3_step(q)) == SQLITE_ROW){
        if(first){
            first = 0;
            int n0 = sqlite3_column_int(q, 0), n3 = sqlite3_column_int(q, 1), nd = sqlite3_column_int(q, 2);
            if(sqlite3_column_type(q, 4) == SQLITE_NULL){
                status = n0 == 0 ? MDB_UPNEXT_EMPTY : MDB_UPNEXT_NOTFOUND;
                have_status = 1; break;
            }
            if(n3 != n0 || nd != n0){ status = MDB_UPNEXT_NOTFOUND; have_status = 1; break; }
            if(sqlite3_column_type(q, 3) == SQLITE_NULL){ status = MDB_UPNEXT_NOTFOUND; have_status = 1; break; }
            target = sqlite3_column_int(q, 3);
        }
        if(sqlite3_column_type(q, 5) == SQLITE_NULL){ status = MDB_UPNEXT_NOTFOUND; have_status = 1; break; }
        int id = sqlite3_column_int(q, 5);
        if(!found){ if(id != target) continue; found = 1; }
        if(n >= cap){ extra++; continue; }
        mdb_qrow_t *r = &out[n++];
        memset(r, 0, sizeof *r);
        r->base_id = id;
        r->ord = sqlite3_column_int(q, 6);
        const char *t = (const char *)sqlite3_column_text(q, 7);
        if(!t || !t[0]) t = (const char *)sqlite3_column_text(q, 8);
        const char *p = (const char *)sqlite3_column_text(q, 10);
        if(!t || !t[0]){ const char *sl = p ? strrchr(p, '/') : NULL; t = sl ? sl + 1 : p; }
        snprintf(r->title, sizeof r->title, "%s", t ? t : "");
        const char *ar = (const char *)sqlite3_column_text(q, 9);
        snprintf(r->artist, sizeof r->artist, "%s", ar ? ar : "");
        snprintf(r->path, sizeof r->path, "%s", p ? p : "");
        sqlite3_int64 dm = sqlite3_column_int64(q, 11);
        r->dur_ms = (dm > 0 && dm <= 2147483647) ? (int)dm : 0;
    }
    int busy = (rc != SQLITE_ROW && rc != SQLITE_DONE) && q_busy(sqlite3_extended_errcode(d));
    sqlite3_finalize(q);
    if(have_status) return status;
    if(rc != SQLITE_DONE) return busy ? MDB_UPNEXT_BUSY : MDB_UPNEXT_ERROR;
    if(first) return MDB_UPNEXT_ERROR;
    if(!found) return MDB_UPNEXT_NOTFOUND;
    if(more) *more = extra;
    return n;
}

int mdb_queue_row_valid(int base_id, int ord, const char *path){
    sqlite3 *d = qdb(); if(!d || !path) return -1;
    sqlite3_stmt *q = NULL;
    if(sqlite3_prepare_v2(d, "SELECT ORD,PATH FROM (SELECT ID,ROW_NUMBER() OVER (ORDER BY ID) AS ORD,PATH FROM LIST_SONG_0) "
                             "WHERE ID=?1;", -1, &q, NULL) != SQLITE_OK){ if(q) sqlite3_finalize(q); return -1; }
    sqlite3_bind_int(q, 1, base_id);
    int v, rc = sqlite3_step(q);
    if(rc == SQLITE_ROW){
        const char *p = (const char *)sqlite3_column_text(q, 1);
        v = sqlite3_column_int(q, 0) == ord && p && !strcmp(p, path);
    } else v = rc == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(q);
    return v;
}

int mdb_sysconfig_play_mode(void){
    sqlite3 *c = NULL; int v = -1;
    if(sqlite3_open_v2(MDB_SYSCONFIG_PATH, &c, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, 200);
        sqlite3_stmt *st;
        if(sqlite3_prepare_v2(c, "SELECT PLAY_MODE FROM SYSCONFIG WHERE ID=1;", -1, &st, NULL) == SQLITE_OK){
            if(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) == SQLITE_INTEGER){
                int x = sqlite3_column_int(st, 0);
                if(x >= 0 && x <= 4) v = x;
            }
            sqlite3_finalize(st);
        }
    }
    if(c) sqlite3_close(c);
    return v;
}

static int sysconfig_flag(const char *sql){
    sqlite3 *c = NULL; int v = -1;
    if(sqlite3_open_v2(MDB_SYSCONFIG_PATH, &c, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, 200);
        sqlite3_stmt *st;
        if(sqlite3_prepare_v2(c, sql, -1, &st, NULL) == SQLITE_OK){
            if(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) == SQLITE_INTEGER){
                int x = sqlite3_column_int(st, 0);
                if(x == 0 || x == 1) v = x;
            }
            sqlite3_finalize(st);
        }
    }
    if(c) sqlite3_close(c);
    return v;
}
int mdb_sysconfig_folder_jump(void){ return sysconfig_flag("SELECT FOLDER_JUMP FROM SYSCONFIG WHERE ID=1;"); }
int mdb_sysconfig_language(void){
    sqlite3 *c = NULL; int v = -1;
    if(sqlite3_open_v2(MDB_SYSCONFIG_PATH, &c, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, 200);
        sqlite3_stmt *st;
        if(sqlite3_prepare_v2(c, "SELECT LANGUAGE FROM SYSCONFIG WHERE ID=1;", -1, &st, NULL) == SQLITE_OK){
            if(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) == SQLITE_INTEGER){
                int x = sqlite3_column_int(st, 0);
                if(x >= 0 && x < 64) v = x;
            }
            sqlite3_finalize(st);
        }
    }
    if(c) sqlite3_close(c);
    return v;
}
int mdb_sysconfig_auto_time(void){
    sqlite3 *c = NULL; int v = -1;
    if(sqlite3_open_v2(MDB_SYSCONFIG_PATH, &c, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, 200);
        sqlite3_stmt *st;
        if(sqlite3_prepare_v2(c, "SELECT AUTO_TIME FROM SYSCONFIG WHERE ID=1;", -1, &st, NULL) == SQLITE_OK){
            if(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) == SQLITE_INTEGER){
                int x = sqlite3_column_int(st, 0);
                if(x == 0 || x == 1) v = x;
            }
            sqlite3_finalize(st);
        }
    }
    if(c) sqlite3_close(c);
    return v;
}
int mdb_sysconfig_set_auto_time(int on){
    sqlite3 *c = NULL; int ok = -1;
    on = on ? 1 : 0;
    if(sqlite3_open_v2(MDB_SYSCONFIG_PATH, &c, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, 1500);
        sqlite3_stmt *st;
        if(sqlite3_prepare_v2(c, "UPDATE SYSCONFIG SET AUTO_TIME=? WHERE ID=1;", -1, &st, NULL) == SQLITE_OK){
            sqlite3_bind_int(st, 1, on);
            if(sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(c) == 1) ok = 0;
            sqlite3_finalize(st);
        }
    }
    if(c) sqlite3_close(c);
    return (ok == 0 && mdb_sysconfig_auto_time() == on) ? 0 : -1;
}
