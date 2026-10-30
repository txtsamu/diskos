/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include "launcher.h"
#include "handoff.h"

static int hang_guard=0, kills=0;
static int fake_execv(const char *path, char *const argv[]){
    assert(!strcmp(path, "/usr/bin/mq_player"));
    assert(argv[0] && !strcmp(argv[0], "mq_player") && !argv[1]);
    assert(kills >= 1);
    sigset_t cur; sigprocmask(SIG_BLOCK, NULL, &cur);
    assert(!sigismember(&cur, SIGALRM));
    struct sigaction sa; sigaction(SIGALRM, NULL, &sa);
    assert(sa.sa_handler == SIG_DFL);
    assert(alarm(0) == 0);
    puts("PLAYER_EXEC_REACHED"); fflush(stdout);
    _exit(0);
}
static int guard=1, exported=0, late_export=0, reset_rc=0, probes=0, resets=0;
static int transient=0;
static int fake_rmguard_ensure(void){ if(hang_guard) for(;;) pause(); return guard; }
static int fake_sd_exported_to_host(void){ probes++; return exported || (late_export && probes>1); }
static int fake_run_bounded(char *const argv[], int timeout){
    resets++;
    assert(timeout == 8000);
    assert(!strcmp(argv[0], "sh") && !strcmp(argv[1], "-c"));
    assert(strstr(argv[2], "UPDATE SYSCONFIG SET WORK_MODE=0"));
    assert(strstr(argv[2], "SELECT WORK_MODE FROM SYSCONFIG WHERE ID=1"));
    assert(strstr(argv[2], PLAYER_SYSCONFIG_DB));
    if(transient && resets==1) return -1;
    return reset_rc;
}
static void fake_kill_bounded(void){ kills++; }
static const struct launcher_env env = {
    .sd_exported_to_host = fake_sd_exported_to_host, .run_bounded = fake_run_bounded,
    .rmguard_ensure = fake_rmguard_ensure, .kill_bounded = fake_kill_bounded,
};

int main(int argc, char **argv){
    launcher_set_env(&env);
    handoff_execv = fake_execv;
    if(argc > 1 && !strcmp(argv[1], "firstrun")){ printf("%d\n", player_config_absent_without_card()); return 0; }
    int fresh = argc > 1 && !strcmp(argv[1], "fresh");
    if(fresh) unlink(PLAYER_SYSCONFIG_DB);
    else { FILE *d = fopen(PLAYER_SYSCONFIG_DB, "w"); assert(d); fclose(d); }
    if(argc > 1){
        if(!strcmp(argv[1], "guard")) guard=0;
        else if(!strcmp(argv[1], "host")) exported=1;
        else if(!strcmp(argv[1], "reset")) reset_rc=-1;
        else if(!strcmp(argv[1], "late")) late_export=1;
        else if(!strcmp(argv[1], "transient")) transient=1;
        else if(!strcmp(argv[1], "hang")) hang_guard=1;
        else if(!strcmp(argv[1], "blockedlog")) guard=0;
        player_launch_prepare();
        assert(launch_status_get() == (guard && !exported && (reset_rc==0) && !late_export));
        exec_stock_player();
        assert(0 && "exec_stock_player must never return");
    }
    guard=0; assert(!fake_rmguard_ensure() && resets==0);
    guard=1; exported=1; probes=0; assert(!player_local_mode_confirmed() && resets==0);
    exported=0; reset_rc=-1; probes=0; assert(!player_local_mode_confirmed() && resets==1);
    reset_rc=0; late_export=1; probes=0; assert(!player_local_mode_confirmed() && resets==2);
    late_export=0; probes=0; assert(player_local_mode_confirmed() && resets==3 && probes==2);
    puts("PASS actual host ownership, reset failure and late export are reported independently");
    {   FILE *f;
        pid_t dead = fork();
        if(dead == 0) _exit(0);
        waitpid(dead, NULL, 0);
        f=fopen(LAUNCH_STATUS_PATH,"w"); assert(f); fprintf(f,"guard=1 local=1 pid=%ld\n",(long)getpid()); fclose(f);
        assert(launch_status_get()==1);
        f=fopen(LAUNCH_STATUS_PATH,"w"); assert(f); fprintf(f,"guard=10 local=10 pid=%ld\n",(long)getpid()); fclose(f);
        assert(launch_status_get()==-1);
        f=fopen(LAUNCH_STATUS_PATH,"w"); assert(f); fprintf(f,"guard=1 local=1 pid=%ld\n",(long)dead); fclose(f);
        assert(launch_status_get()==-1);
        f=fopen(LAUNCH_STATUS_PATH,"w"); assert(f); fprintf(f,"nonsense\n"); fclose(f);
        assert(launch_status_get()==-1);
        f=fopen(LAUNCH_STATUS_PATH,"w"); assert(f); fprintf(f,"guard=1 local=1 pid=%ldjunk\n",(long)getpid()); fclose(f);
        assert(launch_status_get()==-1);
        unlink(LAUNCH_STATUS_PATH);
        assert(launch_status_get()==-1);
        f=fopen(LAUNCH_STATUS_PATH,"w"); assert(f); fprintf(f,"guard=1 local=1 pid=%ld\n",(long)getpid()); fclose(f);
        assert(launch_status_get()==1);
        f=fopen(LAUNCH_STATUS_PATH,"w"); assert(f); fprintf(f,"guard=1 local=1 pid=%ld\n",(long)getppid()); fclose(f);
        assert(launch_status_get()==-1);
        unlink(LAUNCH_STATUS_PATH);
        puts("PASS stale, malformed, junk-suffixed and foreign-pid verdicts are all rejected");
    }
    return 0;
}
