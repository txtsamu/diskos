/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sqlite3.h"
#include "jsmn.h"
#include "sysconfig.h"

#ifndef UIREAD_DB
#error UIREAD_DB
#endif
#define sqlite3_open_v2(p, h, f, v) sqlite3_open_v2(UIREAD_DB, h, f, v)
#include "../musicdb.c"
#undef sqlite3_open_v2

int sysconfig_get(sc_col_t col){ (void)col; return 0; }
int sd_io_begin(void){ return 1; }
void sd_io_end(void){}
int sd_write_begin(void){ return 1; }
void sd_write_end(void){}

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)

static const char *GRAPHIC =
    "[{\"filterType\":0,\"frequency\":32,\"position\":0,\"gain\":\"3.0\",\"qValue\":\"0.7\"},"
    "{\"filterType\":0,\"frequency\":64,\"position\":1,\"gain\":\"0.0\",\"qValue\":\"0.7\"},"
    "{\"filterType\":0,\"frequency\":125,\"position\":2,\"gain\":\"0.0\",\"qValue\":\"0.7\"},"
    "{\"filterType\":0,\"frequency\":250,\"position\":3,\"gain\":\"0.0\",\"qValue\":\"0.7\"},"
    "{\"filterType\":0,\"frequency\":500,\"position\":4,\"gain\":\"0.0\",\"qValue\":\"0.7\"},"
    "{\"filterType\":0,\"frequency\":1000,\"position\":5,\"gain\":\"0.0\",\"qValue\":\"0.7\"},"
    "{\"filterType\":0,\"frequency\":2000,\"position\":6,\"gain\":\"0.0\",\"qValue\":\"0.7\"},"
    "{\"filterType\":0,\"frequency\":4000,\"position\":7,\"gain\":\"0.0\",\"qValue\":\"0.7\"},"
    "{\"filterType\":0,\"frequency\":8000,\"position\":8,\"gain\":\"0.0\",\"qValue\":\"0.7\"},"
    "{\"filterType\":0,\"frequency\":16000,\"position\":9,\"gain\":\"-2.0\",\"qValue\":\"0.7\"}]";

static void sql_exec(sqlite3 *c, const char *sql){
    char *err = NULL;
    if(sqlite3_exec(c, sql, NULL, NULL, &err) != SQLITE_OK){ printf("  setup failed: %s: %s\n", sql, err); exit(2); }
}
static sqlite3 *open_other(void){
    sqlite3 *c = NULL;
    if(sqlite3_open(UIREAD_DB, &c) != SQLITE_OK) exit(2);
    sqlite3_busy_timeout(c, 200);
    return c;
}

static void test_missing_db(void){
    unlink(UIREAD_DB);
    int g[10], m = 0;
    CHECK(mdb_ui_read_begin() == 0, "begin fails without a database");
    CHECK(mdb_get_peq(11, &m, g) == -1, "get_peq reports a failed read without a database");
    CHECK(mdb_peq_is_graphic(11) == -1, "is_graphic reports a failed classify without a database");
    mdb_ui_read_end();
}

static void test_good_read(void){
    unlink(UIREAD_DB);
    sqlite3 *c = open_other();
    sql_exec(c, "CREATE TABLE SONG (ID INTEGER PRIMARY KEY, PATH TEXT);");
    sql_exec(c, "CREATE TABLE PEQ (STYLE_NAME TEXT, MASTER_GAIN REAL, PARAMS_JSON TEXT, STYLE_PRESET INTEGER);");
    char sql[2048];
    snprintf(sql, sizeof sql, "INSERT INTO PEQ VALUES ('USER1', 2, '%s', 11);", GRAPHIC);
    sql_exec(c, sql);
    sql_exec(c, "INSERT INTO PEQ VALUES ('USER2', 0, '[{\"filterType\":1,\"frequency\":32}]', 12);");
    sqlite3_close(c);

    int g[10], m = 0;
    CHECK(mdb_ui_read_begin() == 1, "begin succeeds on a readable database");
    CHECK(sqlite3_get_autocommit(g_db) == 0, "snapshot holds a transaction while open");
    CHECK(mdb_get_peq(11, &m, g) == 1 && m == 2 && g[0] == 3 && g[9] == -2, "stored curve is read");
    CHECK(mdb_peq_is_graphic(11) == 1, "stored curve is graphic");
    CHECK(mdb_peq_is_graphic(12) == 0, "parametric slot is classified as parametric");
    CHECK(mdb_get_peq(15, &m, g) == 0, "absent slot is empty, not a failure");
    CHECK(mdb_ui_read_begin() == 1, "begin nests");
    mdb_ui_read_end();
    CHECK(sqlite3_get_autocommit(g_db) == 0, "inner end keeps the snapshot");
    mdb_ui_read_end();
    CHECK(sqlite3_get_autocommit(g_db) == 1, "outer end releases the snapshot");
    mdb_ui_read_end();
    CHECK(sqlite3_get_autocommit(g_db) == 1, "unbalanced end is harmless");
}

static void test_locked_db(void){
    sqlite3 *c = open_other();
    sql_exec(c, "BEGIN EXCLUSIVE;");
    CHECK(mdb_ui_read_begin() == 0, "begin fails while the database is locked");
    CHECK(sqlite3_get_autocommit(g_db) == 1, "failed begin leaves no open transaction");
    sql_exec(c, "ROLLBACK;");
    sqlite3_close(c);
    CHECK(mdb_ui_read_begin() == 1, "begin succeeds again once the lock clears");
    mdb_ui_read_end();
}

static void test_missing_tables(void){
    sqlite3 *c = open_other();
    sql_exec(c, "DROP TABLE SONG;");
    CHECK(mdb_ui_read_begin() == 0, "begin fails without the SONG table");
    CHECK(sqlite3_get_autocommit(g_db) == 1, "failed probe leaves no open transaction");
    sql_exec(c, "CREATE TABLE SONG (ID INTEGER PRIMARY KEY, PATH TEXT);");
    sql_exec(c, "DROP TABLE PEQ;");
    int g[10], m = 0;
    CHECK(mdb_ui_read_begin() == 1, "begin succeeds with SONG present");
    CHECK(mdb_get_peq(11, &m, g) == -1, "get_peq reports failure without the PEQ table");
    CHECK(mdb_peq_is_graphic(11) == -1, "is_graphic reports failure without the PEQ table");
    mdb_ui_read_end();
    sqlite3_close(c);
}

int main(void){
    test_missing_db();
    test_good_read();
    test_locked_db();
    test_missing_tables();
    if(fails){ printf("uiread_test: %d FAILED\n", fails); return 1; }
    printf("uiread_test: ALL PASS\n");
    return 0;
}
