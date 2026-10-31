/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "coverart.h"
#include "coverart_synth.h"
#include "art.h"
#include "artcache.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CA_TMP
#error "the Makefile points CA_TMP at a temp dir"
#endif

static int g_fail;
#define CHECK(c) do { if(!(c)){ fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); g_fail++; } } while(0)

int sd_io_begin(void){ return 1; }
void sd_io_end(void){}
void sd_io_fault(void){}
int sd_write_begin(void){ return 1; }
void sd_write_end(void){}

static struct { char track[256]; int rgb; char key[24]; } g_store[16];
static int g_nstore, g_n_acc_put;
static int g_static, g_net_ret, g_n_touch, g_n_reject, g_n_net_request;
static char g_net_path[256];
static int g_next_n; static char g_next[4][300];
static int g_io = 1, g_idle = 1, g_temp = 300;

static int st_find(const char *track){ for(int i = 0; i < g_nstore; i++) if(!strcmp(g_store[i].track, track)) return i; return -1; }
static int acc_get(const char *track){ int i = st_find(track); return i < 0 ? 0 : g_store[i].rgb; }
static int acc_fresh(const char *track, const char *key){ int i = st_find(track); return i >= 0 && g_store[i].rgb && !strcmp(g_store[i].key, key); }
static void acc_put(const char *track, int rgb, const char *key){
    int i = st_find(track);
    if(i < 0){ assert(g_nstore < 16); i = g_nstore++; snprintf(g_store[i].track, sizeof g_store[i].track, "%s", track); }
    g_store[i].rgb = rgb; snprintf(g_store[i].key, sizeof g_store[i].key, "%s", key);
    g_n_acc_put++;
}
static int is_static(void){ return g_static; }
static int net_have(const char *a, const char *b, const char *c, char *out, int cap){ (void)a; (void)b; (void)c; if(!g_net_ret) return 0; snprintf(out, (size_t)cap, "%s", g_net_path); return 1; }
static int net_touch(const char *p){ (void)p; g_n_touch++; return 1; }
static void net_reject(const char *p){ (void)p; g_n_reject++; }
static void net_request(const char *t, const char *a, const char *b, const char *c){ (void)t; (void)a; (void)b; (void)c; g_n_net_request++; }
static int net_ready(char *t, int cap){ (void)t; (void)cap; return 0; }
static int next_song(int after, int *id, char *path, int cap){
    if(after >= g_next_n) return 0;
    *id = after + 1; snprintf(path, (size_t)cap, "%s", g_next[after]); return 1;
}
static int temp(void){ return g_temp; }
static int charging(void){ return 0; }
static int io(void){ return g_io; }
static int idle(void){ return g_idle; }
static int64_t now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static const struct coverart_env ENV = {
    .decode = art_make_all_ex_gen, .decode_saver = art_make_saver, .decode_exit = art_last_exit,
    .cancel_gen = art_cancel_gen, .cancel = art_cancel,
    .cache_get = artcache_get, .cache_key = artcache_key, .cache_put_if = artcache_put_if,
    .cache_has = artcache_has, .cache_lookup = artcache_lookup, .cache_get_cover = artcache_get_cover,
    .net_have = net_have, .net_touch = net_touch, .net_reject = net_reject, .net_request = net_request, .net_take_ready = net_ready,
    .accent_is_static = is_static, .accent_get = acc_get, .accent_fresh = acc_fresh, .accent_put = acc_put,
    .next_song = next_song, .temp_dc = temp, .charging = charging, .io_allowed = io, .main_idle = idle, .now_ms = now_ms,
};

static char LOG[256];
static int log_lines(void){
    FILE *f = fopen(LOG, "r"); if(!f) return 0;
    int n = 0, c; while((c = fgetc(f)) != EOF) if(c == '\n') n++;
    fclose(f); return n;
}
static void write_file(const char *path, char first, int size){
    FILE *f = fopen(path, "wb"); assert(f);
    for(int i = 0; i < size; i++) fputc(i == 0 ? first : 'z', f);
    fclose(f);
}
static void mkjob(coverart_job_t *j, const char *track, const char *key, int online){
    memset(j, 0, sizeof *j);
    snprintf(j->track, sizeof j->track, "%s", track);
    snprintf(j->art_key, sizeof j->art_key, "%s", key);
    snprintf(j->artist, sizeof j->artist, "A"); snprintf(j->album, sizeof j->album, "B"); snprintf(j->title, sizeof j->title, "T");
    j->online = online;
    snprintf(j->out, sizeof j->out, "%s/cover.bmp", CA_TMP);
    snprintf(j->tout, sizeof j->tout, "%s/thumb.bmp", CA_TMP);
    snprintf(j->bout, sizeof j->bout, "%s/back.bmp", CA_TMP);
    unlink(j->out); unlink(j->tout); unlink(j->bout);
}
static coverart_event_t run_job(const coverart_job_t *j, int online_art, coverart_job_t *got){
    if(coverart_request(j) != 0) return COVERART_IDLE;
    for(int i = 0; i < 3000; i++){
        coverart_event_t ev = coverart_poll(j->art_key, online_art, got);
        if(ev != COVERART_IDLE) return ev;
        int missed; if(!coverart_busy(&missed)) return COVERART_IDLE;
        usleep(5000);
    }
    return COVERART_IDLE;
}
static int cache_cover_exists(const char *track){
    char fp[24], p[512];
    if(artcache_key(track, fp, sizeof fp) != 0) return 0;
    snprintf(p, sizeof p, "%s/sd/.diskos/artcache/%s/c.bmp", CA_TMP, fp);
    return art_bmp_valid(p, 148);
}

static void test_flow(void){
    char track[256], side[256]; coverart_job_t j, got;
    snprintf(track, sizeof track, "%s/music/a1.flac", CA_TMP);
    snprintf(side, sizeof side, "%s/music/cover.jpg", CA_TMP);
    write_file(track, 'd', 2000);
    mkjob(&j, track, "K", 0);
    CHECK(run_job(&j, 0, &got) == COVERART_SHOW);
    CHECK(art_bmp_valid(j.out, 148) && art_bmp_valid(j.tout, 42) && art_bmp_valid(j.bout, 360));
    CHECK(log_lines() == 1);
    char fp1[24]; CHECK(artcache_key(track, fp1, sizeof fp1) == 0);
    CHECK(cache_cover_exists(track) && artcache_has(track));
    CHECK(acc_get(track) == 0xF53E3E && acc_fresh(track, fp1) && g_n_acc_put == 1);

    mkjob(&j, track, "K", 0);
    CHECK(run_job(&j, 0, &got) == COVERART_SHOW);
    CHECK(art_bmp_valid(j.out, 148) && log_lines() == 1 && g_n_acc_put == 1);

    write_file(track, 'e', 2500);
    char fp2[24]; CHECK(artcache_key(track, fp2, sizeof fp2) == 0 && strcmp(fp1, fp2) != 0);
    CHECK(!artcache_has(track));
    mkjob(&j, track, "K", 0);
    CHECK(run_job(&j, 0, &got) == COVERART_SHOW);
    CHECK(log_lines() == 2 && artcache_has(track));
    CHECK(acc_get(track) == 0x3BC64A && acc_fresh(track, fp2) && !acc_fresh(track, fp1) && g_n_acc_put == 2);

    write_file(side, 'f', 100);
    char fp3[24]; CHECK(artcache_key(track, fp3, sizeof fp3) == 0 && strcmp(fp2, fp3) != 0);
    mkjob(&j, track, "K", 0);
    CHECK(run_job(&j, 0, &got) == COVERART_SHOW);
    CHECK(log_lines() == 3 && acc_fresh(track, fp3));

    g_static = 1;
    write_file(track, 'g', 2600);
    int puts0 = g_n_acc_put;
    mkjob(&j, track, "K", 0);
    CHECK(run_job(&j, 0, &got) == COVERART_SHOW);
    CHECK(g_n_acc_put == puts0);
    g_static = 0;
    unlink(side);
}

static void test_race_and_sidecar(void){
    char track[256], side[256]; coverart_job_t j, got;
    snprintf(track, sizeof track, "%s/music/race.flac", CA_TMP);
    write_file(track, 'h', 3000);
    setenv("ARTDEC_TOUCH", track, 1);
    mkjob(&j, track, "R", 0);
    int lines0 = log_lines();
    CHECK(run_job(&j, 0, &got) == COVERART_SHOW);
    unsetenv("ARTDEC_TOUCH");
    CHECK(log_lines() == lines0 + 1);
    CHECK(!artcache_has(track));

    char d[256]; snprintf(d, sizeof d, "%s/side", CA_TMP); mkdir(d, 0755);
    snprintf(track, sizeof track, "%s/side/bad.flac", CA_TMP);
    snprintf(side, sizeof side, "%s/side/cover.jpg", CA_TMP);
    write_file(track, 'X', 500); write_file(side, 'f', 100);
    lines0 = log_lines();
    mkjob(&j, track, "S", 0);
    CHECK(run_job(&j, 0, &got) == COVERART_SHOW);
    CHECK(log_lines() == lines0 + 2);
    CHECK(art_bmp_valid(j.out, 148) && artcache_has(track) && acc_get(track) == 0x4D7EFB);
    unlink(side);
}

static void test_online(void){
    char track[256]; coverart_job_t j, got;
    char d[256]; snprintf(d, sizeof d, "%s/net", CA_TMP); mkdir(d, 0755);
    snprintf(track, sizeof track, "%s/net/bad.flac", CA_TMP);
    write_file(track, 'X', 500);
    snprintf(g_net_path, sizeof g_net_path, "%s/net/n.jpg", CA_TMP);
    write_file(g_net_path, 'i', 100);
    g_net_ret = 1;
    int lines0 = log_lines(), acc0 = g_n_acc_put;
    mkjob(&j, track, "N", 1);
    CHECK(run_job(&j, 1, &got) == COVERART_SHOW);
    CHECK(got.from_net && g_n_touch == 1 && log_lines() == lines0 + 2);
    CHECK(!artcache_has(track) && g_n_acc_put == acc0);

    write_file(g_net_path, 'X', 100);
    mkjob(&j, track, "N", 1);
    CHECK(run_job(&j, 1, &got) == COVERART_CLEAR);
    CHECK(g_n_reject == 1 && g_n_net_request == 1);
    g_net_ret = 0;
}

static void test_prewarm(void){
    char t1[256], t2[256]; coverart_wait_t w;
    snprintf(t1, sizeof t1, "%s/music/p1.flac", CA_TMP);
    snprintf(t2, sizeof t2, "%s/music/p2.flac", CA_TMP);
    write_file(t1, 'h', 700); write_file(t2, 'j', 800);
    snprintf(g_next[0], sizeof g_next[0], "%s", t1); snprintf(g_next[1], sizeof g_next[1], "%s", t2); g_next_n = 2;
    coverart_prewarm_reset();
    coverart_prewarm_mode(1);
    int lines0 = log_lines();
    coverart_prewarm_step(&w);
    CHECK(w.kind == COVERART_WAIT_SEC && w.amount == 3);
    CHECK(log_lines() == lines0 + 1 && artcache_has(t1) && acc_get(t1) == 0xD74FCD);
    char fp[24]; CHECK(artcache_key(t1, fp, sizeof fp) == 0 && acc_fresh(t1, fp));
    coverart_prewarm_step(&w);
    CHECK(w.kind == COVERART_WAIT_SEC && artcache_has(t2) && acc_get(t2) == 0xF08C14);
    coverart_prewarm_step(&w);
    CHECK(w.kind == COVERART_WAIT_IDLE && w.amount == 60);
    coverart_prewarm_step(&w);
    CHECK(w.kind == COVERART_WAIT_USEC && w.amount == 15000);
    CHECK(log_lines() == lines0 + 2);
    coverart_prewarm_enqueue(t2);
    coverart_prewarm_step(&w);
    CHECK(w.kind == COVERART_WAIT_USEC && w.amount == 15000);
    coverart_prewarm_mode(0);
}

static void test_sharp(void){
    char track[256]; unsigned gen = 0, rgen = 0; uint8_t *buf = NULL;
    snprintf(track, sizeof track, "%s/music/sharp.flac", CA_TMP);
    write_file(track, 'c', 900);
    CHECK(coverart_sharp_ask(track, &gen) == 1);
    int got = 0;
    for(int i = 0; i < 3000 && !got; i++){ got = coverart_sharp_take(&buf, &rgen); if(!got) usleep(5000); }
    CHECK(got && buf && rgen == gen && buf[0] == 0x30 + 2 && buf[360 * 360 * 4 - 1] == 0x30 + 2);
    free(buf);
}

int main(void){
    char d[256];
    mkdir(CA_TMP, 0755);
    snprintf(d, sizeof d, "%s/music", CA_TMP); mkdir(d, 0755);
    snprintf(LOG, sizeof LOG, "%s/log", CA_TMP); unlink(LOG);
    setenv("ARTDEC_LOG", LOG, 1);
    coverart_set_env(&ENV);
    test_flow();
    test_race_and_sidecar();
    test_online();
    test_prewarm();
    test_sharp();
    if(g_fail){ fprintf(stderr, "FAIL (%d failures)\n", g_fail); return 1; }
    printf("PASS (0 failures)\n");
    return 0;
}
