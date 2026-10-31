/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "dacvol_sys.h"
#include "command_spawn.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef DACVOL_AMIXER
#define DACVOL_AMIXER "/usr/bin/amixer"
#endif
#ifndef DACVOL_KEY_SYSFS
#define DACVOL_KEY_SYSFS "/sys/class/input/event%d/device/name"
#endif
#ifndef DACVOL_KEY_DEV
#define DACVOL_KEY_DEV "/dev/input/event%d"
#endif
#ifndef DACVOL_KEY_NAME
#define DACVOL_KEY_NAME "x2000_key"
#endif
#define PROBE_TIMEOUT_MS 2500
#define SET_TIMEOUT_MS   1500
#define SET_GAP_US       30000
#define PROBE_BUF        16384

static struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int started;
    int probe_job, probe_card, probe_state;
    dacvol_ctl_t probe_out;
    int have_ctl, card;
    dacvol_ctl_t ctl;
    int want_set, set_raw, inflight, failed;
} W = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static long long mono_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static int capture(char *const argv[], char *buf, size_t cap, int timeout_ms){
    int fds[2];
    if(pipe2(fds, O_CLOEXEC) != 0) return -1;
    pid_t pid;
    if(command_spawn_io(&pid, argv, fds[1], -1) != 0){ close(fds[0]); close(fds[1]); return -1; }
    close(fds[1]);
    size_t n = 0;
    long long deadline = mono_ms() + timeout_ms;
    int timed_out = 0;
    for(;;){
        long long left = deadline - mono_ms();
        if(left <= 0){ timed_out = 1; break; }
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        int r = poll(&p, 1, (int)left);
        if(r < 0 && errno == EINTR) continue;
        if(r <= 0){ timed_out = 1; break; }
        ssize_t k = read(fds[0], buf + n, cap - 1 - n);
        if(k < 0 && errno == EINTR) continue;
        if(k <= 0) break;
        n += (size_t)k;
        if(n >= cap - 1) break;
    }
    close(fds[0]);
    buf[n] = 0;
    int status = 0;
    if(timed_out){ kill(-pid, SIGKILL); kill(pid, SIGKILL); }
    while(waitpid(pid, &status, 0) < 0 && errno == EINTR){}
    if(timed_out) return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? (int)n : -1;
}

static void run_probe(int card){
    char c[16], buf[PROBE_BUF];
    snprintf(c, sizeof c, "%d", card);
    char *argv[] = { (char *)DACVOL_AMIXER, "-c", c, "contents", NULL };
    dacvol_ctl_t ctl;
    int n = capture(argv, buf, sizeof buf, PROBE_TIMEOUT_MS);
    int state;
    if(n < 0) state = DACVOL_PROBE_ERROR;
    else if(dacvol_parse_contents(buf, &ctl) != 0) state = DACVOL_PROBE_NOCTL;
    else state = DACVOL_PROBE_OK;
    pthread_mutex_lock(&W.mu);
    if(state == DACVOL_PROBE_OK){ W.ctl = ctl; W.have_ctl = 1; W.card = card; W.probe_out = ctl; }
    W.probe_state = state;
    pthread_mutex_unlock(&W.mu);
}

static int run_set(int card, const dacvol_ctl_t *ctl, int raw){
    char c[16], id[32], val[8 * 12 + 1];
    snprintf(c, sizeof c, "%d", card);
    snprintf(id, sizeof id, "numid=%d", ctl->numid);
    size_t o = 0;
    for(int i = 0; i < ctl->channels && i < 8; i++)
        o += (size_t)snprintf(val + o, sizeof val - o, i ? ",%d" : "%d", raw);
    char *argv[] = { (char *)DACVOL_AMIXER, "-c", c, "cset", id, val, NULL };
    return command_wait(argv, SET_TIMEOUT_MS);
}

static void *worker(void *arg){
    (void)arg;
    pthread_mutex_lock(&W.mu);
    for(;;){
        while(!W.probe_job && !W.want_set) pthread_cond_wait(&W.cv, &W.mu);
        if(W.probe_job){
            int card = W.probe_card;
            W.probe_job = 0;
            pthread_mutex_unlock(&W.mu);
            run_probe(card);
            pthread_mutex_lock(&W.mu);
            continue;
        }
        int raw = W.set_raw, card = W.card;
        dacvol_ctl_t ctl = W.ctl;
        W.want_set = 0; W.inflight = 1;
        pthread_mutex_unlock(&W.mu);
        int rc = run_set(card, &ctl, raw);
        if(rc != 0) rc = run_set(card, &ctl, raw);
        usleep(SET_GAP_US);
        pthread_mutex_lock(&W.mu);
        W.inflight = 0;
        if(rc != 0 && !W.want_set) W.failed = 1;
        else if(rc == 0) W.failed = 0;
    }
    return NULL;
}

static int ensure_worker(void){
    if(W.started) return 0;
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 128 * 1024);
    int rc = pthread_create(&th, &at, worker, NULL);
    pthread_attr_destroy(&at);
    if(rc != 0) return -1;
    W.started = 1;
    return 0;
}

int dacvol_sys_mixer_probe(int card){
    pthread_mutex_lock(&W.mu);
    int rc = ensure_worker();
    if(rc == 0){
        W.probe_card = card; W.probe_job = 1; W.probe_state = DACVOL_PROBE_PENDING;
        W.have_ctl = 0; W.want_set = 0; W.failed = 0;
        pthread_cond_signal(&W.cv);
    }
    pthread_mutex_unlock(&W.mu);
    return rc;
}
int dacvol_sys_mixer_probe_poll(dacvol_ctl_t *out){
    pthread_mutex_lock(&W.mu);
    int st = W.probe_state;
    if(st == DACVOL_PROBE_OK && out) *out = W.probe_out;
    pthread_mutex_unlock(&W.mu);
    return st;
}
int dacvol_sys_mixer_set(int raw){
    pthread_mutex_lock(&W.mu);
    int rc = -1;
    if(W.have_ctl){
        W.set_raw = raw; W.want_set = 1; W.failed = 0;
        pthread_cond_signal(&W.cv);
        rc = 0;
    }
    pthread_mutex_unlock(&W.mu);
    return rc;
}
int dacvol_sys_mixer_state(void){
    pthread_mutex_lock(&W.mu);
    int st = (W.want_set || W.inflight) ? DACVOL_MIX_BUSY : W.failed ? DACVOL_MIX_FAILED : DACVOL_MIX_IDLE;
    pthread_mutex_unlock(&W.mu);
    return st;
}

static int g_key_fd = -1;
static int key_find(void){
    for(int i = 0; i < 8; i++){
        char path[96], name[32] = "";
        snprintf(path, sizeof path, DACVOL_KEY_SYSFS, i);
        FILE *f = fopen(path, "r");
        if(!f) continue;
        int got = fgets(name, sizeof name, f) != NULL;
        fclose(f);
        name[strcspn(name, "\n")] = 0;
        if(!got || strcmp(name, DACVOL_KEY_NAME)) continue;
        snprintf(path, sizeof path, DACVOL_KEY_DEV, i);
        return open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    return -1;
}
void dacvol_sys_key_active(int on){
    if(on && g_key_fd < 0) g_key_fd = key_find();
    if(!on && g_key_fd >= 0){ close(g_key_fd); g_key_fd = -1; }
}
int dacvol_sys_key_poll(int *code, int *value){
    while(g_key_fd >= 0){
        struct input_event ev;
        ssize_t n = read(g_key_fd, &ev, sizeof ev);
        if(n == (ssize_t)sizeof ev){
            if(ev.type != EV_KEY) continue;
            *code = ev.code; *value = ev.value;
            return 1;
        }
        if(n < 0 && errno == EINTR) continue;
        if(n < 0 && errno != EAGAIN){ close(g_key_fd); g_key_fd = -1; }
        break;
    }
    return 0;
}
