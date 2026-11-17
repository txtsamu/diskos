/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "sdfs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_dir[64];

static const char *path_of(const char *name){
    static char p[128];
    snprintf(p, sizeof p, "%s/%s", g_dir, name);
    return p;
}

static void write_sector(const char *name, const unsigned char *b){
    FILE *f = fopen(path_of(name), "wb");
    assert(f);
    assert(fwrite(b, 1, 512, f) == 512);
    assert(fclose(f) == 0);
}

static void make_exfat(unsigned char *b){
    memset(b, 0, 512);
    memcpy(b + 3, "EXFAT   ", 8);
}

static void make_fat32(unsigned char *b){
    memset(b, 0, 512);
    b[11] = 0x00; b[12] = 0x02;
    b[13] = 8;
    b[14] = 32; b[15] = 0;
    memcpy(b + 82, "FAT32   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
}

static void make_fat16(unsigned char *b){
    memset(b, 0, 512);
    b[11] = 0x00; b[12] = 0x02;
    b[13] = 4;
    b[14] = 1; b[15] = 0;
    memcpy(b + 54, "FAT16   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
}

static void make_garbage(unsigned char *b){
    for(int i = 0; i < 512; i++) b[i] = (unsigned char)(i * 37 + 11);
}

static void make_sig_only(unsigned char *b){
    memset(b, 0, 512);
    b[510] = 0x55; b[511] = 0xAA;
}

static void make_label_no_sig(unsigned char *b){
    memset(b, 0, 512);
    b[11] = 0x00; b[12] = 0x02;
    b[13] = 4;
    b[14] = 1; b[15] = 0;
    memcpy(b + 82, "FAT32   ", 8);
}

int main(void){
    unsigned char b[512];
    char tmpl[] = "/tmp/diskos_sdfstest.XXXXXX";
    assert(mkdtemp(tmpl));
    snprintf(g_dir, sizeof g_dir, "%s", tmpl);

    make_exfat(b);
    write_sector("exfat", b);
    assert(sdfs_detect(path_of("exfat")) && strcmp(sdfs_detect(path_of("exfat")), "exfat") == 0);

    make_fat32(b);
    write_sector("fat32", b);
    assert(sdfs_detect(path_of("fat32")) && strcmp(sdfs_detect(path_of("fat32")), "vfat") == 0);

    make_fat16(b);
    write_sector("fat16", b);
    assert(sdfs_detect(path_of("fat16")) && strcmp(sdfs_detect(path_of("fat16")), "vfat") == 0);

    make_garbage(b);
    write_sector("garbage", b);
    assert(sdfs_detect(path_of("garbage")) == NULL);

    make_sig_only(b);
    write_sector("sigonly", b);
    assert(sdfs_detect(path_of("sigonly")) == NULL);

    make_label_no_sig(b);
    write_sector("labelnosig", b);
    assert(sdfs_detect(path_of("labelnosig")) == NULL);

    assert(sdfs_detect(path_of("missing")) == NULL);

    const char *files[] = { "exfat", "fat32", "fat16", "garbage", "sigonly", "labelnosig" };
    for(size_t i = 0; i < sizeof files / sizeof files[0]; i++) unlink(path_of(files[i]));
    rmdir(g_dir);

    puts("sdfs_test: OK");
    return 0;
}
