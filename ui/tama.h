/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_TAMA_H
#define DISKOS_TAMA_H
#include <time.h>

#define TAMA_FILE            "/usr/data/diskos_tama"
#define TAMA_MAX_OFFLINE_MIN 720
#define TAMA_STATE_VERSION   1

typedef enum { TAMA_EGG, TAMA_BABY, TAMA_CHILD, TAMA_TEEN, TAMA_ADULT, TAMA_GONE } tama_stage_t;

typedef enum {
    TAMA_MOOD_EGG, TAMA_MOOD_HAPPY, TAMA_MOOD_OK, TAMA_MOOD_HUNGRY, TAMA_MOOD_SAD, TAMA_MOOD_SLEEPY, TAMA_MOOD_SICK,
    TAMA_MOOD_ASLEEP, TAMA_MOOD_DEAD
} tama_mood_t;

typedef struct {
    int      version;
    int      stage;
    int      form;
    int      hunger;
    int      happy;
    int      energy;
    int      weight;
    int      poop;
    int      sick;
    int      asleep;
    int      care;
    long     age_min;
    long     stage_min;
    long     starve_min;
    long     sick_min;
    unsigned rng;
    time_t   born;
    time_t   last;
} tama_t;

typedef enum {
    TAMA_OK = 0,
    TAMA_REFUSED_ASLEEP = -1,
    TAMA_REFUSED_FULL   = -2,
    TAMA_REFUSED_DEAD   = -3,
    TAMA_REFUSED_NOTHING = -4,
    TAMA_REFUSED_TIRED  = -5
} tama_rc;

void        tama_new(tama_t *t, time_t now, unsigned seed);
void        tama_advance(tama_t *t, time_t now);
tama_rc     tama_feed_meal(tama_t *t);
tama_rc     tama_feed_snack(tama_t *t);
tama_rc     tama_play(tama_t *t, int won);
tama_rc     tama_clean(tama_t *t);
tama_rc     tama_heal(tama_t *t);
tama_rc     tama_set_asleep(tama_t *t, int asleep);
tama_mood_t tama_mood(const tama_t *t);
unsigned    tama_rand(tama_t *t, unsigned n);
const char *tama_stage_name(int stage);
const char *tama_mood_name(tama_mood_t m);

int         tama_save(const tama_t *t, const char *path);
int         tama_load(tama_t *t, const char *path);

#endif
