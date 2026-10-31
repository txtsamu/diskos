/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_COVERART_H
#define DISKOS_COVERART_H

#include <stdint.h>
#include "ipc.h"

#define COVERART_BMP 148

typedef struct {
    char track[256];
    int  idx;
    char out[40], tout[40], bout[40];
    char art_key[420];
    int  is_clear;
    char artist[160], album[160], title[IPC_TITLE_CAPACITY];
    int  online;
    int  online_only;
    int  from_net;
} coverart_job_t;

typedef enum { COVERART_IDLE, COVERART_CLEAR, COVERART_SHOW } coverart_event_t;

int  coverart_request(const coverart_job_t *job);
void coverart_clear(void);
int  coverart_busy(int *missed);
coverart_event_t coverart_poll(const char *want_key, int online_art, coverart_job_t *shown);
int  coverart_online_ready(const char *track, int online_art);
int  coverart_accent(const char *track);

void coverart_decode_lock(void);
void coverart_decode_unlock(void);

void coverart_prewarm_start(void);
void coverart_prewarm_mode(int m);
int  coverart_prewarm_enqueue(const char *path);

int  coverart_read_bmp(const char *path, uint8_t *bgra);
int  coverart_accent_from_bgra(const uint8_t *bgra, uint32_t *rgb);
int  coverart_accent_from_bmp(const char *path, uint32_t *rgb);

int  coverart_sharp_ask(const char *path, unsigned *gen);
void coverart_sharp_drop(void);
unsigned coverart_sharp_gen(void);
int  coverart_sharp_take(uint8_t **bgra, unsigned *gen);

typedef enum { COVERART_WAIT_NONE, COVERART_WAIT_USEC, COVERART_WAIT_SEC, COVERART_WAIT_IDLE } coverart_wait_kind_t;
typedef struct { coverart_wait_kind_t kind; long amount; } coverart_wait_t;
void coverart_prewarm_step(coverart_wait_t *wait);
void coverart_prewarm_reset(void);

struct coverart_env {
    int      (*decode)(const char *src, const char *cover, const char *thumb, const char *backdrop, int cancellable, unsigned gen);
    int      (*decode_saver)(const char *src, const char *out_raw, int64_t deadline_ms);
    int      (*decode_exit)(void);
    unsigned (*cancel_gen)(void);
    void     (*cancel)(void);
    int      (*cache_get)(const char *track, const char *cover, const char *thumb, const char *backdrop);
    int      (*cache_key)(const char *track, char *key, int cap);
    void     (*cache_put_if)(const char *track, const char *key, const char *cover, const char *thumb, const char *backdrop);
    int      (*cache_has)(const char *track);
    int      (*cache_lookup)(const char *track, char *key, int cap);
    int      (*cache_get_cover)(const char *track, const char *cover_out);
    int      (*net_have)(const char *artist, const char *album, const char *title, char *out, int cap);
    int      (*net_touch)(const char *path);
    void     (*net_reject)(const char *path);
    void     (*net_request)(const char *track, const char *artist, const char *album, const char *title);
    int      (*net_take_ready)(char *track, int cap);
    int      (*accent_is_static)(void);
    int      (*accent_get)(const char *track);
    int      (*accent_fresh)(const char *track, const char *key);
    void     (*accent_put)(const char *track, int rgb, const char *key);
    int      (*next_song)(int after_id, int *id, char *path, int cap);
    int      (*temp_dc)(void);
    int      (*charging)(void);
    int      (*io_allowed)(void);
    int      (*main_idle)(void);
    int64_t  (*now_ms)(void);
};

void coverart_set_env(const struct coverart_env *env);
void coverart_init(void);

#endif
