/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef CFG_PATH
#define CFG_PATH     "/usr/data/diskos.conf"
#endif
#ifndef CFG_DIR
#define CFG_DIR      "/usr/data"
#endif
#ifndef LEGACY_CONF
#define LEGACY_CONF  "/usr/data/ipodos.conf"
#endif
#ifndef LEGACY_SWIPE
#define LEGACY_SWIPE "/usr/data/swipe_thresh"
#endif
#define CFG_MAX      512
#define KLEN         28
#define VLEN         48

typedef struct { char k[KLEN]; char v[VLEN]; } cfg_entry_t;
static cfg_entry_t g_cfg[CFG_MAX];
static int g_n = 0;
static int g_loaded = 0;

static int g_save_err = 0;
static int g_renamed = 0;
static int g_dirty = 1;

static cfg_entry_t g_snap[CFG_MAX];
static int g_snap_n = 0, g_snap_dirty = 0, g_txn = 0, g_txn_err = 0;
static int g_load_failed = 0;

static cfg_entry_t *find(const char *key){
    for(int i=0;i<g_n;i++) if(!strcmp(g_cfg[i].k,key)) return &g_cfg[i];
    return NULL;
}

static int rewrite(void){
    if(g_load_failed){ g_save_err = 1; return -1; }
    g_renamed = 0;
    if(!g_dirty) return 0;
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s.tmp", CFG_PATH);
    FILE *f = fopen(tmp, "w");
    if(!f){ g_save_err = 1; return -1; }
    int ok = 1;
    for(int i=0;i<g_n;i++)
        if(fprintf(f, "%s=%s\n", g_cfg[i].k, g_cfg[i].v) < 0){ ok = 0; break; }
    if(ok && (fflush(f) != 0 || fsync(fileno(f)) != 0)) ok = 0;
    if(fclose(f) != 0) ok = 0;
    if(!ok || rename(tmp, CFG_PATH) != 0){ unlink(tmp); g_save_err = 1; return -1; }
    g_renamed = 1;
    int dfd = open(CFG_DIR, O_RDONLY | O_DIRECTORY);
    if(dfd < 0){ g_save_err = 1; return -1; }
    int dr = fsync(dfd);
    if(close(dfd) != 0 || dr != 0){ g_save_err = 1; return -1; }
    g_dirty = 0;
    return 0;
}

int cfg_take_save_error(void){ int e = g_save_err; g_save_err = 0; return e; }

static int put(const char *key, const char *val){
    if(strlen(key) >= KLEN || strlen(val) >= VLEN){ if(g_txn) g_txn_err = 1; return -1; }
    cfg_entry_t *e = find(key);
    int isnew = 0;
    if(!e){
        if(g_n >= CFG_MAX){ if(g_txn) g_txn_err = 1; return -1; }
        e = &g_cfg[g_n++];
        e->k[0] = 0; e->v[0] = 0;
        snprintf(e->k, KLEN, "%s", key);
        isnew = 1;
    }
    if(isnew || strcmp(e->v, val) != 0){ snprintf(e->v, VLEN, "%s", val); g_dirty = 1; }
    return 0;
}

static int cfg_parse_stream(FILE *f){
    char line[KLEN+VLEN+4];
    int failed = 0;
    while(fgets(line, sizeof(line), f)){
        size_t ll = strlen(line);
        int has_nl = (ll && line[ll-1]=='\n');
        if(!has_nl && ll == sizeof(line)-1){
            int c; while((c=fgetc(f))!=EOF && c!='\n'){}
            continue;
        }
        if(has_nl) line[ll-1] = 0;
        char *eq = strchr(line, '='); if(!eq) continue;
        *eq = 0;
        if(line[0] && put(line, eq+1) < 0) failed = 1;
    }
    if(ferror(f)) failed = 1;
    return failed ? -1 : 0;
}

void cfg_load(void){
    if(g_loaded) return;
    g_loaded = 1;
    FILE *f = fopen(CFG_PATH, "r");
    if(f){
        if(cfg_parse_stream(f) < 0) g_load_failed = 1;
        fclose(f);
        g_dirty = 0;
        return;
    }
    if(errno != ENOENT){
        g_load_failed = 1;
        return;
    }
    FILE *lc = fopen(LEGACY_CONF, "r");
    if(lc){
        int bad = (cfg_parse_stream(lc) < 0);
        fclose(lc);
        if(!bad) rewrite();
        else g_load_failed = 1;
        return;
    }
    if(errno != ENOENT){
        g_load_failed = 1;
        return;
    }
    FILE *lf = fopen(LEGACY_SWIPE, "r");
    if(lf){
        int v=0;
        if(fscanf(lf,"%d",&v)==1 && v>=20 && v<=200){
            char b[16]; snprintf(b,sizeof(b),"%d",v); put("swipe_thresh", b);
        }
        fclose(lf);
        rewrite();
    }
}

int cfg_get_int(const char *key, int def){
    cfg_entry_t *e = find(key);
    return e ? atoi(e->v) : def;
}

int cfg_set_int(const char *key, int v){
    if(g_load_failed){ g_save_err = 1; return -1; }
    cfg_entry_t *e = find(key);
    if(e && atoi(e->v) == v && !g_dirty) return 0;
    char b[16]; snprintf(b,sizeof(b),"%d",v);
    if(put(key, b) < 0){ g_save_err = 1; return -1; }
    return rewrite();
}
int cfg_set_int_deferred(const char *key, int v){
    if(g_load_failed){ g_save_err = 1; return -1; }
    cfg_entry_t *e = find(key);
    if(e && atoi(e->v) == v) return 0;
    char b[16]; snprintf(b,sizeof(b),"%d",v);
    if(put(key, b) < 0){ g_save_err = 1; return -1; }
    return 0;
}
int cfg_flush(void){ return rewrite(); }

void cfg_begin(void){
    if(g_load_failed){ g_save_err = 1; return; }
    if(g_n > 0) memcpy(g_snap, g_cfg, (size_t)g_n * sizeof g_cfg[0]);
    g_snap_n = g_n; g_snap_dirty = g_dirty; g_txn = 1; g_txn_err = 0;
}
int cfg_commit(void){
    if(!g_txn) return cfg_flush();
    g_txn = 0;
    if(g_txn_err){
        if(g_snap_n > 0) memcpy(g_cfg, g_snap, (size_t)g_snap_n * sizeof g_cfg[0]);
        g_n = g_snap_n; g_dirty = g_snap_dirty; g_save_err = 1;
        return -1;
    }
    return rewrite();
}

const char *cfg_get_str(const char *key, const char *def){
    cfg_entry_t *e = find(key);
    return e ? e->v : def;
}

int cfg_set_str(const char *key, const char *v){
    if(g_load_failed){ g_save_err = 1; return -1; }
    if(put(key, v) < 0){ g_save_err = 1; return -1; }
    return rewrite();
}
int cfg_set_str_deferred(const char *key, const char *v){
    if(g_load_failed){ g_save_err = 1; return -1; }
    if(put(key, v) < 0){ g_save_err = 1; return -1; }
    return 0;
}

int cfg_remove_if(int (*match)(const char *key)){
    if(g_load_failed || g_txn || !match){ g_save_err = 1; return -1; }
    memcpy(g_snap, g_cfg, (size_t)g_n * sizeof g_cfg[0]);
    int old_n = g_n, old_dirty = g_dirty, w = 0;
    for(int i = 0; i < g_n; i++) if(!match(g_cfg[i].k)) g_cfg[w++] = g_cfg[i];
    if(w == g_n) return 0;
    g_n = w; g_dirty = 1;
    if(rewrite() != 0){
        if(g_renamed){
            g_n = 0;
            FILE *f = fopen(CFG_PATH, "r");
            if(f){ if(cfg_parse_stream(f) < 0) g_load_failed = 1; fclose(f); } else g_load_failed = 1;
            g_dirty = 1;
            return -2;
        }
        memcpy(g_cfg, g_snap, (size_t)old_n * sizeof g_cfg[0]);
        g_n = old_n; g_dirty = old_dirty;
        return -1;
    }
    return 0;
}
