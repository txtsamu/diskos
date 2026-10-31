/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "launcher.h"
#include "handoff.h"
#include "bootlog.h"
#include "sysconfig.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PLAYER_SD_CARD_NODE
#define PLAYER_SD_CARD_NODE "/sys/class/block/mmcblk0"
#endif

static const struct launcher_env *g_env;
void launcher_set_env(const struct launcher_env *env){ g_env = env; }

int player_config_absent_without_card(void){
    struct stat sb;
    if(stat(PLAYER_SYSCONFIG_DB, &sb) == 0 || errno != ENOENT) return 0;
    return stat(PLAYER_SD_CARD_NODE, &sb) != 0 && errno == ENOENT;
}
#ifndef LAUNCH_STATUS_PATH
#define LAUNCH_STATUS_PATH "/tmp/.diskos_launch_status"
#endif
#ifndef PLAYER_COMM_NAME
#define PLAYER_COMM_NAME "mq_player"
#endif
#ifndef BOOT_LOG_PATH
#define BOOT_LOG_PATH "/usr/data/diskos_boot.log"
#endif
#ifndef LAUNCH_DEADLINE_S
#define LAUNCH_DEADLINE_S  20
#endif

static void launch_status_invalidate(void){ unlink(LAUNCH_STATUS_PATH); }

static void launch_status_publish(int guard, int local){
    char buf[96];
    int n = snprintf(buf, sizeof buf, "guard=%d local=%d pid=%ld\n", guard, local, (long)getpid());
    if(n <= 0 || n >= (int)sizeof buf) return;
    char tmp[sizeof LAUNCH_STATUS_PATH + 16];
    if(snprintf(tmp, sizeof tmp, "%s.XXXXXX", LAUNCH_STATUS_PATH) >= (int)sizeof tmp) return;
    int fd = mkstemp(tmp);
    if(fd < 0) return;
    size_t len = (size_t)n, pos = 0;
    while(pos < len){ ssize_t w = write(fd, buf+pos, len-pos); if(w < 0 && errno == EINTR) continue; if(w <= 0) break; pos += (size_t)w; }
    int ok = (pos == len) && (fchmod(fd, 0644) == 0);
    close(fd);
    if(!ok || rename(tmp, LAUNCH_STATUS_PATH) != 0) unlink(tmp);
}
int launch_status_get(void){
    int fd = open(LAUNCH_STATUS_PATH, O_RDONLY|O_CLOEXEC);
    if(fd < 0) return -1;
    char buf[96]; ssize_t got = read(fd, buf, sizeof buf - 1); close(fd);
    if(got <= 0) return -1;
    buf[got] = 0;
    int guard = -1, local = -1, used = -1; long pid = -1;
    if(sscanf(buf, "guard=%d local=%d pid=%ld%n", &guard, &local, &pid, &used) != 3) return -1;
    if(used < 0 || guard < 0 || guard > 1 || local < 0 || local > 1 || pid <= 0) return -1;
    for(const char *t = buf + used; *t; t++) if(*t != '\n' && *t != ' ' && *t != '\t') return -1;
    char cp[64]; snprintf(cp, sizeof cp, "/proc/%ld/comm", pid);
    int cfd = open(cp, O_RDONLY|O_CLOEXEC);
    if(cfd < 0) return -1;
    char cn[32]; ssize_t cg = read(cfd, cn, sizeof cn - 1); close(cfd);
    if(cg <= 0) return -1;
    cn[cg] = 0;
    char *nl = strchr(cn, '\n'); if(nl) *nl = 0;
    if(strcmp(cn, PLAYER_COMM_NAME) != 0) return -1;
    { char sp[64]; snprintf(sp, sizeof sp, "/proc/%ld/stat", pid);
      int sfd = open(sp, O_RDONLY|O_CLOEXEC);
      if(sfd < 0) return -1;
      char sb[256]; ssize_t sg = read(sfd, sb, sizeof sb - 1); close(sfd);
      if(sg <= 0) return -1;
      sb[sg] = 0;
      const char *rp = strrchr(sb, ')');
      if(!rp || !rp[1] || !rp[2]) return -1;
      if(rp[2] == 'Z' || rp[2] == 'X') return -1;        }
    { static long pinned = 0;
      if(pinned == 0) pinned = pid;
      else if(pinned != pid) return -1; }
    return (guard == 1 && local == 1) ? 1 : 0;
}
static void boot_note(const char *msg){
    int fd = open(BOOT_LOG_PATH, O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC, 0644);
    if(fd < 0) return;
    (void)write(fd, msg, strlen(msg));
    (void)write(fd, "\n", 1);
    close(fd);
}
int player_local_mode_confirmed(void){
    if(g_env->sd_exported_to_host()) return 0;
    if(player_config_absent_without_card()) return 1;
    if(sysconfig_work_mode_reset_confirm_via(g_env->run_bounded, 8000) != 0) return 0;
    return !g_env->sd_exported_to_host();
}
void exec_stock_player(void){
    bootlog_event("launcher: exec stock player");
    if(g_env && g_env->kill_bounded) g_env->kill_bounded();
    handoff_signal_reset();
    handoff_exec("/usr/bin/mq_player", "mq_player");
}
static void launch_deadline_exec(int sig){
    (void)sig;
    exec_stock_player();
}
void player_launch_prepare(void){
    bootlog_event("launcher start");
    launch_status_invalidate();
    { struct sigaction sa; memset(&sa, 0, sizeof sa);
      sa.sa_handler = launch_deadline_exec; sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
      sigaction(SIGALRM, &sa, NULL); alarm(LAUNCH_DEADLINE_S); }

    int guard = 0, local = 0;
    for(int attempt = 0; attempt < 2; attempt++){
        if(attempt) usleep(250000);
        if(!guard) guard = g_env->rmguard_ensure();
        if(!local) local = player_local_mode_confirmed();
        if(guard && local) break;
    }
    launch_status_publish(guard, local);
    bootlog_event("launcher verdict guard=%d local=%d", guard, local);
    if(!guard)      boot_note("launcher: SD guard unavailable - stock player launched UNPROTECTED; diskOS SD features held");
    else if(!local) boot_note("launcher: local ownership unconfirmed - stock player launched; diskOS SD features held");
}
