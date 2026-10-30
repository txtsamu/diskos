/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "procnet.h"
#include <stdio.h>

/* Is something LISTENing on TCP `port` in this /proc/net/tcp or tcp6 table? Lines look like
 * "  0: 00000000:08AE 00000000:0000 0A ..." (hex local port, state 0A = LISTEN). Pure parsing: host-tested. */
int tcp_listen_in(FILE *f, int port){
    char line[256];
    if(!fgets(line, sizeof line, f)) return 0;                       /* header */
    while(fgets(line, sizeof line, f)){
        unsigned lport, st;
        if(sscanf(line, " %*d: %*[0-9A-Fa-f]:%x %*[0-9A-Fa-f]:%*x %x", &lport, &st) == 2 && st == 0x0A && (int)lport == port)
            return 1;
    }
    return 0;
}
