/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "coverart.h"
#include "coverart_synth.h"
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CA_TMP
#error "the Makefile points CA_TMP at a temp dir"
#endif

static int g_fail;
#define CHECK(c) do { if(!(c)){ fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); g_fail++; } } while(0)

static pthread_mutex_t gm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gc = PTHREAD_COND_INITIALIZER;
static int g_gate_on, g_gate_entered, g_gate_open;

static int g_kind = 3;
static int n_decode, n_cancel, n_put, n_acc_put, n_net_request, n_net_reject, n_net_touch, n_net_have, n_cache_get, n_lookup;
static int g_last_canc = -1;
static char g_src[8][256];
static int g_exit;
static int g_static, g_net_have_ret = 0;
static char g_net_path[256];
static int g_fresh;
static int g_cache_has, g_cache_get_rc = -1, g_cache_key_rc, g_lookup_rc;
static char g_fp[24] = "fp0";
static struct { char track[256]; int rgb; char key[24]; } g_acc[8];
static char g_put_track[256], g_put_key[24];
static char g_netreq_track[256];
static char g_ready_track[256];
static int g_stored_accent;
static int g_temp, g_charging, g_io, g_idle;
static int64_t g_now, g_now_step;
static int g_next_n, g_next_ids[4]; static char g_next_paths[4][300];
static int g_saver_rc;
static int g_saver_calls;
static int g_saver_size = 360 * 360 * 4, g_saver_extra;

static void gate_wait(void){
    pthread_mutex_lock(&gm);
    if(g_gate_on){ g_gate_entered++; pthread_cond_broadcast(&gc); while(!g_gate_open) pthread_cond_wait(&gc, &gm); }
    pthread_mutex_unlock(&gm);
}
static void gate_arm(void){ pthread_mutex_lock(&gm); g_gate_on = 1; g_gate_entered = 0; g_gate_open = 0; pthread_mutex_unlock(&gm); }
static void gate_release(void){ pthread_mutex_lock(&gm); g_gate_open = 1; g_gate_on = 0; pthread_cond_broadcast(&gc); pthread_mutex_unlock(&gm); }
static void gate_wait_entered(int n){
    pthread_mutex_lock(&gm);
    while(g_gate_entered < n) pthread_cond_wait(&gc, &gm);
    pthread_mutex_unlock(&gm);
}

static int fk_decode(const char *src, const char *c, const char *t, const char *b, int canc, unsigned gen){
    (void)gen;
    pthread_mutex_lock(&gm);
    snprintf(g_src[n_decode < 8 ? n_decode : 7], sizeof g_src[0], "%s", src);
    n_decode++; g_last_canc = canc;
    pthread_mutex_unlock(&gm);
    gate_wait();
    if(strstr(src, "/cancel")) return -2;
    if(strstr(src, "/bad")){ g_exit = 3; return -1; }
    if(strstr(src, "/netbad")){ g_exit = 4; return -1; }
    syn_write_bmp_n(c, 148, g_kind); syn_write_bmp_n(t, 42, g_kind); syn_write_bmp_n(b, 360, g_kind);
    return 0;
}
static int fk_decode_saver(const char *src, const char *out, int64_t deadline){
    (void)src; (void)deadline;
    g_saver_calls++;
    if(g_saver_rc) return g_saver_rc;
    FILE *f = fopen(out, "wb"); if(!f) return -1;
    unsigned char row[1440]; memset(row, 0x5a, sizeof row);
    int total = g_saver_size;
    while(total > 0){ int n = total > (int)sizeof row ? (int)sizeof row : total; fwrite(row, 1, (size_t)n, f); total -= n; }
    if(g_saver_extra) fputc('x', f);
    fclose(f);
    return 0;
}
static int fk_exit(void){ return g_exit; }
static unsigned fk_cancel_gen(void){ return 7; }
static void fk_cancel(void){ pthread_mutex_lock(&gm); n_cancel++; pthread_mutex_unlock(&gm); }
static int fk_cache_get(const char *track, const char *c, const char *t, const char *b){
    (void)track; n_cache_get++;
    if(g_cache_get_rc != 0) return -1;
    syn_write_bmp_n(c, 148, g_kind); syn_write_bmp_n(t, 42, g_kind); syn_write_bmp_n(b, 360, g_kind);
    return 0;
}
static int fk_cache_key(const char *track, char *key, int cap){ (void)track; if(g_cache_key_rc) return -1; snprintf(key, (size_t)cap, "%s", g_fp); return 0; }
static void fk_put_if(const char *track, const char *key, const char *c, const char *t, const char *b){
    (void)c; (void)t; (void)b;
    snprintf(g_put_track, sizeof g_put_track, "%s", track); snprintf(g_put_key, sizeof g_put_key, "%s", key); n_put++;
}
static int fk_has(const char *track){ (void)track; return g_cache_has; }
static int fk_lookup(const char *track, char *key, int cap){ (void)track; n_lookup++; snprintf(key, (size_t)cap, "%s", g_fp); return g_lookup_rc; }
static int fk_get_cover(const char *track, const char *out){ (void)track; syn_write_bmp_n(out, 148, g_kind); return 0; }
static int fk_net_have(const char *artist, const char *album, const char *title, char *out, int cap){
    (void)artist; (void)album; (void)title; n_net_have++;
    if(!g_net_have_ret) return 0;
    snprintf(out, (size_t)cap, "%s", g_net_path); return 1;
}
static int fk_net_touch(const char *p){ (void)p; n_net_touch++; return 1; }
static void fk_net_reject(const char *p){ (void)p; n_net_reject++; }
static void fk_net_request(const char *track, const char *artist, const char *album, const char *title){
    (void)artist; (void)album; (void)title; n_net_request++; snprintf(g_netreq_track, sizeof g_netreq_track, "%s", track);
}
static int fk_net_take_ready(char *track, int cap){
    if(!g_ready_track[0]) return 0;
    snprintf(track, (size_t)cap, "%s", g_ready_track); g_ready_track[0] = 0; return 1;
}
static int fk_static(void){ return g_static; }
static int fk_acc_get(const char *track){ (void)track; return g_stored_accent; }
static int fk_acc_fresh(const char *track, const char *key){ (void)track; (void)key; return g_fresh; }
static void fk_acc_put(const char *track, int rgb, const char *key){
    if(n_acc_put < 8){ snprintf(g_acc[n_acc_put].track, sizeof g_acc[0].track, "%s", track); g_acc[n_acc_put].rgb = rgb; snprintf(g_acc[n_acc_put].key, sizeof g_acc[0].key, "%s", key); }
    n_acc_put++;
}
static int fk_next(int after, int *id, char *path, int cap){
    for(int i = 0; i < g_next_n; i++) if(g_next_ids[i] > after){ *id = g_next_ids[i]; snprintf(path, (size_t)cap, "%s", g_next_paths[i]); return 1; }
    return 0;
}
static int fk_temp(void){ return g_temp; }
static int fk_charging(void){ return g_charging; }
static int fk_io(void){ return g_io; }
static int fk_idle(void){ return g_idle; }
static int64_t fk_now(void){ g_now += g_now_step; return g_now; }

static const struct coverart_env FAKE = {
    .decode = fk_decode, .decode_saver = fk_decode_saver, .decode_exit = fk_exit, .cancel_gen = fk_cancel_gen, .cancel = fk_cancel,
    .cache_get = fk_cache_get, .cache_key = fk_cache_key, .cache_put_if = fk_put_if, .cache_has = fk_has, .cache_lookup = fk_lookup,
    .cache_get_cover = fk_get_cover,
    .net_have = fk_net_have, .net_touch = fk_net_touch, .net_reject = fk_net_reject, .net_request = fk_net_request, .net_take_ready = fk_net_take_ready,
    .accent_is_static = fk_static, .accent_get = fk_acc_get, .accent_fresh = fk_acc_fresh, .accent_put = fk_acc_put,
    .next_song = fk_next, .temp_dc = fk_temp, .charging = fk_charging, .io_allowed = fk_io, .main_idle = fk_idle, .now_ms = fk_now,
};

static void reset(void){
    pthread_mutex_lock(&gm);
    n_decode = n_cancel = n_put = n_acc_put = n_net_request = n_net_reject = n_net_touch = n_net_have = n_cache_get = n_lookup = 0;
    pthread_mutex_unlock(&gm);
    g_last_canc = -1; g_exit = 0; g_static = 0; g_net_have_ret = 0; g_fresh = 0; g_cache_has = 0; g_cache_get_rc = -1;
    g_cache_key_rc = 0; g_lookup_rc = 0; g_kind = 3; g_stored_accent = 0; g_temp = 300; g_charging = 0; g_io = 1; g_idle = 1;
    g_now = 1000; g_now_step = 0; g_next_n = 0; g_saver_rc = 0; g_saver_calls = 0; g_saver_size = 360 * 360 * 4; g_saver_extra = 0;
    g_net_path[0] = 0; g_ready_track[0] = 0; g_put_track[0] = 0; g_put_key[0] = 0; g_netreq_track[0] = 0;
    snprintf(g_fp, sizeof g_fp, "fp0");
    memset(g_acc, 0, sizeof g_acc);
    coverart_set_env(&FAKE);
    coverart_prewarm_reset();
    coverart_prewarm_mode(0);
}

static void mkjob(coverart_job_t *j, const char *track, const char *key, int online){
    memset(j, 0, sizeof *j);
    snprintf(j->track, sizeof j->track, "%s", track);
    snprintf(j->art_key, sizeof j->art_key, "%s", key);
    snprintf(j->artist, sizeof j->artist, "Artist");
    snprintf(j->album, sizeof j->album, "Album");
    snprintf(j->title, sizeof j->title, "Title");
    j->online = online;
    snprintf(j->out, sizeof j->out, "%s/cover.bmp", CA_TMP);
    snprintf(j->tout, sizeof j->tout, "%s/thumb.bmp", CA_TMP);
    snprintf(j->bout, sizeof j->bout, "%s/back.bmp", CA_TMP);
}

static coverart_event_t wait_event(const char *want, int online, coverart_job_t *out){
    for(int i = 0; i < 5000; i++){
        coverart_event_t ev = coverart_poll(want, online, out);
        if(ev != COVERART_IDLE) return ev;
        int missed;
        if(!coverart_busy(&missed)) return COVERART_IDLE;
        usleep(2000);
    }
    return COVERART_IDLE;
}
static void settle(void){
    int missed;
    for(int i = 0; i < 5000 && coverart_busy(&missed); i++){ coverart_job_t r; (void)coverart_poll("", 0, &r); usleep(2000); }
}

static void test_accent(void){
    char path[256];
    static uint8_t buf[SYN_N * SYN_N * 4], want[SYN_N * SYN_N * 4];
    for(int k = 0; k < SYN_KINDS; k++){
        snprintf(path, sizeof path, "%s/acc%d.bmp", CA_TMP, k);
        CHECK(syn_write_bmp(path, k) == 0);
        uint32_t rgb = 0;
        int rc = coverart_accent_from_bmp(path, &rgb);
        CHECK(rc == syn_gold[k].rc);
        if(rgb != syn_gold[k].rgb) fprintf(stderr, "kind %d got %06X want %06X\n", k, (unsigned)rgb, (unsigned)syn_gold[k].rgb);
        CHECK(rgb == syn_gold[k].rgb);
        memset(buf, 0, sizeof buf);
        CHECK(coverart_read_bmp(path, buf) == 0);
        syn_bgra(k, want);
        CHECK(!memcmp(buf, want, sizeof buf));
        rgb = 0;
        CHECK(coverart_accent_from_bgra(buf, &rgb) == 1 && rgb == syn_gold[k].rgb);
        unlink(path);
    }
    uint32_t rgb = 0;
    CHECK(coverart_accent_from_bgra(NULL, &rgb) == 0);
    snprintf(path, sizeof path, "%s/missing.bmp", CA_TMP);
    CHECK(coverart_accent_from_bmp(path, &rgb) == 0);
    snprintf(path, sizeof path, "%s/small.bmp", CA_TMP);
    syn_write_bmp_n(path, 42, 3);
    CHECK(coverart_accent_from_bmp(path, &rgb) == 0);
    unlink(path);
}

static void test_cache_miss_then_show(void){
    reset();
    coverart_job_t j, got; mkjob(&j, "/music/a/miss.flac", "K1", 0);
    g_kind = 3;
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("K1", 0, &got) == COVERART_SHOW);
    CHECK(!strcmp(got.track, "/music/a/miss.flac") && !got.from_net);
    CHECK(n_decode == 1 && g_last_canc == 1 && !strcmp(g_src[0], "/music/a/miss.flac"));
    CHECK(n_put == 1 && !strcmp(g_put_key, "fp0") && !strcmp(g_put_track, "/music/a/miss.flac"));
    CHECK(n_acc_put == 1 && g_acc[0].rgb == 0xF53E3E && !strcmp(g_acc[0].key, "fp0") && !strcmp(g_acc[0].track, "/music/a/miss.flac"));
    CHECK(coverart_accent("x") == 0);
    g_stored_accent = 0x123456; CHECK(coverart_accent("x") == 0x123456);
}

static void test_cache_hit(void){
    reset();
    coverart_job_t j, got; mkjob(&j, "/music/a/hit.flac", "K2", 0);
    g_cache_get_rc = 0; g_kind = 5;
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("K2", 0, &got) == COVERART_SHOW);
    CHECK(n_decode == 0 && n_put == 0);
    CHECK(n_acc_put == 1 && g_acc[0].rgb == 0x4D7EFB);
    reset();
    g_cache_get_rc = 0; g_fresh = 1;
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("K2", 0, &got) == COVERART_SHOW);
    CHECK(n_decode == 0 && n_acc_put == 0);
    reset();
    g_cache_get_rc = 0; g_cache_key_rc = -1;
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("K2", 0, &got) == COVERART_SHOW);
    CHECK(n_acc_put == 0);
    reset();
    g_cache_get_rc = 0; g_static = 1;
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("K2", 0, &got) == COVERART_SHOW);
    CHECK(n_acc_put == 0);
    reset();
    g_cache_key_rc = -1; g_kind = 4;
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("K2", 0, &got) == COVERART_SHOW);
    CHECK(n_decode == 1 && n_put == 0 && n_acc_put == 0);
}

static void test_key_mismatch_and_stale(void){
    reset();
    coverart_job_t j, got; mkjob(&j, "/music/a/k.flac", "KA", 0);
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("OTHER", 0, &got) == COVERART_IDLE);
    settle();
    reset();
    gate_arm();
    mkjob(&j, "/music/a/first.flac", "KA", 0);
    CHECK(coverart_request(&j) == 0);
    gate_wait_entered(1);
    coverart_job_t j2; mkjob(&j2, "/music/a/second.flac", "KB", 0);
    CHECK(coverart_request(&j2) == 0);
    CHECK(n_cancel == 1);
    int missed; CHECK(coverart_busy(&missed) == 1);
    gate_release();
    CHECK(wait_event("KB", 0, &got) == COVERART_SHOW);
    CHECK(!strcmp(got.track, "/music/a/second.flac"));
    CHECK(n_decode == 2 && !strcmp(g_src[0], "/music/a/first.flac") && !strcmp(g_src[1], "/music/a/second.flac"));
    CHECK(coverart_busy(&missed) == 0 && missed == 0);
}

static void test_finished_result_superseded(void){
    reset();
    coverart_job_t j, got; mkjob(&j, "/music/a/old.flac", "KOLD", 0);
    CHECK(coverart_request(&j) == 0);
    for(int i = 0; i < 2000 && n_decode < 1; i++) usleep(2000);
    usleep(100000);
    gate_arm();
    coverart_job_t j2; mkjob(&j2, "/music/a/new.flac", "KNEW", 0);
    CHECK(coverart_request(&j2) == 0);
    gate_wait_entered(1);
    CHECK(coverart_poll("KOLD", 0, &got) == COVERART_IDLE);
    CHECK(coverart_poll("KOLD", 0, &got) == COVERART_IDLE);
    gate_release();
    CHECK(wait_event("KNEW", 0, &got) == COVERART_SHOW);
    CHECK(!strcmp(got.track, "/music/a/new.flac"));
}

static void test_clear(void){
    reset();
    coverart_job_t got;
    coverart_clear();
    CHECK(n_cancel == 1);
    int missed; CHECK(coverart_busy(&missed) == 0);
    CHECK(coverart_poll("", 0, &got) == COVERART_IDLE);
    gate_arm();
    coverart_job_t j; mkjob(&j, "/music/a/c.flac", "KC", 0);
    CHECK(coverart_request(&j) == 0);
    gate_wait_entered(1);
    coverart_clear();
    gate_release();
    CHECK(wait_event("KC", 0, &got) == COVERART_CLEAR);
    CHECK(n_decode == 1);
    CHECK(coverart_busy(&missed) == 0);
}

static void test_miss_and_online(void){
    reset();
    coverart_job_t j, got; mkjob(&j, "/music/a/bad.flac", "KM", 0);
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("KM", 1, &got) == COVERART_CLEAR);
    CHECK(n_net_request == 0 && n_net_have == 0);
    int missed; CHECK(coverart_busy(&missed) == 0 && missed == 1);

    reset();
    mkjob(&j, "/music/a/bad.flac", "KM", 1);
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("KM", 1, &got) == COVERART_CLEAR);
    CHECK(n_net_have == 1 && n_net_request == 1 && !strcmp(g_netreq_track, "/music/a/bad.flac") && n_net_touch == 0);
    CHECK(wait_event("KM", 1, &got) == COVERART_IDLE);

    reset();
    mkjob(&j, "/music/a/bad.flac", "KM", 1);
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("OTHER", 1, &got) == COVERART_CLEAR);
    CHECK(n_net_request == 0);

    reset();
    mkjob(&j, "/music/a/bad.flac", "KM", 1);
    g_net_have_ret = 1; snprintf(g_net_path, sizeof g_net_path, "/net/ok.jpg"); g_kind = 7;
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("KM", 1, &got) == COVERART_SHOW);
    CHECK(got.from_net && n_decode == 2 && !strcmp(g_src[0], "/music/a/bad.flac") && !strcmp(g_src[1], "/net/ok.jpg"));
    CHECK(n_net_touch == 1 && n_net_reject == 0 && n_put == 0 && n_acc_put == 0);

    reset();
    mkjob(&j, "/music/a/bad.flac", "KM", 1);
    g_net_have_ret = 1; snprintf(g_net_path, sizeof g_net_path, "/net/ok.jpg");
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("KM", 0, &got) == COVERART_CLEAR);
    CHECK(n_net_request == 0);

    reset();
    mkjob(&j, "/music/a/bad.flac", "KM", 1);
    g_net_have_ret = 1; snprintf(g_net_path, sizeof g_net_path, "/net/netbad.jpg");
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("KM", 1, &got) == COVERART_CLEAR);
    CHECK(n_net_reject == 1 && n_net_request == 1 && n_decode == 2);

    reset();
    mkjob(&j, "/music/a/cancel.flac", "KM", 1);
    g_net_have_ret = 1; snprintf(g_net_path, sizeof g_net_path, "/net/ok.jpg");
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("KM", 1, &got) == COVERART_CLEAR);
    CHECK(n_net_have == 0 && n_decode == 1 && n_net_request == 0);

    reset();
    mkjob(&j, "/music/a/ok.flac", "KM", 1);
    j.online_only = 1; g_cache_get_rc = 0;
    g_net_have_ret = 1; snprintf(g_net_path, sizeof g_net_path, "/net/ok.jpg"); g_kind = 7;
    CHECK(coverart_request(&j) == 0);
    CHECK(wait_event("KM", 1, &got) == COVERART_SHOW);
    CHECK(n_cache_get == 0 && n_decode == 1 && !strcmp(g_src[0], "/net/ok.jpg") && got.from_net);
}

static void test_online_ready(void){
    reset();
    CHECK(coverart_online_ready("/a", 1) == 0);
    snprintf(g_ready_track, sizeof g_ready_track, "/a");
    CHECK(coverart_online_ready("/a", 1) == 1);
    snprintf(g_ready_track, sizeof g_ready_track, "/a");
    CHECK(coverart_online_ready("/b", 1) == 0);
    snprintf(g_ready_track, sizeof g_ready_track, "/a");
    CHECK(coverart_online_ready("/a", 0) == 0);
    CHECK(coverart_online_ready("/a", 1) == 0);
}

#define STEP(w) coverart_prewarm_step(&(w))
#define CHECKW(w, k, a) do { CHECK((w).kind == (k)); CHECK((w).amount == (a)); } while(0)

static void test_prewarm_gating(void){
    coverart_wait_t w;
    reset();
    g_next_n = 1; g_next_ids[0] = 1; snprintf(g_next_paths[0], sizeof g_next_paths[0], "/m/one.flac");
    STEP(w); CHECKW(w, COVERART_WAIT_IDLE, 5);
    coverart_prewarm_mode(1);
    g_io = 0; STEP(w); CHECKW(w, COVERART_WAIT_IDLE, 5);
    g_io = 1; g_idle = 0; STEP(w); CHECKW(w, COVERART_WAIT_IDLE, 5);
    g_idle = 1; coverart_prewarm_mode(2); g_charging = 0; STEP(w); CHECKW(w, COVERART_WAIT_IDLE, 5);
    CHECK(n_decode == 0);
    g_charging = 1; g_temp = -1; STEP(w); CHECKW(w, COVERART_WAIT_SEC, 15);
    g_temp = 420; STEP(w); CHECKW(w, COVERART_WAIT_SEC, 15);
    g_temp = 410; STEP(w); CHECKW(w, COVERART_WAIT_SEC, 15);
    g_temp = 400; STEP(w); CHECKW(w, COVERART_WAIT_SEC, 15);
    CHECK(n_decode == 0);
    g_temp = 399; g_cache_has = 0;
    STEP(w); CHECKW(w, COVERART_WAIT_SEC, 3);
    CHECK(n_decode == 1 && g_last_canc == 0 && !strcmp(g_src[0], "/m/one.flac") && n_put == 1);
    CHECK(n_acc_put == 1 && !strcmp(g_acc[0].track, "/m/one.flac") && g_acc[0].rgb == 0xF53E3E);
    g_temp = 419; STEP(w); CHECKW(w, COVERART_WAIT_IDLE, 60);
    STEP(w); CHECKW(w, COVERART_WAIT_SEC, 3);
    g_temp = 421;
    STEP(w); CHECKW(w, COVERART_WAIT_SEC, 15);
    g_temp = 410; STEP(w); CHECKW(w, COVERART_WAIT_SEC, 15);
}

static void test_prewarm_walk(void){
    coverart_wait_t w;
    reset();
    coverart_prewarm_mode(1);
    STEP(w); CHECKW(w, COVERART_WAIT_IDLE, 1800);
    g_next_n = 3;
    g_next_ids[0] = 1; snprintf(g_next_paths[0], sizeof g_next_paths[0], "/m/a.flac");
    g_next_ids[1] = 2; snprintf(g_next_paths[1], sizeof g_next_paths[1], "/m/b.flac");
    g_next_ids[2] = 3; snprintf(g_next_paths[2], sizeof g_next_paths[2], "/m/c.flac");
    g_cache_has = 1; g_fresh = 1;
    STEP(w); CHECKW(w, COVERART_WAIT_USEC, 15000);
    CHECK(n_decode == 0 && n_acc_put == 0);
    g_fresh = 0; g_kind = 4;
    STEP(w); CHECKW(w, COVERART_WAIT_SEC, 3);
    CHECK(n_decode == 0 && n_put == 0 && n_acc_put == 1 && !strcmp(g_acc[0].track, "/m/b.flac") && g_acc[0].rgb == 0x3BC64A);
    g_cache_has = 0; g_kind = 3;
    snprintf(g_next_paths[2], sizeof g_next_paths[2], "/m/bad.flac");
    STEP(w); CHECKW(w, COVERART_WAIT_USEC, 300000);
    CHECK(n_decode == 1 && n_put == 0 && n_acc_put == 1);
    STEP(w); CHECKW(w, COVERART_WAIT_IDLE, 60);
    STEP(w); CHECKW(w, COVERART_WAIT_SEC, 3);
    CHECK(n_decode == 2);
    reset();
    coverart_prewarm_mode(1);
    g_next_n = 1; g_next_ids[0] = 1; snprintf(g_next_paths[0], sizeof g_next_paths[0], "/m/a.flac");
    g_cache_has = 0; g_cache_key_rc = -1;
    STEP(w); CHECKW(w, COVERART_WAIT_USEC, 300000);
    CHECK(n_decode == 0);
    reset();
    coverart_prewarm_mode(1);
    g_next_n = 1; g_next_ids[0] = 1; g_next_paths[0][0] = 0;
    STEP(w); CHECKW(w, COVERART_WAIT_NONE, 0);
    reset();
    coverart_prewarm_mode(1);
    g_next_n = 1; g_next_ids[0] = 1; snprintf(g_next_paths[0], sizeof g_next_paths[0], "/m/a.flac");
    g_static = 1; g_cache_has = 1;
    STEP(w); CHECKW(w, COVERART_WAIT_USEC, 15000);
    CHECK(n_acc_put == 0);
    gate_arm();
    coverart_job_t j; mkjob(&j, "/m/live.flac", "KL", 0);
    CHECK(coverart_request(&j) == 0);
    gate_wait_entered(1);
    STEP(w); CHECKW(w, COVERART_WAIT_USEC, 300000);
    gate_release();
    coverart_job_t got; (void)wait_event("KL", 0, &got);
}

static void test_prewarm_queue(void){
    coverart_wait_t w;
    reset();
    CHECK(coverart_prewarm_enqueue("") == 1 && coverart_prewarm_enqueue(NULL) == 1);
    CHECK(coverart_prewarm_enqueue("/q/one.flac") == 1);
    CHECK(coverart_prewarm_enqueue("/q/one.flac") == 1);
    g_lookup_rc = 1;
    STEP(w); CHECKW(w, COVERART_WAIT_USEC, 15000);
    CHECK(n_lookup == 1 && n_decode == 0);
    STEP(w); CHECKW(w, COVERART_WAIT_IDLE, 5);
    CHECK(n_lookup == 1);
    CHECK(coverart_prewarm_enqueue("/q/two.flac") == 1);
    g_lookup_rc = 0;
    STEP(w); CHECKW(w, COVERART_WAIT_USEC, 120000);
    CHECK(n_decode == 1 && g_last_canc == 0 && n_put == 1 && !strcmp(g_put_track, "/q/two.flac") && !strcmp(g_put_key, "fp0"));
    CHECK(coverart_prewarm_enqueue("/q/three.flac") == 1);
    g_lookup_rc = -1;
    STEP(w); CHECKW(w, COVERART_WAIT_USEC, 120000);
    CHECK(n_decode == 1);
    CHECK(coverart_prewarm_enqueue("/q/four.flac") == 1);
    g_io = 0; STEP(w); CHECKW(w, COVERART_WAIT_USEC, 200000);
    g_io = 1; g_temp = -1; STEP(w); CHECKW(w, COVERART_WAIT_SEC, 15);
    g_temp = 430; STEP(w); CHECKW(w, COVERART_WAIT_SEC, 15);
    g_temp = 405; STEP(w); CHECKW(w, COVERART_WAIT_SEC, 15);
    g_temp = 300; g_lookup_rc = 1;
    STEP(w); CHECKW(w, COVERART_WAIT_USEC, 15000);
    int ok = 0;
    for(int i = 0; i < 60; i++){ char p[32]; snprintf(p, sizeof p, "/q/f%d.flac", i); if(coverart_prewarm_enqueue(p)) ok++; }
    CHECK(ok == 47);
    CHECK(coverart_prewarm_enqueue("/q/f0.flac") == 0);
    CHECK(coverart_prewarm_enqueue("/q/overflow.flac") == 0);
    coverart_prewarm_reset();
    STEP(w); CHECKW(w, COVERART_WAIT_IDLE, 5);
}

static void test_sharp(void){
    reset();
    unsigned gen = 99; uint8_t *buf = NULL; unsigned rgen = 0;
    CHECK(coverart_sharp_take(&buf, &rgen) == 0);
    unsigned g0 = coverart_sharp_gen();
    CHECK(coverart_sharp_ask("/m/s.flac", &gen) == 1 && gen == g0);
    int got = 0;
    for(int i = 0; i < 5000 && !got; i++){ got = coverart_sharp_take(&buf, &rgen); if(!got) usleep(2000); }
    CHECK(got && buf && rgen == g0 && buf[0] == 0x5a && buf[360 * 360 * 4 - 1] == 0x5a);
    free(buf);
    CHECK(g_saver_calls == 1);

    g_saver_rc = -1;
    CHECK(coverart_sharp_ask("/m/s.flac", &gen) == 1);
    got = 0; buf = (uint8_t *)1;
    for(int i = 0; i < 5000 && !got; i++){ got = coverart_sharp_take(&buf, &rgen); if(!got) usleep(2000); }
    CHECK(got && buf == NULL);

    g_saver_rc = 0; g_saver_size = 1000;
    CHECK(coverart_sharp_ask("/m/s.flac", &gen) == 1);
    got = 0; buf = (uint8_t *)1;
    for(int i = 0; i < 5000 && !got; i++){ got = coverart_sharp_take(&buf, &rgen); if(!got) usleep(2000); }
    CHECK(got && buf == NULL);

    g_saver_size = 360 * 360 * 4; g_saver_extra = 1;
    CHECK(coverart_sharp_ask("/m/s.flac", &gen) == 1);
    got = 0; buf = (uint8_t *)1;
    for(int i = 0; i < 5000 && !got; i++){ got = coverart_sharp_take(&buf, &rgen); if(!got) usleep(2000); }
    CHECK(got && buf == NULL);

    g_saver_extra = 0;
    int calls0 = g_saver_calls;
    CHECK(coverart_sharp_ask("/m/s.flac", &gen) == 1);
    coverart_sharp_drop();
    CHECK(coverart_sharp_gen() != gen);
    usleep(100000);
    CHECK(coverart_sharp_take(&buf, &rgen) == 0);

    calls0 = g_saver_calls;
    g_now_step = 20000;
    CHECK(coverart_sharp_ask("/m/s.flac", &gen) == 1);
    got = 0; buf = (uint8_t *)1;
    for(int i = 0; i < 5000 && !got; i++){ got = coverart_sharp_take(&buf, &rgen); if(!got) usleep(2000); }
    CHECK(got && buf == NULL && g_saver_calls == calls0);
    g_now_step = 0;
}

int main(void){
    mkdir(CA_TMP, 0755);
    test_accent();
    test_cache_miss_then_show();
    test_cache_hit();
    test_key_mismatch_and_stale();
    test_finished_result_superseded();
    test_clear();
    test_miss_and_online();
    test_online_ready();
    test_prewarm_gating();
    test_prewarm_walk();
    test_prewarm_queue();
    test_sharp();
    if(g_fail){ fprintf(stderr, "FAIL (%d failures)\n", g_fail); return 1; }
    printf("PASS (0 failures)\n");
    return 0;
}
