/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "coverart.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef COVERART_PW_COVER
#define COVERART_PW_COVER "/tmp/pw_cover.bmp"
#define COVERART_PW_THUMB "/tmp/pw_thumb.bmp"
#define COVERART_PW_BACKDROP "/tmp/pw_backdrop.bmp"
#endif
#ifndef COVERART_SHARP_OUT
#define COVERART_SHARP_OUT "/tmp/.diskos-vinyl-%u.bgra"
#endif
#define SHARP_W 360
#define PWQ_MAX 48

static struct coverart_env E;
void coverart_set_env(const struct coverart_env *env){ E = *env; }

static void cpy(char *dst, size_t n, const char *src)
{
    if(n == 0) return;
    if(src == NULL) src = "";
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

static pthread_mutex_t g_decode_mu = PTHREAD_MUTEX_INITIALIZER;
void coverart_decode_lock(void){ pthread_mutex_lock(&g_decode_mu); }
void coverart_decode_unlock(void){ pthread_mutex_unlock(&g_decode_mu); }

static pthread_mutex_t g_art_mu = PTHREAD_MUTEX_INITIALIZER;
static coverart_job_t g_art_target;
static unsigned  g_art_req      = 0;
static int       g_art_inflight = 0;
static int       g_art_ready    = 0;
static int       g_art_rc       = -1;
static unsigned  g_art_done_req = 0;
static coverart_job_t g_art_result;

int coverart_accent(const char *track){ return E.accent_get(track); }

static int accent_stale(const char *track, const char *key)
{
    return !E.accent_is_static() && key[0] && !E.accent_fresh(track, key);
}

static void accent_refresh(const char *track, const char *key, const char *cover)
{
    uint32_t rgb;
    if(coverart_accent_from_bmp(cover, &rgb)) E.accent_put(track, (int)(rgb & 0xFFFFFF), key);
}

static void *art_worker(void *arg)
{
    (void)arg;
    for(;;){
        coverart_job_t job; unsigned my_req, my_gen;
        pthread_mutex_lock(&g_art_mu);
        job = g_art_target; my_req = g_art_req;
        my_gen = E.cancel_gen();
        pthread_mutex_unlock(&g_art_mu);

        int rc, from_net = 0;
        char fp0[24] = "";
        if(job.is_clear) rc = -1;
        else if(!job.online_only && E.cache_get(job.track, job.out, job.tout, job.bout) == 0){ rc = 0;
               if(E.cache_key(job.track, fp0, sizeof fp0) != 0) fp0[0] = 0; }
        else { int have_fp = E.cache_key(job.track, fp0, sizeof fp0) == 0;
               pthread_mutex_lock(&g_decode_mu);
               rc = job.online_only ? -1 : E.decode(job.track, job.out, job.tout, job.bout, 1, my_gen);
               char np[300];
               if(rc != 0 && rc != -2 && job.online && E.net_have(job.artist, job.album, job.title, np, sizeof np)){
                   if(E.net_touch(np)){
                       rc = E.decode(np, job.out, job.tout, job.bout, 1, my_gen);
                       from_net = rc == 0;
                       int ex = E.decode_exit();
                       if(rc != 0 && (ex == 3 || ex == 4 || ex == 5)) E.net_reject(np);
                   }
               }
               pthread_mutex_unlock(&g_decode_mu);
               if(rc == 0 && have_fp && !from_net) E.cache_put_if(job.track, fp0, job.out, job.tout, job.bout); }

        if(rc == 0 && !from_net && !job.is_clear && accent_stale(job.track, fp0))
            accent_refresh(job.track, fp0, job.out);

        job.from_net = from_net;
        pthread_mutex_lock(&g_art_mu);
        if(g_art_req == my_req){
            g_art_rc = rc; g_art_result = job; g_art_done_req = my_req;
            g_art_ready = 1; g_art_inflight = 0;
            pthread_mutex_unlock(&g_art_mu);
            return NULL;
        }
        pthread_mutex_unlock(&g_art_mu);
    }
}

int coverart_request(const coverart_job_t *job)
{
    int launch = 0;
    pthread_mutex_lock(&g_art_mu);
    g_art_req++;
    g_art_target = *job;
    if(!g_art_inflight){ g_art_inflight = 1; launch = 1; }
    else E.cancel();
    pthread_mutex_unlock(&g_art_mu);
    if(!launch) return 0;
    pthread_t th;
    if(pthread_create(&th, NULL, art_worker, NULL) == 0){
        pthread_detach(th);
        return 0;
    }
    pthread_mutex_lock(&g_art_mu); g_art_inflight = 0; pthread_mutex_unlock(&g_art_mu);
    return -1;
}

void coverart_clear(void)
{
    pthread_mutex_lock(&g_art_mu);
    g_art_req++;
    memset(&g_art_target, 0, sizeof g_art_target);
    g_art_target.is_clear = 1;
    pthread_mutex_unlock(&g_art_mu);
    E.cancel();
}

int coverart_busy(int *missed)
{
    pthread_mutex_lock(&g_art_mu);
    int waiting = g_art_inflight || g_art_ready;
    *missed = g_art_rc != 0 && g_art_rc != -2;
    pthread_mutex_unlock(&g_art_mu);
    return waiting;
}

coverart_event_t coverart_poll(const char *want_key, int online_art, coverart_job_t *shown)
{
    int ready = 0, rc = -1; coverart_job_t res; unsigned done_req = 0, cur_req = 0;
    pthread_mutex_lock(&g_art_mu);
    if(g_art_ready){
        ready = 1; g_art_ready = 0;
        rc = g_art_rc; res = g_art_result; done_req = g_art_done_req; cur_req = g_art_req;
    }
    pthread_mutex_unlock(&g_art_mu);
    if(!ready) return COVERART_IDLE;
    if(done_req != cur_req) return COVERART_IDLE;
    if(res.is_clear) return COVERART_CLEAR;
    if(rc == 0 && res.from_net && !online_art) return COVERART_CLEAR;
    if(rc == 0){
        if(strcmp(res.art_key, want_key) != 0) return COVERART_IDLE;
        *shown = res;
        return COVERART_SHOW;
    }
    if(rc != -2 && res.online && online_art && strcmp(res.art_key, want_key) == 0)
        E.net_request(res.track, res.artist, res.album, res.title);
    return COVERART_CLEAR;
}

int coverart_online_ready(const char *track, int online_art)
{
    char got[512];
    return E.net_take_ready(got, sizeof got) && online_art && strcmp(got, track) == 0;
}

static char            pwq[PWQ_MAX][512];
static int             pwq_head, pwq_tail;
static pthread_mutex_t pwq_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  pwq_cv;
static pthread_once_t  pwq_once = PTHREAD_ONCE_INIT;
static _Atomic int g_prewarm_mode = 0;
static unsigned pwq_mode_seq;
static void pwq_init(void){
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&pwq_cv, &attr);
    pthread_condattr_destroy(&attr);
}
void coverart_prewarm_mode(int m){
    m = (m<0)?0:(m>2?2:m);
    pthread_once(&pwq_once, pwq_init);
    pthread_mutex_lock(&pwq_mu);
    if(g_prewarm_mode != m){
        g_prewarm_mode = m;
        pwq_mode_seq++;
        pthread_cond_signal(&pwq_cv);
    }
    pthread_mutex_unlock(&pwq_mu);
}
int coverart_prewarm_enqueue(const char *path){
    if(!path || !path[0]) return 1;
    int ok = 0;
    pthread_once(&pwq_once, pwq_init);
    pthread_mutex_lock(&pwq_mu);
    int nt = (pwq_tail+1) % PWQ_MAX;
    if(nt != pwq_head){
        int dup = 0;
        for(int i=pwq_head; i!=pwq_tail; i=(i+1)%PWQ_MAX) if(!strcmp(pwq[i], path)){ dup=1; break; }
        if(dup) ok = 1;
        else { cpy(pwq[pwq_tail], sizeof pwq[pwq_tail], path); pwq_tail = nt; ok = 1; pthread_cond_signal(&pwq_cv); }
    }
    pthread_mutex_unlock(&pwq_mu);
    return ok;
}
static int pwq_pop(char *out, int cap){
    int got = 0;
    pthread_mutex_lock(&pwq_mu);
    if(pwq_head != pwq_tail){ cpy(out, (size_t)cap, pwq[pwq_head]); pwq_head = (pwq_head+1)%PWQ_MAX; got = 1; }
    pthread_mutex_unlock(&pwq_mu);
    return got;
}
static void idle_wait(int secs){
    struct timespec until;
    clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_sec += secs;
    pthread_once(&pwq_once, pwq_init);
    pthread_mutex_lock(&pwq_mu);
    unsigned seq = pwq_mode_seq;
    while(pwq_head == pwq_tail && seq == pwq_mode_seq){
        if(g_prewarm_mode == 0) pthread_cond_wait(&pwq_cv, &pwq_mu);
        else if(pthread_cond_timedwait(&pwq_cv, &pwq_mu, &until) == ETIMEDOUT) break;
    }
    pthread_mutex_unlock(&pwq_mu);
}

static struct { int id, hot, did_work, have_queued; char queued[512]; } pw;

void coverart_prewarm_reset(void)
{
    memset(&pw, 0, sizeof pw);
    pthread_once(&pwq_once, pwq_init);
    pthread_mutex_lock(&pwq_mu);
    pwq_head = pwq_tail = 0;
    pthread_mutex_unlock(&pwq_mu);
}

static void wait_for(coverart_wait_t *w, coverart_wait_kind_t kind, long amount){ w->kind = kind; w->amount = amount; }

static int window_open(void){
    if(!E.io_allowed()) return 0;
    int m = g_prewarm_mode; if(m==0) return 0;
    if(!E.main_idle()) return 0;
    if(m==2) return E.charging();
    return 1;
}

static int thermal_blocked(coverart_wait_t *w){
    int t = E.temp_dc();
    if(t < 0){ wait_for(w, COVERART_WAIT_SEC, 15); return 1; }
    if(pw.hot){ if(t < 400) pw.hot = 0; else { wait_for(w, COVERART_WAIT_SEC, 15); return 1; } }
    else if(t >= 420){ pw.hot = 1; wait_for(w, COVERART_WAIT_SEC, 15); return 1; }
    return 0;
}

static int decoder_busy(coverart_wait_t *w){
    int busy; pthread_mutex_lock(&g_art_mu); busy = g_art_inflight; pthread_mutex_unlock(&g_art_mu);
    if(busy){ wait_for(w, COVERART_WAIT_USEC, 300*1000); return 1; }
    return 0;
}

static int prewarm_decode(const char *path, const char *key)
{
    pthread_mutex_lock(&g_decode_mu);
    int r = E.decode(path, COVERART_PW_COVER, COVERART_PW_THUMB, COVERART_PW_BACKDROP, 0, E.cancel_gen());
    pthread_mutex_unlock(&g_decode_mu);
    if(r == 0) E.cache_put_if(path, key, COVERART_PW_COVER, COVERART_PW_THUMB, COVERART_PW_BACKDROP);
    return r;
}

void coverart_prewarm_step(coverart_wait_t *w)
{
    wait_for(w, COVERART_WAIT_NONE, 0);
    if(!pw.have_queued && pwq_pop(pw.queued, sizeof pw.queued)) pw.have_queued = 1;
    if(pw.have_queued){
        if(!E.io_allowed()){ wait_for(w, COVERART_WAIT_USEC, 200000); return; }
        if(thermal_blocked(w) || decoder_busy(w)) return;
        pw.have_queued = 0;
        char fp0[24];
        int cached = E.cache_lookup(pw.queued, fp0, sizeof fp0);
        if(cached == 1){ wait_for(w, COVERART_WAIT_USEC, 15*1000); return; }
        if(cached == 0) prewarm_decode(pw.queued, fp0);
        wait_for(w, COVERART_WAIT_USEC, 120*1000);
        return;
    }
    if(!window_open()){ pw.id = 0; pw.did_work = 0; wait_for(w, COVERART_WAIT_IDLE, 5); return; }
    if(thermal_blocked(w) || decoder_busy(w)) return;

    char path[300];
    if(!E.next_song(pw.id, &pw.id, path, sizeof path)){
        pw.id = 0; wait_for(w, COVERART_WAIT_IDLE, pw.did_work ? 60 : 1800); pw.did_work = 0; return;
    }
    if(!path[0]) return;

    int have_art = E.cache_has(path);
    char acc_fp[24] = "";
    int need_acc = !E.accent_is_static() && E.cache_key(path, acc_fp, sizeof acc_fp) == 0
                   && !E.accent_fresh(path, acc_fp);
    if(have_art && !need_acc){ wait_for(w, COVERART_WAIT_USEC, 15*1000); return; }

    if(!have_art){
        char fp0[24];
        if(E.cache_key(path, fp0, sizeof fp0) != 0){ wait_for(w, COVERART_WAIT_USEC, 300*1000); return; }
        if(prewarm_decode(path, fp0) != 0){ wait_for(w, COVERART_WAIT_USEC, 300*1000); return; }
    }
    if(need_acc){
        int cover_ready = have_art ? (E.cache_get_cover(path, COVERART_PW_COVER) == 0) : 1;
        if(cover_ready) accent_refresh(path, acc_fp, COVERART_PW_COVER);
    }
    pw.did_work = 1;
    wait_for(w, COVERART_WAIT_SEC, 3);
}

static void *prewarm_worker(void *arg)
{
    (void)arg;
    (void)!nice(15);
    for(;;){
        coverart_wait_t w;
        coverart_prewarm_step(&w);
        if(w.kind == COVERART_WAIT_USEC) usleep((useconds_t)w.amount);
        else if(w.kind == COVERART_WAIT_SEC) sleep((unsigned)w.amount);
        else if(w.kind == COVERART_WAIT_IDLE) idle_wait((int)w.amount);
    }
    return NULL;
}

void coverart_prewarm_start(void)
{
    pthread_t th;
    if(pthread_create(&th, NULL, prewarm_worker, NULL) == 0) pthread_detach(th);
}

static pthread_mutex_t g_vmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_vcv = PTHREAD_COND_INITIALIZER;
static unsigned g_vgen;
static int g_vwork_started, g_vwant;
static char g_vwant_path[1024];
static unsigned g_vwant_gen;
static int64_t g_vwant_deadline;
static uint8_t *g_vres; static unsigned g_vres_gen; static int g_vres_ready;

static void *sharp_worker(void *arg)
{
    (void)arg;
    const size_t need = (size_t)SHARP_W * SHARP_W * 4;
    for (;;) {
        pthread_mutex_lock(&g_vmu);
        while (!g_vwant) pthread_cond_wait(&g_vcv, &g_vmu);
        char path[1024]; snprintf(path, sizeof path, "%s", g_vwant_path);
        unsigned gen = g_vwant_gen; int64_t deadline = g_vwant_deadline; g_vwant = 0;
        pthread_mutex_unlock(&g_vmu);

        char out[64]; snprintf(out, sizeof out, COVERART_SHARP_OUT, gen);
        uint8_t *buf = NULL;
        coverart_decode_lock();
        pthread_mutex_lock(&g_vmu);
        int superseded = g_vgen != gen;
        pthread_mutex_unlock(&g_vmu);
        int rc = (superseded || E.now_ms() >= deadline) ? -1 : E.decode_saver(path, out, deadline);
        coverart_decode_unlock();
        if (rc == 0) {
            int fd = open(out, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            buf = malloc(need);
            size_t got = 0;
            if (fd >= 0 && buf) {
                ssize_t n;
                while (got < need && (n = read(fd, buf + got, need - got)) > 0) got += (size_t)n;
                char extra; if (got == need && read(fd, &extra, 1) != 0) got = 0;
            }
            if (fd >= 0) close(fd);
            if (got != need) { free(buf); buf = NULL; }
        }
        unlink(out);
        pthread_mutex_lock(&g_vmu);
        if (gen != g_vgen) { free(buf); pthread_mutex_unlock(&g_vmu); continue; }
        free(g_vres);
        g_vres = buf; g_vres_gen = gen; g_vres_ready = 1;
        pthread_mutex_unlock(&g_vmu);
    }
    return NULL;
}

int coverart_sharp_ask(const char *path, unsigned *gen)
{
    int ok = 0;
    pthread_mutex_lock(&g_vmu);
    if (!g_vwork_started) {
        pthread_t th;
        if (pthread_create(&th, NULL, sharp_worker, NULL) == 0) { pthread_detach(th); g_vwork_started = 1; }
    }
    if (g_vwork_started) {
        snprintf(g_vwant_path, sizeof g_vwant_path, "%s", path);
        g_vwant_gen = g_vgen; g_vwant = 1;
        g_vwant_deadline = E.now_ms() + 15000;
        pthread_cond_signal(&g_vcv);
        *gen = g_vgen;
        ok = 1;
    }
    pthread_mutex_unlock(&g_vmu);
    return ok;
}

void coverart_sharp_drop(void)
{
    pthread_mutex_lock(&g_vmu);
    g_vgen++; g_vwant = 0;
    free(g_vres); g_vres = NULL; g_vres_ready = 0;
    pthread_mutex_unlock(&g_vmu);
}

unsigned coverart_sharp_gen(void)
{
    pthread_mutex_lock(&g_vmu);
    unsigned g = g_vgen;
    pthread_mutex_unlock(&g_vmu);
    return g;
}

int coverart_sharp_take(uint8_t **bgra, unsigned *gen)
{
    pthread_mutex_lock(&g_vmu);
    if (!g_vres_ready) { pthread_mutex_unlock(&g_vmu); return 0; }
    *bgra = g_vres; *gen = g_vres_gen;
    g_vres = NULL; g_vres_ready = 0;
    pthread_mutex_unlock(&g_vmu);
    return 1;
}
