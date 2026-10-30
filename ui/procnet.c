/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "procnet.h"
#include <stdio.h>

int tcp_listen_in(FILE *f, int port){
    char line[256];
    if(!fgets(line, sizeof line, f)) return 0;
    while(fgets(line, sizeof line, f)){
        unsigned lport, st;
        if(sscanf(line, " %*d: %*[0-9A-Fa-f]:%x %*[0-9A-Fa-f]:%*x %x", &lport, &st) == 2 && st == 0x0A && (int)lport == port)
            return 1;
    }
    return 0;
}
