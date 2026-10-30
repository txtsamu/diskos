/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "handoff.h"
#include <signal.h>
#include <unistd.h>

int (*handoff_execv)(const char *path, char *const argv[]) = execv;

void handoff_signal_reset(void){
    alarm(0);
    signal(SIGALRM, SIG_IGN);
    sigset_t s; sigemptyset(&s); sigaddset(&s, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &s, NULL);
    signal(SIGALRM, SIG_DFL);
}

void handoff_exec(const char *path, const char *argv0){
    char *pa[2]; pa[0] = (char*)argv0; pa[1] = NULL;
    handoff_execv(path, pa);
    _exit(127);
}
