/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "tama.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EGG_MIN            3
#define BABY_MIN           60
#define CHILD_MIN          180
#define TEEN_MIN           360
#define FORM1_CARE         34
#define FORM2_CARE         67
#define HUNGER_EVERY       4
#define HAPPY_EVERY        5
#define HAPPY_FAST_MUL     2
#define ENERGY_EVERY       8
#define SLEEP_ENERGY_EVERY 2
#define SLEEP_HUNGER_EVERY 12
#define SLEEP_MAX_MIN      600
#define POOP_ODDS          90
#define SICK_ODDS          600
#define SICK_ODDS_RISK     120
#define SICK_POOP          2
#define SICK_WEIGHT        80
#define HUNGRY_FAST        25
#define STARVE_DEATH_MIN   720
#define SICK_DEATH_MIN     1440
#define CARE_EVERY         10
#define CARE_GOOD_STAT     50
#define CARE_BAD_HUNGER    25
#define CARE_BAD_POOP      3
#define MEAL_HUNGER        40
#define MEAL_HAPPY         2
#define MEAL_WEIGHT        1
#define FULL_HUNGER        90
#define SNACK_HUNGER       10
#define SNACK_HAPPY        15
#define SNACK_WEIGHT       3
#define PLAY_WIN_HAPPY     20
#define PLAY_LOSE_HAPPY    8
#define PLAY_ENERGY        10
#define PLAY_TIRED         15
#define PLAY_WEIGHT        1
#define PLAY_WIN_CARE      1
#define CLEAN_HAPPY        3
#define CLEAN_CARE         2
#define HEAL_HAPPY         5
#define MOOD_HUNGRY        30
#define MOOD_SLEEPY        25
#define MOOD_SAD           30
#define MOOD_HAPPY         70
#define MOOD_HAPPY_FED     50
#define START_WEIGHT       20
#define START_CARE         50
#define MAX_MESS           4

#define MAX_MIN   100000000L
#define MAX_TIME  ((long long)(sizeof(time_t) > 4 ? 4102444800LL : 2147483647LL))
#define MAX_FILE  1024

static int clampi(int v, int lo, int hi){ return v < lo ? lo : v > hi ? hi : v; }

unsigned tama_rand(tama_t *t, unsigned n){
    unsigned x = t->rng ? t->rng : 1;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    t->rng = x;
    return n ? x % n : 0;
}

void tama_new(tama_t *t, time_t now, unsigned seed){
    memset(t, 0, sizeof *t);
    t->version = TAMA_STATE_VERSION;
    t->stage = TAMA_EGG;
    t->hunger = t->happy = t->energy = 100;
    t->weight = START_WEIGHT;
    t->care = START_CARE;
    t->born = t->last = now;
    t->rng = seed ? seed : 1;
}

static void grow(tama_t *t){
    static const long need[] = { EGG_MIN, BABY_MIN, CHILD_MIN, TEEN_MIN };
    if(t->stage < TAMA_EGG || t->stage > TAMA_TEEN || t->stage_min < need[t->stage]) return;
    if(t->stage == TAMA_TEEN) t->form = t->care < FORM1_CARE ? 0 : t->care < FORM2_CARE ? 1 : 2;
    t->stage++;
    t->stage_min = 0;
}

static int step(tama_t *t, long *slept){
    if(t->age_min < MAX_MIN) t->age_min++;
    if(t->stage_min < MAX_MIN) t->stage_min++;
    if(t->stage == TAMA_EGG){ grow(t); return 1; }
    long m = t->age_min;

    if(t->asleep){
        if(m % SLEEP_ENERGY_EVERY == 0) t->energy = clampi(t->energy + 1, 0, 100);
        if(m % SLEEP_HUNGER_EVERY == 0) t->hunger = clampi(t->hunger - 1, 0, 100);
        if(++*slept >= SLEEP_MAX_MIN){ t->asleep = 0; *slept = 0; }
    } else {
        if(m % HUNGER_EVERY == 0) t->hunger = clampi(t->hunger - 1, 0, 100);
        if(m % HAPPY_EVERY == 0){
            int fast = t->hunger < HUNGRY_FAST || t->poop >= SICK_POOP || t->sick;
            t->happy = clampi(t->happy - (fast ? HAPPY_FAST_MUL : 1), 0, 100);
        }
        if(m % ENERGY_EVERY == 0) t->energy = clampi(t->energy - 1, 0, 100);
        if(t->poop < MAX_MESS && tama_rand(t, POOP_ODDS) == 0) t->poop++;
    }

    if(!t->sick){
        int risk = t->poop >= SICK_POOP || t->hunger == 0 || t->weight > SICK_WEIGHT;
        if(tama_rand(t, risk ? SICK_ODDS_RISK : SICK_ODDS) == 0) t->sick = 1;
    }
    t->starve_min = t->hunger == 0 ? t->starve_min + 1 : 0;
    t->sick_min   = t->sick ? t->sick_min + 1 : 0;
    if(t->starve_min >= STARVE_DEATH_MIN || t->sick_min >= SICK_DEATH_MIN){
        t->stage = TAMA_GONE;
        t->stage_min = 0;
        return 0;
    }

    if(m % CARE_EVERY == 0){
        if(t->hunger >= CARE_GOOD_STAT && t->happy >= CARE_GOOD_STAT && t->poop == 0 && !t->sick) t->care = clampi(t->care + 1, 0, 100);
        else if(t->hunger < CARE_BAD_HUNGER || t->poop >= CARE_BAD_POOP || t->sick) t->care = clampi(t->care - 1, 0, 100);
    }
    grow(t);
    return 1;
}

void tama_advance(tama_t *t, time_t now){
    if(t->stage == TAMA_GONE || now <= t->last) return;
    long long gap = (long long)now - (long long)t->last;
    long long mins = gap / 60;
    if(mins == 0) return;
    int capped = mins > TAMA_MAX_OFFLINE_MIN;
    if(capped) mins = TAMA_MAX_OFFLINE_MIN;
    long slept = 0;
    for(long long i = 0; i < mins; i++) if(!step(t, &slept)) break;
    t->last = capped ? now : (time_t)((long long)t->last + mins * 60);
}

static tama_rc gate(const tama_t *t){
    if(t->stage == TAMA_GONE || t->stage == TAMA_EGG) return TAMA_REFUSED_DEAD;
    if(t->asleep) return TAMA_REFUSED_ASLEEP;
    return TAMA_OK;
}

tama_rc tama_feed_meal(tama_t *t){
    tama_rc rc = gate(t);
    if(rc != TAMA_OK) return rc;
    if(t->hunger >= FULL_HUNGER) return TAMA_REFUSED_FULL;
    t->hunger = clampi(t->hunger + MEAL_HUNGER, 0, 100);
    t->weight = clampi(t->weight + MEAL_WEIGHT, 1, 99);
    t->happy  = clampi(t->happy + MEAL_HAPPY, 0, 100);
    return TAMA_OK;
}

tama_rc tama_feed_snack(tama_t *t){
    tama_rc rc = gate(t);
    if(rc != TAMA_OK) return rc;
    t->hunger = clampi(t->hunger + SNACK_HUNGER, 0, 100);
    t->happy  = clampi(t->happy + SNACK_HAPPY, 0, 100);
    t->weight = clampi(t->weight + SNACK_WEIGHT, 1, 99);
    return TAMA_OK;
}

tama_rc tama_play(tama_t *t, int won){
    tama_rc rc = gate(t);
    if(rc != TAMA_OK) return rc;
    if(t->energy < PLAY_TIRED) return TAMA_REFUSED_TIRED;
    t->happy  = clampi(t->happy + (won ? PLAY_WIN_HAPPY : PLAY_LOSE_HAPPY), 0, 100);
    t->energy = clampi(t->energy - PLAY_ENERGY, 0, 100);
    t->weight = clampi(t->weight - PLAY_WEIGHT, 1, 99);
    if(won) t->care = clampi(t->care + PLAY_WIN_CARE, 0, 100);
    return TAMA_OK;
}

tama_rc tama_clean(tama_t *t){
    tama_rc rc = gate(t);
    if(rc != TAMA_OK) return rc;
    if(t->poop == 0) return TAMA_REFUSED_NOTHING;
    t->poop = 0;
    t->happy = clampi(t->happy + CLEAN_HAPPY, 0, 100);
    t->care  = clampi(t->care + CLEAN_CARE, 0, 100);
    return TAMA_OK;
}

tama_rc tama_heal(tama_t *t){
    tama_rc rc = gate(t);
    if(rc != TAMA_OK) return rc;
    if(!t->sick) return TAMA_REFUSED_NOTHING;
    t->sick = 0;
    t->sick_min = 0;
    t->happy = clampi(t->happy - HEAL_HAPPY, 0, 100);
    return TAMA_OK;
}

tama_rc tama_set_asleep(tama_t *t, int asleep){
    if(t->stage == TAMA_GONE || t->stage == TAMA_EGG) return TAMA_REFUSED_DEAD;
    t->asleep = asleep ? 1 : 0;
    return TAMA_OK;
}

tama_mood_t tama_mood(const tama_t *t){
    if(t->stage == TAMA_GONE) return TAMA_MOOD_DEAD;
    if(t->stage == TAMA_EGG) return TAMA_MOOD_EGG;
    if(t->asleep) return TAMA_MOOD_ASLEEP;
    if(t->sick) return TAMA_MOOD_SICK;
    if(t->hunger < MOOD_HUNGRY) return TAMA_MOOD_HUNGRY;
    if(t->energy < MOOD_SLEEPY) return TAMA_MOOD_SLEEPY;
    if(t->happy < MOOD_SAD) return TAMA_MOOD_SAD;
    if(t->happy >= MOOD_HAPPY && t->hunger >= MOOD_HAPPY_FED) return TAMA_MOOD_HAPPY;
    return TAMA_MOOD_OK;
}

const char *tama_stage_name(int stage){
    static const char *const n[] = { "egg", "baby", "child", "teen", "adult", "gone" };
    return stage >= 0 && stage <= TAMA_GONE ? n[stage] : "?";
}

const char *tama_mood_name(tama_mood_t m){
    static const char *const n[] = { "egg", "happy", "ok", "hungry", "sad", "sleepy", "sick", "asleep", "gone" };
    return (int)m >= 0 && (int)m <= TAMA_MOOD_DEAD ? n[m] : "?";
}

enum { F_VERSION, F_STAGE, F_FORM, F_HUNGER, F_HAPPY, F_ENERGY, F_WEIGHT, F_POOP, F_SICK, F_ASLEEP, F_CARE,
       F_AGE, F_STAGE_MIN, F_STARVE, F_SICK_MIN, F_RNG, F_BORN, F_LAST, F_COUNT };

static const struct { const char *key; long long lo, hi; } fields[F_COUNT] = {
    [F_VERSION] = { "version",    TAMA_STATE_VERSION, TAMA_STATE_VERSION },
    [F_STAGE]   = { "stage",      0, TAMA_GONE },
    [F_FORM]    = { "form",       0, 2 },
    [F_HUNGER]  = { "hunger",     0, 100 },
    [F_HAPPY]   = { "happy",      0, 100 },
    [F_ENERGY]  = { "energy",     0, 100 },
    [F_WEIGHT]  = { "weight",     1, 99 },
    [F_POOP]    = { "poop",       0, MAX_MESS },
    [F_SICK]    = { "sick",       0, 1 },
    [F_ASLEEP]  = { "asleep",     0, 1 },
    [F_CARE]    = { "care",       0, 100 },
    [F_AGE]     = { "age_min",    0, MAX_MIN },
    [F_STAGE_MIN] = { "stage_min", 0, MAX_MIN },
    [F_STARVE]  = { "starve_min", 0, MAX_MIN },
    [F_SICK_MIN] = { "sick_min",  0, MAX_MIN },
    [F_RNG]     = { "rng",        0, 0xFFFFFFFFLL },
    [F_BORN]    = { "born",       0, MAX_TIME },
    [F_LAST]    = { "last",       0, MAX_TIME },
};

static long long get_field(const tama_t *t, int i){
    switch(i){
    case F_VERSION: return t->version;   case F_STAGE: return t->stage;     case F_FORM: return t->form;
    case F_HUNGER:  return t->hunger;    case F_HAPPY: return t->happy;     case F_ENERGY: return t->energy;
    case F_WEIGHT:  return t->weight;    case F_POOP: return t->poop;       case F_SICK: return t->sick;
    case F_ASLEEP:  return t->asleep;    case F_CARE: return t->care;       case F_AGE: return t->age_min;
    case F_STAGE_MIN: return t->stage_min; case F_STARVE: return t->starve_min; case F_SICK_MIN: return t->sick_min;
    case F_RNG:     return (long long)t->rng;
    case F_BORN:    return (long long)t->born;
    default:        return (long long)t->last;
    }
}

static void set_field(tama_t *t, int i, long long v){
    switch(i){
    case F_VERSION: t->version = (int)v;   break;  case F_STAGE: t->stage = (int)v;     break;  case F_FORM: t->form = (int)v;     break;
    case F_HUNGER:  t->hunger = (int)v;    break;  case F_HAPPY: t->happy = (int)v;     break;  case F_ENERGY: t->energy = (int)v; break;
    case F_WEIGHT:  t->weight = (int)v;    break;  case F_POOP: t->poop = (int)v;       break;  case F_SICK: t->sick = (int)v;     break;
    case F_ASLEEP:  t->asleep = (int)v;    break;  case F_CARE: t->care = (int)v;       break;  case F_AGE: t->age_min = (long)v;  break;
    case F_STAGE_MIN: t->stage_min = (long)v; break; case F_STARVE: t->starve_min = (long)v; break; case F_SICK_MIN: t->sick_min = (long)v; break;
    case F_RNG:     t->rng = (unsigned)v;  break;
    case F_BORN:    t->born = (time_t)v;   break;
    default:        t->last = (time_t)v;   break;
    }
}

static int valid(const tama_t *t){
    for(int i = 0; i < F_COUNT; i++){
        long long v = get_field(t, i);
        if(v < fields[i].lo || v > fields[i].hi) return 0;
    }
    return 1;
}

int tama_save(const tama_t *t, const char *path){
    if(!valid(t)) return -1;
    char tmp[256];
    if(snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    char b[MAX_FILE];
    int n = 0;
    for(int i = 0; i < F_COUNT; i++){
        int w = snprintf(b + n, sizeof b - (size_t)n, "%s=%lld\n", fields[i].key, get_field(t, i));
        if(w < 0 || (size_t)w >= sizeof b - (size_t)n) return -1;
        n += w;
    }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) return -1;
    int ok = write(fd, b, (size_t)n) == n && fsync(fd) == 0;
    ok = (close(fd) == 0) && ok;
    if(!ok || rename(tmp, path) != 0){ unlink(tmp); return -1; }
    return 0;
}

static int parse_num(const char *s, size_t len, long long *out){
    if(len == 0 || len > 20) return 0;
    size_t i = s[0] == '-' ? 1 : 0;
    if(i == len) return 0;
    for(size_t k = i; k < len; k++) if(s[k] < '0' || s[k] > '9') return 0;
    char b[24];
    memcpy(b, s, len); b[len] = 0;
    errno = 0;
    char *end = NULL;
    long long v = strtoll(b, &end, 10);
    if(errno || end != b + len) return 0;
    *out = v;
    return 1;
}

int tama_load(tama_t *t, const char *path){
    FILE *f = fopen(path, "r");
    if(!f) return -1;
    char b[MAX_FILE + 1];
    size_t n = fread(b, 1, sizeof b, f);
    int err = ferror(f);
    fclose(f);
    if(err || n == 0 || n > MAX_FILE || memchr(b, 0, n)) return -1;

    tama_t v;
    memset(&v, 0, sizeof v);
    unsigned seen = 0;
    size_t pos = 0;
    while(pos < n){
        const char *line = b + pos;
        const char *nl = memchr(line, '\n', n - pos);
        size_t len = nl ? (size_t)(nl - line) : n - pos;
        pos += len + (nl ? 1 : 0);
        const char *eq = memchr(line, '=', len);
        if(!eq) return -1;
        size_t klen = (size_t)(eq - line);
        int idx = -1;
        for(int i = 0; i < F_COUNT; i++)
            if(strlen(fields[i].key) == klen && memcmp(fields[i].key, line, klen) == 0){ idx = i; break; }
        long long val;
        if(idx < 0 || (seen & (1u << idx))) return -1;
        if(!parse_num(eq + 1, len - klen - 1, &val)) return -1;
        if(val < fields[idx].lo || val > fields[idx].hi) return -1;
        set_field(&v, idx, val);
        seen |= 1u << idx;
    }
    if(seen != (1u << F_COUNT) - 1) return -1;
    if(!valid(&v)) return -1;
    *t = v;
    return 0;
}
