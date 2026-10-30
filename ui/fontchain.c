/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "fontchain.h"
#include "theme.h"
#include "fontft.h"
#include "fonts_intl.h"

static lv_font_t s_cjk;
static lv_font_t s_ft[41], s_font[41], s_intl[41];
static const lv_font_t *ui_intl_font(int px)
{
    switch(px) {
    case 14: return &font_intl_14;
    case 16: return &font_intl_16;
    case 18: return &font_intl_18;
    case 20: return &font_intl_20;
    default: return NULL;
    }
}
void ui_fonts_init(void)
{
    if(s_cjk.get_glyph_dsc) return;
    s_cjk = lv_font_source_han_16_cjk; s_cjk.fallback = &font_cjk_extra_16;
}

const lv_font_t *ui_text_font(int px){
    return ui_font_cjk(px);
}

const lv_font_t *ui_font_cjk(int size)
{
    ui_fonts_init();
    if(size != 10 && size != 12 && size != 14 && size != 16 && size != 18 && size != 20 &&
       size != 22 && size != 24 && size != 28 && size != 32 && size != 36 && size != 40) size = 16;
    const lv_font_t *base = theme_font_base(size);
    if(!s_font[size].get_glyph_dsc) {
        const lv_font_t *intl = ui_intl_font(size);
        const lv_font_t *ft = fontft_font(size);
        const lv_font_t *next = &s_cjk;
        if(ft) { s_ft[size] = *ft; s_ft[size].fallback = next; next = &s_ft[size]; }
        if(intl) { s_intl[size] = *intl; s_intl[size].fallback = next; next = &s_intl[size]; }
        s_font[size] = *base;
        s_font[size].fallback = next;
    }
    return &s_font[size];
}
