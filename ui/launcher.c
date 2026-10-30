/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "launcher.h"
#include "handoff.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Paths the launcher reads, overridable by a host test (like SD_EXPORT_MARKER in srcswitch.c). */
#ifndef PLAYER_SYSCONFIG_DB
#define PLAYER_SYSCONFIG_DB "/usr/data/fiio/db/sysconfig.db"
#endif
#ifndef PLAYER_SD_CARD_NODE
#define PLAYER_SD_CARD_NODE "/sys/class/block/mmcblk0"
#endif

static const struct launcher_env *g_env;
void launcher_set_env(const struct launcher_env *env){ g_env = env; }

int player_config_absent_without_card(void){
    struct stat sb;
    if(stat(PLAYER_SYSCONFIG_DB, &sb) == 0 || errno != ENOENT) return 0;
    /* Let stock create its full default schema only with the card removed.
     * A corrupt/unreadable existing database never takes this first-run path. */
    return stat(PLAYER_SD_CARD_NODE, &sb) != 0 && errno == ENOENT;
}
/* ---- launch outcome, published by the player launcher for the UI ------------------------------
 * On tmpfs, so its lifetime is exactly one boot and it can never carry a stale success into the next.
 * The UI treats "absent" and "unreadable" as NOT safe, so a launcher that never got to publish (crash,
 * deadline, read-only /tmp) keeps diskOS's own SD features off without ever delaying the player. */
#ifndef LAUNCH_STATUS_PATH
#define LAUNCH_STATUS_PATH "/tmp/.diskos_launch_status"
#endif
#ifndef PLAYER_COMM_NAME
#define PLAYER_COMM_NAME "mq_player"   /* what the launcher becomes after exec; overridden by tests */
#endif
#ifndef BOOT_LOG_PATH
#define BOOT_LOG_PATH "/usr/data/diskos_boot.log"
#endif
#ifndef LAUNCH_DEADLINE_S
#define LAUNCH_DEADLINE_S  20      /* hard cap on the WHOLE preparation path before we exec stock */
#endif

/* Invalidate any record from an EARLIER player before we start preparing. Without this, player A's
 * success stays readable while player B is still deciding - and a crash, a deadline or a failed publish
 * would leave A's success standing in for B. */
static void launch_status_invalidate(void){ unlink(LAUNCH_STATUS_PATH); }

static void launch_status_publish(int guard, int local){
    char buf[96];
    int n = snprintf(buf, sizeof buf, "guard=%d local=%d pid=%ld\n", guard, local, (long)getpid());
    if(n <= 0 || n >= (int)sizeof buf) return;
    char tmp[sizeof LAUNCH_STATUS_PATH + 16];
    if(snprintf(tmp, sizeof tmp, "%s.XXXXXX", LAUNCH_STATUS_PATH) >= (int)sizeof tmp) return;
    int fd = mkstemp(tmp);              /* unique: a shared name is unsafe with overlapping writers */
    if(fd < 0) return;
    size_t len = (size_t)n, pos = 0;
    while(pos < len){ ssize_t w = write(fd, buf+pos, len-pos); if(w < 0 && errno == EINTR) continue; if(w <= 0) break; pos += (size_t)w; }
    int ok = (pos == len) && (fchmod(fd, 0644) == 0);
    close(fd);
    if(!ok || rename(tmp, LAUNCH_STATUS_PATH) != 0) unlink(tmp);
}
/* 1 = THIS player's launch was confirmed; 0 = it was reported unprotected/unconfirmed; -1 = not published
 * yet, or published by a player that is no longer the one running.
 *
 * tmpfs gives the record a BOOT lifetime, which is not the same as a PLAYER-GENERATION lifetime: the
 * firmware watchdog can respawn the stock player directly, bypassing our launcher entirely, and that
 * player inherits no verdict. So the recorded pid is verified to still be the live mq_player. Parsing is
 * strict - a substring match would accept "guard=10". */
int launch_status_get(void){
    int fd = open(LAUNCH_STATUS_PATH, O_RDONLY|O_CLOEXEC);
    if(fd < 0) return -1;
    char buf[96]; ssize_t got = read(fd, buf, sizeof buf - 1); close(fd);
    if(got <= 0) return -1;
    buf[got] = 0;
    int guard = -1, local = -1, used = -1; long pid = -1;
    /* %n gives the consumed length, so trailing junk ("pid=123junk") is rejected rather than ignored. */
    if(sscanf(buf, "guard=%d local=%d pid=%ld%n", &guard, &local, &pid, &used) != 3) return -1;
    if(used < 0 || guard < 0 || guard > 1 || local < 0 || local > 1 || pid <= 0) return -1;
    for(const char *t = buf + used; *t; t++) if(*t != '\n' && *t != ' ' && *t != '\t') return -1;
    /* identity: the launcher execs INTO the stock player, so the recorded pid is the player's pid */
    char cp[64]; snprintf(cp, sizeof cp, "/proc/%ld/comm", pid);
    int cfd = open(cp, O_RDONLY|O_CLOEXEC);
    if(cfd < 0) return -1;                       /* that player is gone -> no verdict applies */
    char cn[32]; ssize_t cg = read(cfd, cn, sizeof cn - 1); close(cfd);
    if(cg <= 0) return -1;
    cn[cg] = 0;
    char *nl = strchr(cn, '\n'); if(nl) *nl = 0;
    if(strcmp(cn, PLAYER_COMM_NAME) != 0) return -1;  /* pid reused by something else */
    /* A zombie keeps its name, so it would otherwise pass as a live player. */
    { char sp[64]; snprintf(sp, sizeof sp, "/proc/%ld/stat", pid);
      int sfd = open(sp, O_RDONLY|O_CLOEXEC);
      if(sfd < 0) return -1;
      char sb[256]; ssize_t sg = read(sfd, sb, sizeof sb - 1); close(sfd);
      if(sg <= 0) return -1;
      sb[sg] = 0;
      const char *rp = strrchr(sb, ')');                 /* comm can contain spaces; state follows ") " */
      if(!rp || !rp[1] || !rp[2]) return -1;
      if(rp[2] == 'Z' || rp[2] == 'X') return -1;        /* zombie/dead is not a running player */ }
    /* PIN the generation: once a verdict has been accepted, a DIFFERENT player may not inherit it.
     * (A replacement writes its own record, or none at all if the firmware watchdog respawned stock
     * directly - either way this rejects it and the gate timer revokes.) */
    { static long pinned = 0;
      if(pinned == 0) pinned = pid;
      else if(pinned != pid) return -1; }
    return (guard == 1 && local == 1) ? 1 : 0;
}
/* Bounded, best-effort APPEND to the boot log. Usable before stderr is redirected (the launcher runs
 * long before that) and it never truncates, so a launcher note survives the UI opening the same file. */
static void boot_note(const char *msg){
    int fd = open(BOOT_LOG_PATH, O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC, 0644);
    if(fd < 0) return;
    (void)write(fd, msg, strlen(msg));
    (void)write(fd, "\n", 1);
    close(fd);
}
/* Local ownership confirmed? Deliberately SEPARATE from the guard check: the two failures are
 * independent, mean different things, and are reported independently (a single boolean lost that). */
int player_local_mode_confirmed(void){
    /* Check actual host ownership; a saved database mode is not a detach acknowledgement. */
    if(g_env->sd_exported_to_host()) return 0;
    if(player_config_absent_without_card()) return 1;
    /* Fresh, bounded reset and read-back in the launcher itself. An old marker from
     * another UI instance cannot authorize this launch, and elapsed time is not success. */
    char *a[] = { "sh", "-c",
        "[ -f " PLAYER_SYSCONFIG_DB " ] && "
        "sqlite3 -cmd '.timeout 3000' " PLAYER_SYSCONFIG_DB " 'UPDATE SYSCONFIG SET WORK_MODE=0 WHERE ID=1 AND WORK_MODE IS NOT 0' && "
        "[ \"$(sqlite3 -cmd '.timeout 3000' " PLAYER_SYSCONFIG_DB " 'SELECT WORK_MODE FROM SYSCONFIG WHERE ID=1')\" = \"0\" ]", NULL };
    if(g_env->run_bounded(a, 8000) != 0) return 0;
    return !g_env->sd_exported_to_host();
}
/* The ONE place that hands off to the stock player. Kills any preparation child first: a shell or
 * sqlite left running would otherwise keep mutating state after stock has started. */
void exec_stock_player(void){
    if(g_env && g_env->kill_bounded) g_env->kill_bounded();   /* the group and the child itself, grouped or not */
    handoff_signal_reset();
    handoff_exec("/usr/bin/mq_player", "mq_player");   /* exec failed -> _exit(127): let the watchdog respawn stock */
}
/* Deadline handler. Deliberately does NO I/O: a write to the boot log or to stderr can itself block on
 * the very storage that wedged preparation, which would defeat the whole point of having a deadline. */
static void launch_deadline_exec(int sig){
    (void)sig;
    exec_stock_player();
}
/* HARD RULE (owner constraint, 2026-09-23): this must NEVER prevent the stock player from running.
 * diskOS may switch off its OWN features; it may not take away the user's player or their route back
 * to stock firmware. The earlier version ended in for(;;)pause() while keeping the name mq_player, so
 * the watchdog saw a live player and never respawned stock - a transient database lock could therefore
 * deny the device a player until it was reflashed, and it also broke the "Default UI = Stock" and
 * hold-Vol-Up recovery paths, which only redirect the UI and still route the PLAYER through us.
 *
 * Now: bounded attempts under an overall deadline, publish the outcome for the UI, always fall through
 * to the stock exec. An unprotected launch is NOT claimed to be safe - it is unprotected recovery, and
 * the UI says so - but it is strictly the user's device to run. */
void player_launch_prepare(void){
    launch_status_invalidate();            /* no earlier player's verdict may stand in for this one */
    { struct sigaction sa; memset(&sa, 0, sizeof sa);
      sa.sa_handler = launch_deadline_exec; sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;                     /* no SA_RESTART: blocking syscalls return EINTR */
      sigaction(SIGALRM, &sa, NULL); alarm(LAUNCH_DEADLINE_S); }

    int guard = 0, local = 0;
    for(int attempt = 0; attempt < 2; attempt++){
        if(attempt) usleep(250000);        /* one retry for a transient db lock / startup overlap */
        if(!guard) guard = g_env->rmguard_ensure();
        if(!local) local = player_local_mode_confirmed();
        if(guard && local) break;
    }
    /* The diagnostics stay INSIDE the deadline. They touch NAND, so a stalled write here would
     * otherwise recreate exactly the failure this deadline exists to prevent: a live launcher that
     * never reaches the stock player. If the alarm fires during them, the handler execs stock and the
     * missing note is the acceptable loss. alarm(0) happens in exec_stock_player(), not here. */
    launch_status_publish(guard, local);
    if(!guard)      boot_note("launcher: SD guard unavailable - stock player launched UNPROTECTED; diskOS SD features held");
    else if(!local) boot_note("launcher: local ownership unconfirmed - stock player launched; diskOS SD features held");
}
