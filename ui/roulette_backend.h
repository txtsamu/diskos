/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include "musicdb.h"
#define DISC 268                     /* displayed diameter: animation geometry stays the same */
#define ROULETTE_ART_SIZE 224         /* stored cover, upscaled by LVGL */
#define POOL 37
#define MIN_TRACKS 4
typedef struct {
    mdb_album_key_t key;
    char path[1024];
    uint32_t *pixels;
    int pixels_mapped;
} album_t;
extern album_t albums[POOL];
extern int album_count;
extern atomic_int prepared, progress, cancelled;   /* prepared: 0 working, 1 ready, -1 failed (load_error) */
extern char load_error[160];
void *prepare_albums(void *unused);
uint32_t random_below(uint32_t n);
void placeholder(uint32_t *p, int seed);
void release_artwork(album_t *album);
void roulette_reset(void);
void runtime_log(const char *message);
int memory_headroom(uint64_t required_kib);
