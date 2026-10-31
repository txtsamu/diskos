/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "coverart_synth.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv){
    if(argc < 4) return 64;
    const char *log = getenv("ARTDEC_LOG");
    if(log){
        FILE *l = fopen(log, "a");
        if(l){ fprintf(l, "%s %s\n", argv[1], argv[2]); fclose(l); }
    }
    const char *touch = getenv("ARTDEC_TOUCH");
    if(touch){
        struct timeval tv[2]; gettimeofday(&tv[0], NULL); tv[0].tv_sec += 1000; tv[1] = tv[0];
        utimes(touch, tv);
    }
    int fd = open(argv[2], O_RDONLY);
    if(fd < 0) return 2;
    unsigned char b = 0;
    if(read(fd, &b, 1) != 1) b = 0;
    close(fd);
    if(b == 'X') return 4;
    if(b == 'Y') return 3;
    int kind = b >= 'a' && b < 'a' + SYN_KINDS ? b - 'a' : 3;
    char p[512];
    if(!strcmp(argv[1], "saver")){
        FILE *f = fopen(argv[3], "wb"); if(!f) return 5;
        unsigned char row[1440]; memset(row, 0x30 + kind, sizeof row);
        for(int i = 0; i < 360 * 360 * 4 / (int)sizeof row; i++) fwrite(row, 1, sizeof row, f);
        return fclose(f) == 0 ? 0 : 5;
    }
    snprintf(p, sizeof p, "%s/c.bmp", argv[3]); if(syn_write_bmp_n(p, 148, kind)) return 5;
    snprintf(p, sizeof p, "%s/t.bmp", argv[3]); if(syn_write_bmp_n(p, 42, kind)) return 5;
    snprintf(p, sizeof p, "%s/b.bmp", argv[3]); if(syn_write_bmp_n(p, 360, kind)) return 5;
    return 0;
}
