/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Host test for tama.c: the pet's rules (growth, decay, actions, death, mood) and its atomic state file. */
#include "tama.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define T0 1793000000LL   /* 2026-10-26: a sane "now" */
#define MIN 60LL

static char dir[] = "/tmp/tama_test_XXXXXX";
static char path[128], tmp[136];

/* A baby at age 0 with the clock at T0, so the periodic decay lines up with minute 0. */
static tama_t baby(unsigned seed){
    tama_t t;
    tama_new(&t, T0, seed);
    t.stage = TAMA_BABY;
    return t;
}

static void adv(tama_t *t, long long minutes){ tama_advance(t, t->last + minutes * MIN); }

/* A seed for which `proto` gets no new mess and no new sickness over `span` minutes, so decay can be checked exactly. */
static unsigned quiet_seed(const tama_t *proto, long long span){
    for(unsigned s = 1; s < 1000000; s++){
        tama_t c = *proto;
        c.rng = s;
        adv(&c, span);
        if(c.poop == proto->poop && c.sick == proto->sick && c.stage != TAMA_GONE) return s;
    }
    assert(!"no quiet seed");
    return 0;
}

static int same(const tama_t *a, const tama_t *b){
    return a->version == b->version && a->stage == b->stage && a->form == b->form && a->hunger == b->hunger && a->happy == b->happy &&
           a->energy == b->energy && a->weight == b->weight && a->poop == b->poop && a->sick == b->sick && a->asleep == b->asleep &&
           a->care == b->care && a->age_min == b->age_min && a->stage_min == b->stage_min && a->starve_min == b->starve_min &&
           a->sick_min == b->sick_min && a->rng == b->rng && a->born == b->born && a->last == b->last;
}

static void put(const char *s){ FILE *f = fopen(path, "w"); assert(f); fputs(s, f); fclose(f); }

/* The golden state's file text, with the line for `key` replaced by `line` (NULL drops it) and `extra` appended. */
static void variant(const tama_t *t, const char *key, const char *line, const char *extra){
    assert(tama_save(t, path) == 0);
    FILE *f = fopen(path, "r"); assert(f);
    char in[1100], out[1400] = "";
    size_t n = fread(in, 1, sizeof in - 1, f); fclose(f);
    in[n] = 0;
    size_t kl = strlen(key);
    for(char *p = in, *e; *p; p = e){
        e = strchr(p, '\n'); assert(e); e++;
        if(strncmp(p, key, kl) == 0 && p[kl] == '='){ if(line) strcat(out, line); continue; }
        strncat(out, p, (size_t)(e - p));
    }
    if(extra) strcat(out, extra);
    put(out);
}

static const char *const keys[] = { "version", "stage", "form", "hunger", "happy", "energy", "weight", "poop", "sick", "asleep",
                                    "care", "age_min", "stage_min", "starve_min", "sick_min", "rng", "born", "last" };
#define NKEYS ((int)(sizeof keys / sizeof keys[0]))

static void test_new(void){
    tama_t t;
    tama_new(&t, T0, 0);
    assert(t.version == TAMA_STATE_VERSION && t.stage == TAMA_EGG && t.form == 0);
    assert(t.hunger == 100 && t.happy == 100 && t.energy == 100 && t.weight == 20 && t.care == 50);
    assert(t.poop == 0 && !t.sick && !t.asleep);
    assert(t.age_min == 0 && t.stage_min == 0 && t.starve_min == 0 && t.sick_min == 0);
    assert(t.born == T0 && t.last == T0);
    assert(t.rng == 1);                                  /* a zero seed is replaced */
    tama_new(&t, T0, 77);
    assert(t.rng == 77);
}

static void test_time(void){
    tama_t t;
    /* an egg hatches after exactly 3 minutes, and does not decay meanwhile */
    tama_new(&t, T0, 5);
    adv(&t, 2);
    assert(t.stage == TAMA_EGG && t.stage_min == 2 && t.hunger == 100 && t.energy == 100 && t.poop == 0 && !t.sick);
    assert(t.last == T0 + 2 * MIN);
    adv(&t, 1);
    assert(t.stage == TAMA_BABY && t.stage_min == 0 && t.age_min == 3);
    /* the same in one go, and with a partial minute left over */
    tama_new(&t, T0, 5);
    tama_advance(&t, T0 + 3 * MIN - 1);
    assert(t.stage == TAMA_EGG && t.stage_min == 2 && t.last == T0 + 2 * MIN);   /* the 59 spare seconds are kept */
    tama_advance(&t, T0 + 3 * MIN);
    assert(t.stage == TAMA_BABY && t.last == T0 + 3 * MIN);
    tama_new(&t, T0, 5);
    tama_advance(&t, T0 + 59);                                                    /* under a minute: nothing, last stays */
    assert(t.age_min == 0 && t.last == T0);
    tama_advance(&t, T0 + 90);
    assert(t.age_min == 1 && t.last == T0 + 60);

    /* a clock that went backwards (or stood still) advances nothing */
    tama_new(&t, T0, 5);
    adv(&t, 10);
    tama_t before = t;
    tama_advance(&t, t.last);
    assert(memcmp(&t, &before, sizeof t) == 0);
    tama_advance(&t, t.last - 3600);
    assert(memcmp(&t, &before, sizeof t) == 0);
    tama_advance(&t, 0);
    assert(memcmp(&t, &before, sizeof t) == 0);

    /* stage progression, minute-exact */
    t = baby(1);
    t.stage_min = 0; t.hunger = 100;
    tama_t p = t;
    p.rng = quiet_seed(&t, 600);
    t = p;
    adv(&t, 59);  assert(t.stage == TAMA_BABY);
    adv(&t, 1);   assert(t.stage == TAMA_CHILD && t.stage_min == 0);
    adv(&t, 179); assert(t.stage == TAMA_CHILD);
    adv(&t, 1);   assert(t.stage == TAMA_TEEN && t.stage_min == 0);
    adv(&t, 359); assert(t.stage == TAMA_TEEN);
    adv(&t, 1);   assert(t.stage == TAMA_ADULT && t.stage_min == 0);
    assert(t.age_min == 600);
    adv(&t, 10);  assert(t.stage == TAMA_ADULT && t.stage_min == 10);

    /* the adult form follows care at the moment of growing up */
    int care_in[] = { 0, 33, 34, 66, 67, 100 };
    int form_out[] = { 0, 0, 1, 1, 2, 2 };
    for(int i = 0; i < 6; i++){
        t = baby(3);
        t.stage = TAMA_TEEN; t.stage_min = 359; t.care = care_in[i]; t.age_min = 1;    /* age 2 next: no care step at the final minute */
        adv(&t, 1);
        assert(t.stage == TAMA_ADULT && t.form == form_out[i]);
    }
    t = baby(3); t.stage = TAMA_CHILD; t.stage_min = 179; adv(&t, 1);
    assert(t.stage == TAMA_TEEN && t.form == 0);
}

static void test_decay(void){
    tama_t t = baby(1);
    t.rng = quiet_seed(&t, 240);
    adv(&t, 240);
    assert(t.hunger == 40);                              /* -1 per 4 min */
    assert(t.happy == 52);                               /* -1 per 5 min */
    assert(t.energy == 70);                              /* -1 per 8 min */
    assert(t.weight == 20 && t.poop == 0 && !t.sick);
    assert(t.care == 70);                                /* +1 per 10 min while hunger >= 50 (minutes 10..200) */
    assert(t.stage == TAMA_TEEN);
    assert(t.starve_min == 0 && t.sick_min == 0);

    /* hungry, messy or sick pets lose happy twice as fast */
    t = baby(1); t.hunger = 20;
    t.rng = quiet_seed(&t, 50);
    adv(&t, 50);
    assert(t.happy == 100 - 2 * 10);                     /* hunger stays under 25 the whole time */
    t = baby(1); t.sick = 1; t.hunger = 100;
    adv(&t, 50);                                         /* stays sick */
    assert(t.sick && t.happy == 100 - 2 * 10);
    t = baby(1); t.poop = 2;
    t.rng = quiet_seed(&t, 50);
    adv(&t, 50);
    assert(t.poop >= 2 && t.happy == 100 - 2 * 10);

    /* floors: nothing goes negative */
    t = baby(1); t.hunger = 1; t.happy = 1; t.energy = 1;
    adv(&t, 100);
    assert(t.hunger == 0 && t.happy == 0 && t.energy == 0);

    /* care drops while neglected: sick pet loses 1 per 10 min */
    t = baby(1); t.sick = 1; t.care = 50;
    adv(&t, 100);
    assert(t.care == 40);
    t = baby(1); t.sick = 1; t.care = 3;
    adv(&t, 100);
    assert(t.care == 0);
}

static void test_offline(void){
    tama_t t = baby(9);
    tama_advance(&t, T0 + 3 * 86400);                    /* away for three days */
    assert(t.age_min == TAMA_MAX_OFFLINE_MIN);           /* only 12 h are simulated ... */
    assert(t.last == T0 + 3 * 86400);                    /* ... and the clock catches up fully */
    assert(t.stage == TAMA_ADULT);
    assert(t.hunger == 0);                               /* 100 - 720/4 would be -80: floored */
    assert(t.starve_min > 0 && t.starve_min < 720);      /* empty for the last 5 h 20 min only: alive */
    assert(t.stage != TAMA_GONE);
    /* exactly the cap is not capped, one minute more is */
    t = baby(9);
    tama_advance(&t, T0 + TAMA_MAX_OFFLINE_MIN * MIN);
    assert(t.age_min == TAMA_MAX_OFFLINE_MIN && t.last == T0 + TAMA_MAX_OFFLINE_MIN * MIN);
    t = baby(9);
    tama_advance(&t, T0 + (TAMA_MAX_OFFLINE_MIN + 1) * MIN + 30);
    assert(t.age_min == TAMA_MAX_OFFLINE_MIN && t.last == T0 + (TAMA_MAX_OFFLINE_MIN + 1) * MIN + 30);
    /* a pet that sleeps through the gap wakes after 10 h and spends the rest awake: 600/12 + 120/4 hunger lost */
    t = baby(9);
    t.asleep = 1; t.energy = 0;
    tama_advance(&t, T0 + 10 * 86400);
    assert(!t.asleep && t.age_min == 720 && t.hunger == 100 - 50 - 30);
    assert(t.energy == 100 - 15);                        /* rested while it slept, then 15 awake energy steps (minutes 608..720) */
}

static void test_actions(void){
    tama_t t;
    /* meal */
    t = baby(1); t.hunger = 30; t.happy = 50; t.weight = 20;
    assert(tama_feed_meal(&t) == TAMA_OK);
    assert(t.hunger == 70 && t.weight == 21 && t.happy == 52);
    t.hunger = 70; assert(tama_feed_meal(&t) == TAMA_OK && t.hunger == 100);       /* capped */
    t.hunger = 89; assert(tama_feed_meal(&t) == TAMA_OK && t.hunger == 100);
    t.hunger = 90; assert(tama_feed_meal(&t) == TAMA_REFUSED_FULL);
    t.hunger = 100; assert(tama_feed_meal(&t) == TAMA_REFUSED_FULL);
    t.hunger = 10; t.happy = 99; t.weight = 99; assert(tama_feed_meal(&t) == TAMA_OK && t.happy == 100 && t.weight == 99);
    /* snack */
    t = baby(1); t.hunger = 50; t.happy = 50; t.weight = 20;
    assert(tama_feed_snack(&t) == TAMA_OK);
    assert(t.hunger == 60 && t.happy == 65 && t.weight == 23);
    t.hunger = 100; t.happy = 100; t.weight = 98;
    assert(tama_feed_snack(&t) == TAMA_OK && t.hunger == 100 && t.happy == 100 && t.weight == 99);   /* a snack is never "full" */
    /* play */
    t = baby(1); t.happy = 50; t.energy = 50; t.weight = 20; t.care = 50;
    assert(tama_play(&t, 1) == TAMA_OK);
    assert(t.happy == 70 && t.energy == 40 && t.weight == 19 && t.care == 51);
    assert(tama_play(&t, 0) == TAMA_OK);
    assert(t.happy == 78 && t.energy == 30 && t.weight == 18 && t.care == 51);
    t.energy = 15; assert(tama_play(&t, 0) == TAMA_OK && t.energy == 5);
    t.energy = 14; assert(tama_play(&t, 1) == TAMA_REFUSED_TIRED && t.energy == 14);
    t.energy = 50; t.weight = 1; t.happy = 95; t.care = 100;
    assert(tama_play(&t, 1) == TAMA_OK && t.weight == 1 && t.happy == 100 && t.care == 100);
    /* clean */
    t = baby(1); t.happy = 50; t.care = 50;
    assert(tama_clean(&t) == TAMA_REFUSED_NOTHING && t.happy == 50 && t.care == 50);
    t.poop = 3;
    assert(tama_clean(&t) == TAMA_OK && t.poop == 0 && t.happy == 53 && t.care == 52);
    t.poop = 1; t.care = 99; t.happy = 99;
    assert(tama_clean(&t) == TAMA_OK && t.care == 100 && t.happy == 100);
    /* heal */
    t = baby(1); t.happy = 50;
    assert(tama_heal(&t) == TAMA_REFUSED_NOTHING && t.happy == 50);
    t.sick = 1; t.sick_min = 300;
    assert(tama_heal(&t) == TAMA_OK && !t.sick && t.sick_min == 0 && t.happy == 45);
    t.sick = 1; t.happy = 2;
    assert(tama_heal(&t) == TAMA_OK && t.happy == 0);
    /* sleep */
    t = baby(1);
    assert(tama_set_asleep(&t, 1) == TAMA_OK && t.asleep == 1);
    assert(tama_set_asleep(&t, 1) == TAMA_OK && t.asleep == 1);
    assert(tama_set_asleep(&t, 0) == TAMA_OK && t.asleep == 0);
    assert(tama_set_asleep(&t, 5) == TAMA_OK && t.asleep == 1);                      /* any nonzero is "asleep" */

    /* refusals while asleep: priority ASLEEP before the specific reason, nothing changes */
    t = baby(1); t.asleep = 1; t.hunger = 100; t.energy = 5; t.poop = 0; t.sick = 0;
    tama_t s = t;
    assert(tama_feed_meal(&t)  == TAMA_REFUSED_ASLEEP);                              /* not FULL */
    assert(tama_feed_snack(&t) == TAMA_REFUSED_ASLEEP);
    assert(tama_play(&t, 1)    == TAMA_REFUSED_ASLEEP);                              /* not TIRED */
    assert(tama_clean(&t)      == TAMA_REFUSED_ASLEEP);                              /* not NOTHING */
    assert(tama_heal(&t)       == TAMA_REFUSED_ASLEEP);
    assert(memcmp(&t, &s, sizeof t) == 0);

    /* refusals when dead or still an egg: DEAD before everything else */
    for(int k = 0; k < 2; k++){
        t = baby(1);
        t.stage = k ? TAMA_GONE : TAMA_EGG;
        t.asleep = 1; t.hunger = 100; t.energy = 0; t.poop = 0; t.sick = 0;
        s = t;
        assert(tama_feed_meal(&t)  == TAMA_REFUSED_DEAD);
        assert(tama_feed_snack(&t) == TAMA_REFUSED_DEAD);
        assert(tama_play(&t, 1)    == TAMA_REFUSED_DEAD);
        assert(tama_clean(&t)      == TAMA_REFUSED_DEAD);
        assert(tama_heal(&t)       == TAMA_REFUSED_DEAD);
        assert(tama_set_asleep(&t, 0) == TAMA_REFUSED_DEAD);
        assert(tama_set_asleep(&t, 1) == TAMA_REFUSED_DEAD);
        assert(memcmp(&t, &s, sizeof t) == 0);
    }
    /* an egg may be awake and still refuses (an egg has no stomach to fill) */
    tama_new(&t, T0, 1);
    assert(tama_feed_meal(&t) == TAMA_REFUSED_DEAD && tama_set_asleep(&t, 1) == TAMA_REFUSED_DEAD && t.asleep == 0);
    /* every adult stage accepts actions */
    for(int st = TAMA_BABY; st <= TAMA_ADULT; st++){
        t = baby(1); t.stage = st; t.hunger = 10;
        assert(tama_feed_meal(&t) == TAMA_OK);
    }
}

static void test_sleep(void){
    tama_t t = baby(1);
    t.energy = 0; t.hunger = 100; t.happy = 80; t.asleep = 1;
    t.rng = quiet_seed(&t, 100);
    adv(&t, 100);
    assert(t.energy == 50 && t.hunger == 100 - 8 && t.happy == 80);                  /* +1/2 min, -1/12 min, happy untouched */
    assert(t.asleep && t.poop == 0);
    adv(&t, 100);
    assert(t.energy == 100 && t.asleep);                                             /* tops out at 100 and stays asleep */

    /* never sleeps longer than 10 h of simulated time within one catch-up (the count is not saved) */
    t = baby(1);
    t.asleep = 1; t.energy = 0;
    tama_t p = t;
    p.rng = quiet_seed(&t, 608);
    t = p;
    adv(&t, 599);
    assert(t.asleep && t.energy == 100 && t.stage != TAMA_GONE);
    t = p;
    adv(&t, 600);
    assert(!t.asleep && t.energy == 100);
    t = p;
    adv(&t, 608);
    assert(!t.asleep && t.energy == 100 - 1);                                        /* awake again: minute 608 costs one energy */
    /* awake energy decay resumes; hunger decays slowly while asleep */
    t = baby(1); t.asleep = 1; t.energy = 100;
    t.rng = quiet_seed(&t, 120);
    adv(&t, 120);
    assert(t.hunger == 100 - 10 && t.energy == 100);
    /* poop only happens while awake */
    t = baby(4); t.asleep = 1;
    for(int i = 0; i < 6; i++) adv(&t, 90);
    assert(t.poop == 0);
}

static tama_t sim(unsigned seed, long hours, int *max_poop, int *sick_seen){
    tama_t t = baby(seed);
    *max_poop = 0; *sick_seen = 0;
    for(long h = 0; h < hours; h++){
        adv(&t, 60);
        assert(t.stage != TAMA_GONE);
        if(t.poop > *max_poop) *max_poop = t.poop;
        assert(t.poop >= 0 && t.poop <= 4);
        if(t.sick){ (*sick_seen)++; assert(tama_heal(&t) == TAMA_OK); }
        (void)tama_feed_meal(&t);
    }
    return t;
}

static void test_poop_sick(void){
    int mp1, sk1, mp2, sk2, mp3, sk3;
    tama_t a = sim(42, 100, &mp1, &sk1);                 /* ~100 h of care-free messes, fed and cured */
    tama_t b = sim(42, 100, &mp2, &sk2);
    assert(memcmp(&a, &b, sizeof a) == 0 && mp1 == mp2 && sk1 == sk2);               /* same seed, same life */
    assert(mp1 >= 1 && mp1 <= 4);                        /* messes do appear ... */
    assert(sk1 >= 1);                                    /* ... and so does sickness */
    assert(mp1 == 4);                                    /* never cleaned: it saturates at 4 and never goes beyond */
    tama_t c = sim(43, 100, &mp3, &sk3);
    assert(memcmp(&a, &c, sizeof a) != 0);               /* another seed, another life */

    /* a mess takes about an hour and a half: not within the first few minutes, and rarely twice in an hour */
    int none = 0;
    for(unsigned s = 1; s <= 50; s++){ tama_t t = baby(s); adv(&t, 5); if(t.poop == 0) none++; }
    assert(none >= 40);
    /* a fed, clean, healthy pet that is left alone for a short while keeps its stats (no random jolt) */
    tama_t t = baby(42);
    adv(&t, 1);
    assert(t.hunger == 100 && t.happy == 100 && t.stage == TAMA_BABY);
}

static void test_death(void){
    tama_t t;
    /* starvation: 12 h at hunger 0 */
    t = baby(1);
    t.hunger = 0; t.happy = 50;
    adv(&t, 719);
    assert(t.stage != TAMA_GONE && t.starve_min == 719);
    adv(&t, 1);
    assert(t.stage == TAMA_GONE && tama_mood(&t) == TAMA_MOOD_DEAD);
    tama_t g = t;
    adv(&t, 600);                                        /* nothing changes any more, not even the clock */
    assert(memcmp(&t, &g, sizeof t) == 0);
    assert(tama_feed_meal(&t) == TAMA_REFUSED_DEAD);
    assert(tama_set_asleep(&t, 1) == TAMA_REFUSED_DEAD);
    /* feeding resets the clock of the starvation */
    t = baby(1); t.hunger = 0;
    adv(&t, 500);
    assert(t.starve_min == 500);
    t.hunger = 10;
    adv(&t, 1);
    assert(t.starve_min == 0 && t.stage != TAMA_GONE);
    /* death can come in the middle of a catch-up: the rest of it is not simulated */
    t = baby(1); t.hunger = 0; t.starve_min = 700;
    adv(&t, 720);
    assert(t.stage == TAMA_GONE && t.starve_min == 720 && t.age_min == 20 && t.last == T0 + 720 * MIN);

    /* untreated sickness: 24 h (fed all the way, so only the illness kills) */
    t = baby(1);
    t.sick = 1;
    for(int h = 0; h < 23; h++){ adv(&t, 60); (void)tama_feed_meal(&t); assert(t.stage != TAMA_GONE); }
    adv(&t, 59);
    assert(t.stage != TAMA_GONE && t.sick_min == 1439);
    adv(&t, 1);
    assert(t.stage == TAMA_GONE);
    /* cured in time, the clock restarts */
    t = baby(1); t.sick = 1; t.sick_min = 1400;
    assert(tama_heal(&t) == TAMA_OK && t.sick_min == 0);
    adv(&t, 30);
    assert(t.stage != TAMA_GONE);
}

static void test_mood(void){
    tama_t t = baby(1);
    assert(tama_mood(&t) == TAMA_MOOD_HAPPY);
    t.happy = 69; assert(tama_mood(&t) == TAMA_MOOD_OK);
    t.happy = 70; t.hunger = 49; assert(tama_mood(&t) == TAMA_MOOD_OK);             /* hungry-ish but not hungry enough */
    t.hunger = 50; assert(tama_mood(&t) == TAMA_MOOD_HAPPY);
    t.happy = 30; assert(tama_mood(&t) == TAMA_MOOD_OK);
    t.happy = 29; assert(tama_mood(&t) == TAMA_MOOD_SAD);
    t.energy = 24; assert(tama_mood(&t) == TAMA_MOOD_SLEEPY);                       /* sleepy beats sad */
    t.energy = 25; assert(tama_mood(&t) == TAMA_MOOD_SAD);
    t.energy = 24; t.hunger = 29; assert(tama_mood(&t) == TAMA_MOOD_HUNGRY);       /* hungry beats sleepy */
    t.hunger = 30; assert(tama_mood(&t) == TAMA_MOOD_SLEEPY);
    t.hunger = 29; t.sick = 1; assert(tama_mood(&t) == TAMA_MOOD_SICK);            /* sick beats hungry */
    t.asleep = 1; assert(tama_mood(&t) == TAMA_MOOD_ASLEEP);                       /* asleep beats sick */
    t.stage = TAMA_EGG; assert(tama_mood(&t) == TAMA_MOOD_EGG);                    /* egg beats asleep */
    t.stage = TAMA_GONE; assert(tama_mood(&t) == TAMA_MOOD_DEAD);                  /* dead beats everything */
    t = baby(1); t.stage = TAMA_EGG; assert(tama_mood(&t) == TAMA_MOOD_EGG);

    assert(!strcmp(tama_stage_name(TAMA_EGG), "egg") && !strcmp(tama_stage_name(TAMA_BABY), "baby"));
    assert(!strcmp(tama_stage_name(TAMA_CHILD), "child") && !strcmp(tama_stage_name(TAMA_TEEN), "teen"));
    assert(!strcmp(tama_stage_name(TAMA_ADULT), "adult") && !strcmp(tama_stage_name(TAMA_GONE), "gone"));
    assert(!strcmp(tama_stage_name(-1), "?") && !strcmp(tama_stage_name(6), "?"));
    const char *mn[] = { "egg", "happy", "ok", "hungry", "sad", "sleepy", "sick", "asleep", "gone" };
    for(int m = TAMA_MOOD_EGG; m <= TAMA_MOOD_DEAD; m++) assert(!strcmp(tama_mood_name((tama_mood_t)m), mn[m]));
    assert(!strcmp(tama_mood_name((tama_mood_t)-1), "?") && !strcmp(tama_mood_name((tama_mood_t)99), "?"));
}

static void test_rand(void){
    tama_t a, b;
    tama_new(&a, T0, 1234); tama_new(&b, T0, 1234);
    int seen[10] = { 0 };
    for(int i = 0; i < 2000; i++){
        unsigned x = tama_rand(&a, 10), y = tama_rand(&b, 10);
        assert(x == y && x < 10);                        /* deterministic, in range */
        seen[x]++;
    }
    for(int i = 0; i < 10; i++) assert(seen[i] > 100);   /* roughly uniform (expected 200 each) */
    assert(a.rng == b.rng);
    assert(tama_rand(&a, 0) == 0);
    assert(tama_rand(&a, 1) == 0);
    for(int i = 0; i < 100; i++) assert(tama_rand(&a, 2) < 2);
    tama_new(&b, T0, 1235);
    int diff = 0;
    tama_new(&a, T0, 1234);
    for(int i = 0; i < 20; i++) if(tama_rand(&a, 1000) != tama_rand(&b, 1000)) diff++;
    assert(diff > 10);                                   /* another seed, another sequence */
    a.rng = 0;                                           /* a zeroed generator must not get stuck */
    unsigned u = tama_rand(&a, 1000000), v = tama_rand(&a, 1000000);
    assert(a.rng != 0 && u != v);
}

static tama_t golden(void){
    tama_t t;
    tama_new(&t, T0, 0xDEADBEEFu);
    t.stage = TAMA_ADULT; t.form = 2; t.hunger = 61; t.happy = 72; t.energy = 83; t.weight = 44; t.poop = 3;
    t.sick = 1; t.asleep = 1; t.care = 91; t.age_min = 123456; t.stage_min = 789; t.starve_min = 5; t.sick_min = 17;
    t.last = T0 + 9000;
    return t;
}

static void test_saveload(void){
    tama_t g = golden(), l;
    memset(&l, 0, sizeof l);
    assert(tama_load(&l, path) == -1);                   /* missing file */
    assert(tama_save(&g, path) == 0);
    assert(access(tmp, F_OK) != 0);                      /* atomic: no temp file left behind */
    assert(tama_load(&l, path) == 0);
    assert(same(&l, &g));
    /* the file is small, text, version first */
    struct stat st; assert(stat(path, &st) == 0 && st.st_size < 1024);
    FILE *f = fopen(path, "r"); char first[32] = ""; assert(f && fgets(first, sizeof first, f)); fclose(f);
    assert(strcmp(first, "version=1\n") == 0);
    /* a new egg and an extreme-but-valid pet round trip too */
    tama_t e; tama_new(&e, T0, 0); assert(tama_save(&e, path) == 0 && tama_load(&l, path) == 0 && l.stage == TAMA_EGG && l.rng == 1);
    e = g; e.rng = 0xFFFFFFFFu; e.weight = 99; e.hunger = 0; e.poop = 4; e.age_min = 100000000L;
    assert(tama_save(&e, path) == 0 && tama_load(&l, path) == 0 && l.rng == 0xFFFFFFFFu && l.age_min == 100000000L && l.weight == 99);
    /* a no-final-newline file still parses (matches timekeep) */
    assert(tama_save(&g, path) == 0);
    f = fopen(path, "r"); char buf[1100]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n - 1] = 0; put(buf);
    memset(&l, 0, sizeof l); assert(tama_load(&l, path) == 0 && l.last == g.last);

    /* rejections leave the target untouched */
    tama_t keep = golden(); keep.hunger = 7;
    tama_t r = keep;
#define REJECT() do { assert(tama_load(&r, path) == -1); assert(memcmp(&r, &keep, sizeof r) == 0); } while(0)
    /* wrong version */
    variant(&g, "version", "version=2\n", NULL);  REJECT();
    variant(&g, "version", "version=0\n", NULL);  REJECT();
    /* unknown, duplicate, missing keys */
    variant(&g, "none", NULL, "colour=3\n");      REJECT();
    variant(&g, "none", NULL, "hunger=5\n");      REJECT();                           /* duplicate */
    variant(&g, "none", NULL, "version=1\n");     REJECT();
    for(int i = 0; i < NKEYS; i++){ variant(&g, keys[i], NULL, NULL); REJECT(); }     /* each key is required */
    variant(&g, "hunger", "Hunger=5\n", NULL);    REJECT();                           /* keys are case-sensitive */
    variant(&g, "hunger", "hunge=5\n", NULL);     REJECT();
    variant(&g, "hunger", "hungers=5\n", NULL);   REJECT();
    /* trailing junk */
    variant(&g, "none", NULL, "junk\n");          REJECT();
    variant(&g, "none", NULL, "\n");              REJECT();
    variant(&g, "none", NULL, "last=5\n");        REJECT();
    variant(&g, "hunger", "hunger=5x\n", NULL);   REJECT();
    variant(&g, "hunger", "hunger=5 \n", NULL);   REJECT();
    variant(&g, "hunger", "hunger=5\r\n", NULL);  REJECT();
    variant(&g, "hunger", "hunger= 5\n", NULL);   REJECT();
    variant(&g, "hunger", "hunger=+5\n", NULL);   REJECT();
    variant(&g, "hunger", "hunger=\n", NULL);     REJECT();
    variant(&g, "hunger", "hunger=-\n", NULL);    REJECT();
    variant(&g, "hunger", "hunger=0x10\n", NULL); REJECT();
    variant(&g, "hunger", "hunger=1e1\n", NULL);  REJECT();
    variant(&g, "hunger", "hunger=5.0\n", NULL);  REJECT();
    variant(&g, "hunger", "hunger=abc\n", NULL);  REJECT();
    variant(&g, "hunger", "hunger=5=5\n", NULL);  REJECT();
    variant(&g, "hunger", "hunger\n", NULL);      REJECT();
    variant(&g, "hunger", "hunger=99999999999999999999999\n", NULL); REJECT();
    put("");                                       REJECT();
    put("\n");                                     REJECT();
    put("garbage\n");                              REJECT();
    put("version=1\n");                            REJECT();
    /* embedded NUL */
    { variant(&g, "none", NULL, NULL); FILE *w = fopen(path, "a"); fputc(0, w); fclose(w); REJECT(); }
    /* values out of range */
    static const struct { const char *key, *bad; } oor[] = {
        { "stage", "-1" }, { "stage", "6" }, { "form", "-1" }, { "form", "3" },
        { "hunger", "-1" }, { "hunger", "101" }, { "happy", "-1" }, { "happy", "101" }, { "energy", "-1" }, { "energy", "101" },
        { "weight", "0" }, { "weight", "100" }, { "weight", "-3" }, { "poop", "-1" }, { "poop", "5" },
        { "sick", "2" }, { "sick", "-1" }, { "asleep", "2" }, { "asleep", "-1" }, { "care", "-1" }, { "care", "101" },
        { "age_min", "-1" }, { "stage_min", "-1" }, { "starve_min", "-1" }, { "sick_min", "-1" },
        { "age_min", "100000001" }, { "stage_min", "100000001" }, { "starve_min", "100000001" }, { "sick_min", "100000001" },
        { "rng", "-1" }, { "rng", "4294967296" }, { "born", "-1" }, { "born", "99999999999999" },
        { "last", "-1" }, { "last", "99999999999999" }, { "last", "4102444801" }, { "version", "-1" },
    };
    for(size_t i = 0; i < sizeof oor / sizeof oor[0]; i++){
        char line[64];
        snprintf(line, sizeof line, "%s=%s\n", oor[i].key, oor[i].bad);
        variant(&g, oor[i].key, line, NULL);
        REJECT();
    }
    /* a file over 1 KiB, even if the first 1 KiB is a valid state */
    { char big[1200]; memset(big, '#', sizeof big); big[sizeof big - 1] = 0; variant(&g, "none", NULL, big); REJECT(); }
    { char pad[1100]; memset(pad, ' ', sizeof pad); pad[sizeof pad - 1] = 0; put(pad); REJECT(); }
    /* the boundary values of the same keys are accepted */
    variant(&g, "hunger", "hunger=100\n", NULL);  assert(tama_load(&l, path) == 0 && l.hunger == 100);
    variant(&g, "weight", "weight=1\n", NULL);    assert(tama_load(&l, path) == 0 && l.weight == 1);
    variant(&g, "stage", "stage=5\n", NULL);      assert(tama_load(&l, path) == 0 && l.stage == TAMA_GONE);
    variant(&g, "poop", "poop=0004\n", NULL);     assert(tama_load(&l, path) == 0 && l.poop == 4);   /* leading zeros are fine */
    variant(&g, "last", "last=4102444800\n", NULL); assert(tama_load(&l, path) == 0 || sizeof(time_t) <= 4);
    /* keys in another order load fine */
    put("last=1793000000\nborn=1793000000\nrng=1\nsick_min=0\nstarve_min=0\nstage_min=0\nage_min=0\ncare=50\nasleep=0\nsick=0\n"
        "poop=0\nweight=20\nenergy=100\nhappy=100\nhunger=100\nform=0\nstage=0\nversion=1\n");
    assert(tama_load(&l, path) == 0 && l.stage == TAMA_EGG && l.born == T0);

    /* a directory or an unreadable path is just a failed load */
    assert(tama_load(&r, dir) == -1);
    assert(memcmp(&r, &keep, sizeof r) == 0);

    /* saving an invalid struct fails, leaves the old file alone and leaves no temp file */
    assert(tama_save(&g, path) == 0);
    struct stat before; assert(stat(path, &before) == 0);
    for(int i = 0; i < NKEYS; i++){
        for(int side = 0; side < 2; side++){
            tama_t bad = g;
            switch(i){
            case 0:  bad.version = side ? 2 : 0; break;
            case 1:  bad.stage = side ? 6 : -1; break;
            case 2:  bad.form = side ? 3 : -1; break;
            case 3:  bad.hunger = side ? 101 : -1; break;
            case 4:  bad.happy = side ? 101 : -1; break;
            case 5:  bad.energy = side ? 101 : -1; break;
            case 6:  bad.weight = side ? 100 : 0; break;
            case 7:  bad.poop = side ? 5 : -1; break;
            case 8:  bad.sick = side ? 2 : -1; break;
            case 9:  bad.asleep = side ? 2 : -1; break;
            case 10: bad.care = side ? 101 : -1; break;
            case 11: bad.age_min = side ? 100000001L : -1; break;
            case 12: bad.stage_min = side ? 100000001L : -1; break;
            case 13: bad.starve_min = side ? 100000001L : -1; break;
            case 14: bad.sick_min = side ? 100000001L : -1; break;
            case 15: continue;                                                       /* every rng value is valid */
            case 16: bad.born = side ? (time_t)4102444801LL : -1; if(sizeof(time_t) <= 4 && side) continue; break;
            default: bad.last = side ? (time_t)4102444801LL : -1; if(sizeof(time_t) <= 4 && side) continue; break;
            }
            assert(tama_save(&bad, path) == -1);
            assert(access(tmp, F_OK) != 0);
        }
    }
    struct stat after; assert(stat(path, &after) == 0 && after.st_ino == before.st_ino && after.st_size == before.st_size);
    assert(tama_load(&l, path) == 0 && l.hunger == g.hunger);
    /* and a save into a missing directory, or to a too long path, fails cleanly */
    assert(tama_save(&g, "/nonexistent-dir/tama") == -1);
    char longp[400]; memset(longp, 'a', sizeof longp); longp[sizeof longp - 1] = 0; longp[0] = '/';
    assert(tama_save(&g, longp) == -1);
    /* a rewrite replaces the state; no temp file again */
    g.hunger = 12;
    assert(tama_save(&g, path) == 0 && access(tmp, F_OK) != 0 && tama_load(&l, path) == 0 && l.hunger == 12);
}

/* A whole pet life through the file: save, "close the app", reload, catch up. */
static void test_life(void){
    tama_t t;
    tama_new(&t, T0, 31337);
    assert(tama_save(&t, path) == 0);
    tama_t l;
    assert(tama_load(&l, path) == 0);
    tama_advance(&l, T0 + 5 * MIN);
    assert(l.stage == TAMA_BABY && l.age_min == 5);
    assert(tama_save(&l, path) == 0);
    tama_t m;
    assert(tama_load(&m, path) == 0);
    assert(same(&m, &l));
}

int main(void){
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/tama", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    test_new();
    test_time();
    test_decay();
    test_offline();
    test_actions();
    test_sleep();
    test_poop_sick();
    test_death();
    test_mood();
    test_rand();
    test_saveload();
    test_life();
    unlink(path); unlink(tmp); rmdir(dir);
    puts("tama_test: ok");
    return 0;
}
