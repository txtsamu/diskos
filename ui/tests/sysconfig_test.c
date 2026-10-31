/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "sysconfig.h"
#include "sqlite3.h"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)

static char g_dir[128], g_db[192], g_log[192], g_fake[192];

static void sql_exec(sqlite3 *c, const char *sql){
    char *err = NULL;
    if(sqlite3_exec(c, sql, NULL, NULL, &err) != SQLITE_OK){ printf("  setup failed: %s: %s\n", sql, err); exit(2); }
}
static void make_db(const char *row_values){
    unlink(g_db);
    sqlite3 *c = NULL;
    if(sqlite3_open(g_db, &c) != SQLITE_OK) exit(2);
    sql_exec(c, "CREATE TABLE SYSCONFIG (ID INTEGER PRIMARY KEY, OUT_DEV INT, WIFI_STATUS INT, BT_STATUS INT, WORK_MODE INT,"
                " SCREEN_ROT INT, KEY_SINGLE_CLICK_SLE INT, KEY_DOUBLE_CLICK_SLE INT, KEY_LONG_PRESS_SLE INT,"
                " POWER_SAVE INT, CHARGE_PROTECT INT, PLAY_MODE INT, LANGUAGE INT, AUTO_TIME INT, FOLDER_JUMP INT,"
                " ARTIST_CLASS_TYPE INT);");
    char sql[512];
    snprintf(sql, sizeof sql, "INSERT INTO SYSCONFIG VALUES (1,%s);", row_values);
    sql_exec(c, sql);
    sqlite3_close(c);
}
static const char *GOOD = "2,1,0,4, 3,1,0,1, 600,1,2,17,1,0,1";

static void test_valid_reads(void){
    make_db(GOOD);
    CHECK(sysconfig_get(SC_OUT_DEV) == 2, "OUT_DEV");
    CHECK(sysconfig_get(SC_WIFI_STATUS) == 1, "WIFI_STATUS");
    CHECK(sysconfig_get(SC_BT_STATUS) == 0, "BT_STATUS");
    CHECK(sysconfig_get(SC_WORK_MODE) == 4, "WORK_MODE");
    CHECK(sysconfig_get(SC_SCREEN_ROT) == 3, "SCREEN_ROT");
    CHECK(sysconfig_get(SC_KEY_SINGLE) == 1, "KEY_SINGLE");
    CHECK(sysconfig_get(SC_KEY_DOUBLE) == 0, "KEY_DOUBLE");
    CHECK(sysconfig_get(SC_KEY_LONG) == 1, "KEY_LONG");
    CHECK(sysconfig_get(SC_POWER_SAVE) == 600, "POWER_SAVE");
    CHECK(sysconfig_get(SC_CHARGE_PROTECT) == 1, "CHARGE_PROTECT");
    CHECK(sysconfig_get(SC_PLAY_MODE) == 2, "PLAY_MODE");
    CHECK(sysconfig_get(SC_LANGUAGE) == 17, "LANGUAGE");
    CHECK(sysconfig_get(SC_AUTO_TIME) == 1, "AUTO_TIME");
    CHECK(sysconfig_get(SC_FOLDER_JUMP) == 0, "FOLDER_JUMP");
    CHECK(sysconfig_get(SC_ARTIST_CLASS) == 1, "ARTIST_CLASS_TYPE");
}

static void test_range_policy(void){
    make_db("-1,2,2,-5, -1,-1,-1,-1, -3,-1,5,64,2,2,2");
    CHECK(sysconfig_get(SC_OUT_DEV) == -1, "negative OUT_DEV unreadable");
    CHECK(sysconfig_get(SC_WIFI_STATUS) == -1, "WIFI_STATUS 2 unreadable");
    CHECK(sysconfig_get(SC_WORK_MODE) == -1, "negative WORK_MODE unreadable");
    CHECK(sysconfig_get(SC_SCREEN_ROT) == -1, "negative SCREEN_ROT unreadable");
    CHECK(sysconfig_get(SC_POWER_SAVE) == -1, "negative POWER_SAVE unreadable");
    CHECK(sysconfig_get(SC_CHARGE_PROTECT) == -1, "negative CHARGE_PROTECT unreadable");
    CHECK(sysconfig_get(SC_PLAY_MODE) == -1, "PLAY_MODE 5 unreadable");
    CHECK(sysconfig_get(SC_LANGUAGE) == -1, "LANGUAGE 64 unreadable");
    CHECK(sysconfig_get(SC_AUTO_TIME) == -1, "AUTO_TIME 2 unreadable");
    CHECK(sysconfig_get(SC_FOLDER_JUMP) == -1, "FOLDER_JUMP 2 unreadable");
    CHECK(sysconfig_get(SC_ARTIST_CLASS) == -1, "ARTIST_CLASS_TYPE 2 unreadable");
    make_db("0,0,0,0, 0,0,0,0, 0,0,4,63,0,0,0");
    CHECK(sysconfig_get(SC_PLAY_MODE) == 4, "PLAY_MODE 4 is the top of its range");
    CHECK(sysconfig_get(SC_LANGUAGE) == 63, "LANGUAGE 63 is the top of its range");
    CHECK(sysconfig_get(SC_OUT_DEV) == 0, "zero is a value, not unreadable");
}

static void test_null_and_text(void){
    make_db("NULL,'abc',NULL,4.5, NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,NULL,NULL,NULL");
    CHECK(sysconfig_get(SC_OUT_DEV) == -1, "NULL is unreadable");
    CHECK(sysconfig_get(SC_WIFI_STATUS) == -1, "text is unreadable, not coerced");
    CHECK(sysconfig_get(SC_WORK_MODE) == -1, "real is unreadable, not truncated");
    CHECK(sysconfig_get(SC_AUTO_TIME) == -1, "NULL AUTO_TIME unreadable");
}

static void test_missing(void){
    unlink(g_db);
    CHECK(sysconfig_get(SC_PLAY_MODE) == -1, "missing file reads -1");
    CHECK(access(g_db, F_OK) != 0, "a read never creates the database");
    CHECK(sysconfig_set(SC_AUTO_TIME, 1) == -1, "missing file write fails");
    CHECK(access(g_db, F_OK) != 0, "a write never creates the database");
    sqlite3 *c = NULL; sqlite3_open(g_db, &c);
    sql_exec(c, "CREATE TABLE OTHER (X INT);");
    sqlite3_close(c);
    CHECK(sysconfig_get(SC_PLAY_MODE) == -1, "missing table reads -1");
    unlink(g_db);
    sqlite3_open(g_db, &c);
    sql_exec(c, "CREATE TABLE SYSCONFIG (ID INTEGER PRIMARY KEY, PLAY_MODE INT, AUTO_TIME INT);");
    sqlite3_close(c);
    CHECK(sysconfig_get(SC_PLAY_MODE) == -1, "empty table reads -1");
    CHECK(sysconfig_get(SC_LANGUAGE) == -1, "missing column reads -1");
    CHECK(sysconfig_set(SC_AUTO_TIME, 1) == -1, "write that matches no row fails");
    CHECK(sysconfig_get((sc_col_t)99) == -1, "unknown column reads -1");
}

static void test_get_many(void){
    make_db("2,9,0,4, 3,1,7,NULL, 1,1,2,1,1,0,1");
    sc_col_t cols[4] = { SC_SCREEN_ROT, SC_KEY_SINGLE, SC_KEY_DOUBLE, SC_KEY_LONG };
    int v[4] = { 7, 7, 7, 7 };
    CHECK(sysconfig_get_many(cols, 4, v) == 3, "three of four valid");
    CHECK(v[0] == 3 && v[1] == 1 && v[2] == 7 && v[3] == -1, "valid values kept, NULL is -1");
    sc_col_t radios[2] = { SC_WIFI_STATUS, SC_BT_STATUS };
    CHECK(sysconfig_get_many(radios, 2, v) == 1 && v[0] == -1 && v[1] == 0, "out-of-range WIFI_STATUS only voids itself");
    CHECK(sysconfig_get_many(cols, 0, v) == -1, "zero columns refused");
    CHECK(sysconfig_get_many(cols, SC_MAX_READ + 1, v) == -1, "too many columns refused");
    CHECK(sysconfig_get_many(NULL, 1, v) == -1, "NULL columns refused");
    CHECK(sysconfig_get_many(cols, 1, NULL) == -1, "NULL out refused");
    unlink(g_db);
    CHECK(sysconfig_get_many(cols, 4, v) == 0 && v[0] == -1 && v[1] == -1 && v[2] == -1 && v[3] == -1,
          "unreadable database leaves every value -1");
}

static void test_set(void){
    make_db(GOOD);
    CHECK(sysconfig_set(SC_AUTO_TIME, 0) == 0 && sysconfig_get(SC_AUTO_TIME) == 0, "AUTO_TIME written and read back (0)");
    CHECK(sysconfig_set(SC_AUTO_TIME, 1) == 0 && sysconfig_get(SC_AUTO_TIME) == 1, "AUTO_TIME written and read back (1)");
    CHECK(sysconfig_set(SC_AUTO_TIME, 2) == -1 && sysconfig_get(SC_AUTO_TIME) == 1, "out-of-range value refused, row untouched");
    CHECK(sysconfig_set(SC_AUTO_TIME, -1) == -1, "negative value refused");
    CHECK(sysconfig_set(SC_PLAY_MODE, 1) == -1 && sysconfig_get(SC_PLAY_MODE) == 2, "a column the Player owns is not writable");
    CHECK(sysconfig_set(SC_WORK_MODE, 0) == -1 && sysconfig_get(SC_WORK_MODE) == 4, "WORK_MODE is not writable in-process");
    CHECK(sysconfig_set((sc_col_t)99, 0) == -1, "unknown column refused");
}

static void test_locked(void){
    make_db(GOOD);
    sqlite3 *holder = NULL;
    sqlite3_open(g_db, &holder);
    sql_exec(holder, "BEGIN EXCLUSIVE;");
    CHECK(sysconfig_get(SC_PLAY_MODE) == -1, "exclusively locked database reads -1 after the busy bound");
    CHECK(sysconfig_set(SC_AUTO_TIME, 0) == -1, "exclusively locked database write fails after the busy bound");
    sql_exec(holder, "ROLLBACK;");
    sqlite3_close(holder);
    CHECK(sysconfig_get(SC_AUTO_TIME) == 1, "locked write left the row untouched");
    CHECK(sysconfig_get(SC_PLAY_MODE) == 2, "readable again once unlocked");
}

static void test_path_seam(void){
    make_db(GOOD);
    char other[192];
    snprintf(other, sizeof other, "%s/other.db", g_dir);
    sysconfig_set_path(other);
    CHECK(!strcmp(sysconfig_path(), other), "path is overridable at run time");
    CHECK(sysconfig_get(SC_PLAY_MODE) == -1, "reads follow the overridden path");
    sysconfig_set_path(NULL);
    CHECK(!strcmp(sysconfig_path(), PLAYER_SYSCONFIG_DB), "NULL restores the compiled-in default");
    sysconfig_set_path(g_db);
}

static char g_cmd[1024], g_reply[64];
static int g_cap_calls, g_cap_ms;
static int fake_cap(const char *cmd, char *out, int cap, int ms){
    g_cap_calls++; g_cap_ms = ms;
    snprintf(g_cmd, sizeof g_cmd, "%s", cmd);
    snprintf(out, cap, "%s", g_reply);
    return (int)strlen(out);
}
static int cap_get(sc_col_t col, const char *reply){
    snprintf(g_reply, sizeof g_reply, "%s", reply);
    return sysconfig_get_via(fake_cap, col, 250);
}

static void test_cli_reads(void){
    g_cap_calls = 0;
    CHECK(cap_get(SC_OUT_DEV, "2\n") == 2, "OUT_DEV 2 with newline");
    CHECK(g_cap_calls == 1 && g_cap_ms == 250, "the caller's timeout reaches the runner");
    CHECK(strstr(g_cmd, "sqlite3 '") == g_cmd && strstr(g_cmd, g_db) && strstr(g_cmd, "SELECT OUT_DEV FROM SYSCONFIG WHERE ID=1") &&
          strstr(g_cmd, "2>/dev/null"), "single-column command names the configured path and column");
    CHECK(cap_get(SC_OUT_DEV, "6") == 6, "OUT_DEV 6 without newline");
    CHECK(cap_get(SC_OUT_DEV, "") == -1, "no output is unreadable");
    CHECK(cap_get(SC_OUT_DEV, "Error: unable to open database\n") == -1, "error text is unreadable");
    CHECK(cap_get(SC_OUT_DEV, "2x\n") == -1, "trailing junk is unreadable");
    CHECK(cap_get(SC_OUT_DEV, " 2\n") == -1, "leading space is unreadable");
    CHECK(cap_get(SC_OUT_DEV, "-1\n") == -1, "negative is unreadable");
    CHECK(cap_get(SC_OUT_DEV, "2\n\n") == -1, "two newlines are unreadable");
    CHECK(cap_get(SC_WIFI_STATUS, "1\n") == 1 && cap_get(SC_WIFI_STATUS, "0") == 0, "WIFI_STATUS 0 and 1");
    CHECK(cap_get(SC_WIFI_STATUS, "2\n") == -1, "WIFI_STATUS 2 is out of range");
    CHECK(cap_get(SC_WIFI_STATUS, "99999999999999\n") == -1, "overlong number is unreadable");

    sc_col_t radios[2] = { SC_WIFI_STATUS, SC_BT_STATUS };
    int v[2];
    snprintf(g_reply, sizeof g_reply, "0|1\n");
    CHECK(sysconfig_get_many_via(fake_cap, radios, 2, v, 1000) == 2 && v[0] == 0 && v[1] == 1, "pair read");
    CHECK(strstr(g_cmd, "SELECT WIFI_STATUS||'|'||BT_STATUS FROM SYSCONFIG WHERE ID=1"), "pair command joins columns with a pipe");
    snprintf(g_reply, sizeof g_reply, "0|1");
    CHECK(sysconfig_get_many_via(fake_cap, radios, 2, v, 1000) == 2, "pair read without newline");
    snprintf(g_reply, sizeof g_reply, "0|\n");
    CHECK(sysconfig_get_many_via(fake_cap, radios, 2, v, 1000) == 0 && v[0] == -1 && v[1] == -1, "half a pair voids both");
    snprintf(g_reply, sizeof g_reply, "0|2\n");
    CHECK(sysconfig_get_many_via(fake_cap, radios, 2, v, 1000) == 1 && v[0] == 0 && v[1] == -1, "an out-of-range half voids only itself");
    snprintf(g_reply, sizeof g_reply, "1\n");
    CHECK(sysconfig_get_many_via(fake_cap, radios, 2, v, 1000) == 0, "a single value is not a pair");
    snprintf(g_reply, sizeof g_reply, "0|1|1\n");
    CHECK(sysconfig_get_many_via(fake_cap, radios, 2, v, 1000) == 0, "a triple is not a pair");

    CHECK(sysconfig_get_via(NULL, SC_OUT_DEV, 250) == -1, "no runner reads -1");
    CHECK(sysconfig_get_via(fake_cap, (sc_col_t)99, 250) == -1, "unknown column reads -1");
    sysconfig_set_path("/tmp/it's.db");
    g_cap_calls = 0;
    CHECK(sysconfig_get_via(fake_cap, SC_OUT_DEV, 250) == -1 && g_cap_calls == 0, "a path that cannot be quoted never reaches the shell");
    sysconfig_set_path(g_db);
}

static char g_argv[3][1024];
static int g_run_calls, g_run_ms, g_run_rc;
static int fake_run(char *const argv[], int ms){
    g_run_calls++; g_run_ms = ms;
    for(int i = 0; i < 3; i++) snprintf(g_argv[i], sizeof g_argv[i], "%s", argv[i] ? argv[i] : "(null)");
    return g_run_rc;
}

static void test_work_mode_commands(void){
    g_run_rc = 0; g_run_calls = 0;
    CHECK(sysconfig_work_mode_reset_via(fake_run, 8000) == 0 && g_run_calls == 1 && g_run_ms == 8000, "reset runs once with the caller's timeout");
    CHECK(!strcmp(g_argv[0], "sh") && !strcmp(g_argv[1], "-c"), "reset goes through sh -c");
    CHECK(strstr(g_argv[2], "[ -f '") == g_argv[2] && strstr(g_argv[2], g_db) &&
          strstr(g_argv[2], "UPDATE SYSCONFIG SET WORK_MODE=0 WHERE ID=1 AND WORK_MODE IS NOT 0") &&
          strstr(g_argv[2], ".timeout 3000") && !strstr(g_argv[2], "SELECT"), "reset is a guarded UPDATE with a 3 s busy timeout and no read-back");
    CHECK(sysconfig_work_mode_confirm_via(fake_run, 5000) == 0 && g_run_ms == 5000, "confirm uses the caller's timeout");
    CHECK(strstr(g_argv[2], "[ -f '") == g_argv[2] && strstr(g_argv[2], "SELECT WORK_MODE FROM SYSCONFIG WHERE ID=1") &&
          strstr(g_argv[2], "= \"0\"") && !strstr(g_argv[2], "UPDATE"), "confirm is a guarded read-back of 0 and no update");
    CHECK(sysconfig_work_mode_reset_confirm_via(fake_run, 8000) == 0, "combined runs");
    CHECK(strstr(g_argv[2], "UPDATE SYSCONFIG SET WORK_MODE=0") && strstr(g_argv[2], "SELECT WORK_MODE FROM SYSCONFIG WHERE ID=1") &&
          strstr(g_argv[2], "UPDATE") < strstr(g_argv[2], "SELECT") && strstr(g_argv[2], g_db), "combined updates then reads back in one command");
    g_run_rc = -1;
    CHECK(sysconfig_work_mode_reset_via(fake_run, 1) == -1 && sysconfig_work_mode_confirm_via(fake_run, 1) == -1 &&
          sysconfig_work_mode_reset_confirm_via(fake_run, 1) == -1, "a failing runner is a failure");
    g_run_rc = 1;
    CHECK(sysconfig_work_mode_reset_via(fake_run, 1) == -1, "any non-zero runner result is a failure");
    g_run_calls = 0;
    CHECK(sysconfig_work_mode_reset_via(NULL, 1) == -1, "no runner is a failure");
    sysconfig_set_path("/tmp/it's.db");
    CHECK(sysconfig_work_mode_reset_via(fake_run, 1) == -1 && g_run_calls == 0, "a path that cannot be quoted never reaches the shell");
    sysconfig_set_path(g_db);
}

static int shell_run(char *const argv[], int ms){
    (void)ms;
    pid_t p = fork();
    if(p == 0){ execvp(argv[0], argv); _exit(127); }
    int st = 0;
    if(p < 0 || waitpid(p, &st, 0) < 0) return -1;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}
static int shell_cap(const char *cmd, char *out, int cap, int ms){
    (void)ms;
    out[0] = 0;
    FILE *f = popen(cmd, "r");
    if(!f) return 0;
    int n = (int)fread(out, 1, (size_t)cap - 1, f);
    if(n < 0) n = 0;
    out[n] = 0;
    pclose(f);
    return n;
}

static void write_fake_sqlite3(void){
    FILE *f = fopen(g_fake, "w");
    if(!f) exit(2);
    fprintf(f, "#!/bin/sh\n"
               "for a in \"$@\"; do printf '%%s\\n' \"$a\" >> '%s'; done\n"
               "case \"$*\" in\n"
               "  *'SELECT WORK_MODE'*) printf '%%s\\n' \"$FAKE_WORK_MODE\" ;;\n"
               "  *'SELECT WIFI_STATUS||'*) printf '1|0\\n' ;;\n"
               "  *'SELECT OUT_DEV'*) printf '4\\n' ;;\n"
               "esac\n", g_log);
    fclose(f);
    chmod(g_fake, 0755);
}
static int log_has(const char *needle){
    FILE *f = fopen(g_log, "r");
    if(!f) return 0;
    char line[1024]; int hit = 0;
    while(fgets(line, sizeof line, f)) if(strstr(line, needle)) hit = 1;
    fclose(f);
    return hit;
}

static void test_real_shell(void){
    char path[2048];
    snprintf(g_log, sizeof g_log, "%s/args.log", g_dir);
    snprintf(g_fake, sizeof g_fake, "%s/bin", g_dir);
    mkdir(g_fake, 0755);
    snprintf(g_fake, sizeof g_fake, "%s/bin/sqlite3", g_dir);
    write_fake_sqlite3();
    snprintf(path, sizeof path, "%s/bin:%s", g_dir, getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
    setenv("PATH", path, 1);
    make_db(GOOD);

    setenv("FAKE_WORK_MODE", "0", 1);
    unlink(g_log);
    CHECK(sysconfig_work_mode_reset_confirm_via(shell_run, 8000) == 0, "real sh: reset and confirm succeed when the read-back is 0");
    CHECK(log_has("UPDATE SYSCONFIG SET WORK_MODE=0 WHERE ID=1 AND WORK_MODE IS NOT 0") && log_has(g_db) && log_has(".timeout 3000"),
          "real sh: the UPDATE reached sqlite3 with the path and the busy timeout as separate arguments");
    setenv("FAKE_WORK_MODE", "4", 1);
    CHECK(sysconfig_work_mode_reset_confirm_via(shell_run, 8000) == -1, "real sh: a read-back other than 0 fails");
    CHECK(sysconfig_work_mode_confirm_via(shell_run, 5000) == -1, "real sh: confirm alone fails on 4");
    setenv("FAKE_WORK_MODE", "0", 1);
    CHECK(sysconfig_work_mode_confirm_via(shell_run, 5000) == 0, "real sh: confirm alone passes on 0");
    unlink(g_db);
    unlink(g_log);
    CHECK(sysconfig_work_mode_reset_confirm_via(shell_run, 8000) == -1 && !log_has("UPDATE"), "real sh: a missing database is never touched");
    CHECK(sysconfig_work_mode_confirm_via(shell_run, 5000) == -1 && access(g_log, F_OK) != 0, "real sh: a missing database is never read");
    make_db(GOOD);

    CHECK(sysconfig_get_via(shell_cap, SC_OUT_DEV, 250) == 4, "real sh: single read parses the output");
    sc_col_t radios[2] = { SC_WIFI_STATUS, SC_BT_STATUS };
    int v[2];
    CHECK(sysconfig_get_many_via(shell_cap, radios, 2, v, 1000) == 2 && v[0] == 1 && v[1] == 0, "real sh: pair read parses the output");
    CHECK(log_has("SELECT WIFI_STATUS||'|'||BT_STATUS FROM SYSCONFIG WHERE ID=1"), "real sh: the pipe-joined SELECT arrives as one argument");
}

int main(void){
    snprintf(g_dir, sizeof g_dir, "/tmp/diskos_sysconfig_test.%ld", (long)getpid());
    mkdir(g_dir, 0700);
    snprintf(g_db, sizeof g_db, "%s/sysconfig.db", g_dir);
    sysconfig_set_path(g_db);
    test_valid_reads();
    test_range_policy();
    test_null_and_text();
    test_missing();
    test_get_many();
    test_set();
    test_locked();
    test_path_seam();
    test_cli_reads();
    test_work_mode_commands();
    test_real_shell();
    char rm[256];
    snprintf(rm, sizeof rm, "rm -rf '%s'", g_dir);
    if(system(rm) != 0) fails++;
    if(fails){ printf("FAIL (%d failures)\n", fails); return 1; }
    printf("PASS (0 failures)\n");
    return 0;
}
