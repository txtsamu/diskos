/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PINCTRL_BASE 0x10010000UL
#define GPB_PXPIN    0x100

static long long now_ms(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int is_mountpoint(const char *dir){
    struct stat a, b;
    char parent[4096];
    if(stat(dir, &a) != 0 || !S_ISDIR(a.st_mode)) return -1;
    if(snprintf(parent, sizeof parent, "%s/..", dir) >= (int)sizeof parent) return -1;
    if(stat(parent, &b) != 0) return -1;
    return (a.st_dev != b.st_dev || a.st_ino == b.st_ino) ? 1 : 0;
}

static void child(int wfd, pid_t supervisor, const char *pref, const char *mnt, const char *memdev){
    if(prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) _exit(10);
    if(getppid() != supervisor) _exit(11);
    if(setpgid(0, 0) != 0 && getpgrp() != getpid()) _exit(12);
    int nul = open("/dev/null", O_RDWR | O_CLOEXEC);
    if(nul < 0) _exit(13);
    if(dup2(nul, 0) < 0 || dup2(nul, 1) < 0 || dup2(nul, 2) < 0) _exit(14);
    if(nul > 2) close(nul);

    char flag = 'E';
    if(is_mountpoint(mnt) == 1){
        struct stat st;
        if(lstat(pref, &st) == 0) flag = '1';
        else if(errno == ENOENT) flag = '0';
    }

    int pinrc = 1;
    uint32_t gpb = 0;
    int fd = open(memdev, O_RDONLY | O_SYNC | O_CLOEXEC);
    if(fd >= 0){
        void *map = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, (off_t)PINCTRL_BASE);
        if(map != MAP_FAILED){
            gpb = *(volatile uint32_t *)((char *)map + GPB_PXPIN);
            pinrc = 0;
            munmap(map, 4096);
        }
        close(fd);
    }

    char line[64];
    int n = pinrc == 0 ? snprintf(line, sizeof line, "%c 0 0x%08X\n", flag, (unsigned)gpb)
                       : snprintf(line, sizeof line, "%c 1 -\n", flag);
    if(n > 0 && write(wfd, line, (size_t)n) != n) _exit(1);
    _exit(0);
}

int main(int argc, char **argv){
    if(argc < 4 || argc > 5) return 2;
    char *end = NULL;
    long deadline = strtol(argv[3], &end, 10);
    if(!end || *end || deadline <= 0 || deadline > 600000) return 2;
    const char *memdev = argc == 5 ? argv[4] : "/dev/mem";

    struct sigaction dfl;
    memset(&dfl, 0, sizeof dfl);
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    if(sigaction(SIGCHLD, &dfl, NULL) != 0){ puts("X 1 -"); return 0; }

    long long t0 = now_ms();
    if(t0 < 0){ puts("X 1 -"); return 0; }
    long long t_end = t0 + deadline;
    int p[2];
    if(pipe2(p, O_CLOEXEC) != 0){ puts("X 1 -"); return 0; }
    pid_t self = getpid();
    pid_t pid = fork();
    if(pid < 0){ puts("X 1 -"); return 0; }
    if(pid == 0){ close(p[0]); child(p[1], self, argv[1], argv[2], memdev); }
    close(p[1]);
    setpgid(pid, pid);

    char buf[64];
    size_t got = 0;
    int done = 0, late = 0, failed = 0;
    while(!done){
        long long now = now_ms();
        if(now < 0){ failed = 1; break; }
        long long left = t_end - now;
        struct pollfd pf = { p[0], POLLIN, 0 };
        int r = poll(&pf, 1, left > 0 ? (int)left : 0);
        if(r < 0){ if(errno == EINTR) continue; break; }
        if(r == 0){ late = 1; break; }
        ssize_t k = read(p[0], buf + got, sizeof buf - 1 - got);
        if(k < 0){ if(errno == EINTR) continue; break; }
        if(k == 0) done = 1;
        else { got += (size_t)k; if(got >= sizeof buf - 1) break; }
    }
    {
        long long now = now_ms();
        if(now < 0) failed = 1;
        else if(now >= t_end) late = 1;
    }

    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, WNOHANG);

    buf[got] = 0;
    if(failed){ puts("X 1 -"); return 0; }
    if(late){ puts("T 1 -"); return 0; }
    if(!done || got == 0 || buf[got - 1] != '\n' || memchr(buf, '\n', got) != buf + got - 1){
        puts("X 1 -");
        return 0;
    }
    fputs(buf, stdout);
    return 0;
}
