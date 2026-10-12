/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ART_PIXELS_H
#define ART_PIXELS_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "art.h"

/* Worker-only admission: retain 6 MiB of available RAM after preparation.
 * Under pressure use the original file-to-static-buffer apply path instead. */
#ifndef ART_MEMINFO
#define ART_MEMINFO "/proc/meminfo"
#endif
static inline int art_pixels_memory_ok(size_t bytes){
    FILE *f=fopen(ART_MEMINFO,"r");if(!f)return 0;
    char line[128];unsigned long long kb=0;int found=0;
    while(fgets(line,sizeof line,f))if(sscanf(line,"MemAvailable: %llu kB",&kb)==1){found=1;break;}
    fclose(f);return found && kb*1024ULL >= (unsigned long long)bytes+6*1024*1024;
}

/* Decoder files have a fixed, validated layout. No LVGL or shared scratch. */
static inline int art_pixels_bmp(const char *path, int size, uint8_t *dst){
    if(!art_bmp_valid(path, size)) return -1;
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    uint8_t row[360 * 3]; int ok = size > 0 && size <= 360;
    if(!ok || fseek(f, 54, SEEK_SET)){ fclose(f); return -1; }
    for(int y = 0; y < size && ok; y++){
        if(fread(row, 1, (size_t)size * 3, f) != (size_t)size * 3){ ok = 0; break; }
        uint8_t *d = dst + (size_t)(size - 1 - y) * size * 4;
        for(int x = 0; x < size; x++){
            memcpy(d + x * 4, row + x * 3, 3); d[x * 4 + 3] = 0xff;
        }
    }
    fclose(f); return ok ? 0 : -1;
}

static inline int art_pixels_sharp(const char *path, uint8_t *dst){
    struct stat st;
    if(stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size != 364 * 364 * 4) return -1;
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    uint8_t row[364 * 4]; int ok = 1;
    for(int y = 0; y < 364; y++){
        if(fread(row, 1, sizeof row, f) != sizeof row){ ok = 0; break; }
        for(int x = 0; x < 364; x++) memcpy(dst + ((size_t)y * 364 + x) * 3, row + x * 4, 3);
    }
    fclose(f); return ok ? 0 : -1;
}
static inline void art_pixels_tint(const uint8_t *src, uint8_t *dst, uint32_t canvas, int light){
    int cb = canvas & 255, cg = (canvas >> 8) & 255, cr = (canvas >> 16) & 255;
    int keep = light ? 84 : 112;
    for(int i = 0; i < 360 * 360; i++){
        const uint8_t *p = src + (size_t)i * 4; uint8_t *o = dst + (size_t)i * 4;
        o[0] = (uint8_t)((p[0] * keep + cb * (256 - keep)) >> 8);
        o[1] = (uint8_t)((p[1] * keep + cg * (256 - keep)) >> 8);
        o[2] = (uint8_t)((p[2] * keep + cr * (256 - keep)) >> 8);
        o[3] = 255;
    }
}
#endif
