/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "timekeep.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int in_range(long long t){ return t >= TIMEKEEP_FLOOR && t < TIMEKEEP_CEIL; }

time_t timekeep_load(const char *path){
    FILE *f = fopen(path, "r");
    if(!f) return 0;
    char b[48] = "";
    char *got = fgets(b, sizeof b, f);
    fclose(f);
    if(!got) return 0;
    char *end = NULL;
    errno = 0;
    long long v = strtoll(b, &end, 10);
    if(errno || end == b || (*end != '\n' && *end != 0)) return 0;
    return in_range(v) ? (time_t)v : 0;
}

int timekeep_save(const char *path, time_t t){
    if(!in_range((long long)t)) return -1;
    char tmp[256];
    if(snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) return -1;
    char b[32];
    int n = snprintf(b, sizeof b, "%lld\n", (long long)t);
    int ok = write(fd, b, (size_t)n) == n && fsync(fd) == 0;
    ok = (close(fd) == 0) && ok;
    if(!ok || rename(tmp, path) != 0){ unlink(tmp); return -1; }
    return 0;
}

time_t timekeep_pick(time_t now, time_t saved){
    if(!in_range((long long)saved)) return 0;
    return (long long)now + TIMEKEEP_TOL_S < (long long)saved ? saved : 0;
}

time_t timekeep_rtc_epoch(const char *dev){
    int fd = open(dev, O_RDONLY);
    if(fd < 0) return 0;
    struct rtc_time r;
    memset(&r, 0, sizeof r);
    int rc = ioctl(fd, RTC_RD_TIME, &r);
    close(fd);
    if(rc != 0) return 0;
    struct tm tm; memset(&tm, 0, sizeof tm);
    tm.tm_year = r.tm_year; tm.tm_mon = r.tm_mon; tm.tm_mday = r.tm_mday;
    tm.tm_hour = r.tm_hour; tm.tm_min = r.tm_min; tm.tm_sec = r.tm_sec;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    return (t == (time_t)-1 || t <= 0) ? 0 : t;
}
