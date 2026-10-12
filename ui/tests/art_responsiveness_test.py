#!/usr/bin/env python3
"""Production pixel/cache helpers, real files, SD admission and SQLite locks."""
from pathlib import Path
import subprocess, tempfile, struct

app = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='diskos-responsiveness-') as td:
    root = Path(td)
    history = root/'history'
    common = ['cc', '-O1', '-g', '-D_GNU_SOURCE', '-DLV_CONF_INCLUDE_SIMPLE',
              '-ffunction-sections', '-fdata-sections', '-I'+str(app),
              '-Wl,--gc-sections', '-pthread']
    subprocess.run(common + [str(app/'tests/history_async_test.c'), str(app/'sqlite3.c'), '-lm', '-o', str(history)], check=True)
    subprocess.run([str(history), str(root/'history.db')], check=True, timeout=10)
    sd = root/'sd'; sd.mkdir()
    track = sd/'track.flac'; track.write_bytes(b'identity')
    sharp = root/'sharp'; sharp.write_bytes(bytes((11, 71, 209, 255)) * 364 * 364)
    # Fixed decoder BMP layout, bottom-up. Distinct first/last rows catch inversion.
    for size in (148, 360):
        pixels = b''.join(bytes((y % 256, 71, 209)) * size for y in range(size))
        header = b'BM'+struct.pack('<IHHI',54+len(pixels),0,0,54)+struct.pack('<IiiHHIIiiII',40,size,size,1,24,0,len(pixels),0,0,0,0)
        (root/f'{size}.bmp').write_bytes(header+pixels)
    source = root/'cache_test.c'
    source.write_text(r'''
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include "art_pixels.h"
#include "artcache.h"
static int local=1, writer=1, leases;
int sd_io_begin(void){if(!local)return 0;leases++;return 1;}
void sd_io_end(void){assert(leases>0);leases--;}
int sd_write_begin(void){return writer;}
void sd_write_end(void){}
int main(int argc,char **argv){
 assert(argc==6);char fp[24],out[1024],cache[1024];
 snprintf(out,sizeof out,"%s/out",argv[1]);
 assert(artcache_key(argv[2],fp,sizeof fp)==0);
 writer=0;artcache_put_sharp_if(argv[2],fp,argv[3]);assert(artcache_get_sharp(argv[2],out)<0);
 writer=1;artcache_put_sharp_if(argv[2],"wrong",argv[3]);assert(artcache_get_sharp(argv[2],out)<0);
 artcache_put_sharp_if(argv[2],fp,argv[3]);assert(artcache_get_sharp(argv[2],out)==0);
 uint8_t *rgb=malloc(364*364*3),*bg=malloc(360*360*4),*tint=malloc(360*360*4);
 assert(rgb&&bg&&tint);assert(art_pixels_sharp(out,rgb)==0);
 assert(rgb[0]==11&&rgb[1]==71&&rgb[2]==209);
 assert(art_pixels_bmp(argv[4],148,bg)==0&&bg[0]==147&&bg[3]==255);
 assert(art_pixels_bmp(argv[5],360,bg)==0&&bg[0]==(359%256));
 art_pixels_tint(bg,tint,0x123456,0);
 assert(tint[0]==(uint8_t)((bg[0]*112+0x56*144)>>8));
 art_pixels_tint(bg,tint,0x123456,1);
 assert(tint[2]==(uint8_t)((bg[2]*84+0x12*172)>>8));
 local=0;assert(artcache_get_sharp(argv[2],out)<0);local=1;
 snprintf(cache,sizeof cache,ART_SD_ROOT "/.diskos/artcache/%s/p7.bgra",fp);
 assert(truncate(cache,12)==0);assert(artcache_get_sharp(argv[2],out)<0);
 assert(truncate(argv[4],54)==0);assert(art_pixels_bmp(argv[4],148,bg)<0);
 assert(truncate(out,12)==0);assert(art_pixels_sharp(out,rgb)<0);
 artcache_put_sharp_if(argv[2],fp,argv[3]);
 FILE *f=fopen(argv[2],"ab");assert(f);fputc(1,f);fclose(f);
 assert(artcache_get_sharp(argv[2],out)<0);assert(leases==0);
 free(rgb);free(bg);free(tint);
 puts("PASS: sharp cache hit/miss/invalidation, SD guards, corrupt files, pixel orientation/tint");
}
''')
    exe = root/'cache'
    subprocess.run(common + ['-DART_SD_ROOT="'+str(sd)+'"', str(source),
                           str(app/'artcache.c'), str(app/'art.c'), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe),str(root),str(track),str(sharp),str(root/'148.bmp'),str(root/'360.bmp')],check=True,timeout=10)
