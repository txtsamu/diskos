/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "sdio.h"
#include "artcache.h"
#include "art.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdatomic.h>

#ifndef CACHE_ROOT
#define CACHE_ROOT "/tmp/sdcard/.diskos/artcache"
#endif
#ifndef ARTCACHE_BASE_DIR
#define ARTCACHE_BASE_DIR "/tmp/sdcard/.diskos"
#endif
#ifndef ARTCACHE_FREE_PATH
#define ARTCACHE_FREE_PATH "/tmp/sdcard"
#endif
#ifndef MIN_FREE_BYTES
#define MIN_FREE_BYTES (1024ULL*1024*1024)
#endif
#define ART_RECIPE_VER 7

static int fingerprint(const char *track, char *out, int cap){
    struct stat stt;
    if(!track || stat(track, &stt) != 0) return -1;
    uint64_t h = 1469598103934665603ULL;
    h ^= (unsigned)ART_RECIPE_VER; h *= 1099511628211ULL;
    for(const char *p=track; *p; p++){ h ^= (unsigned char)*p; h *= 1099511628211ULL; }
    uint64_t sz=(uint64_t)stt.st_size, mt=(uint64_t)stt.st_mtime;
    for(int i=0;i<8;i++){ h ^= (sz>>(i*8))&0xff; h *= 1099511628211ULL; }
    for(int i=0;i<8;i++){ h ^= (mt>>(i*8))&0xff; h *= 1099511628211ULL; }
    art_sidecar_signature(track, &h);
    snprintf(out, cap, "%016llx", (unsigned long long)h);
    return 0;
}
static void fsync_parent_dir(const char *path){
    char d[600]; snprintf(d, sizeof d, "%s", path);
    char *slash = strrchr(d, '/');
    if(!slash) return;
    if(slash == d) slash[1] = 0; else slash[0] = 0;
    int fd = open(d, O_RDONLY | O_DIRECTORY);
    if(fd < 0) return;
    fsync(fd);
    close(fd);
}
#define CV_OK(p) art_bmp_valid(p, 148)
#define TH_OK(p) art_bmp_valid(p, 42)
#define BG_OK(p) art_bmp_valid(p, 360)
static void cache_paths(const char *fp, char *cv, char *th, char *bg, int cap){
    snprintf(cv,cap,"%s/%s/c.bmp",CACHE_ROOT,fp);
    snprintf(th,cap,"%s/%s/t.bmp",CACHE_ROOT,fp);
    snprintf(bg,cap,"%s/%s/b.bmp",CACHE_ROOT,fp);
}
static int copy_file(const char *src, const char *dst){
    FILE *in=fopen(src,"rb"); if(!in) return -1;
    static _Atomic unsigned g_tmp_ctr = 0;
    unsigned seq = atomic_fetch_add(&g_tmp_ctr, 1u);
    char tmp[600]; snprintf(tmp,sizeof tmp,"%s.tmp%u",dst,seq);
    FILE *out=fopen(tmp,"wb"); if(!out){ fclose(in); return -1; }
    char buf[16384]; size_t n; int ok=1;
    while((n=fread(buf,1,sizeof buf,in))>0){ if(fwrite(buf,1,n,out)!=n){ ok=0; break; } }
    if(ferror(in)) ok=0;
    fclose(in);
    if(fflush(out)!=0) ok=0;
    if(ok){
        int fd = fileno(out);
        if(fd >= 0 && fsync(fd) != 0 && errno != EINVAL && errno != ENOSYS && errno != EOPNOTSUPP) ok = 0;
    }
    fclose(out);
    if(!ok){ unlink(tmp); return -1; }
    if(rename(tmp,dst)!=0){ unlink(tmp); return -1; }
    fsync_parent_dir(dst);
    return 0;
}
static int enough_free(void){
    struct statvfs v;
    if(statvfs(ARTCACHE_FREE_PATH, &v) != 0) return 0;
    return ((uint64_t)v.f_bavail * v.f_frsize) > MIN_FREE_BYTES;
}

static int entry_ok(const char *fp){
    char cv[600],th[600],bg[600]; cache_paths(fp,cv,th,bg,600);
    return CV_OK(cv) && TH_OK(th) && BG_OK(bg);
}
static int artcache_has_leased(const char *track){
    char fp[24]; if(fingerprint(track,fp,sizeof fp)!=0) return 0;
    return entry_ok(fp);
}

static int artcache_get_leased(const char *track, const char *cover_out, const char *thumb_out, const char *bg_out){
    char fp[24]; if(fingerprint(track,fp,sizeof fp)!=0) return -1;
    char cv[600],th[600],bg[600]; cache_paths(fp,cv,th,bg,600);
    if(!(CV_OK(cv) && TH_OK(th) && BG_OK(bg))) return -1;
    if(copy_file(cv,cover_out)!=0) return -1;
    if(copy_file(th,thumb_out)!=0) return -1;
    if(copy_file(bg,bg_out)!=0)    return -1;
    return 0;
}

static int artcache_get_thumb_leased(const char *track, const char *thumb_out){
    char fp[24]; if(fingerprint(track,fp,sizeof fp)!=0) return -1;
    char cv[600],th[600],bg[600]; cache_paths(fp,cv,th,bg,600);
    if(!TH_OK(th)) return -1;
    if(copy_file(th,thumb_out)!=0) return -1;
    return 0;
}

static int artcache_cover_path_leased(const char *track, char *out, int cap){
    char fp[24]; if(fingerprint(track,fp,sizeof fp)!=0) return -1;
    char cv[600],th[600],bg[600]; cache_paths(fp,cv,th,bg,600);
    if(!CV_OK(cv)) return -1;
    snprintf(out, cap, "%s", cv);
    return 0;
}

static int artcache_get_cover_leased(const char *track, const char *cover_out){
    char fp[24]; if(fingerprint(track,fp,sizeof fp)!=0) return -1;
    char cv[600],th[600],bg[600]; cache_paths(fp,cv,th,bg,600);
    if(!CV_OK(cv)) return -1;
    if(copy_file(cv,cover_out)!=0) return -1;
    return 0;
}

static _Atomic unsigned g_ac_gen;
unsigned artcache_gen(void){ return atomic_load(&g_ac_gen); }

static void artcache_put_leased(const char *track, const char *fp_before, const char *cover, const char *thumb, const char *bg){
    char fp[24]; if(fingerprint(track,fp,sizeof fp)!=0) return;
    if(fp_before && strcmp(fp, fp_before) != 0) return;
    if(!enough_free()) return;
    if(!sd_write_begin()) return;
    char dir[600];
    mkdir(ARTCACHE_BASE_DIR, 0755);
    mkdir(CACHE_ROOT, 0755);
    snprintf(dir,sizeof dir,"%s/%s",CACHE_ROOT,fp);
    mkdir(dir, 0755);
    char cv[600],th[600],bg2[600]; cache_paths(fp,cv,th,bg2,600);
    int cover_ok = (copy_file(cover,cv)==0);
    copy_file(thumb,th); copy_file(bg,bg2);
    sd_write_end();
    if(cover_ok) atomic_fetch_add(&g_ac_gen, 1);
}

int artcache_has(const char *track){
    if(!sd_io_begin()) return 0;
    int result = artcache_has_leased(track);
    sd_io_end();
    return result;
}

int artcache_lookup(const char *track, char *key_out, int cap){
    if(cap < 24 || !sd_io_begin()) return -1;
    int r = fingerprint(track, key_out, cap) != 0 ? -1 : entry_ok(key_out);
    sd_io_end();
    return r;
}

int artcache_get(const char *track, const char *cover_out, const char *thumb_out, const char *bg_out){
    if(!sd_io_begin()) return -1;
    int result = artcache_get_leased(track, cover_out, thumb_out, bg_out);
    sd_io_end();
    return result;
}

int artcache_get_thumb(const char *track, const char *thumb_out){
    if(!sd_io_begin()) return -1;
    int result = artcache_get_thumb_leased(track, thumb_out);
    sd_io_end();
    return result;
}

int artcache_get_cover(const char *track, const char *cover_out){
    if(!sd_io_begin()) return -1;
    int result = artcache_get_cover_leased(track, cover_out);
    sd_io_end();
    return result;
}

int artcache_cover_path(const char *track, char *out, int cap){
    if(!sd_io_begin()) return -1;
    int result = artcache_cover_path_leased(track, out, cap);
    sd_io_end();
    return result;
}

void artcache_put(const char *track, const char *cover, const char *thumb, const char *bg){
    if(!sd_io_begin()) return;
    artcache_put_leased(track, NULL, cover, thumb, bg);
    sd_io_end();
}
int artcache_key(const char *track, char *out, int cap){
    if(cap < 24 || !sd_io_begin()) return -1;
    int r = fingerprint(track, out, cap);
    sd_io_end();
    return r;
}
void artcache_put_if(const char *track, const char *fp_before, const char *cover, const char *thumb, const char *bg){
    if(!sd_io_begin()) return;
    artcache_put_leased(track, fp_before, cover, thumb, bg);
    sd_io_end();
}
