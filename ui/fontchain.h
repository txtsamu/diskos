/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_FONTCHAIN_H
#define DISKOS_FONTCHAIN_H

#include "lvgl/lvgl.h"

/* The user-text font chain: Montserrat at a theme size, then (where generated) the Cyrillic/Greek/Latin-ext font, the stock
 * TTC (CJK, when usable), Source Han 16 and the music-metadata supplement, so track, album and artist names render with full
 * glyph coverage. Needs LVGL's font types only; the theme base font and the TTC layer are looked up through theme.h and
 * fontft.h (a host test links fakes of those and the real LVGL font code). tests/font_coverage_test.py rasterizes it. */

/* Idempotent; ui_font_cjk() calls it, and ui_create() calls it eagerly. */
void ui_fonts_init(void);

/* Fallback-chained user-text font for a theme role size (10..40); a size that is not a role falls back to 16. */
const lv_font_t *ui_text_font(int px);
const lv_font_t *ui_font_cjk(int size);

#endif
