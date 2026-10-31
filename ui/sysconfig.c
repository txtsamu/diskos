/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "sysconfig.h"
#include "sqlite3.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define SC_BUSY_READ_MS  200
#define SC_BUSY_WRITE_MS 1500
#define SC_CMD_MAX       1024

static const struct { const char *name; int lo, hi, writable; } SC[SC_N] = {
    [SC_OUT_DEV]        = { "OUT_DEV",              0, INT_MAX, 0 },
    [SC_WIFI_STATUS]    = { "WIFI_STATUS",          0, 1,       0 },
    [SC_BT_STATUS]      = { "BT_STATUS",            0, 1,       0 },
    [SC_WORK_MODE]      = { "WORK_MODE",            0, INT_MAX, 0 },
    [SC_SCREEN_ROT]     = { "SCREEN_ROT",           0, INT_MAX, 0 },
    [SC_KEY_SINGLE]     = { "KEY_SINGLE_CLICK_SLE", 0, INT_MAX, 0 },
    [SC_KEY_DOUBLE]     = { "KEY_DOUBLE_CLICK_SLE", 0, INT_MAX, 0 },
    [SC_KEY_LONG]       = { "KEY_LONG_PRESS_SLE",   0, INT_MAX, 0 },
    [SC_POWER_SAVE]     = { "POWER_SAVE",           0, INT_MAX, 0 },
    [SC_CHARGE_PROTECT] = { "CHARGE_PROTECT",       0, INT_MAX, 0 },
    [SC_PLAY_MODE]      = { "PLAY_MODE",            0, 4,       0 },
    [SC_LANGUAGE]       = { "LANGUAGE",             0, 63,      0 },
    [SC_AUTO_TIME]      = { "AUTO_TIME",            0, 1,       1 },
    [SC_FOLDER_JUMP]    = { "FOLDER_JUMP",          0, 1,       0 },
    [SC_ARTIST_CLASS]   = { "ARTIST_CLASS_TYPE",    0, 1,       0 },
};

static const char *g_path = PLAYER_SYSCONFIG_DB;

const char *sysconfig_path(void){ return g_path; }
void sysconfig_set_path(const char *path){ g_path = path ? path : PLAYER_SYSCONFIG_DB; }

static int col_ok(sc_col_t c){ return (int)c >= 0 && c < SC_N; }
static int val_ok(sc_col_t c, long long v){ return v >= SC[c].lo && v <= SC[c].hi; }

static int cols_ok(const sc_col_t *cols, int n, int *out){
    if(!out || !cols || n < 1 || n > SC_MAX_READ) return 0;
    for(int i = 0; i < n; i++) out[i] = -1;
    for(int i = 0; i < n; i++) if(!col_ok(cols[i])) return 0;
    return 1;
}

int sysconfig_get_many(const sc_col_t *cols, int n, int *out){
    if(!cols_ok(cols, n, out)) return -1;
    char sql[256];
    int len = snprintf(sql, sizeof sql, "SELECT ");
    for(int i = 0; i < n; i++) len += snprintf(sql + len, sizeof sql - len, "%s%s", i ? "," : "", SC[cols[i]].name);
    snprintf(sql + len, sizeof sql - len, " FROM SYSCONFIG WHERE ID=1;");
    sqlite3 *c = NULL; int got = 0;
    if(sqlite3_open_v2(g_path, &c, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, SC_BUSY_READ_MS);
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(c, sql, -1, &st, NULL) == SQLITE_OK){
            if(sqlite3_step(st) == SQLITE_ROW){
                for(int i = 0; i < n; i++){
                    if(sqlite3_column_type(st, i) != SQLITE_INTEGER) continue;
                    sqlite3_int64 x = sqlite3_column_int64(st, i);
                    if(val_ok(cols[i], x)){ out[i] = (int)x; got++; }
                }
            }
            sqlite3_finalize(st);
        }
    }
    if(c) sqlite3_close(c);
    return got;
}

int sysconfig_get(sc_col_t col){
    int v = -1;
    sysconfig_get_many(&col, 1, &v);
    return v;
}

int sysconfig_set(sc_col_t col, int v){
    if(!col_ok(col) || !SC[col].writable || !val_ok(col, v)) return -1;
    char sql[128];
    snprintf(sql, sizeof sql, "UPDATE SYSCONFIG SET %s=? WHERE ID=1;", SC[col].name);
    sqlite3 *c = NULL; int ok = -1;
    if(sqlite3_open_v2(g_path, &c, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, SC_BUSY_WRITE_MS);
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(c, sql, -1, &st, NULL) == SQLITE_OK){
            sqlite3_bind_int(st, 1, v);
            if(sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(c) == 1) ok = 0;
            sqlite3_finalize(st);
        }
    }
    if(c) sqlite3_close(c);
    return (ok == 0 && sysconfig_get(col) == v) ? 0 : -1;
}

static int parse_row(const char *s, int n, const sc_col_t *cols, int *out){
    int got = 0;
    for(int i = 0; i < n; i++){
        if(i){ if(*s != '|') return 0; s++; }
        if(*s < '0' || *s > '9') return 0;
        long long x = 0; int digits = 0;
        while(*s >= '0' && *s <= '9'){ if(++digits > 10) return 0; x = x * 10 + (*s++ - '0'); }
        if(val_ok(cols[i], x)){ out[i] = (int)x; got++; }
    }
    if(*s == '\n') s++;
    return *s == 0 ? got : 0;
}

int sysconfig_get_many_via(sysconfig_cap_t cap, const sc_col_t *cols, int n, int *out, int timeout_ms){
    if(!cols_ok(cols, n, out) || !cap || strchr(g_path, '\'')) return -1;
    char cmd[SC_CMD_MAX], buf[64] = {0};
    int len = snprintf(cmd, sizeof cmd, "sqlite3 '%s' \"SELECT ", g_path);
    for(int i = 0; i < n && len < (int)sizeof cmd; i++)
        len += snprintf(cmd + len, sizeof cmd - len, "%s%s", i ? "||'|'||" : "", SC[cols[i]].name);
    if(len < (int)sizeof cmd) len += snprintf(cmd + len, sizeof cmd - len, " FROM SYSCONFIG WHERE ID=1\" 2>/dev/null");
    if(len >= (int)sizeof cmd) return -1;
    cap(cmd, buf, sizeof buf, timeout_ms);
    int tmp[SC_MAX_READ];
    for(int i = 0; i < n; i++) tmp[i] = -1;
    int got = parse_row(buf, n, cols, tmp);
    for(int i = 0; i < n; i++) out[i] = got ? tmp[i] : -1;
    return got;
}

int sysconfig_get_via(sysconfig_cap_t cap, sc_col_t col, int timeout_ms){
    int v = -1;
    sysconfig_get_many_via(cap, &col, 1, &v, timeout_ms);
    return v;
}

static int work_mode_run(sysconfig_run_t run, int timeout_ms, int reset, int confirm){
    const char *p = g_path;
    if(!run || strchr(p, '\'')) return -1;
    char cmd[SC_CMD_MAX];
    int len = snprintf(cmd, sizeof cmd, "[ -f '%s' ]", p);
    if(reset && len < (int)sizeof cmd)
        len += snprintf(cmd + len, sizeof cmd - len,
                        " && sqlite3 -cmd '.timeout 3000' '%s' 'UPDATE SYSCONFIG SET WORK_MODE=0 WHERE ID=1 AND WORK_MODE IS NOT 0'", p);
    if(confirm && len < (int)sizeof cmd)
        len += snprintf(cmd + len, sizeof cmd - len,
                        " && [ \"$(sqlite3 -cmd '.timeout 3000' '%s' 'SELECT WORK_MODE FROM SYSCONFIG WHERE ID=1')\" = \"0\" ]", p);
    if(len >= (int)sizeof cmd) return -1;
    char *argv[] = { "sh", "-c", cmd, NULL };
    return run(argv, timeout_ms) == 0 ? 0 : -1;
}

int sysconfig_work_mode_reset_via(sysconfig_run_t run, int timeout_ms){ return work_mode_run(run, timeout_ms, 1, 0); }
int sysconfig_work_mode_confirm_via(sysconfig_run_t run, int timeout_ms){ return work_mode_run(run, timeout_ms, 0, 1); }
int sysconfig_work_mode_reset_confirm_via(sysconfig_run_t run, int timeout_ms){ return work_mode_run(run, timeout_ms, 1, 1); }
