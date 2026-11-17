/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "sdfs.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define SDFS_SECTOR 512

static int sector_size_ok(unsigned v){
    return v == 512 || v == 1024 || v == 2048 || v == 4096;
}

static int cluster_size_ok(unsigned v){
    return v >= 1 && v <= 128 && !(v & (v - 1));
}

static int bpb_ok(const unsigned char *b){
    unsigned bytes_per_sector = b[11] | (unsigned)(b[12] << 8);
    unsigned reserved_sectors = b[14] | (unsigned)(b[15] << 8);
    if(!sector_size_ok(bytes_per_sector)) return 0;
    if(!cluster_size_ok(b[13])) return 0;
    if(!reserved_sectors) return 0;
    return 1;
}

static int fat_label_ok(const unsigned char *b){
    return memcmp(b + 54, "FAT", 3) == 0 || memcmp(b + 82, "FAT", 3) == 0;
}

const char *sdfs_detect(const char *dev){
    unsigned char b[SDFS_SECTOR] = {0};
    int fd = open(dev, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return NULL;
    size_t got = 0;
    while(got < sizeof b){
        ssize_t n = read(fd, b + got, sizeof b - got);
        if(n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    if(got != sizeof b) return NULL;
    if(memcmp(b + 3, "EXFAT   ", 8) == 0) return "exfat";
    if(b[510] != 0x55 || b[511] != 0xAA) return NULL;
    if(fat_label_ok(b) || bpb_ok(b)) return "vfat";
    return NULL;
}
