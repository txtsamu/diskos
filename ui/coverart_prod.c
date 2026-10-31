/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "coverart.h"
#include "art.h"
#include "artcache.h"
#include "netart.h"
#include "musicdb.h"
#include "sdio.h"
#include <stdio.h>
#include <time.h>

int ui_accent_is_static(void);
int ui_main_is_idle(void);

static int temp_dc(void){
    FILE *f=fopen("/sys/class/power_supply/cw221X-bat/temp","r"); if(!f) return -1;
    int t=-1; if(fscanf(f,"%d",&t)!=1) t=-1; fclose(f); return t;
}
static int charging(void){
    long cur=0;
    FILE *f=fopen("/sys/class/power_supply/cw221X-bat/current_now","r"); if(!f) return 0;
    if(fscanf(f,"%ld",&cur)!=1) cur=0;
    fclose(f);
    return cur > 0;
}
static int64_t now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

void coverart_init(void)
{
    static const struct coverart_env prod = {
        .decode = art_make_all_ex_gen, .decode_saver = art_make_saver, .decode_exit = art_last_exit,
        .cancel_gen = art_cancel_gen, .cancel = art_cancel,
        .cache_get = artcache_get, .cache_key = artcache_key, .cache_put_if = artcache_put_if,
        .cache_has = artcache_has, .cache_lookup = artcache_lookup, .cache_get_cover = artcache_get_cover,
        .net_have = netart_have, .net_touch = netart_touch, .net_reject = netart_reject,
        .net_request = netart_request, .net_take_ready = netart_take_ready,
        .accent_is_static = ui_accent_is_static, .accent_get = mdb_song_accent,
        .accent_fresh = mdb_song_accent_fresh, .accent_put = mdb_set_song_accent,
        .next_song = mdb_prewarm_next,
        .temp_dc = temp_dc, .charging = charging, .io_allowed = sd_io_allowed, .main_idle = ui_main_is_idle,
        .now_ms = now_ms,
    };
    coverart_set_env(&prod);
}
