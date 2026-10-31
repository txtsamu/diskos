/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "sdio.h"
#include "art.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/syscall.h>

static int64_t art_now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec*1000LL + t.tv_nsec/1000000LL; }

#ifndef ARTDEC_PATH
#define ARTDEC_PATH "/opt/diskos/bin/diskos-artdec"
#endif
static uint32_t le32_(const unsigned char *p){ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
int art_bmp_valid(const char *path, int n){
    uint32_t stride = ((uint32_t)n * 3 + 3) & ~3u, dsz = stride * (uint32_t)n, want = 54 + dsz;
    unsigned char h[54];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return 0;
    struct stat st;
    int ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && (uint64_t)st.st_size == want && read(fd, h, 54) == 54 &&
             h[0] == 'B' && h[1] == 'M' && le32_(h + 2) == want && le32_(h + 6) == 0 && le32_(h + 10) == 54 &&
             le32_(h + 14) == 40 && le32_(h + 18) == (uint32_t)n && le32_(h + 22) == (uint32_t)n &&
             (h[26] | h[27] << 8) == 1 && (h[28] | h[29] << 8) == 24 && le32_(h + 30) == 0 &&
             (le32_(h + 34) == dsz || le32_(h + 34) == 0);
    close(fd);
    return ok;
}
struct art_dirent64 { uint64_t ino; int64_t off; unsigned short reclen; unsigned char type; char name[]; };
static int art_child_close_fds(int fallback_max, int fallback_complete){
    long dfd;
    do dfd = syscall(SYS_openat, AT_FDCWD, "/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    while(dfd < 0 && errno == EINTR);
    if(dfd < 0){
        for(int fd = 3; fd < fallback_max; fd++) close(fd);
        return fallback_complete ? 0 : -1;
    }
    char buf[2048];
    for(;;){
        long n = syscall(SYS_getdents64, (int)dfd, buf, sizeof buf);
        if(n < 0 && errno == EINTR) continue;
        if(n < 0){ close((int)dfd); return -1; }
        if(n == 0) break;
        for(long off = 0; off < n;){
            struct art_dirent64 *d = (struct art_dirent64 *)(buf + off);
            if(d->reclen == 0) { close((int)dfd); return -1; }
            int v = 0, ok = d->name[0] != 0;
            for(const char *c = d->name; *c; c++){ if(*c < '0' || *c > '9'){ ok = 0; break; } v = v * 10 + (*c - '0'); }
            if(ok && v >= 3 && v != (int)dfd) close(v);
            off += d->reclen;
        }
    }
    close((int)dfd);
    return 0;
}
static pthread_mutex_t g_pid_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile pid_t  g_live_pid = 0;
#define ART_MAX_CHILDREN 8
static volatile pid_t g_all_pids[ART_MAX_CHILDREN];
static int art_child_slot_free(void){
    for(int i = 0; i < ART_MAX_CHILDREN; i++) if(g_all_pids[i] == 0) return 1;
    return 0;
}
static void art_child_add(pid_t p){
    for(int i = 0; i < ART_MAX_CHILDREN; i++) if(g_all_pids[i] == 0){ g_all_pids[i] = p; return; }
}
static void art_child_del_locked(pid_t p){
    for(int i = 0; i < ART_MAX_CHILDREN; i++) if(g_all_pids[i] == p){ g_all_pids[i] = 0; break; }
    if(g_live_pid == p) g_live_pid = 0;
}
static void art_reap_leftovers_locked(void){
    for(int i = 0; i < ART_MAX_CHILDREN; i++){
        pid_t p = g_all_pids[i];
        if(p > 0 && waitpid(p, NULL, WNOHANG) == p) art_child_del_locked(p);
    }
}
static volatile unsigned g_cancel_gen = 0;
static volatile unsigned g_kill_gen = 0;
static __thread int      t_kill_armed = 0;
static __thread unsigned t_kill0 = 0;
static __thread int      t_kill_preset_valid = 0;
static __thread unsigned t_kill_preset = 0;

void art_cancel(void){
    pthread_mutex_lock(&g_pid_mu);
    g_cancel_gen++;
    pid_t p = g_live_pid;
    if(p > 0) kill(-p, SIGKILL);
    pthread_mutex_unlock(&g_pid_mu);
}
void art_kill_all(void){
    pthread_mutex_lock(&g_pid_mu);
    g_cancel_gen++;
    g_kill_gen++;
    for(int i = 0; i < ART_MAX_CHILDREN; i++){
        pid_t p = g_all_pids[i];
        if(p > 0){ kill(-p, SIGKILL); kill(p, SIGKILL); }
    }
    pthread_mutex_unlock(&g_pid_mu);
}

static __thread int t_art_exit = -1;
int art_last_exit(void){ return t_art_exit; }
static int art_run_helper(const char *input, const char *saver_raw, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0,
                    int64_t deadline_ms){
    t_art_exit = -1;
    if(art_now_ms() >= deadline_ms) return -1;
    char stage[64]; snprintf(stage, sizeof stage, "/tmp/.diskos-artdec-XXXXXX");
    if(!mkdtemp(stage)) return -1;
    char sc[96], st_[96], sb[96], sv[96];
    snprintf(sc, sizeof sc, "%s/c.bmp", stage); snprintf(st_, sizeof st_, "%s/t.bmp", stage); snprintf(sb, sizeof sb, "%s/b.bmp", stage);
    snprintf(sv, sizeof sv, "%s/v.bgra", stage);
    char *argv_art[] = { "diskos-artdec", "art", (char *)input, stage, NULL };
    char *argv_sav[] = { "diskos-artdec", "saver", (char *)input, sv, NULL };
    char **argv = saver_raw ? argv_sav : argv_art;
    struct rlimit nof; int have_lim = getrlimit(RLIMIT_NOFILE, &nof) == 0;
    int maxfd = (have_lim && nof.rlim_cur <= 65536) ? (int)nof.rlim_cur : 65536;
    int fallback_complete = have_lim && nof.rlim_cur <= 65536;

    pthread_mutex_lock(&g_pid_mu);
    int ret = -1;
    if(cancellable && g_cancel_gen != gen0){ pthread_mutex_unlock(&g_pid_mu); ret = -2; goto cleanup; }
    if(t_kill_armed && g_kill_gen != t_kill0){ pthread_mutex_unlock(&g_pid_mu); ret = -2; goto cleanup; }
    if(!art_child_slot_free()){ pthread_mutex_unlock(&g_pid_mu); goto cleanup; }
    pid_t pid = fork();
    if(pid < 0){ pthread_mutex_unlock(&g_pid_mu); goto cleanup; }
    if(pid == 0){
        setpgid(0, 0);
        if(art_child_close_fds(maxfd, fallback_complete) != 0) _exit(126);
        execv(ARTDEC_PATH, argv);
        _exit(127);
    }
    setpgid(pid, pid);
    if(cancellable) g_live_pid = pid;
    art_child_add(pid);
    pthread_mutex_unlock(&g_pid_mu);
    int status = 0, killed = 0; pid_t w = 0;
    for(;;){
        if(art_now_ms() >= deadline_ms){
            pthread_mutex_lock(&g_pid_mu); kill(-pid, SIGKILL); kill(pid, SIGKILL); pthread_mutex_unlock(&g_pid_mu);
            killed = 1;
            int64_t rdl = art_now_ms() + 1000;
            int reaped = 0;
            while(art_now_ms() < rdl){
                pthread_mutex_lock(&g_pid_mu);
                if(waitpid(pid,&status,WNOHANG)==pid){ art_child_del_locked(pid); reaped=1; }
                pthread_mutex_unlock(&g_pid_mu);
                if(reaped) break;
                usleep(10*1000);
            }
            if(!reaped) sd_io_fault();
            break;
        }
        pthread_mutex_lock(&g_pid_mu);
        w = waitpid(pid, &status, WNOHANG);
        if(w == pid) art_child_del_locked(pid);
        pthread_mutex_unlock(&g_pid_mu);
        if(w == pid) break;
        if(w < 0 && errno == EINTR) continue;
        if(w < 0){
            pthread_mutex_lock(&g_pid_mu); art_child_del_locked(pid); pthread_mutex_unlock(&g_pid_mu);
            sd_io_fault(); killed = 1; break;
        }
        usleep(20*1000);
    }
    if(killed) goto cleanup;
    if(WIFEXITED(status)) t_art_exit = WEXITSTATUS(status);
    if(!WIFEXITED(status) || WEXITSTATUS(status) != 0) goto cleanup;
    if(saver_raw){
        struct stat vs;
        if(stat(sv, &vs) != 0 || !S_ISREG(vs.st_mode) || vs.st_size != 360 * 360 * 4) goto cleanup;
    } else if(!art_bmp_valid(sc, 148) || !art_bmp_valid(st_, 42) || !art_bmp_valid(sb, 360)) goto cleanup;
    pthread_mutex_lock(&g_pid_mu);
    if((cancellable && g_cancel_gen != gen0) || (t_kill_armed && g_kill_gen != t_kill0)) ret = -2;
    else if(art_now_ms() >= deadline_ms) ret = -1;
    else if(saver_raw) ret = rename(sv, saver_raw) == 0 ? 0 : -1;
    else if(rename(sc, cover_bmp) != 0) ret = -1;
    else if(rename(st_, thumb_bmp) != 0){ unlink(cover_bmp); ret = -1; }
    else if(rename(sb, backdrop_bmp) != 0){ unlink(cover_bmp); unlink(thumb_bmp); ret = -1; }
    else ret = 0;
    pthread_mutex_unlock(&g_pid_mu);
cleanup:
    unlink(sc); unlink(st_); unlink(sb); unlink(sv);
    rmdir(stage);
    return ret;
}

static int art_run_pipeline(const char *input, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0,
                    int64_t deadline_ms){
    return art_run_helper(input, NULL, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0, deadline_ms);
}

static const char *ART_COVER_NAMES[] = {
    "cover.jpg","folder.jpg","cover.jpeg","folder.jpeg","cover.png","folder.png","cover.bmp","folder.bmp",
    "Cover.jpg","Folder.jpg","AlbumArt.jpg","albumart.jpg",
};
static const unsigned ART_COVER_N = sizeof ART_COVER_NAMES / sizeof ART_COVER_NAMES[0];
void art_sidecar_signature(const char *track, uint64_t *h){
    const char *slash = strrchr(track, '/');
    if(!slash) return;
    size_t dlen = (size_t)(slash - track);
    if(dlen == 0 || dlen > 900) return;
    for(unsigned i = 0; i < ART_COVER_N; i++){
        char cov[1024]; struct stat st;
        int n = snprintf(cov, sizeof cov, "%.*s/%s", (int)dlen, track, ART_COVER_NAMES[i]);
        if(n <= 0 || n >= (int)sizeof cov || stat(cov, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        uint64_t v[3] = { i + 1u, (uint64_t)st.st_size, (uint64_t)st.st_mtime };
        for(int k = 0; k < 3; k++) for(int b = 0; b < 8; b++){ *h ^= (v[k] >> (b * 8)) & 0xff; *h *= 1099511628211ULL; }
    }
}

unsigned art_cancel_gen(void){
    unsigned g; pthread_mutex_lock(&g_pid_mu); g = g_cancel_gen; pthread_mutex_unlock(&g_pid_mu); return g;
}

static int art_make_all_ex_gen_leased(const char *track, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0){
    int64_t deadline = art_now_ms() + 15000;
    if(t_kill_preset_valid){ t_kill0 = t_kill_preset; t_kill_preset_valid = 0; }
    else { pthread_mutex_lock(&g_pid_mu); t_kill0 = g_kill_gen; pthread_mutex_unlock(&g_pid_mu); }
    t_kill_armed = 1;
    int r = art_run_pipeline(track, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0, deadline);
    if(r == 0){ t_kill_armed = 0; return 0; }
    if(r == -2){ t_kill_armed = 0; return -1; }

    const char *slash = strrchr(track, '/');
    if(!slash){ t_kill_armed = 0; return -1; }
    size_t dlen = (size_t)(slash - track);
    if(dlen == 0 || dlen > 900){ t_kill_armed = 0; return -1; }
    for(unsigned i=0; i<sizeof ART_COVER_NAMES/sizeof ART_COVER_NAMES[0]; i++){
        if(art_now_ms() >= deadline) break;
        unsigned kg; pthread_mutex_lock(&g_pid_mu); kg = g_kill_gen; pthread_mutex_unlock(&g_pid_mu);
        if(kg != t_kill0) break;
        char cov[1024];
        int n = snprintf(cov, sizeof cov, "%.*s/%s", (int)dlen, track, ART_COVER_NAMES[i]);
        if(n <= 0 || n >= (int)sizeof cov) continue;
        if(access(cov, R_OK) != 0) continue;
        r = art_run_pipeline(cov, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0, deadline);
        if(r == 0)  break;
        if(r == -2) break;
    }
    t_kill_armed = 0;
    return r == 0 ? 0 : -1;
}

int art_make_saver(const char *track, const char *out_raw, int64_t deadline_ms){
    unsigned kill0;
    pthread_mutex_lock(&g_pid_mu); art_reap_leftovers_locked(); kill0 = g_kill_gen; pthread_mutex_unlock(&g_pid_mu);
    if(!sd_io_begin()) return -1;
    t_kill0 = kill0; t_kill_armed = 1;
    int64_t deadline = deadline_ms;
    int r = art_run_helper(track, out_raw, NULL, NULL, NULL, 0, 0, deadline);
    if(r == -1){
        const char *slash = strrchr(track, '/');
        size_t dlen = slash ? (size_t)(slash - track) : 0;
        for(unsigned i = 0; dlen > 0 && dlen <= 900 && i < ART_COVER_N; i++){
            if(art_now_ms() >= deadline) break;
            unsigned kg; pthread_mutex_lock(&g_pid_mu); kg = g_kill_gen; pthread_mutex_unlock(&g_pid_mu);
            if(kg != t_kill0) break;
            char cov[1024];
            int n = snprintf(cov, sizeof cov, "%.*s/%s", (int)dlen, track, ART_COVER_NAMES[i]);
            if(n <= 0 || n >= (int)sizeof cov || access(cov, R_OK) != 0) continue;
            r = art_run_helper(cov, out_raw, NULL, NULL, NULL, 0, 0, deadline);
            if(r == 0 || r == -2) break;
        }
    }
    t_kill_armed = 0;
    sd_io_end();
    return r == 0 ? 0 : -1;
}

int art_make_all_ex(const char *track, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable){
    unsigned gen0;
    pthread_mutex_lock(&g_pid_mu); gen0 = g_cancel_gen; pthread_mutex_unlock(&g_pid_mu);
    return art_make_all_ex_gen(track, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0);
}

int art_make_all(const char *track, const char *cover_bmp,
                 const char *thumb_bmp, const char *backdrop_bmp){
    return art_make_all_ex(track, cover_bmp, thumb_bmp, backdrop_bmp, 0);   /* NOLINT */
}

int art_make_all_ex_gen(const char *track, const char *cover_bmp, const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0){
    unsigned kill0;
    pthread_mutex_lock(&g_pid_mu); art_reap_leftovers_locked(); kill0 = g_kill_gen; pthread_mutex_unlock(&g_pid_mu);
    if(!sd_io_begin()) return -1;
    t_kill_preset = kill0; t_kill_preset_valid = 1;
    int result = art_make_all_ex_gen_leased(track, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0);
    t_kill_preset_valid = 0;
    sd_io_end();
    return result;
}
