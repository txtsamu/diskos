/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_FONTCHAIN_H
#define DISKOS_FONTCHAIN_H

#include "lvgl/lvgl.h"

void ui_fonts_init(void);

const lv_font_t *ui_text_font(int px);
const lv_font_t *ui_font_cjk(int size);

#endif
