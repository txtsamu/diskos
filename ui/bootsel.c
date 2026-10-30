/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "bootsel.h"
#include "handoff.h"
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int boot_select_is_diskos(const char *path){
    char rec[8] = {0};
    ssize_t n = -1;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if(fd < 0) return 0;
    struct stat st;
    if(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 7)
        n = read(fd, rec, sizeof rec);
    close(fd);
    return n == 7 && memcmp(rec, "diskos\n", 7) == 0;
}

void boot_select_enforce(const char *path){
    if(boot_select_is_diskos(path)) return;
    handoff_signal_reset();
    handoff_exec("/usr/bin/mq_ui", "mq_ui");
}
