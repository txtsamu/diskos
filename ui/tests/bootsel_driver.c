/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Driver for tests/boot_select_test.py: links the REAL bootsel.c and handoff.c, fakes only the exec (handoff_execv).
 *   driver read <path>      prints 1 or 0: boot_select_is_diskos(path)
 *   driver enforce <path>   runs boot_select_enforce(path); EXEC_FAILS=1 makes the fake exec fail.
 * The fake exec reports whether the signal state had been normalised by then (no pending alarm, SIGALRM default and not
 * blocked), which is what handoff_signal_reset() is for. */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bootsel.h"
#include "handoff.h"

static int fake_execv(const char *p, char *const v[]){
    sigset_t cur; sigprocmask(SIG_BLOCK, NULL, &cur);
    struct sigaction sa; sigaction(SIGALRM, NULL, &sa);
    int reset = alarm(0) == 0 && sa.sa_handler == SIG_DFL && !sigismember(&cur, SIGALRM);
    printf("EXEC %s %s reset=%d\n", p, v[0], reset); fflush(stdout);
    return getenv("EXEC_FAILS") ? -1 : (_exit(0), 0);
}

int main(int argc, char **argv){
    if(argc != 3) return 2;
    handoff_execv = fake_execv;
    if(!strcmp(argv[1], "read")){ printf("%d\n", boot_select_is_diskos(argv[2])); return 0; }
    /* the state a running diskOS is in: a pending watchdog timer, SIGALRM ignored and blocked */
    sigset_t b; sigemptyset(&b); sigaddset(&b, SIGALRM); sigprocmask(SIG_BLOCK, &b, NULL);
    signal(SIGALRM, SIG_IGN);
    alarm(60);
    boot_select_enforce(argv[2]);
    printf("FELL_THROUGH_TO_DISKOS\n");
    return 0;
}
