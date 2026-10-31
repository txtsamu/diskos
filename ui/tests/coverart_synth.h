/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef COVERART_SYNTH_H
#define COVERART_SYNTH_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYN_N 148
#define SYN_KINDS 28

static inline uint32_t syn_lcg(uint32_t *s){ *s = *s * 1664525u + 1013904223u; return *s >> 8; }
static inline int syn_clamp(int v){ return v < 0 ? 0 : (v > 255 ? 255 : v); }

static inline void syn_hsv(float h, float s, float v, int *r, int *g, int *b){
    float c = v * s; float hh = h * 6.0f; int i = (int)hh; float f = hh - (float)i;
    float p = v - c, q = v - c * f, t = v - c * (1.0f - f);
    float R, G, B;
    switch(i % 6){
        case 0: R = v; G = t; B = p; break;
        case 1: R = q; G = v; B = p; break;
        case 2: R = p; G = v; B = t; break;
        case 3: R = p; G = q; B = v; break;
        case 4: R = t; G = p; B = v; break;
        default: R = v; G = p; B = q; break;
    }
    *r = (int)(R * 255.0f + 0.5f); *g = (int)(G * 255.0f + 0.5f); *b = (int)(B * 255.0f + 0.5f);
}

static inline void syn_pixel(int kind, int x, int y, uint32_t *seed, int *r, int *g, int *b){
    int u = x * 255 / (SYN_N - 1), w = y * 255 / (SYN_N - 1);
    switch(kind){
        case 0: *r = *g = *b = 128; break;
        case 1: *r = *g = *b = 0; break;
        case 2: *r = *g = *b = 255; break;
        case 3: *r = 220; *g = 30; *b = 40; break;
        case 4: *r = 30; *g = 200; *b = 60; break;
        case 5: *r = 20; *g = 60; *b = 230; break;
        case 6: *r = 30; *g = 220; *b = 230; break;
        case 7: *r = 230; *g = 40; *b = 220; break;
        case 8: *r = 240; *g = 230; *b = 40; break;
        case 9: *r = 240; *g = 140; *b = 20; break;
        case 10: *r = 250; *g = 200; *b = 210; break;
        case 11: *r = 10; *g = 12; *b = 60; break;
        case 12: *r = u; *g = u; *b = u; break;
        case 13: *r = w; *g = 255 - w; *b = 128; break;
        case 14: syn_hsv((float)(x + y) / (float)(2 * SYN_N), 0.85f, 0.9f, r, g, b); break;
        case 15:
            if(x < SYN_N / 2){ *r = 200; *g = 20; *b = 30; } else { *r = 20; *g = 30; *b = 200; }
            break;
        case 16:
            if(((x / 12) + (y / 12)) & 1){ *r = 240; *g = 240; *b = 240; } else { *r = 20; *g = 20; *b = 120; }
            break;
        case 17: *r = (int)(syn_lcg(seed) & 255); *g = (int)(syn_lcg(seed) & 255); *b = (int)(syn_lcg(seed) & 255); break;
        case 18: *r = 150 + (int)(syn_lcg(seed) & 63); *g = 70 + (int)(syn_lcg(seed) & 31); *b = 30 + (int)(syn_lcg(seed) & 15); break;
        case 19: {
            int dx = x - 74, dy = y - 74; int d = dx * dx + dy * dy; int v = d > 5476 ? 0 : 255 - d * 255 / 5476;
            *r = v; *g = v / 2; *b = 255 - v;
        } break;
        case 20: *r = 120; *g = 124; *b = 130; break;
        case 21: *r = 235; *g = 190; *b = 160; break;
        case 22: {
            int dx = x - 74, dy = y - 74;
            if(dx * dx + dy * dy < 400){ *r = 250; *g = 60; *b = 20; } else { *r = 8; *g = 8; *b = 10; }
        } break;
        case 23: syn_hsv(0.08f + 0.0004f * (float)y, 0.45f, 0.55f + 0.001f * (float)x, r, g, b); break;
        case 24: syn_hsv((float)x / (float)SYN_N, 0.95f, 0.5f, r, g, b); break;
        case 25: *r = 40 + (int)(syn_lcg(seed) & 15); *g = 40 + (int)(syn_lcg(seed) & 15); *b = 44 + (int)(syn_lcg(seed) & 15); break;
        case 26:
            if(y < 100){ *r = 18; *g = 90; *b = 70; } else { *r = 230; *g = 230; *b = 100; }
            break;
        default: syn_hsv((float)((x * 7 + y * 3) % 360) / 360.0f, 0.6f, 0.75f, r, g, b); break;
    }
    *r = syn_clamp(*r); *g = syn_clamp(*g); *b = syn_clamp(*b);
}

static inline void syn_bgra(int kind, uint8_t *buf){
    uint32_t seed = 12345u + (uint32_t)kind;
    for(int y = 0; y < SYN_N; y++) for(int x = 0; x < SYN_N; x++){
        int r, g, b; syn_pixel(kind, x, y, &seed, &r, &g, &b);
        uint8_t *p = buf + ((size_t)y * SYN_N + x) * 4;
        p[0] = (uint8_t)b; p[1] = (uint8_t)g; p[2] = (uint8_t)r; p[3] = 0xFF;
    }
}

static inline void syn_put_le(unsigned char *p, uint32_t v, int n){ for(int i = 0; i < n; i++) p[i] = (unsigned char)(v >> (8 * i)); }

static inline int syn_write_bmp_n(const char *path, int n, int kind){
    uint32_t stride = ((uint32_t)n * 3 + 3) & ~3u, dsz = stride * (uint32_t)n;
    unsigned char h[54]; memset(h, 0, sizeof h);
    h[0] = 'B'; h[1] = 'M'; syn_put_le(h + 2, 54 + dsz, 4); syn_put_le(h + 10, 54, 4); syn_put_le(h + 14, 40, 4);
    syn_put_le(h + 18, (uint32_t)n, 4); syn_put_le(h + 22, (uint32_t)n, 4); syn_put_le(h + 26, 1, 2); syn_put_le(h + 28, 24, 2);
    syn_put_le(h + 34, dsz, 4);
    FILE *f = fopen(path, "wb"); if(!f) return -1;
    fwrite(h, 1, 54, f);
    uint32_t seed = 12345u + (uint32_t)kind;
    unsigned char *img = calloc(1, (size_t)n * stride);
    for(int y = 0; y < n; y++) for(int x = 0; x < n; x++){
        int r, g, b; int sx = x * SYN_N / n, sy = y * SYN_N / n;
        syn_pixel(kind, sx, sy, &seed, &r, &g, &b);
        unsigned char *p = img + (size_t)y * stride + (size_t)x * 3;
        p[0] = (unsigned char)b; p[1] = (unsigned char)g; p[2] = (unsigned char)r;
    }
    for(int y = n - 1; y >= 0; y--) fwrite(img + (size_t)y * stride, 1, stride, f);
    free(img);
    return fclose(f) == 0 ? 0 : -1;
}

static inline int syn_write_bmp(const char *path, int kind){ return syn_write_bmp_n(path, SYN_N, kind); }

static const struct { int rc; uint32_t rgb; } syn_gold[SYN_KINDS] = {
    {1, 0xAEB4BE},
    {1, 0xAEB4BE},
    {1, 0xAEB4BE},
    {1, 0xF53E3E},
    {1, 0x3BC64A},
    {1, 0x4D7EFB},
    {1, 0x1EDCE6},
    {1, 0xD74FCD},
    {1, 0xD9D028},
    {1, 0xF08C14},
    {1, 0xFAB4C3},
    {1, 0x7584C0},
    {1, 0xAEB4BE},
    {1, 0x38C880},
    {1, 0x20A0E6},
    {1, 0xF04641},
    {1, 0x647FE7},
    {1, 0x88E076},
    {1, 0xD3632B},
    {1, 0xF17810},
    {1, 0xAEB4BE},
    {1, 0xF4BB94},
    {1, 0xF64320},
    {1, 0xA89154},
    {1, 0x469939},
    {1, 0xAEB4BE},
    {1, 0xD2D24E},
    {1, 0x63BF5B},
};

#endif
