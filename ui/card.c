/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "card.h"
#include "bootlog.h"
#include "rmguard.h"
#include "sdfs.h"
#include "srcswitch.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef CARD_PROC_ROOT
#define CARD_PROC_ROOT "/proc"
#endif
#ifndef CARD_MNT
#define CARD_MNT "/tmp/sdcard"
#endif
#ifndef CARD_DEV
#define CARD_DEV "/dev/mmcblk0p1"
#endif
#ifndef CARD_DEV_WHOLE
#define CARD_DEV_WHOLE "/dev/mmcblk0"
#endif
#ifndef CARD_UEVENT
#define CARD_UEVENT "/sys/block/mmcblk0/uevent"
#endif
#ifndef CARD_GADGET_DIR
#define CARD_GADGET_DIR "/sys/kernel/config/usb_gadget/storage_demo"
#endif
#ifndef CARD_LOG_PATH
#define CARD_LOG_PATH "/usr/data/coldplug.log"
#endif

static const struct card_env *E;
void card_set_env(const struct card_env *env){ E = env; }

static int card_flush(void){
    int fd = open(CARD_DEV, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -1;
    int rc = fsync(fd);
    int e = errno;
    close(fd);
    if(rc != 0) bootlog_event("card flush failed errno=%d(%s)", e, strerror(e));
    return rc;
}

int card_unmount(void){
    if(E->umount(CARD_MNT) == 0){ card_flush(); bootlog_event("card unmounted before shutdown or restart"); return 0; }
    int e = errno;
    if(e == EINVAL || e == ENOENT){ card_flush(); bootlog_event("card not mounted before shutdown or restart"); return 0; }
    bootlog_event("card busy errno=%d(%s), asking the player to release it", e, strerror(e));
    int sw = E->send_source(SRCSWITCH_MODE_USB_STORAGE);
    if(sw < 0) bootlog_event("player refused the storage switch (rc=%d), retrying the unmount", sw);
    for(int i = 0; i < 40; i++){
        E->unmount_wait();
        if(E->umount(CARD_MNT) == 0){ card_flush(); bootlog_event("card unmounted after player release in %d tries", i + 1); return 0; }
        if(errno == EINVAL || errno == ENOENT){ card_flush(); bootlog_event("card not mounted after player release"); return 0; }
    }
    if(E->umount_lazy(CARD_MNT, MNT_DETACH) == 0){ card_flush(); bootlog_event("card lazily detached, still busy errno=%d(%s)", e, strerror(e)); return 1; }
    bootlog_event("card unmount failed errno=%d(%s), continuing", errno, strerror(errno));
    return -1;
}

int card_player_guarded(void){
    DIR *d = opendir(CARD_PROC_ROOT); if(!d) return 0;
    struct dirent *e; int found = 0, ok = 1;
    while(ok && (e = readdir(d))){
        if(e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p[320], comm[64]; snprintf(p, sizeof p, "%s/%s/comm", CARD_PROC_ROOT, e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        char *got = fgets(comm, sizeof comm, f); fclose(f);
        if(!got || strcmp(comm, "mq_player\n")) continue;
        found++;
        snprintf(p, sizeof p, "%s/%s/environ", CARD_PROC_ROOT, e->d_name);
        f = fopen(p, "r"); if(!f){ ok = 0; break; }
        char env[8192]; size_t n = fread(env, 1, sizeof env - 1, f);
        int err = ferror(f); fclose(f); env[n] = 0;
        if(err || n == sizeof env - 1){ ok = 0; break; }
        int guarded = 0;
        for(size_t i = 0; i < n; i += strlen(env + i) + 1){
            if(strncmp(env + i, "PATH=", 5)) continue;
            char *path = env + i + 5, *sep = strchr(path, ':');
            if(sep) *sep = 0;
            guarded = path[0] == '/' && rmguard_dir_ok(path);
            break;
        }
        if(!guarded) ok = 0;
    }
    closedir(d);
    return found > 0 && ok;
}

int card_mounted(void){
    char p[256];
    if(snprintf(p, sizeof p, "%s/mounts", CARD_PROC_ROOT) >= (int)sizeof p) return 0;
    FILE *m = fopen(p, "r");
    if(!m) return 0;
    char line[512], dev[128], target[128]; int ok = 0;
    while(fgets(line, sizeof line, m)){
        if(sscanf(line, "%127s %127s", dev, target) != 2) continue;
        if(!strcmp(target, CARD_MNT) &&
           (!strcmp(dev, CARD_DEV) || !strcmp(dev, CARD_DEV_WHOLE))){ ok = 1; break; }
    }
    fclose(m);
    return ok;
}

static int sd_mount_utf8(const char *fstype){
    if(E->mount(CARD_DEV, CARD_MNT, fstype, 0, "iocharset=utf8") == 0) return 0;
    if(errno != EINVAL) return -1;
    return E->mount(CARD_DEV, CARD_MNT, fstype, 0, NULL);
}

static void coldplug_log(const char *msg){
    FILE *f = fopen(CARD_LOG_PATH, "a");
    if(f){ fprintf(f, "%ld %s\n", (long)time(NULL), msg); fclose(f); }
}

int card_exported_to_host(void){
    char udcp[256], fndir[256];
    if(snprintf(udcp, sizeof udcp, "%s/UDC", CARD_GADGET_DIR) >= (int)sizeof udcp) return 1;
    if(snprintf(fndir, sizeof fndir, "%s/functions", CARD_GADGET_DIR) >= (int)sizeof fndir) return 1;
    struct stat sb;
    errno = 0; int have_udc = (stat(udcp, &sb) == 0); int udc_err = (!have_udc && errno != ENOENT);
    if(udc_err) return 1;
    if(have_udc){
        FILE *f = fopen(udcp, "r");
        if(!f) return 1;
        char b[64] = {0}; size_t n = fread(b, 1, sizeof b - 1, f); int rerr = ferror(f); fclose(f);
        if(rerr) return 1;
        for(size_t i = 0; i < n; i++) if(b[i] > ' ') return 1;
    }
    errno = 0;
    DIR *d = opendir(fndir);
    if(!d) return (errno == ENOENT) ? 0 : 1;
    int exported = 0; struct dirent *e;
    while(!exported && (e = readdir(d))){
        if(strncmp(e->d_name, "mass_storage.", 13) != 0) continue;
        char lunp[300]; snprintf(lunp, sizeof lunp, "%s/%s/lun.0/file", fndir, e->d_name);
        FILE *g = fopen(lunp, "r");
        if(!g){ exported = 1; break; }
        char l[300] = {0}; size_t m = fread(l, 1, sizeof l - 1, g); int rerr = ferror(g); fclose(g);
        if(rerr || (m && strstr(l, "mmcblk0"))) exported = 1;
    }
    closedir(d);
    return exported;
}

int card_host_confirmed(void){
    char p[512], b[320];
    if(snprintf(p, sizeof p, "%s/UDC", CARD_GADGET_DIR) >= (int)sizeof p) return 0;
    FILE *f = fopen(p, "r"); if(!f) return 0;
    size_t n = fread(b, 1, sizeof b - 1, f); int err = ferror(f); fclose(f); b[n] = 0;
    int bound = 0;
    for(size_t i = 0; i < n; i++) if(b[i] > ' ') bound = 1;
    if(err || !bound) return 0;
    if(snprintf(p, sizeof p, "%s/functions", CARD_GADGET_DIR) >= (int)sizeof p) return 0;
    DIR *d = opendir(p); if(!d) return 0;
    struct dirent *e; int backed = 0;
    while((e = readdir(d))){
        if(strncmp(e->d_name, "mass_storage.", 13)) continue;
        snprintf(p, sizeof p, "%s/functions/%s/lun.0/file", CARD_GADGET_DIR, e->d_name);
        f = fopen(p, "r"); if(!f) continue;
        n = fread(b, 1, sizeof b - 1, f); err = ferror(f); fclose(f); b[n] = 0;
        while(n && (b[n-1] == '\n' || b[n-1] == '\r')) b[--n] = 0;
        if(!err && (!strcmp(b, "/dev/mmcblk0") || !strcmp(b, "/dev/mmcblk0p1"))) backed = 1;
    }
    closedir(d);
    return backed;
}

static int cold_mount_allowed(void){
    char p[256];
    if(snprintf(p, sizeof p, "%s/uptime", CARD_PROC_ROOT) >= (int)sizeof p) return 0;
    double up = -1.0; FILE *pu = fopen(p, "r");
    if(pu){ if(fscanf(pu, "%lf", &up) != 1) up = -1.0; fclose(pu); }
    return (up >= 0.0 && up <= 150.0 && !card_exported_to_host() && E->writable());
}

static int coldplug_act(int i, int direct, int *nudged){
    if(!direct || *nudged || i >= 10){
        if(direct && !*nudged) coldplug_log("direct mount failed -> falling back to the player's uevent listener (one-way: no more direct mounts)");
        *nudged = 1;
        int fd = open(CARD_UEVENT, O_WRONLY | O_CLOEXEC);
        if(fd >= 0){ ssize_t w = write(fd, "add\n", 4); (void)w; close(fd); }
        return 0;
    }
    mkdir(CARD_MNT, 0755);
    const char *pref = sdfs_detect(CARD_DEV);
    const char *fstype[2] = { "exfat", "vfat" };
    if(pref && !strcmp(pref, "vfat")){ fstype[0] = "vfat"; fstype[1] = "exfat"; }
    const char *mounted_type = NULL;
    for(int k = 0; k < 2 && !mounted_type; k++)
        if(cold_mount_allowed() && sd_mount_utf8(fstype[k]) == 0) mounted_type = fstype[k];
    if(mounted_type){
        if(card_exported_to_host()){
            if(E->umount(CARD_MNT) != 0 && E->umount_lazy(CARD_MNT, MNT_DETACH) != 0)
                coldplug_log("WARNING: post-mount export + unmount BOTH failed (card busy) - possible transient dual-access");
            else
                coldplug_log("post-mount export detected -> unmounted, host owns card (one-shot gives up)");
            return 1;
        }
        { char b[64]; snprintf(b, sizeof b, "SD direct-mounted (V2.40 %s)", mounted_type); coldplug_log(b); }
        return 1;
    }
    if(!cold_mount_allowed()){
        coldplug_log("V2.40 SD one-shot: window elapsed or card exported (SD not mounted)");
        return 1;
    }
    { char b[96]; snprintf(b, sizeof b, "SD direct-mount failed errno=%d(%s)", errno, strerror(errno)); coldplug_log(b); }
    return 0;
}

int card_coldplug_step(int i, int direct, int *nudged){
    int cold_writable = E->coldplug_begin();
    int done = 0;
    if(cold_writable && E->source_mode() == 0 && !card_exported_to_host() && !card_mounted())
        done = coldplug_act(i, direct, nudged);
    E->coldplug_end();
    return done;
}

static void *coldplug_thread(void *arg){
    (void)arg;
    const int direct_sd_mount = E->fw_direct_mount();
    if(card_mounted()) return NULL;
    coldplug_log("coldplug worker start");
    for(int w = 0; ; w++){
        int v = E->launch_status();
        if(v == 1) break;
        if(v == 0){ coldplug_log("launcher reported unprotected/unconfirmed - SD left untouched this boot"); return NULL; }
        if(w >= 40){ coldplug_log("no launcher verdict within 20s - SD left untouched this boot"); return NULL; }
        struct timespec hs = { 0, 500L*1000*1000 }; nanosleep(&hs, NULL);
    }
    int nudged = 0;
    for(int i = 0; i < 120; i++){
        if(card_mounted()){ coldplug_log("SD mounted - done"); return NULL; }
        if(card_coldplug_step(i, direct_sd_mount, &nudged)) return NULL;
        struct timespec ts = { 3, 0 }; nanosleep(&ts, NULL);
    }
    coldplug_log("coldplug TIMEOUT - SD never mounted");
    return NULL;
}

static int coldplug_should_run(void){
    char p[256];
    if(snprintf(p, sizeof p, "%s/uptime", CARD_PROC_ROOT) >= (int)sizeof p) return 0;
    double up = -1.0;
    FILE *u = fopen(p, "r");
    if(u){ if(fscanf(u, "%lf", &up) != 1) up = -1.0; fclose(u); }
    if(up < 0.0 || up > 150.0){ coldplug_log("skip: uptime unreadable or late (not a verified cold boot)"); return 0; }
    if(E->source_mode() != 0){ coldplug_log("skip: not in Local mode (card may be exported to a host)"); return 0; }
    return 1;
}

void card_coldplug_start(void){
    if(!coldplug_should_run()) return;
    pthread_t th;
    if(pthread_create(&th, NULL, coldplug_thread, NULL) == 0) pthread_detach(th);
}
