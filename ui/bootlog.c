/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bootlog.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LINE_MAX_BYTES 512

static void numbered(char *out, size_t cap, const char *path, int n){ snprintf(out, cap, "%s.%d", path, n); }

void bootlog_rotate(const char *path, long max, int keep){
    struct stat st;
    if(keep < 1 || stat(path, &st) != 0 || st.st_size <= max) return;
    char from[300], to[300];
    numbered(to, sizeof to, path, keep);
    unlink(to);
    for(int n = keep - 1; n >= 1; n--){
        numbered(from, sizeof from, path, n);
        numbered(to, sizeof to, path, n + 1);
        rename(from, to);
    }
    numbered(to, sizeof to, path, 1);
    rename(path, to);
}

void bootlog_open_stderr(void){
    bootlog_rotate(BOOTLOG_MAIN_PATH, BOOTLOG_MAIN_MAX, BOOTLOG_KEEP);
    freopen(BOOTLOG_MAIN_PATH, "a", stderr);
}

static long uptime_ds(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (long)ts.tv_sec * 10 + ts.tv_nsec / 100000000L;
}

static void append_line(const char *path, long max, const char *line, size_t len){
    bootlog_rotate(path, max, BOOTLOG_KEEP);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if(fd < 0) return;
    (void)!write(fd, line, len);
    close(fd);
}

void bootlog_event(const char *fmt, ...){
    char line[LINE_MAX_BYTES];
    long up = uptime_ds();
    int n = snprintf(line, sizeof line, "u=%ld.%ld t=%ld pid=%ld ", up / 10, up % 10, (long)time(NULL), (long)getpid());
    if(n < 0 || n >= (int)sizeof line) return;
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
    va_end(ap);
    if(m < 0) return;
    size_t len = (size_t)n + (size_t)m;
    if(len > sizeof line - 2) len = sizeof line - 2;
    line[len++] = '\n';
    append_line(BOOTLOG_JOURNAL_PATH, BOOTLOG_JOURNAL_MAX, line, len);
}

void bootlog_event_file(const char *label, const char *path){
    char buf[200];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t got = fd >= 0 ? read(fd, buf, sizeof buf - 1) : -1;
    if(fd >= 0) close(fd);
    if(got <= 0){ bootlog_event("%s: unreadable", label); return; }
    buf[got] = 0;
    for(ssize_t i = 0; i < got; i++) if(buf[i] == '\n') buf[i] = ' ';
    bootlog_event("%s: %s", label, buf);
}

void bootlog_dmesg(const char *why, int lines){
    if(lines < 1) return;
    char cmd[200];
    snprintf(cmd, sizeof cmd, "%s | tail -n %d", BOOTLOG_DMESG_CMD, lines);
    FILE *p = popen(cmd, "r");
    if(!p) return;
    bootlog_rotate(BOOTLOG_DMESG_PATH, BOOTLOG_DMESG_MAX, BOOTLOG_KEEP);
    FILE *o = fopen(BOOTLOG_DMESG_PATH, "a");
    if(o){
        fprintf(o, "=== dmesg tail (%s) u=%ld t=%ld ===\n", why, uptime_ds() / 10, (long)time(NULL));
        char l[300];
        while(fgets(l, sizeof l, p)) fputs(l, o);
        fclose(o);
    }
    pclose(p);
}

void bootlog_signal_note(const char *msg, int len){
    int fd = open(BOOTLOG_JOURNAL_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(fd < 0) return;
    (void)!write(fd, msg, (size_t)len);
    close(fd);
}
