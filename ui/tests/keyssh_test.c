/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "procnet.h"

static int run(const char *text, int port){
    FILE *f = tmpfile(); assert(f);
    fputs(text, f); rewind(f);
    int r = tcp_listen_in(f, port);
    fclose(f);
    return r;
}

#define HDR4 "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
#define HDR6 "  sl  local_address                         remote_address                        st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"

int main(void){
    assert(run(HDR4 "   0: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 1234 1 00000000 100 0 0 10 0\n"
                    "   1: 00000000:08AE 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 1235 1 00000000 100 0 0 10 0\n", 2222) == 1);
    assert(run(HDR4 "   0: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 1234 1 00000000 100 0 0 10 0\n", 2222) == 0);
    assert(run(HDR4 "   0: 0100007F:08AE 0100007F:C350 01 00000000:00000000 00:00000000 00000000     0        0 1236 1 00000000 20 4 30 10 -1\n"
                    "   1: 0100007F:C351 0100007F:08AE 0A 00000000:00000000 00:00000000 00000000     0        0 1237 1 00000000 20 4 30 10 -1\n", 2222) == 0);
    assert(run(HDR6 "   0: 00000000000000000000000000000000:08AE 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 4321 1 00000000 100 0 0 10 0\n", 2222) == 1);
    assert(run(HDR4 "  12: 00000000:08ae 00000000:0000 0a 00000000:00000000 00:00000000 00000000     0        0 9 1 00000000 100 0 0 10 0\n", 2222) == 1);
    assert(run(HDR4, 2222) == 0);
    assert(run("", 2222) == 0);
    assert(run(HDR4 "not a socket line\n:::\n", 2222) == 0);
    assert(run(HDR4 "   0: 00000000:08AE 00000000:0000 0A", 2222) == 1);
    printf("keyssh_test ok\n");
    return 0;
}
