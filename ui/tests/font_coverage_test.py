#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
"""Check the real fallback chain (fontchain.c, linked) and rasterize reported glyphs with LVGL on the host."""
from pathlib import Path
import os, subprocess, tempfile
app = Path(__file__).resolve().parents[1]
lvgl = next(c for c in (app/'lvgl', app.parent/'lvgl') if (c/'src/font/lv_font.c').exists())
points=sorted(set(map(ord,'世界贈予我的妖精帝國曼衍珠汝華Nada Upasana PundarikaLudwig GöranssonZeus’s Law‘’ЛенинградΑθήνα'))- {32})
header=r'''
#include "fontchain.h"
#include "theme.h"
#include "fontft.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
/* Host A8 target. Glyph lookup and unpacking use the real vendored LVGL code. */
uint32_t lv_draw_buf_width_to_stride(uint32_t w, lv_color_format_t cf){
    assert(cf == LV_COLOR_FORMAT_A8);
    return (w + LV_DRAW_BUF_STRIDE_ALIGN - 1) & ~(LV_DRAW_BUF_STRIDE_ALIGN - 1);
}
/* ui_font_cjk(size) takes a runtime size, so the chain base is a function. Only the sizes this harness links
 * (14/16/18/20) are mapped; like the real theme_font_base, anything else falls back to Montserrat 16. */
const lv_font_t *theme_font_base(int px){
    switch(px){
    case 14: return &lv_font_montserrat_14;
    case 18: return &lv_font_montserrat_18;
    case 20: return &lv_font_montserrat_20;
    default: return &lv_font_montserrat_16;
    }
}
/* No stock TTC on the host: the FreeType layer is disabled, exactly as fontft_font() behaves without the font file. */
const lv_font_t *fontft_font(int px){ (void)px; return NULL; }
'''
test=r'''
int main(void){
    unsigned cp[] = {POINTS};
    int sizes[] = {14,16,18,20};
    for(unsigned s=0;s<4;s++) for(unsigned i=0;i<sizeof cp/sizeof cp[0];i++){
        lv_font_glyph_dsc_t d={0}; const lv_font_t *f=ui_text_font(sizes[s]);
        if(!lv_font_get_glyph_dsc(f,&d,cp[i],0) || d.is_placeholder || !d.resolved_font){
            fprintf(stderr,"missing U+%04X at %dpx\n",cp[i],sizes[s]); return 1;
        }
        const lv_font_fmt_txt_dsc_t *fd=d.resolved_font->dsc;
        assert(fd->bitmap_format==LV_FONT_FMT_TXT_PLAIN);
        assert(d.box_w>0 && d.box_h>0 && d.box_w<=64 && d.box_h<=64);
        uint8_t pixels[4096]={0}; lv_draw_buf_t draw={0}; draw.data=pixels;
        assert(lv_font_get_glyph_bitmap(&d,&draw)!=NULL);
        int visible=0; for(unsigned j=0;j<sizeof pixels;j++) visible |= pixels[j];
        assert(visible);
        /* Ensure every glyph fits the line box used by its primary font. */
        int top=(int)f->line_height-f->base_line-d.box_h-d.ofs_y;
        int bottom=(int)f->line_height-f->base_line-d.ofs_y;
        assert(top>=0 && bottom<=(int)f->line_height);
    }
    puts("PASS reported CJK/punctuation plus Latin, Cyrillic and Greek: descriptors, pixels and line metrics at four sizes");
    puts("HARNESS COMPLETE");
}
'''.replace('POINTS',','.join(hex(c) for c in points))
def run_harness(exe, timeout):
    """Run a compiled harness; its LAST stdout line must be HARNESS COMPLETE, so an early return 0 fails."""
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=timeout)
    print(r.stdout, end='')
    if r.stderr:
        print(r.stderr, end='')
    if r.returncode != 0:
        raise SystemExit(f'FAIL harness exited {r.returncode}')
    lines = r.stdout.strip().splitlines()
    if not lines or lines[-1] != 'HARNESS COMPLETE':
        raise SystemExit('FAIL harness did not reach its HARNESS COMPLETE line')


with tempfile.TemporaryDirectory(prefix='diskos-font-test-') as tmp:
    p=Path(tmp); (p/'test.c').write_text(header+test)
    sources=[p/'test.c', app/'fontchain.c', lvgl/'src/font/lv_font.c', lvgl/'src/font/lv_font_fmt_txt.c',lvgl/'src/misc/lv_utils.c',app/'font_cjk_extra_16.c',lvgl/'src/font/lv_font_source_han_16_cjk.c']
    for size in [14,16,18,20]: sources += [app/f'font_intl_{size}.c',lvgl/f'src/font/lv_font_montserrat_{size}.c']
    cmd=[os.environ.get('CC','cc'),'-g','-O1','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(app),'-I'+str(app.parent),*[str(x) for x in sources],'-Wl,--gc-sections','-o',str(p/'test')]
    subprocess.run(cmd,check=True);run_harness(p/'test',15)

print('COMPLETE font_coverage_test.py')
