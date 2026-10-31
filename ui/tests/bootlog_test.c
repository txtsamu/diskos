/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "bootlog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c, m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)

static void wipe(void){
    char c[400];
    snprintf(c, sizeof c, "rm -rf %s && mkdir -p %s", BOOTLOG_TEST_DIR, BOOTLOG_TEST_DIR);
    assert(system(c) == 0);
}
static void put(const char *path, size_t n, char fill){
    FILE *f = fopen(path, "w");
    assert(f);
    for(size_t i = 0; i < n; i++) fputc(fill, f);
    fclose(f);
}
static long size_of(const char *path){
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}
static int first_byte(const char *path){
    FILE *f = fopen(path, "r");
    if(!f) return -1;
    int c = fgetc(f);
    fclose(f);
    return c;
}
static int contains(const char *path, const char *needle){
    FILE *f = fopen(path, "r");
    if(!f) return 0;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, needle) != NULL;
}
static void mk(char *out, size_t cap, const char *name){ snprintf(out, cap, "%s/%s", BOOTLOG_TEST_DIR, name); }

static void test_rotate_shifts_and_caps(void){
    wipe();
    char f[300], f1[300], f2[300], f3[300], f4[300];
    mk(f, sizeof f, "log"); snprintf(f1, sizeof f1, "%s.1", f); snprintf(f2, sizeof f2, "%s.2", f);
    snprintf(f3, sizeof f3, "%s.3", f); snprintf(f4, sizeof f4, "%s.4", f);
    put(f, 100, 'a');
    bootlog_rotate(f, 100, 3);
    CHECK(size_of(f) == 100 && size_of(f1) < 0, "rotate: a file at the limit is left alone");
    put(f, 101, 'A');
    bootlog_rotate(f, 100, 3);
    CHECK(size_of(f) < 0 && first_byte(f1) == 'A', "rotate: oversize file becomes .1");
    put(f, 101, 'B'); bootlog_rotate(f, 100, 3);
    put(f, 101, 'C'); bootlog_rotate(f, 100, 3);
    CHECK(first_byte(f1) == 'C' && first_byte(f2) == 'B' && first_byte(f3) == 'A', "rotate: history shifts newest first");
    put(f, 101, 'D'); bootlog_rotate(f, 100, 3);
    CHECK(first_byte(f1) == 'D' && first_byte(f2) == 'C' && first_byte(f3) == 'B', "rotate: oldest generation is dropped");
    CHECK(size_of(f4) < 0, "rotate: never more than keep generations");
    bootlog_rotate(f, 100, 0);
    bootlog_rotate("/nonexistent/dir/log", 1, 3);
    CHECK(1, "rotate: keep 0 and a missing file are harmless");
}

static void test_event_line(void){
    wipe();
    bootlog_event("gate denied status=%d waited=%d", -1, 60);
    bootlog_event("second %s", "line");
    CHECK(contains(BOOTLOG_JOURNAL_PATH, "gate denied status=-1 waited=60\n"), "event: formatted text is written");
    CHECK(contains(BOOTLOG_JOURNAL_PATH, "second line\n"), "event: lines are appended, not overwritten");
    CHECK(contains(BOOTLOG_JOURNAL_PATH, "u="), "event: carries uptime");
    char pid[32]; snprintf(pid, sizeof pid, "pid=%ld ", (long)getpid());
    CHECK(contains(BOOTLOG_JOURNAL_PATH, pid), "event: carries the pid");
}

static void test_event_is_bounded_and_rotates(void){
    wipe();
    char big[800]; memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0;
    bootlog_event("%s", big);
    CHECK(size_of(BOOTLOG_JOURNAL_PATH) < 520, "event: one line is capped to its buffer");
    wipe();
    for(int i = 0; i < 400; i++) bootlog_event("filler line number %d to push the journal over its limit", i);
    char j1[300]; snprintf(j1, sizeof j1, "%s.1", BOOTLOG_JOURNAL_PATH);
    CHECK(size_of(j1) > 0, "journal: rotates once it passes its limit");
    CHECK(size_of(BOOTLOG_JOURNAL_PATH) <= BOOTLOG_JOURNAL_MAX + 200, "journal: live file stays near the limit");
    CHECK(contains(BOOTLOG_JOURNAL_PATH, "number 399"), "journal: newest line is in the live file");
}

static void test_event_file(void){
    wipe();
    char p[300]; mk(p, sizeof p, "status");
    FILE *f = fopen(p, "w"); fputs("guard=1 local=1 pid=1173\n", f); fclose(f);
    bootlog_event_file("launch status", p);
    CHECK(contains(BOOTLOG_JOURNAL_PATH, "launch status: guard=1 local=1 pid=1173 \n"), "event_file: contents with newlines flattened");
    bootlog_event_file("missing", "/nonexistent/file");
    CHECK(contains(BOOTLOG_JOURNAL_PATH, "missing: unreadable\n"), "event_file: a missing file is recorded, not skipped");
}

static void test_dmesg(void){
    wipe();
    bootlog_dmesg("gate", 2);
    CHECK(contains(BOOTLOG_DMESG_PATH, "=== dmesg tail (gate)"), "dmesg: header names the reason");
    CHECK(contains(BOOTLOG_DMESG_PATH, "line4\nline5\n") && !contains(BOOTLOG_DMESG_PATH, "line3"), "dmesg: only the last N lines");
    bootlog_dmesg("again", 2);
    CHECK(contains(BOOTLOG_DMESG_PATH, "(again)") && contains(BOOTLOG_DMESG_PATH, "(gate)"), "dmesg: snapshots accumulate");
    bootlog_dmesg("none", 0);
    CHECK(!contains(BOOTLOG_DMESG_PATH, "(none)"), "dmesg: zero lines writes nothing");
}

static void test_signal_note(void){
    wipe();
    const char m[] = "fatal signal 11\n";
    bootlog_signal_note(m, (int)sizeof m - 1);
    CHECK(contains(BOOTLOG_JOURNAL_PATH, "fatal signal 11\n"), "signal_note: raw message reaches the journal");
}

static void test_open_stderr(void){
    wipe();
    put(BOOTLOG_MAIN_PATH, BOOTLOG_MAIN_MAX + 10, 'o');
    pid_t c = fork();
    if(c == 0){
        bootlog_open_stderr();
        fprintf(stderr, "new boot line\n");
        fflush(stderr);
        _exit(0);
    }
    int st; waitpid(c, &st, 0);
    char m1[300]; snprintf(m1, sizeof m1, "%s.1", BOOTLOG_MAIN_PATH);
    CHECK(size_of(m1) == BOOTLOG_MAIN_MAX + 10, "open_stderr: the oversize log is kept as .1, not wiped");
    CHECK(contains(BOOTLOG_MAIN_PATH, "new boot line\n") && size_of(BOOTLOG_MAIN_PATH) < 100, "open_stderr: stderr starts a fresh log");
}

int main(void){
    test_rotate_shifts_and_caps();
    test_event_line();
    test_event_is_bounded_and_rotates();
    test_event_file();
    test_dmesg();
    test_signal_note();
    test_open_stderr();
    if(fails){ printf("FAIL (%d failures)\n", fails); return 1; }
    printf("PASS (0 failures)\n");
    return 0;
}
