/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "config.h"

#define TDIR "/tmp/diskos_cfgtest"
#define CFGP TDIR "/diskos.conf"

static int file_has(const char *needle){
    FILE *f = fopen(CFGP, "r"); if(!f) return 0;
    char buf[16384]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
    return strstr(buf, needle) != NULL;
}

#define CHECK(cond, msg) do{ if(!(cond)){ fprintf(stderr, "    assert failed: %s (%s:%d)\n", msg, __FILE__, __LINE__); _exit(1); } }while(0)

#define CASE_OK 42
static int g_case_done;
#define CASE_DONE() (g_case_done = 1)

static void c_basic(void){
    CHECK(cfg_set_int("a", 5) == 0, "set int ok");
    CHECK(cfg_get_int("a", 0) == 5, "get int");
    CHECK(file_has("a=5"), "int persisted");
    CHECK(cfg_set_str("s", "hi") == 0, "set str ok");
    CHECK(strcmp(cfg_get_str("s", ""), "hi") == 0, "get str");
    CHECK(file_has("s=hi"), "str persisted");
    CHECK(cfg_take_save_error() == 0, "no save error");
    CASE_DONE();
}

static void c_reject_too_long(void){
    char longkey[64]; memset(longkey, 'k', 40); longkey[40] = 0;
    CHECK(cfg_set_int(longkey, 1) == -1, "too-long key rejected");
    CHECK(cfg_take_save_error() == 1, "save error set on reject");
    char longval[80]; memset(longval, 'v', 60); longval[60] = 0;
    CHECK(cfg_set_str("k", longval) == -1, "too-long value rejected");
    CHECK(cfg_take_save_error() == 1, "save error set on value reject");
    CHECK(cfg_get_int(longkey, -99) == -99, "rejected key not stored");
    CASE_DONE();
}

static void c_overflow(void){
    char k[16];
    for(int i = 0; i < 512; i++){ snprintf(k, sizeof k, "k%d", i); CHECK(cfg_set_int_deferred(k, i) == 0, "fill within cap"); }
    CHECK(cfg_set_int_deferred("overflow", 1) == -1, "over-cap rejected");
    CHECK(cfg_take_save_error() == 1, "save error on overflow");
    CHECK(cfg_flush() == 0, "flush the 512 filled keys");
    CASE_DONE();
}

static void c_failed_persist_retry(void){
    CHECK(cfg_set_int("x", 7) == 0, "initial set ok");
    CHECK(file_has("x=7"), "x=7 on disk");
    CHECK(chmod(TDIR, 0500) == 0, "make dir read-only");
    CHECK(cfg_set_int("x", 9) == -1, "set fails while dir RO");
    CHECK(cfg_take_save_error() == 1, "save error reported");
    CHECK(file_has("x=7") && !file_has("x=9"), "old value intact after failed write");
    CHECK(chmod(TDIR, 0700) == 0, "restore dir writable");
    CHECK(cfg_set_int("x", 9) == 0, "same-value retry now succeeds");
    CHECK(file_has("x=9"), "retry actually persisted x=9");
    CASE_DONE();
}

static void c_deferred_atomic(void){
    CHECK(cfg_set_int("base", 1) == 0, "seed file exists");
    CHECK(cfg_set_str_deferred("api", "KEY") == 0, "defer api");
    CHECK(cfg_set_str_deferred("sec", "SEC") == 0, "defer sec");
    CHECK(!file_has("api=KEY"), "api not on disk before flush");
    CHECK(!file_has("sec=SEC"), "sec not on disk before flush");
    CHECK(cfg_flush() == 0, "flush");
    CHECK(file_has("api=KEY") && file_has("sec=SEC"), "both creds persisted atomically");
    CASE_DONE();
}

static void c_new_empty_key_dirty(void){
    CHECK(cfg_set_int("x", 7) == 0, "seed clean");
    CHECK(cfg_set_str_deferred("empty", "") == 0, "add a new empty-valued key (deferred)");
    CHECK(cfg_set_int("x", 7) == 0, "same-value set must flush the pending new key");
    CHECK(file_has("empty="), "empty key persisted (new key marked dirty)");
    CASE_DONE();
}

static void c_txn_rollback(void){
    char k[16];
    for(int i = 0; i < 511; i++){ snprintf(k, sizeof k, "f%d", i); CHECK(cfg_set_int_deferred(k, i) == 0, "fill to 511"); }
    CHECK(cfg_flush() == 0, "persist the 511 fillers");
    cfg_begin();
    CHECK(cfg_set_str_deferred("aa", "1") == 0, "first new key fits (slot 512)");
    CHECK(cfg_set_str_deferred("bb", "2") == -1, "second new key overflows the store");
    CHECK(cfg_commit() == -1, "commit rolls back on a mid-batch failure");
    CHECK(cfg_take_save_error() == 1, "save error reported after rollback");
    CHECK(!file_has("aa=1") && !file_has("bb=2"), "neither batch key persisted");
    CHECK(cfg_get_int("aa", -9) == -9, "aa rolled out of memory too");
    CASE_DONE();
}

int main(void){
    if(system("rm -rf " TDIR " && mkdir -p " TDIR) != 0){ fprintf(stderr, "setup failed\n"); return 2; }
    struct { const char *name; void (*fn)(void); } cases[] = {
        {"basic_roundtrip",       c_basic},
        {"reject_too_long",       c_reject_too_long},
        {"overflow_rejected",     c_overflow},
        {"failed_persist_retry",  c_failed_persist_retry},
        {"deferred_atomic_creds", c_deferred_atomic},
        {"new_empty_key_dirty",   c_new_empty_key_dirty},
        {"txn_rollback",          c_txn_rollback},
    };
    int pass = 0, total = sizeof cases / sizeof cases[0];
    for(int i = 0; i < total; i++){
        fflush(NULL);
        pid_t pid = fork();
        if(pid < 0){ perror("fork"); return 2; }
        if(pid == 0){ cases[i].fn(); _exit(g_case_done ? CASE_OK : 3); }
        int st = 0;
        if(waitpid(pid, &st, 0) != pid){ perror("waitpid"); return 2; }
        int ok = WIFEXITED(st) && WEXITSTATUS(st) == CASE_OK;
        printf("[%s] %s\n", ok ? "PASS" : "FAIL", cases[i].name);
        pass += ok;
    }
    printf("\n%d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
