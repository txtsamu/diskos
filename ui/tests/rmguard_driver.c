/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Driver for tests/rmguard_test.py: links the REAL rmguard.c. The python test compiles both with -DRMGUARD_ROOTFS=...,
 * -DRMGUARD_SYSTEM=..., -DRMGUARD_FALLBACK_PARENT=... and -DRMGUARD_FALLBACK=... pointing into a temp dir, so nothing
 * under /usr/data is touched. Usage: driver <case>; it sets the case up, calls rmguard_ensure() twice and prints
 *   <first result> <second result> <PATH unchanged by the second call>\n<PATH>\n */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "rmguard.h"

static void put(const char *dir, const char *data, int mode){
    char p[300]; snprintf(p, sizeof p, "%s/rm", dir);
    int fd = open(p, O_CREAT|O_TRUNC|O_WRONLY, 0600);
    if(fd < 0 || write(fd, data, strlen(data)) != (ssize_t)strlen(data)) abort();
    if(fchmod(fd, mode) != 0 || close(fd) != 0) abort();
}

int main(int argc, char **argv){
    if(argc != 2) return 2;
    const char *c = argv[1];
    size_t n; const char *script = rmguard_script(&n);
    mkdir(RMGUARD_ROOTFS, 0755);
    setenv("PATH", "/bin:/usr/bin", 1);
    if(!strcmp(c, "root")) put(RMGUARD_ROOTFS, script, 0755);
    if(!strcmp(c, "header")) put(RMGUARD_ROOTFS, "#!/bin/sh\n# diskOS SD guard\nexec /bin/rm \"$@\"\n", 0755);
    if(!strcmp(c, "changed")){
        char *bad = malloc(n + 1); memcpy(bad, script, n + 1);
        bad[n - 2] ^= 1; put(RMGUARD_ROOTFS, bad, 0755); free(bad);
    }
    if(!strcmp(c, "nonexec")) put(RMGUARD_ROOTFS, script, 0644);
    if(!strcmp(c, "symlink")){
        char p[300]; snprintf(p, sizeof p, "%s/rm", RMGUARD_ROOTFS); symlink("/bin/rm", p);
    }
    if(!strcmp(c, "fifo")){
        char p[300]; snprintf(p, sizeof p, "%s/rm", RMGUARD_ROOTFS); mkfifo(p, 0755);
    }
    if(!strcmp(c, "fallback_changed")){
        mkdir(RMGUARD_FALLBACK_PARENT, 0755); mkdir(RMGUARD_FALLBACK, 0755);
        put(RMGUARD_FALLBACK, "#!/bin/sh\nexec /bin/rm \"$@\"\n", 0755);
    }
    if(!strcmp(c, "unwritable")){
        int fd = open(RMGUARD_FALLBACK_PARENT, O_CREAT|O_WRONLY, 0600); if(fd < 0) abort(); close(fd);
    }
    int result = rmguard_ensure();
    char *first = strdup(getenv("PATH"));
    int again = rmguard_ensure();
    printf("%d %d %d\n%s\n", result, again, !strcmp(first, getenv("PATH")), getenv("PATH"));
    free(first);
    return 0;
}
