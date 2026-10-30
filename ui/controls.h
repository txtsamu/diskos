/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_CONTROLS_H
#define DISKOS_CONTROLS_H
#include <stddef.h>
#include <stdint.h>

#define CTL_ROT_N 4
typedef enum { CTL_KEY_SINGLE, CTL_KEY_DOUBLE, CTL_KEY_LONG, CTL_KEY_N } ctl_key_t;
enum { CTL_ACT_TRACK = 0, CTL_ACT_VOLUME = 1 };
typedef enum { CTL_DO_TRACK, CTL_DO_VOLUME } ctl_effect_t;

int  ctl_supported_ver(int os_ver);
int  ctl_supported(void);

int  ctl_rot_deg(int v);
void ctl_rot_map(int v, int w, int h, int x, int y, int *rx, int *ry);
void ctl_rot_touch_cal(int v, int size, int *swap, int *min_x, int *min_y, int *max_x, int *max_y);
void ctl_blit(uint8_t *base, uint32_t line_bytes, int w, int h, int v, const uint32_t *src, int x1, int y1, int x2, int y2);
static inline void ctl_flush_default(uint8_t *base, uint32_t line, int W, int H, const uint32_t *src, int x1, int y1, int x2, int y2){
    int aw = x2 - x1 + 1;
    for(int sy = y1; sy <= y2; sy++){
        const uint32_t *srcrow = src + (size_t)(sy - y1) * aw;
        uint32_t *dstrow = (uint32_t*)(base + (size_t)(H - 1 - sy) * line);
        for(int sx = x1; sx <= x2; sx++)
            dstrow[W - 1 - sx] = srcrow[sx - x1];
    }
}
int  ctl_rot_get(void);
void ctl_rot_set(int v);

const char *ctl_key_tag(int key);
const char *ctl_key_column(int key);
ctl_effect_t ctl_key_effect(int action);

int  ctl_frame_rot(char *buf, size_t n, int v);
int  ctl_frame_key(char *buf, size_t n, int key, int act);
int  ctl_apply_rot_via(int (*send)(const char *), int v);
int  ctl_apply_key_via(int (*send)(const char *), int key, int act);

typedef struct { int rot, key[CTL_KEY_N]; } ctl_state_t;
int  ctl_read_state(const char *db_path, ctl_state_t *st);
void ctl_mirror_via(const ctl_state_t *st, void (*set_int)(const char *key, int v));
#define CTL_ROLLBACK_MS 10000
typedef struct { int pending, prev, cur; uint32_t start; } ctl_rb_t;
void ctl_rb_begin(ctl_rb_t *r, int prev, int cur, uint32_t now_ms);
int  ctl_rb_left_ms(const ctl_rb_t *r, uint32_t now_ms);
int  ctl_rb_expired(ctl_rb_t *r, uint32_t now_ms, int *revert_to);
int  ctl_rb_keep(ctl_rb_t *r, int *value);
int  ctl_rb_revert(ctl_rb_t *r, int *value);
#define CTL_SYSCONFIG "/usr/data/fiio/db/sysconfig.db"
#define CTL_CFG_ROT "screen_rot"

#ifndef CTL_CORE_ONLY
void controls_boot_rotation(void);
void controls_startup_mirror(void);
int  ui_set_screen_rot(int v);
void controls_tick(void);
int  ctl_rotation_pending(void);
void ctl_prompt_keep(void);
void ctl_prompt_revert(void);
int  ui_set_volume_key(int key, int act);
#endif
#endif
