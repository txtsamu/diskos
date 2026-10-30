/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum { ST_OK = 0, ST_USAGE = 2, ST_NOPIC = 3, ST_UNSUPPORTED = 4, ST_MALFORMED = 5, ST_LIMIT = 6, ST_IO = 7 };

#define MAX_PIC_BYTES   (8u << 20)
#define MAX_SIDE        4096
#define MAX_PIXELS      4194304u
#define BUDGET_BYTES    (32u << 20)
#define MAX_TAG_READ    (9u << 20)
#define MAX_CANDIDATES  16
#define MAX_WALK        4096
#define MAX_DEPTH       8

static size_t g_live, g_peak;
static int g_over_budget;
typedef union { size_t n; max_align_t a; } bhdr;
static void *bmalloc(size_t n){
    if(n > BUDGET_BYTES - sizeof(bhdr) || g_live + sizeof(bhdr) + n > BUDGET_BYTES){ g_over_budget = 1; return NULL; }
    bhdr *h = malloc(sizeof(bhdr) + n);
    if(!h){ g_over_budget = 1; return NULL; }
    h->n = n; g_live += sizeof(bhdr) + n; if(g_live > g_peak) g_peak = g_live;
    return h + 1;
}
static void bfree(void *p){
    if(!p) return;
    bhdr *h = (bhdr *)p - 1;
    g_live -= sizeof(bhdr) + h->n; free(h);
}
static void *brealloc(void *p, size_t n){
    if(!p) return bmalloc(n);
    bhdr *h = (bhdr *)p - 1;
    size_t old = h->n;
    void *q = bmalloc(n);
    if(!q) return NULL;
    memcpy(q, p, old < n ? old : n);
    bfree(p);
    return q;
}

#define STBI_MALLOC(sz)          bmalloc(sz)
#define STBI_REALLOC(p, newsz)   brealloc(p, newsz)
#define STBI_FREE(p)             bfree(p)
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_MAX_DIMENSIONS MAX_SIDE
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static int g_fd = -1;
static uint64_t g_fsize;

static int g_ioerr;
#ifdef ARTDEC_TESTING
static ssize_t test_pread(int fd, void *b, size_t n, off_t o){
    static long left = -2;
    if(left == -2){ const char *e = getenv("ARTDEC_FAIL_READ"); left = e ? atol(e) : -1; }
    if(left > 0 && --left == 0){ errno = EIO; return -1; }
    return pread(fd, b, n, o);
}
#define pread test_pread
#endif
static int rd(uint64_t off, void *buf, size_t n){
    if(off > g_fsize || n > g_fsize - off) return -1;
    size_t got = 0;
    while(got < n){
        ssize_t k = pread(g_fd, (char *)buf + got, n - got, (off_t)(off + got));
        if(k < 0){ if(errno == EINTR) continue; g_ioerr = 1; return -1; }
        if(k == 0) return -1;
        got += (size_t)k;
    }
    return 0;
}
static uint32_t be32(const unsigned char *p){ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static uint32_t be24(const unsigned char *p){ return (uint32_t)p[0]<<16 | (uint32_t)p[1]<<8 | p[2]; }
static uint32_t le32(const unsigned char *p){ return (uint32_t)p[3]<<24 | (uint32_t)p[2]<<16 | (uint32_t)p[1]<<8 | p[0]; }
static uint64_t be64(const unsigned char *p){ return (uint64_t)be32(p) << 32 | be32(p + 4); }
static int syncsafe(const unsigned char *p, uint32_t *out){
    if((p[0] | p[1] | p[2] | p[3]) & 0x80) return -1;
    *out = (uint32_t)p[0]<<21 | (uint32_t)p[1]<<14 | (uint32_t)p[2]<<7 | p[3];
    return 0;
}

typedef struct {
    int front;
    const unsigned char *mem;
    uint64_t off; uint32_t len;
} cand_t;
static cand_t g_cand[MAX_CANDIDATES];
static int g_ncand, g_saw_picture, g_malformed, g_limited;

static void add_cand(int front, const unsigned char *mem, uint64_t off, uint64_t len){
    g_saw_picture = 1;
    if(len == 0) return;
    if(g_ncand >= MAX_CANDIDATES) return;
    if(len > MAX_PIC_BYTES){ g_cand[g_ncand++] = (cand_t){ front, NULL, 0, 0 }; return; }
    g_cand[g_ncand++] = (cand_t){ front, mem, off, (uint32_t)len };
}

static size_t de_unsync(unsigned char *p, size_t n){
    size_t o = 0;
    for(size_t i = 0; i < n; i++){
        p[o++] = p[i];
        if(p[i] == 0xFF && i + 1 < n && p[i + 1] == 0x00) i++;
    }
    return o;
}
static size_t skip_text(const unsigned char *p, size_t n, int enc){
    if(enc == 1 || enc == 2){
        for(size_t i = 0; i + 1 < n; i += 2) if(p[i] == 0 && p[i + 1] == 0) return i + 2;
        return 0;
    }
    for(size_t i = 0; i < n; i++) if(p[i] == 0) return i + 1;
    return 0;
}
#define MAX_TAGS 4
static unsigned char *g_tags[MAX_TAGS];
static int g_ntags;
static void free_tags(void){ for(int i = 0; i < g_ntags; i++) bfree(g_tags[i]); g_ntags = 0; }

static int parse_id3_at(uint64_t base, uint64_t limit_end){
    unsigned char h[10];
    if(rd(base, h, 10) || memcmp(h, "ID3", 3)) return -1;
    int ver = h[3];
    if(ver < 2 || ver > 4 || h[4] == 0xFF) return -1;
    uint32_t size;
    if(syncsafe(h + 6, &size)){ g_malformed = 1; return -1; }
    int tag_unsync = h[5] & 0x80, ext = h[5] & 0x40;
    uint64_t body_off = base + 10;
    uint64_t avail = (limit_end > body_off) ? limit_end - body_off : 0;
    uint64_t want = size;
    if(want > avail){ g_malformed = 1; want = avail; }
    if(want > MAX_TAG_READ) want = MAX_TAG_READ;
    if(want == 0) return 0;
    if(g_ntags >= MAX_TAGS){ g_limited = 1; return -1; }
    unsigned char *g_tag = bmalloc((size_t)want);
    if(!g_tag){ g_limited = 1; return -1; }
    if(rd(body_off, g_tag, (size_t)want)){ bfree(g_tag); g_ioerr = 1; return -1; }
    int ncand0 = g_ncand;
    size_t n = (size_t)want, pos = 0;
    if(ver < 4 && tag_unsync) n = de_unsync(g_tag, n);
    if(ext){
        int bad = 0;
        if(ver == 3){
            uint32_t es = n >= 4 ? be32(g_tag) : 0;
            if(n < 4 || es > n - 4) bad = 1; else pos = 4 + es;
        } else if(ver == 4){
            uint32_t es;
            if(n < 4 || syncsafe(g_tag, &es) || es < 6 || es > n) bad = 1; else pos = es;
        }
        else bad = 1;
        if(bad){ g_malformed = 1; bfree(g_tag); return 0; }
    }
    int frames = 0;
    size_t hlen = (ver == 2) ? 6 : 10;
    while(pos + hlen <= n && frames++ < MAX_WALK){
        const unsigned char *f = g_tag + pos;
        if(f[0] == 0) break;
        uint32_t fs; uint16_t fl = 0;
        if(ver == 2) fs = be24(f + 3);
        else if(ver == 3) fs = be32(f + 4);
        else if(syncsafe(f + 4, &fs)){ g_malformed = 1; break; }
        if(ver > 2) fl = (uint16_t)(f[8] << 8 | f[9]);
        if(fs > n - pos - hlen){ g_malformed = 1; break; }
        unsigned char *d = g_tag + pos + hlen;
        size_t dl = fs;
        pos += hlen + fs;
        int is_pic = (ver == 2) ? !memcmp(f, "PIC", 3) : !memcmp(f, "APIC", 4);
        if(!is_pic) continue;
        if(ver == 3){
            if(fl & 0x00C0) continue;
            if(fl & 0x0020){ if(dl < 1) continue; d++; dl--; }
        } else if(ver == 4){
            if(fl & 0x000C) continue;
            if((fl & 0x0002) || tag_unsync) dl = de_unsync(d, dl);
            if(fl & 0x0040){ if(dl < 1) continue; d++; dl--; }
            if(fl & 0x0001){ if(dl < 4) continue; d += 4; dl -= 4; }
        }
        if(dl < 2) continue;
        int enc = d[0];
        size_t p = 1;
        if(ver == 2){ if(dl < 5) continue; p += 3; }
        else { size_t m = skip_text(d + p, dl - p, 0); if(!m) continue; p += m; }
        if(p >= dl) continue;
        int ptype = d[p++];
        size_t ds = skip_text(d + p, dl - p, enc);
        if(!ds) continue;
        p += ds;
        if(p >= dl) continue;
        add_cand(ptype == 3, d + p, 0, dl - p);
    }
    if(g_ncand > ncand0) g_tags[g_ntags++] = g_tag;
    else bfree(g_tag);
    return 0;
}

static int parse_flac(uint64_t off){
    unsigned char b[4];
    if(rd(off, b, 4) || memcmp(b, "fLaC", 4)) return -1;
    off += 4;
    for(int blocks = 0; blocks < MAX_WALK; blocks++){
        unsigned char bh[4];
        if(rd(off, bh, 4)){ g_malformed = 1; return 0; }
        int last = bh[0] & 0x80, type = bh[0] & 0x7F;
        uint32_t len = be24(bh + 1);
        uint64_t data = off + 4;
        if(len > g_fsize - data){ g_malformed = 1; return 0; }
        if(type == 6 && len >= 32){
            unsigned char ph[8];
            if(rd(data, ph, 8)){ g_malformed = 1; return 0; }
            uint32_t ptype = be32(ph), mlen = be32(ph + 4);
            uint64_t q = data + 8;
            uint64_t end = data + len;
            if(mlen > end - q){ g_malformed = 1; goto next; }
            char mime[4] = {0};
            if(mlen == 3 && rd(q, mime, 3) == 0 && !memcmp(mime, "-->", 3)){ g_saw_picture = 1; goto next; }
            q += mlen;
            unsigned char dl4[4];
            if(end - q < 4 || rd(q, dl4, 4)){ g_malformed = 1; goto next; }
            uint32_t dlen = be32(dl4); q += 4;
            if(dlen > end - q){ g_malformed = 1; goto next; }
            q += dlen;
            if(end - q < 20){ g_malformed = 1; goto next; }
            unsigned char tail[20];
            if(rd(q, tail, 20)){ g_malformed = 1; goto next; }
            uint32_t plen = be32(tail + 16); q += 20;
            if(plen > end - q){ g_malformed = 1; goto next; }
            add_cand(ptype == 3, NULL, q, plen);
        }
    next:
        off = data + len;
        if(last) return 0;
    }
    return 0;
}

static int g_atoms;
static void mp4_walk(uint64_t start, uint64_t end, int depth, int in_covr){
    uint64_t off = start;
    while(off + 8 <= end && g_atoms++ < MAX_WALK){
        unsigned char h[16];
        if(rd(off, h, 8)){ g_malformed = 1; return; }
        uint64_t size = be32(h), hdr = 8;
        if(size == 1){
            if(rd(off + 8, h + 8, 8)){ g_malformed = 1; return; }
            size = be64(h + 8); hdr = 16;
        } else if(size == 0) size = end - off;
        if(size < hdr || size > end - off){ g_malformed = 1; return; }
        const unsigned char *t = h + 4;
        uint64_t body = off + hdr, bend = off + size;
        if(depth < MAX_DEPTH){
            if(!memcmp(t, "moov", 4) || !memcmp(t, "udta", 4) || !memcmp(t, "ilst", 4))
                mp4_walk(body, bend, depth + 1, 0);
            else if(!memcmp(t, "meta", 4)){
                unsigned char v[8];
                uint64_t child = body;
                if(bend - body >= 8 && rd(body, v, 8) == 0 && memcmp(v + 4, "hdlr", 4) != 0) child = body + 4;
                mp4_walk(child, bend, depth + 1, 0);
            }
            else if(!memcmp(t, "covr", 4)) mp4_walk(body, bend, depth + 1, 1);
            else if(in_covr && !memcmp(t, "data", 4) && bend - body >= 8){
                unsigned char dh[8];
                if(rd(body, dh, 8) == 0){
                    uint32_t ty = be32(dh) & 0xFFFFFF;
                    (void)ty;
                    add_cand(1, NULL, body + 8, bend - body - 8);
                }
            }
        }
        off = bend;
    }
}

static int parse_wav(void){
    unsigned char h[12];
    if(rd(0, h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return -1;
    uint64_t off = 12, end = (uint64_t)le32(h + 4) + 8;
    if(end > g_fsize) end = g_fsize;
    for(int n = 0; off + 8 <= end && n < MAX_WALK; n++){
        unsigned char ch[8];
        if(rd(off, ch, 8)){ g_malformed = 1; return 0; }
        uint64_t len = le32(ch + 4), body = off + 8;
        if(len > end - body){ g_malformed = 1; return 0; }
        if(!memcmp(ch, "id3 ", 4) || !memcmp(ch, "ID3 ", 4)) parse_id3_at(body, body + len);
        off = body + len + (len & 1);
    }
    return 0;
}

static int img_sig(const unsigned char *p, size_t n){
    if(n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return 1;
    if(n >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8)) return 1;
    if(n >= 2 && p[0] == 'B' && p[1] == 'M') return 1;
    if(n >= 6 && (!memcmp(p, "GIF87a", 6) || !memcmp(p, "GIF89a", 6))) return 1;
    return 0;
}
static int header_dims(const unsigned char *p, size_t n, int64_t *w, int64_t *h){
    if(n >= 24 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8) && !memcmp(p + 12, "IHDR", 4)){
        *w = be32(p + 16); *h = be32(p + 20); return 1;
    }
    if(n >= 10 && (!memcmp(p, "GIF87a", 6) || !memcmp(p, "GIF89a", 6))){
        *w = p[6] | p[7] << 8; *h = p[8] | p[9] << 8; return 1;
    }
    if(n >= 26 && p[0] == 'B' && p[1] == 'M'){
        uint32_t dib = le32(p + 14);
        if(dib == 12){ *w = p[18] | p[19] << 8; *h = p[20] | p[21] << 8; return 1; }
        *w = (int32_t)le32(p + 18); *h = (int32_t)le32(p + 22); return 1;
    }
    if(n >= 4 && p[0] == 0xFF && p[1] == 0xD8){
        size_t i = 2;
        for(int seg = 0; seg < 256 && i + 4 <= n; seg++){
            if(p[i] != 0xFF){ i++; continue; }
            int m = p[i + 1];
            if(m == 0xFF){ i++; continue; }
            if(m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01){ i += 2; continue; }
            if(m == 0xD9 || m == 0xDA) return 0;
            size_t L = (size_t)p[i + 2] << 8 | p[i + 3];
            if(L < 2 || L > n - i - 2) return 0;
            if(m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC){
                if(L < 7) return 0;
                *h = (int64_t)p[i + 5] << 8 | p[i + 6]; *w = (int64_t)p[i + 7] << 8 | p[i + 8]; return 1;
            }
            i += 2 + L;
        }
    }
    return 0;
}

static int exif_orientation(const unsigned char *p, size_t n){
    if(n < 4 || p[0] != 0xFF || p[1] != 0xD8) return 1;
    size_t i = 2;
    for(int seg = 0; seg < 64 && i + 4 <= n; seg++){
        if(p[i] != 0xFF) return 1;
        int m = p[i + 1];
        if(m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01){ i += 2; continue; }
        if(m == 0xDA || m == 0xD9) return 1;
        size_t L = (size_t)p[i + 2] << 8 | p[i + 3];
        if(L < 2 || L > n - i - 2) return 1;
        const unsigned char *s = p + i + 4; size_t sl = L - 2;
        if(m == 0xE1 && sl >= 14 && !memcmp(s, "Exif\0\0", 6)){
            const unsigned char *t = s + 6; size_t tl = sl - 6;
            int le = !memcmp(t, "II", 2);
            if(!le && memcmp(t, "MM", 2)) return 1;
#define U16(q) (le ? (uint32_t)((q)[0] | (q)[1] << 8) : (uint32_t)((q)[0] << 8 | (q)[1]))
#define U32(q) (le ? le32(q) : be32(q))
            if(U16(t + 2) != 42) return 1;
            uint32_t ifd = U32(t + 4);
            if(ifd > tl || tl - ifd < 2) return 1;
            uint32_t cnt = U16(t + ifd);
            for(uint32_t e = 0; e < cnt && e < 256; e++){
                size_t eo = ifd + 2 + (size_t)e * 12;
                if(eo + 12 > tl) return 1;
                if(U16(t + eo) == 0x0112){
                    uint32_t v = U16(t + eo + 8);
                    return (v >= 1 && v <= 8) ? (int)v : 1;
                }
            }
#undef U16
#undef U32
            return 1;
        }
        i += 2 + L;
    }
    return 1;
}

#define WBITS 14
#define WSUM  (1 << WBITS)
typedef struct { int first, count; uint16_t *w; } contrib_t;
static contrib_t *make_contrib(int src, int dst, int box){
    contrib_t *c = bmalloc(sizeof(contrib_t) * (size_t)dst);
    if(!c) return NULL;
    int64_t S = src, N = dst;
    int maxw = (int)(src / dst) * 2 + 6;
    uint16_t *pool = bmalloc(sizeof(uint16_t) * (size_t)dst * (size_t)maxw);
    uint32_t *raw = bmalloc(sizeof(uint32_t) * (size_t)maxw);
    if(!pool || !raw){ bfree(pool); bfree(raw); bfree(c); return NULL; }
    for(int i = 0; i < dst; i++){
        c[i].w = pool + (size_t)i * maxw; c[i].count = 0;
        uint64_t tot = 0;
        if(box && S >= N){
            int64_t a = i * S, b = (i + 1) * S;
            int j0 = (int)(a / N), j1 = (int)((b - 1) / N);
            if(j1 > src - 1) j1 = src - 1;
            c[i].first = j0;
            for(int j = j0; j <= j1 && c[i].count < maxw; j++){
                int64_t lo = j * N > a ? j * N : a, hi = (j + 1) * N < b ? (j + 1) * N : b;
                raw[c[i].count++] = (uint32_t)(hi > lo ? hi - lo : 0); tot += hi > lo ? (uint64_t)(hi - lo) : 0;
            }
        } else {
            int64_t C = (2 * i + 1) * S - N;
            int64_t sup = (!box && S > N) ? 2 * S : 2 * N;
            int64_t jlo = (C - sup) / (2 * N) - 1, jhi = (C + sup) / (2 * N) + 1;
            if(jlo < 0) jlo = 0;
            if(jhi > src - 1) jhi = src - 1;
            c[i].first = (int)jlo;
            for(int64_t j = jlo; j <= jhi && c[i].count < maxw; j++){
                int64_t d = 2 * N * j - C; if(d < 0) d = -d;
                uint32_t w = d < sup ? (uint32_t)(sup - d) : 0;
                raw[c[i].count++] = w; tot += w;
            }
            if(tot == 0){
                int64_t j = (C + N) / (2 * N); if(j < 0) j = 0; if(j > src - 1) j = src - 1;
                c[i].first = (int)j; c[i].count = 1; raw[0] = 1; tot = 1;
            }
        }
        uint32_t sum = 0; int big = 0;
        for(int k = 0; k < c[i].count; k++){
            c[i].w[k] = (uint16_t)((raw[k] * (uint64_t)WSUM + tot / 2) / tot);
            sum += c[i].w[k];
            if(raw[k] > raw[big]) big = k;
        }
        c[i].w[big] = (uint16_t)(c[i].w[big] + (int32_t)WSUM - (int32_t)sum);
    }
    bfree(raw);
    return c;
}
static void free_contrib(contrib_t *c){ if(c){ bfree(c[0].w); bfree(c); } }

static uint16_t *resample(const unsigned char *src, int sw, int sh, int N, int box){
    contrib_t *cx = make_contrib(sw, N, box), *cy = make_contrib(sh, N, box);
    uint16_t *tmp = NULL, *out = NULL;
    if(!cx || !cy) goto fail;
    tmp = bmalloc(sizeof(uint16_t) * (size_t)sh * N * 3);
    out = bmalloc(sizeof(uint16_t) * (size_t)N * N * 3);
    if(!tmp || !out) goto fail;
    for(int y = 0; y < sh; y++){
        const unsigned char *row = src + (size_t)y * sw * 3;
        uint16_t *t = tmp + (size_t)y * N * 3;
        for(int x = 0; x < N; x++){
            uint32_t r = 0, g = 0, b = 0;
            const unsigned char *p = row + (size_t)cx[x].first * 3;
            for(int k = 0; k < cx[x].count; k++, p += 3){
                uint32_t w = cx[x].w[k]; r += w * p[0]; g += w * p[1]; b += w * p[2];
            }
            t[x * 3] = (uint16_t)((r + (1 << (WBITS - 9))) >> (WBITS - 8));
            t[x * 3 + 1] = (uint16_t)((g + (1 << (WBITS - 9))) >> (WBITS - 8));
            t[x * 3 + 2] = (uint16_t)((b + (1 << (WBITS - 9))) >> (WBITS - 8));
        }
    }
    for(int y = 0; y < N; y++){
        uint16_t *o = out + (size_t)y * N * 3;
        for(int x = 0; x < N * 3; x++){
            uint32_t v = 0;
            for(int k = 0; k < cy[y].count; k++) v += (uint32_t)cy[y].w[k] * tmp[(size_t)(cy[y].first + k) * N * 3 + x];
            o[x] = (uint16_t)((v + (WSUM / 2)) >> WBITS);
        }
    }
    bfree(tmp); free_contrib(cx); free_contrib(cy);
    return out;
fail:
    bfree(tmp); bfree(out); free_contrib(cx); free_contrib(cy);
    return NULL;
}

static int orient(uint16_t **img, int N, int o){
    if(o <= 1) return 0;
    uint16_t *src = *img, *dst = bmalloc(sizeof(uint16_t) * (size_t)N * N * 3);
    if(!dst) return -1;
    for(int y = 0; y < N; y++) for(int x = 0; x < N; x++){
        int sx, sy;
        switch(o){
        case 2: sx = N - 1 - x; sy = y; break;
        case 3: sx = N - 1 - x; sy = N - 1 - y; break;
        case 4: sx = x; sy = N - 1 - y; break;
        case 5: sx = y; sy = x; break;
        case 6: sx = y; sy = N - 1 - x; break;
        case 7: sx = N - 1 - y; sy = N - 1 - x; break;
        default: sx = N - 1 - y; sy = x; break;
        }
        memcpy(dst + ((size_t)y * N + x) * 3, src + ((size_t)sy * N + sx) * 3, sizeof(uint16_t) * 3);
    }
    bfree(src); *img = dst;
    return 0;
}

static const int BLUR_BOXES_S19[3] = { 37, 37, 39 };
static int blur(uint16_t *img, int N){
    uint16_t *line = bmalloc(sizeof(uint16_t) * (size_t)N * 3);
    if(!line) return -1;
    for(int pass = 0; pass < 3; pass++){
        int r = (BLUR_BOXES_S19[pass] - 1) / 2, w = 2 * r + 1;
        for(int axis = 0; axis < 2; axis++){
            for(int a = 0; a < N; a++){
                for(int c = 0; c < 3; c++){
                    uint32_t acc = 0;
#define AT(i) (axis ? img[((size_t)(i) * N + a) * 3 + c] : img[((size_t)a * N + (i)) * 3 + c])
                    for(int k = -r; k <= r; k++){ int i = k < 0 ? 0 : (k > N - 1 ? N - 1 : k); acc += AT(i); }
                    for(int b = 0; b < N; b++){
                        line[b * 3 + c] = (uint16_t)((acc + (uint32_t)w / 2) / (uint32_t)w);
                        int add = b + r + 1, sub = b - r;
                        if(add > N - 1) add = N - 1;
                        if(sub < 0) sub = 0;
                        acc += AT(add); acc -= AT(sub);
                    }
#undef AT
                }
                for(int b = 0; b < N; b++) for(int c = 0; c < 3; c++){
                    if(axis) img[((size_t)b * N + a) * 3 + c] = line[b * 3 + c];
                    else img[((size_t)a * N + b) * 3 + c] = line[b * 3 + c];
                }
            }
        }
    }
    bfree(line);
    return 0;
}

static unsigned char px8(uint16_t v){ uint32_t q = ((uint32_t)v + 128) >> 8; return q > 255 ? 255 : (unsigned char)q; }

static int write_all(int fd, const unsigned char *p, size_t n){
    while(n){ ssize_t k = write(fd, p, n); if(k < 0){ if(errno == EINTR) continue; return -1; } p += k; n -= (size_t)k; }
    return 0;
}
static int write_bmp(const char *path, const uint16_t *img, int N, int *created){
    size_t stride = ((size_t)N * 3 + 3) & ~(size_t)3, dsz = stride * N;
    unsigned char *buf = bmalloc(54 + dsz);
    if(!buf) return ST_LIMIT;
    memset(buf, 0, 54 + dsz);
    unsigned char *h = buf;
    uint32_t fsz = (uint32_t)(54 + dsz);
    h[0] = 'B'; h[1] = 'M';
    memcpy(h + 2, (uint8_t[]){ fsz, fsz >> 8, fsz >> 16, fsz >> 24 }, 4);
    h[10] = 54; h[14] = 40;
    memcpy(h + 18, (uint8_t[]){ N, N >> 8, 0, 0 }, 4);
    memcpy(h + 22, (uint8_t[]){ N, N >> 8, 0, 0 }, 4);
    h[26] = 1; h[28] = 24;
    memcpy(h + 34, (uint8_t[]){ dsz, dsz >> 8, dsz >> 16, dsz >> 24 }, 4);
    for(int y = 0; y < N; y++){
        unsigned char *row = buf + 54 + (size_t)(N - 1 - y) * stride;
        const uint16_t *s = img + (size_t)y * N * 3;
        for(int x = 0; x < N; x++){ row[x * 3] = px8(s[x * 3 + 2]); row[x * 3 + 1] = px8(s[x * 3 + 1]); row[x * 3 + 2] = px8(s[x * 3]); }
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    int rc = ST_IO;
    if(fd >= 0){
        *created = 1;
        if(write_all(fd, buf, 54 + dsz) == 0) rc = ST_OK;
        if(close(fd) != 0) rc = ST_IO;
    }
    bfree(buf);
    return rc;
}
static int write_bgra(const char *path, const uint16_t *img, int N, int *created){
    unsigned char *buf = bmalloc((size_t)N * N * 4);
    if(!buf) return ST_LIMIT;
    for(size_t i = 0; i < (size_t)N * N; i++){
        buf[i * 4] = px8(img[i * 3 + 2]); buf[i * 4 + 1] = px8(img[i * 3 + 1]);
        buf[i * 4 + 2] = px8(img[i * 3]); buf[i * 4 + 3] = 255;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    int rc = ST_IO;
    if(fd >= 0){
        *created = 1;
        if(write_all(fd, buf, (size_t)N * N * 4) == 0) rc = ST_OK;
        if(close(fd) != 0) rc = ST_IO;
    }
    bfree(buf);
    return rc;
}

typedef struct { unsigned char *rgb; int w, h, orient; } pic_t;
static int decode(const cand_t *c, pic_t *out){
    if(c->len == 0) return ST_LIMIT;
    const unsigned char *bytes = c->mem;
    unsigned char *owned = NULL;
    if(!bytes){
        owned = bmalloc(c->len);
        if(!owned) return ST_LIMIT;
        if(rd(c->off, owned, c->len)){ bfree(owned); return ST_IO; }
        bytes = owned;
    }
    int rc;
    int w, h, comp;
    if(!img_sig(bytes, c->len)){ rc = ST_UNSUPPORTED; goto done; }
    int64_t hw, hh;
    if(header_dims(bytes, c->len, &hw, &hh)){
        if(hh < 0) hh = -hh;
        if(hw > MAX_SIDE || hh > MAX_SIDE || (uint64_t)hw * (uint64_t)hh > MAX_PIXELS){ rc = ST_LIMIT; goto done; }
    }
    if(!stbi_info_from_memory(bytes, (int)c->len, &w, &h, &comp)){
        const char *why = stbi_failure_reason();
        rc = (why && !strcmp(why, "too large")) ? ST_LIMIT : ST_MALFORMED;
        goto done;
    }
    if(h < 0 && bytes[0] == 'B' && bytes[1] == 'M'){
        if(h == INT_MIN){ rc = ST_MALFORMED; goto done; }
        h = -h;
    }
    if(w <= 0 || h <= 0){ rc = ST_MALFORMED; goto done; }
    if(w > MAX_SIDE || h > MAX_SIDE || (uint64_t)w * (uint64_t)h > MAX_PIXELS){ rc = ST_LIMIT; goto done; }
    int want = (comp == 2 || comp == 4) ? 4 : 3;
    g_over_budget = 0;
    unsigned char *px = stbi_load_from_memory(bytes, (int)c->len, &w, &h, &comp, want);
    if(!px){
        const char *why = stbi_failure_reason();
        rc = (g_over_budget || (why && (!strcmp(why, "too large") || !strcmp(why, "outofmem")))) ? ST_LIMIT : ST_MALFORMED;
        goto done;
    }
    if(want == 4){
        for(size_t i = 0; i < (size_t)w * h; i++){
            unsigned a = px[i * 4 + 3];
            px[i * 3]     = (unsigned char)((px[i * 4]     * a + 127) / 255);
            px[i * 3 + 1] = (unsigned char)((px[i * 4 + 1] * a + 127) / 255);
            px[i * 3 + 2] = (unsigned char)((px[i * 4 + 2] * a + 127) / 255);
        }
    }
    out->rgb = px; out->w = w; out->h = h; out->orient = exif_orientation(bytes, c->len);
    rc = ST_OK;
done:
    bfree(owned);
    return rc;
}

static int make(const pic_t *p, int N, int box, int blurred, uint16_t **res){
    uint16_t *img = resample(p->rgb, p->w, p->h, N, box);
    if(!img) return ST_LIMIT;
    if(orient(&img, N, p->orient)){ bfree(img); return ST_LIMIT; }
    if(blurred && blur(img, N)){ bfree(img); return ST_LIMIT; }
    *res = img;
    return ST_OK;
}

static int limits(void){
    int bad = 0;
#ifndef ARTDEC_NO_RLIMIT
    struct rlimit as = { 48u << 20, 48u << 20 };
    bad |= setrlimit(RLIMIT_AS, &as);
#endif
    struct rlimit core = { 0, 0 }, fsz = { 1u << 20, 1u << 20 }, nofile = { 16, 16 };
    bad |= setrlimit(RLIMIT_CORE, &core);
    bad |= setrlimit(RLIMIT_FSIZE, &fsz);
    bad |= setrlimit(RLIMIT_NOFILE, &nofile);
    return bad ? -1 : 0;
}

int main(int argc, char **argv){
    if(argc != 4) return ST_USAGE;
    int saver = !strcmp(argv[1], "saver");
    if(!saver && strcmp(argv[1], "art")) return ST_USAGE;
    if(limits()) return ST_LIMIT;
    g_fd = open(argv[2], O_RDONLY | O_CLOEXEC);
    if(g_fd < 0) return ST_IO;
    struct stat st;
    if(fstat(g_fd, &st) || !S_ISREG(st.st_mode)) return ST_IO;
    g_fsize = (uint64_t)st.st_size;

    unsigned char head[12] = {0};
    size_t hn = g_fsize < 12 ? (size_t)g_fsize : 12;
    if(hn && rd(0, head, hn)) return ST_IO;
    if(hn >= 3 && !memcmp(head, "ID3", 3)){
        unsigned char h10[10];
        uint32_t sz = 0;
        if(rd(0, h10, 10) == 0 && syncsafe(h10 + 6, &sz) == 0){
            parse_id3_at(0, g_fsize);
            uint64_t after = 10 + (uint64_t)sz + ((h10[5] & 0x10) ? 10 : 0);
            unsigned char f4[4];
            if(after + 4 <= g_fsize && rd(after, f4, 4) == 0 && !memcmp(f4, "fLaC", 4)) parse_flac(after);
        } else g_malformed = 1;
    }
    else if(hn >= 4 && !memcmp(head, "fLaC", 4)) parse_flac(0);
    else if(hn >= 8 && !memcmp(head + 4, "ftyp", 4)) mp4_walk(0, g_fsize, 0, 0);
    else if(hn >= 12 && !memcmp(head, "RIFF", 4) && !memcmp(head + 8, "WAVE", 4)) parse_wav();
    else if(img_sig(head, hn)) add_cand(1, NULL, 0, g_fsize);
    else if(hn == 0) return ST_MALFORMED;
    else if(g_fsize > 3 && (head[0] == 0xFF && (head[1] & 0xE0) == 0xE0)) return ST_NOPIC;
    else return ST_UNSUPPORTED;

    if(g_ncand == 0) return g_ioerr ? ST_IO : g_limited ? ST_LIMIT : g_malformed ? ST_MALFORMED : ST_NOPIC;

    int order[MAX_CANDIDATES], no = 0, best = ST_NOPIC;
    for(int pass = 0; pass < 2; pass++)
        for(int i = 0; i < g_ncand; i++) if(g_cand[i].front == !pass) order[no++] = i;
    pic_t pic = {0};
    int got = 0;
    for(int k = 0; k < no && !got; k++){
        int rc = decode(&g_cand[order[k]], &pic);
        if(rc == ST_OK) got = 1;
        else if(best == ST_NOPIC || rc == ST_LIMIT) best = rc;
    }
    if(!got) return g_ioerr ? ST_IO : best;
    free_tags();

    int rc = ST_OK;
    if(saver){
        uint16_t *img = NULL;
        rc = make(&pic, 360, 0, 0, &img);
        stbi_image_free(pic.rgb);
        int made = 0;
        if(rc == ST_OK){ rc = write_bgra(argv[3], img, 360, &made); if(rc != ST_OK && made) unlink(argv[3]); }
        bfree(img);
        return rc;
    }
    char pc[4096], pt[4096], pb[4096];
    if(snprintf(pc, sizeof pc, "%s/c.bmp", argv[3]) >= (int)sizeof pc ||
       snprintf(pt, sizeof pt, "%s/t.bmp", argv[3]) >= (int)sizeof pt ||
       snprintf(pb, sizeof pb, "%s/b.bmp", argv[3]) >= (int)sizeof pb){ stbi_image_free(pic.rgb); return ST_USAGE; }
    uint16_t *c = NULL, *t = NULL, *b = NULL;
    rc = make(&pic, 148, 1, 0, &c);
    if(rc == ST_OK) rc = make(&pic, 42, 1, 0, &t);
    if(rc == ST_OK) rc = make(&pic, 360, 0, 1, &b);
    stbi_image_free(pic.rgb);
    int mc = 0, mt = 0, mb = 0;
    if(rc == ST_OK) rc = write_bmp(pc, c, 148, &mc);
    if(rc == ST_OK) rc = write_bmp(pt, t, 42, &mt);
    if(rc == ST_OK) rc = write_bmp(pb, b, 360, &mb);
    if(rc != ST_OK){ if(mc) unlink(pc); if(mt) unlink(pt); if(mb) unlink(pb); }
    bfree(c); bfree(t); bfree(b);
    return rc;
}
