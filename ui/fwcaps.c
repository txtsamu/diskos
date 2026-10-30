/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "fwcaps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { int ver; const char *tag; } GAIN_MAP[] = {
    { 209, "0645" },
    { 228, "0649" },
    { 240, "0649" },
    { 257, "0649" },
};

static int parse_os_ver(void){
    FILE *f = fopen("/etc/product_version/version.in", "r");
    if(!f) return 0;
    char line[128];
    int ver = 0;
    while(fgets(line, sizeof line, f)){
        size_t ll = strlen(line);
        if(!(ll && line[ll-1]=='\n') && ll == sizeof(line)-1){
            int c; while((c=fgetc(f))!=EOF && c!='\n'){}
            continue;
        }
        char *p = line;
        while(*p == ' ' || *p == '\t') p++;
        if(strncmp(p, "MAIN_OS_VER=", 12) == 0){
            char *num = p + 12, *end = NULL;
            long n = strtol(num, &end, 10);
            if(end == num){ ver = 0; break; }
            while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n') end++;
            ver = (*end == '\0' && n > 0 && n < 100000) ? (int)n : 0;
            break;
        }
    }
    fclose(f);
    return ver > 0 ? ver : 0;
}

int fw_os_ver(void){
    static int cached = -1;
    if(cached < 0) cached = parse_os_ver();
    return cached;
}

int fw_needs_localplayer_init(void){ int v = fw_os_ver(); return v == 240 || v == 257; }
int fw_needs_direct_sd_mount(void){  int v = fw_os_ver(); return v == 240 || v == 257; }

int fw_custom_peq_writable(void){ int v = fw_os_ver(); return v == 209 || v == 228 || v == 240; }

int fw_has_radio_boot_arbitration(void){ return fw_os_ver() == 257; }
int fw_fav_play_by_love_id(void){ return fw_os_ver() == 257; }
int fw_power_key_driveable(void){ return fw_os_ver() == 257; }
int fw_artist_class_settable(void){ int v = fw_os_ver(); return v == 209 || v == 228 || v == 240 || v == 257; }
int fw_folder_jump_settable(void){ int v = fw_os_ver(); return v == 228 || v == 240 || v == 257; }
int fw_track_display_settable(void){ int v = fw_os_ver(); return v == 240 || v == 257; }

const char *fw_gain_tag(void){
    int v = fw_os_ver();
    for(unsigned i = 0; i < sizeof GAIN_MAP / sizeof GAIN_MAP[0]; i++)
        if(GAIN_MAP[i].ver == v) return GAIN_MAP[i].tag;
    return NULL;
}
