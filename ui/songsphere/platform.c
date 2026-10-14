/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What a stand-alone app needs from the Disc: the screen (mq_ui's own fb_pan.c, linked as is), the screen rotation
 * and the touch panel, plus the look mq_ui hands over in /tmp/diskos-app-env. The rotation helpers are the ones in
 * controls.c, trimmed to what fb_pan.c calls: the rotation comes from the env file instead of the player database. */
#include "platform.h"
#include "fb_pan.h"
#include "controls.h"
#include "lvgl/src/drivers/evdev/lv_evdev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

ss_look_t g_look = {                                   /* used when the app is started without mq_ui's env file */
    .canvas = 0x0B0D12, .surface = 0x1A1F29, .surface_raised = 0x232A36, .surface_pressed = 0x2A3240, .border = 0x2A3140,
    .text = 0xF2F4F8, .text_secondary = 0xA9B4C3, .text_muted = 0x6B7686, .accent = 0x22D3EE, .on_accent = 0x061016,
    .variant = 0, .rotation = 0 };

void look_load(void){
    FILE *f = fopen("/tmp/diskos-app-env", "r"); if(!f) return;
    char line[96];
    if(!fgets(line, sizeof line, f) || strncmp(line, "diskos-app-env-v1", 17)){ fclose(f); return; }
    static const struct { const char *k; size_t off; } K[] = {
        { "canvas", offsetof(ss_look_t, canvas) }, { "surface", offsetof(ss_look_t, surface) },
        { "surface_raised", offsetof(ss_look_t, surface_raised) }, { "surface_pressed", offsetof(ss_look_t, surface_pressed) },
        { "border", offsetof(ss_look_t, border) }, { "text", offsetof(ss_look_t, text) },
        { "text_secondary", offsetof(ss_look_t, text_secondary) }, { "text_muted", offsetof(ss_look_t, text_muted) },
        { "accent", offsetof(ss_look_t, accent) }, { "on_accent", offsetof(ss_look_t, on_accent) } };
    while(fgets(line, sizeof line, f)){
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '='); if(!eq) continue; *eq = 0; const char *v = eq + 1;
        if(!strcmp(line, "variant")) g_look.variant = atoi(v);
        else if(!strcmp(line, "rotation")) g_look.rotation = atoi(v);
        else for(size_t i = 0; i < sizeof K / sizeof K[0]; i++)
            if(!strcmp(line, K[i].k)) *(uint32_t *)((char *)&g_look + K[i].off) = (uint32_t)strtoul(v, NULL, 16) & 0xFFFFFF;
    }
    fclose(f);
}
lv_color_t look(uint32_t rgb){ return lv_color_hex(rgb); }

/* ---- rotation (as controls.c) ---- */
static const int ROT_DEG[CTL_ROT_N] = { 180, 270, 0, 90 };
static int g_rot;
int ctl_rot_deg(int v){ return (v >= 0 && v < CTL_ROT_N) ? ROT_DEG[v] : 180; }
int ctl_rot_get(void){ return g_rot; }
void ctl_rot_set(int v){ if(v >= 0 && v < CTL_ROT_N) g_rot = v; }
void controls_boot_rotation(void){ ctl_rot_set(g_look.rotation); }
void ctl_rot_touch_cal(int v, int size, int *swap, int *min_x, int *min_y, int *max_x, int *max_y){
    int e = size - 1;
    switch(ctl_rot_deg(v)){
        case 0:   *swap = 0; *min_x = 0; *min_y = 0; *max_x = e; *max_y = e; break;
        case 90:  *swap = 1; *min_x = 0; *min_y = e; *max_x = e; *max_y = 0; break;
        case 270: *swap = 1; *min_x = e; *min_y = 0; *max_x = 0; *max_y = e; break;
        default:  *swap = 0; *min_x = e; *min_y = e; *max_x = 0; *max_y = 0; break;
    }
}
void ctl_blit(uint8_t *base, uint32_t line_bytes, int w, int h, int v, const uint32_t *src, int x1, int y1, int x2, int y2){
    if(!base || !src || x1 < 0 || y1 < 0 || x2 >= w || y2 >= h || x1 > x2 || y1 > y2) return;
    int d = ctl_rot_deg(v);
    if(w != h && (d == 90 || d == 270)) d = 180;
    const int aw = x2 - x1 + 1;
    switch(d){
        case 0:
            for(int sy = y1; sy <= y2; sy++)
                memcpy(base + (size_t)sy * line_bytes + (size_t)x1 * 4, src + (size_t)(sy - y1) * aw, (size_t)aw * 4);
            break;
        case 90:
            for(int sy = y1; sy <= y2; sy++){ const uint32_t *row = src + (size_t)(sy - y1) * aw;
                for(int sx = x1; sx <= x2; sx++) ((uint32_t *)(base + (size_t)sx * line_bytes))[h - 1 - sy] = row[sx - x1]; }
            break;
        case 270:
            for(int sy = y1; sy <= y2; sy++){ const uint32_t *row = src + (size_t)(sy - y1) * aw;
                for(int sx = x1; sx <= x2; sx++) ((uint32_t *)(base + (size_t)(w - 1 - sx) * line_bytes))[sy] = row[sx - x1]; }
            break;
        default:
            for(int sy = y1; sy <= y2; sy++){ const uint32_t *row = src + (size_t)(sy - y1) * aw;
                uint32_t *dst = (uint32_t *)(base + (size_t)(h - 1 - sy) * line_bytes);
                for(int sx = x1; sx <= x2; sx++) dst[w - 1 - sx] = row[sx - x1]; }
            break;
    }
}

int platform_init(void){
    for(int i = 0; i < 20; i++){ if(fbpan_create("/dev/fb0")) break; if(i == 19) return 0; usleep(150000); }
    if(access("/usr/data/touch_off", 0) != 0){
        lv_indev_t *t = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event1");
        if(t){ fbpan_bind_touch(t); lv_indev_set_scroll_limit(t, 10); lv_indev_set_long_press_time(t, 900); }
    }
    return 1;
}
lv_color_t sphere_rgb(uint32_t rgb){ return lv_color_hex(rgb); }   /* the add-on: plain LVGL */
