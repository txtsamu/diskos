/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "dacvol_sys.h"
#include <assert.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef DACVOL_TEST_DIR
#error "the Makefile points the fake amixer and the input nodes at a temp dir"
#endif
#define D DACVOL_TEST_DIR

#include "ipc.h"
void ipc_get_state(track_state_t *out){ memset(out, 0, sizeof *out); }
int cfg_get_int(const char *key, int def){ (void)key; return def; }
int cfg_set_int(const char *key, int v){ (void)key; (void)v; return 0; }

static const char SCRIPT[] =
"#!/bin/sh\n"
"D=\"" D "\"\n"
"echo \"$@\" >> \"$D/calls\"\n"
"case \"$3\" in\n"
"contents)\n"
"  [ -f \"$D/probe_fail\" ] && exit 3\n"
"  [ -f \"$D/probe_hang\" ] && sleep 6\n"
"  cat \"$D/contents\"\n"
"  exit 0 ;;\n"
"cset)\n"
"  [ -f \"$D/set_hang\" ] && sleep 6\n"
"  [ -f \"$D/slow\" ] && sleep 0.3\n"
"  [ -f \"$D/set_fail\" ] && exit 1\n"
"  echo \"$5\" >> \"$D/values\"\n"
"  exit 0 ;;\n"
"esac\n"
"exit 2\n";

static const char CONTENTS[] =
"numid=5,iface=MIXER,name='PCM Playback Switch'\n  ; type=BOOLEAN,access=rw------,values=1\n  : values=on\n"
"numid=6,iface=MIXER,name='PCM Playback Volume'\n  ; type=INTEGER,access=rw---R--,values=2,min=0,max=100,step=0\n  : values=100,100\n"
"  | dBminmax-min=-50.00dB,max=0.00dB\n";
static const char NO_CONTROL[] = "numid=4,iface=MIXER,name='Mic Capture Volume'\n  ; type=INTEGER,access=rw---R--,values=1,min=0,max=100,step=0\n  : values=100\n";

static void put(const char *name, const char *text){
    char p[256]; snprintf(p, sizeof p, "%s/%s", D, name);
    FILE *f = fopen(p, "w"); assert(f); fputs(text, f); fclose(f);
}
static void flag(const char *name, int on){
    char p[256]; snprintf(p, sizeof p, "%s/%s", D, name);
    if(on){ FILE *f = fopen(p, "w"); assert(f); fclose(f); } else unlink(p);
}
static int lines(const char *name, char *last, size_t cap){
    char p[256]; snprintf(p, sizeof p, "%s/%s", D, name);
    FILE *f = fopen(p, "r");
    if(!f) return 0;
    char b[256]; int n = 0;
    if(last) last[0] = 0;
    while(fgets(b, sizeof b, f)){ n++; if(last){ b[strcspn(b, "\n")] = 0; snprintf(last, cap, "%s", b); } }
    fclose(f);
    return n;
}
static void clear_logs(void){ flag("calls", 0); flag("values", 0); }
static int wait_state(int want, int ms){
    for(int t = 0; t < ms; t += 5){ if(dacvol_sys_mixer_state() == want) return 1; usleep(5000); }
    return 0;
}
static int wait_probe(int want, int ms){
    for(int t = 0; t < ms; t += 5){ if(dacvol_sys_mixer_probe_poll(NULL) == want) return 1; usleep(5000); }
    return 0;
}

int main(void){
    char cmd[300], last[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/sys/event0 %s/sys/event1 %s/dev", D, D, D, D);
    assert(system(cmd) == 0);
    char script[300]; snprintf(script, sizeof script, "%s/amixer", D);
    FILE *f = fopen(script, "w"); assert(f); fputs(SCRIPT, f); fclose(f);
    assert(chmod(script, 0755) == 0);
    put("contents", CONTENTS);

    assert(dacvol_sys_mixer_set(10) == -1);
    assert(dacvol_sys_mixer_state() == DACVOL_MIX_IDLE);

    assert(dacvol_sys_mixer_probe(1) == 0);
    assert(wait_probe(DACVOL_PROBE_OK, 3000));
    dacvol_ctl_t c;
    assert(dacvol_sys_mixer_probe_poll(&c) == DACVOL_PROBE_OK);
    assert(c.numid == 6 && c.min == 0 && c.max == 100 && c.cur == 100 && c.channels == 2 && c.db_min_cdb == -5000);
    assert(lines("calls", last, sizeof last) == 1 && !strcmp(last, "-c 1 contents"));

    clear_logs();
    assert(dacvol_sys_mixer_set(40) == 0);
    assert(wait_state(DACVOL_MIX_IDLE, 3000));
    usleep(100000);
    assert(lines("calls", last, sizeof last) == 1 && !strcmp(last, "-c 1 cset numid=6 40,40"));
    assert(lines("values", last, sizeof last) == 1 && !strcmp(last, "40,40"));

    clear_logs();
    flag("slow", 1);
    for(int v = 1; v <= 50; v++){ assert(dacvol_sys_mixer_set(v) == 0); usleep(2000); }
    assert(dacvol_sys_mixer_state() == DACVOL_MIX_BUSY);
    assert(wait_state(DACVOL_MIX_IDLE, 5000));
    int n = lines("values", last, sizeof last);
    assert(n >= 1 && n <= 3);
    assert(!strcmp(last, "50,50"));
    flag("slow", 0);

    clear_logs();
    flag("set_fail", 1);
    assert(dacvol_sys_mixer_set(10) == 0);
    assert(wait_state(DACVOL_MIX_FAILED, 3000));
    assert(lines("calls", NULL, 0) == 2);
    flag("set_fail", 0);
    assert(dacvol_sys_mixer_set(11) == 0);
    assert(wait_state(DACVOL_MIX_IDLE, 3000));
    assert(lines("values", last, sizeof last) == 1 && !strcmp(last, "11,11"));

    clear_logs();
    flag("set_hang", 1);
    assert(dacvol_sys_mixer_set(12) == 0);
    assert(wait_state(DACVOL_MIX_FAILED, 6000));
    flag("set_hang", 0);
    assert(dacvol_sys_mixer_set(13) == 0);
    assert(wait_state(DACVOL_MIX_IDLE, 3000));

    flag("probe_fail", 1);
    assert(dacvol_sys_mixer_probe(1) == 0);
    assert(wait_probe(DACVOL_PROBE_ERROR, 3000));
    assert(dacvol_sys_mixer_set(10) == -1);
    flag("probe_fail", 0);

    put("contents", NO_CONTROL);
    assert(dacvol_sys_mixer_probe(1) == 0);
    assert(wait_probe(DACVOL_PROBE_NOCTL, 3000));
    assert(dacvol_sys_mixer_set(10) == -1);
    put("contents", CONTENTS);

    flag("probe_hang", 1);
    assert(dacvol_sys_mixer_probe(1) == 0);
    assert(dacvol_sys_mixer_probe_poll(NULL) == DACVOL_PROBE_PENDING);
    assert(wait_probe(DACVOL_PROBE_ERROR, 4000));
    flag("probe_hang", 0);

    char p[256];
    snprintf(p, sizeof p, "%s/sys/event0/name", D); f = fopen(p, "w"); fputs("cst816t\n", f); fclose(f);
    snprintf(p, sizeof p, "%s/sys/event1/name", D); f = fopen(p, "w"); fputs("x2000_key\n", f); fclose(f);
    snprintf(p, sizeof p, "%s/dev/event1", D); f = fopen(p, "wb"); assert(f);
    struct input_event ev[5]; memset(ev, 0, sizeof ev);
    ev[0].type = EV_SYN;
    ev[1].type = EV_KEY; ev[1].code = DACVOL_KEY_UP; ev[1].value = 1;
    ev[2].type = EV_SYN;
    ev[3].type = EV_KEY; ev[3].code = DACVOL_KEY_DOWN; ev[3].value = 2;
    ev[4].type = EV_KEY; ev[4].code = 265; ev[4].value = 0;
    fwrite(ev, sizeof ev[0], 5, f); fclose(f);
    int code, value;
    assert(dacvol_sys_key_poll(&code, &value) == 0);
    dacvol_sys_key_active(1);
    assert(dacvol_sys_key_poll(&code, &value) == 1 && code == DACVOL_KEY_UP && value == 1);
    assert(dacvol_sys_key_poll(&code, &value) == 1 && code == DACVOL_KEY_DOWN && value == 2);
    assert(dacvol_sys_key_poll(&code, &value) == 1 && code == 265 && value == 0);
    assert(dacvol_sys_key_poll(&code, &value) == 0);
    dacvol_sys_key_active(0);
    assert(dacvol_sys_key_poll(&code, &value) == 0);
    unlink(p);
    dacvol_sys_key_active(1);
    assert(dacvol_sys_key_poll(&code, &value) == 0);
    dacvol_sys_key_active(0);

    puts("PASS (0 failures)");
    return 0;
}
