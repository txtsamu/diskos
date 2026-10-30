/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FONTFT_H
#define FONTFT_H
#include "lvgl/lvgl.h"

void fontft_init(void);
const lv_font_t *fontft_font(int px);
#endif
