/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_TAMA_H
#define DISKOS_TAMA_H
#include <time.h>

/* Pet (Tamagotchi-style virtual pet): the game rules. Pure C, no LVGL, host-testable; the screen is tama_ui.c.
 * The pet lives in wall-clock time: tama_advance() simulates everything that happened since the pet was last advanced,
 * so it keeps living while the app is closed or the Disc is off (the catch-up is capped, see TAMA_MAX_OFFLINE_MIN,
 * so a few days away is not fatal). The state is one small file, TAMA_FILE, written atomically. */

#define TAMA_FILE            "/usr/data/diskos_tama"
#define TAMA_MAX_OFFLINE_MIN 720   /* a gap longer than this (12 h) only counts as this much, so a weekend does not kill the pet */
#define TAMA_STATE_VERSION   1

typedef enum { TAMA_EGG, TAMA_BABY, TAMA_CHILD, TAMA_TEEN, TAMA_ADULT, TAMA_GONE /* passed away */ } tama_stage_t;

typedef enum {
    TAMA_MOOD_EGG, TAMA_MOOD_HAPPY, TAMA_MOOD_OK, TAMA_MOOD_HUNGRY, TAMA_MOOD_SAD, TAMA_MOOD_SLEEPY, TAMA_MOOD_SICK,
    TAMA_MOOD_ASLEEP, TAMA_MOOD_DEAD
} tama_mood_t;   /* what the sprite should show, most urgent first (see tama_mood) */

typedef struct {
    int      version;      /* TAMA_STATE_VERSION */
    int      stage;        /* tama_stage_t */
    int      form;         /* 0..2: adult form picked by care when growing from TEEN to ADULT; 0 in every other stage */
    int      hunger;       /* 0 starving .. 100 full */
    int      happy;        /* 0 miserable .. 100 joyful */
    int      energy;       /* 0 exhausted .. 100 rested */
    int      weight;       /* 1..99; overfeeding (snacks) raises it, playing lowers it */
    int      poop;         /* 0..4 messes waiting to be cleaned */
    int      sick;         /* 0 / 1 */
    int      asleep;       /* 0 / 1: lights off, energy recovers, nothing else decays quickly */
    int      care;         /* 0..100 how well the pet has been looked after; decides the adult form */
    long     age_min;      /* minutes lived (not counted once GONE) */
    long     stage_min;    /* minutes spent in the current stage */
    long     starve_min;   /* consecutive minutes at hunger 0 */
    long     sick_min;     /* consecutive minutes sick */
    unsigned rng;          /* state of the pet's own random generator (poop, getting sick, mini-game) */
    time_t   born;         /* wall time the pet was created */
    time_t   last;         /* wall time the simulation was last advanced to */
} tama_t;

typedef enum {
    TAMA_OK = 0,
    TAMA_REFUSED_ASLEEP = -1,   /* the pet is asleep (wake it first) */
    TAMA_REFUSED_FULL   = -2,   /* not hungry / refuses more food */
    TAMA_REFUSED_DEAD   = -3,   /* the pet is gone (start a new egg) or still an egg */
    TAMA_REFUSED_NOTHING = -4,  /* nothing to do (no mess to clean, not sick) */
    TAMA_REFUSED_TIRED  = -5    /* too tired to play */
} tama_rc;

void        tama_new(tama_t *t, time_t now, unsigned seed);   /* a fresh egg; age 0, hunger/happy/energy 100 */
void        tama_advance(tama_t *t, time_t now);              /* simulate up to `now`; a clock that went backwards advances nothing */
tama_rc     tama_feed_meal(tama_t *t);                        /* hunger up a lot; refused when already full */
tama_rc     tama_feed_snack(tama_t *t);                       /* hunger up a little, happy up, weight up; refused when asleep/gone */
tama_rc     tama_play(tama_t *t, int won);                    /* the mini-game's result: happy up (more if won), energy and weight down */
tama_rc     tama_clean(tama_t *t);                            /* removes every mess; refused when there is none */
tama_rc     tama_heal(tama_t *t);                             /* cures sickness; refused when not sick */
tama_rc     tama_set_asleep(tama_t *t, int asleep);           /* lights off / on; refused when gone */
tama_mood_t tama_mood(const tama_t *t);                       /* DEAD > EGG > ASLEEP > SICK > HUNGRY > SLEEPY > SAD > HAPPY > OK */
unsigned    tama_rand(tama_t *t, unsigned n);                 /* uniform 0..n-1 from the pet's generator (the UI mini-game uses it) */
const char *tama_stage_name(int stage);                       /* "egg" "baby" "child" "teen" "adult" "gone" */
const char *tama_mood_name(tama_mood_t m);                    /* "happy" "ok" "hungry" ... short lowercase words for the UI */

int         tama_save(const tama_t *t, const char *path);     /* atomic (temp + fsync + rename); 0 ok, -1 failed */
int         tama_load(tama_t *t, const char *path);           /* 0 ok; -1 missing, malformed, wrong version or out of range (t untouched) */

#endif
