/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SS_PLATFORM_H
#define SS_PLATFORM_H
#include "lvgl/lvgl.h"
#include <stddef.h>
#include <stdint.h>
/* the theme colours mq_ui hands to apps (/tmp/diskos-app-env), 0xRRGGBB */
typedef struct {
    uint32_t canvas, surface, surface_raised, surface_pressed, border, text, text_secondary, text_muted, accent, on_accent;
    int variant, rotation;
} ss_look_t;
extern ss_look_t g_look;
void look_load(void);
lv_color_t look(uint32_t rgb);
int platform_init(void);     /* the Disc's screen and touch; 0 = no screen */
#endif
