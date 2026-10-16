from pathlib import Path
u=Path(__file__).resolve().parents[2]
# Font source: original coarse alphabet plus one-bit fine DejaVu monospace faces.
from pathlib import Path
patterns={
'A':['01110','10001','10001','11111','10001','10001','10001'],
'B':['11110','10001','10001','11110','10001','10001','11110'],
'C':['01111','10000','10000','10000','10000','10000','01111'],
'D':['11110','10001','10001','10001','10001','10001','11110'],
'E':['11111','10000','10000','11110','10000','10000','11111'],
'F':['11111','10000','10000','11110','10000','10000','10000'],
'G':['01111','10000','10000','10111','10001','10001','01110'],
'H':['10001','10001','10001','11111','10001','10001','10001'],
'I':['11111','00100','00100','00100','00100','00100','11111'],
'J':['00111','00010','00010','00010','10010','10010','01100'],
'K':['10001','10010','10100','11000','10100','10010','10001'],
'L':['10000','10000','10000','10000','10000','10000','11111'],
'M':['10001','11011','10101','10101','10001','10001','10001'],
'N':['10001','11001','10101','10011','10001','10001','10001'],
'O':['01110','10001','10001','10001','10001','10001','01110'],
'P':['11110','10001','10001','11110','10000','10000','10000'],
'Q':['01110','10001','10001','10001','10101','10010','01101'],
'R':['11110','10001','10001','11110','10100','10010','10001'],
'S':['01111','10000','10000','01110','00001','00001','11110'],
'T':['11111','00100','00100','00100','00100','00100','00100'],
'U':['10001','10001','10001','10001','10001','10001','01110'],
'V':['10001','10001','10001','10001','10001','01010','00100'],
'W':['10001','10001','10001','10101','10101','10101','01010'],
'X':['10001','10001','01010','00100','01010','10001','10001'],
'Y':['10001','10001','01010','00100','00100','00100','00100'],
'Z':['11111','00001','00010','00100','01000','10000','11111']}

digits=['01110 10001 10011 10101 11001 10001 01110','00100 01100 00100 00100 00100 00100 01110','01110 10001 00001 00010 00100 01000 11111','11110 00001 00001 01110 00001 00001 11110','00010 00110 01010 10010 11111 00010 00010','11111 10000 10000 11110 00001 00001 11110','01110 10000 10000 11110 10001 10001 01110','11111 00001 00010 00100 01000 01000 01000','01110 10001 10001 01110 10001 10001 01110','01110 10001 10001 01111 00001 00001 01110']
patterns.update({str(i):s.split() for i,s in enumerate(digits)})
patterns.update({':':['00000','00100','00100','00000','00100','00100','00000'],'-':['00000']*3+['11111']+['00000']*3,'/':['00001','00010','00010','00100','01000','01000','10000'],'.':['00000']*5+['00100','00100'],'<':['00010','00100','01000','10000','01000','00100','00010'],'>':['01000','00100','00010','00001','00010','00100','01000']})
from PIL import Image,ImageDraw,ImageFont
out=['/* Original Null5x7 alphabet and DejaVu-derived one-bit fine text. GPL3; see fonts/null/LICENSE-DejaVu.txt. */','#include "lvgl/lvgl.h"']
for name,size,scale in [('null_pixel_14',14,2),('null_pixel_21',21,3),('null_pixel_28',28,4),('null_pixel_56',56,8),('null_pixel_70',70,10),('null_fine_12',12,0),('null_fine_14',14,0),('null_fine_16',16,0),('null_fine_18',18,0),('null_fine_20',20,0),('null_fine_24',24,0)]:
 w=5*scale if scale else round(ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',size).getlength('M'));h=7*scale if scale else size+2;adv=w+(scale if scale else 1);data=[];ds=['{0}']
 for code in range(32,127):
  if scale:
   pat=patterns.get(chr(code).upper(),None)
   if pat is None:
    im=Image.new('L',(5,7));ImageDraw.Draw(im).text((0,-2),chr(code),font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',8),fill=255);pat=[''.join('1' if im.getpixel((x,y))>=100 else '0' for x in range(5)) for y in range(7)]
   bits=[int(pat[y//scale][x//scale]) for y in range(h) for x in range(w)]
  else:
   im=Image.new('L',(w,h));ImageDraw.Draw(im).text((0,-1),chr(code),font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',size),fill=255);bits=[int(v>=105) for v in im.getdata()]
  packed=[sum(v<<(7-j) for j,v in enumerate(bits[i:i+8])) for i in range(0,len(bits),8)]
  ds.append('{.bitmap_index=%d,.adv_w=%d,.box_w=%d,.box_h=%d}'%(len(data),adv*16,w,h));data+=packed
 out.extend([f'static const uint8_t {name}_bits[]={{'+','.join(map(str,data))+'};',f'static const lv_font_fmt_txt_glyph_dsc_t {name}_glyphs[]={{'+','.join(ds)+'};',f'static const lv_font_fmt_txt_cmap_t {name}_map[]={{{{.range_start=32,.range_length=95,.glyph_id_start=1,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}}}};',f'static const lv_font_fmt_txt_dsc_t {name}_dsc={{.glyph_bitmap={name}_bits,.glyph_dsc={name}_glyphs,.cmaps={name}_map,.cmap_num=1,.bpp=1}};',f'const lv_font_t {name}={{.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height={h+2},.base_line=0,.dsc=&{name}_dsc}};'])
(u/'font_null.c').write_text('\n'.join(out)+'\n')
