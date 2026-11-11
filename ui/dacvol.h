/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_DACVOL_H
#define DISKOS_DACVOL_H

#include <stdint.h>

#define DACVOL_NAME_MAX 64
#define DACVOL_NO_DB  (-1000000)
#define DACVOL_KEY_UP   251
#define DACVOL_KEY_DOWN 252
#define DACVOL_KEY_DBL_UP   266
#define DACVOL_KEY_DBL_DOWN 267
#define DACVOL_KEY_HOLD_DOWN 262
#define DACVOL_KEY_HOLD_UP   263

typedef struct {
    int  numid, channels, min, max, cur;
    int  db_min_cdb, db_max_cdb;
    char name[DACVOL_NAME_MAX];
} dacvol_ctl_t;

enum { DACVOL_PROBE_PENDING, DACVOL_PROBE_OK, DACVOL_PROBE_NOCTL, DACVOL_PROBE_ERROR };
enum { DACVOL_MIX_IDLE, DACVOL_MIX_BUSY, DACVOL_MIX_FAILED };

struct dacvol_env {
    uint32_t (*now)(void);
    int  (*usb_ready)(void);
    int  (*usb_card)(void);
    int  (*mixer_probe)(int card);
    int  (*mixer_probe_poll)(dacvol_ctl_t *out);
    int  (*mixer_set)(int raw);
    int  (*mixer_state)(void);
    int  (*player_send)(int vol);
    int  (*player_maxvol)(int vol);
    void (*player_maxvol_restore)(void);
    void (*key_active)(int on);
    int  (*key_poll)(int *code, int *value);
    int  (*keys_adjust_volume)(void);
    int  (*single_click_volume)(void);
    int  (*long_press_volume)(void);
    int  (*user_cap)(void);
    void (*log)(const char *msg);
};

void dacvol_init(const struct dacvol_env *env);
void dacvol_tick(void);
int  dacvol_owns_volume(void);
int  dacvol_release_now(int current);
int  dacvol_set_user(int level);
int  dacvol_effective_volume(int player_volume);
int  dacvol_take_change(int *level);

int  dacvol_parse_contents(const char *text, dacvol_ctl_t *out);
int  dacvol_map(const dacvol_ctl_t *ctl, int level);

#endif
