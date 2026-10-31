#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
"""Run the actual handoff controller (srcswitch.c) and SD access module (sdio.c) with simulated IPC, mounts and gadget state.

The filesystem marker and SD admission implementation are real, and so are session.c (the Source mode / Output route the
module reads and writes) and outseq.c. Only hardware and running-player inspection are replaced, through the struct
srcswitch_env and struct sd_env tables the modules take, plus the ipc/cfg seams below. The test includes sdio.c and
srcswitch.c themselves (not a slice of main.c) so it can also set the phase, the way a UI restart leaves it. The via-Local and Output-route paths run for
real here (no stubs of them); srcswitch_test.c in this directory covers them in depth.
"""
import os
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
def run_harness(exe, timeout):
    """Run a compiled harness; its LAST stdout line must be HARNESS COMPLETE, so an early return 0 fails."""
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=timeout)
    print(r.stdout, end='')
    if r.stderr:
        print(r.stderr, end='')
    if r.returncode != 0:
        raise SystemExit(f'FAIL harness exited {r.returncode}')
    lines = r.stdout.strip().splitlines()
    if not lines or lines[-1] != 'HARNESS COMPLETE':
        raise SystemExit('FAIL harness did not reach its HARNESS COMPLETE line')


with tempfile.TemporaryDirectory(prefix="diskos-storage-tests-") as tmp:
    root = Path(tmp)
    headers = r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>
#include <glob.h>
#include <dirent.h>
#include "sdio.h"
#include "outseq.h"
#include "modes.h"   /* OUT_USB */
#include "ipc.h"     /* track_state_t, ipc_get_state(), ipc_send_cmd() */
#include "config.h"  /* cfg_get_int() */
#include "session.h" /* the real session.c is linked: Source mode and Output route */
#include "srcswitch.h"
#include "admit.h"   /* admit_check(): faked below */
static uint32_t test_clock;
static int test_mounted=1, test_exported, test_host, test_guarded=1, test_scan, test_playing;
static int test_sends, test_fail, test_export_sends, test_seed, test_attached, test_gadget, test_restarts;
static int test_volume_set, test_scans;
static struct { char key[24]; int v; } test_cfg[8]; static int test_ncfg;   /* cfg_*: a tiny in-memory store (session.c saves the pick) */
int cfg_get_int(const char *key, int def){ for(int i=0;i<test_ncfg;i++) if(!strcmp(test_cfg[i].key,key)) return test_cfg[i].v; return def; }
int cfg_set_int(const char *key, int v){
    for(int i=0;i<test_ncfg;i++) if(!strcmp(test_cfg[i].key,key)){ test_cfg[i].v=v; return 0; }
    assert(test_ncfg<8); snprintf(test_cfg[test_ncfg].key,sizeof test_cfg[0].key,"%s",key); test_cfg[test_ncfg++].v=v; return 0;
}
static int test_local_vol;
void ipc_get_state(track_state_t *out){ memset(out, 0, sizeof *out); out->volume = test_local_vol; }
unsigned ipc_generation(void){ return 1; }
int ipc_is_ready(void){ return 1; }
const char *admit_check(admit_what_t what){ (void)what; return NULL; }   /* no output switch in flight */
/* The seam outseq.c (linked below, the real module) sends every frame through; external so it can link. */
int ipc_send_cmd(const char *s){
    test_sends++;
    if(test_fail) return -1;
    if(!strcmp(s,"0642000C0001")){
        assert(!sd_io_allowed() && sd_drained());
        test_export_sends++;
    }
    return 0;
}
/* The environment table: gadget, mount and Player facts the test sets, and the effects it counts. */
static uint32_t env_now(void){ return test_clock; }
static int env_mounted(void){ return test_mounted; }
static int env_exported(void){ return test_exported; }
static int env_host_confirmed(void){ return test_host; }
static int env_attached(void){ return test_attached; }
static int env_gadget(void){ return test_gadget; }
static int env_guarded(void){ return test_guarded; }
static int env_playing(void){ return test_playing; }
static int env_scan_active(void){ return test_scan; }
static void env_scan_empty(void){ test_scans++; }
static void env_nothing(void){}
static void env_seed(void){ test_seed++; }
static void env_restart(void){ test_restarts++; }
static void env_toast(const char *s){ (void)s; }
static void env_volume(int v){ (void)v; test_volume_set++; }
static int env_seek(long ms){ (void)ms; return 0; }
static const struct srcswitch_env test_env = {
    .now = env_now, .host_attached = env_attached, .dac_gadget_present = env_gadget,
    .playing = env_playing, .toast = env_toast, .set_volume = env_volume,
    .seek_to = env_seek,
};
static const struct sd_env test_sd_env = {
    .now = env_now, .media_mounted = env_mounted, .exported_to_host = env_exported, .host_confirmed = env_host_confirmed,
    .player_guarded = env_guarded, .playing = env_playing, .scan_active = env_scan_active, .scan_if_library_empty = env_scan_empty,
    .art_cancel = env_nothing, .card_returned = env_seed, .restart = env_restart, .toast = env_toast,
    .send_source = srcswitch_send_source,
};
#include "sdio.c"
#include "srcswitch.c"   /* the module under test, in this translation unit */
'''
    tests = r'''
static void reset(void){
    srcswitch_set_env(&test_env); sd_set_env(&test_sd_env);
    g_launch_verdict = 1;   /* a confirmed launch; the gate itself is covered by launcher_test */
    assert(sd_drained());
    unlink(SD_EXPORT_MARKER);
    test_clock=0; test_mounted=1; test_exported=0; test_host=0;
    test_guarded=1; test_scan=0; test_playing=0; test_attached=0; test_gadget=0; test_restarts=0; test_scans=0; test_volume_set=0; test_local_vol=0; g_dac_vol_due=0; g_dacx.active=0; test_sends=0; test_fail=0; test_export_sends=0; test_seed=0;
    g_sd_hold=0; g_sd_phase=SD_LOCAL; session_set_source_mode(0); session_set_route(OUT_INTERNAL); g_sd_reissue=0;
    memset(&g_osw,0,sizeof g_osw); memset(&g_leave,0,sizeof g_leave); g_rec=0; memset(&g_rec_tm,0,sizeof g_rec_tm); memset(&g_usbw,0,sizeof g_usbw); memset(&g_usbgone,0,sizeof g_usbgone); memset(&g_bootusb,0,sizeof g_bootusb); g_listener=NULL;
    g_sd_writable=1; g_sd_writers=0;
    srcswitch_init(); assert(sd_io_allowed() && sd_local_playback_allowed());
}
static void enter_host(void){
    assert(ui_set_source_mode(3)==0);
    srcswitch_tick();
    assert(g_sd_phase==SD_WAIT_HOST && test_export_sends==1);
    test_mounted=0; test_exported=1; test_host=1;
    srcswitch_tick(); assert(g_sd_phase==SD_HOST);
}
int main(void){
    /* Until the launcher publishes a positive verdict for THIS player, nothing may touch the card:
     * no lease, no resume, and no playback - whatever the mount and gadget state look like. */
    reset();
    g_launch_verdict = -1;
    assert(!sd_io_begin());
    assert(!sd_io_resume());
    assert(!sd_local_playback_allowed());
    g_launch_verdict = 0;
    assert(!sd_io_begin());
    assert(!sd_io_resume());
    assert(!sd_local_playback_allowed());
    puts("PASS a pending or negative launch verdict closes leases, resume and playback");

    reset();
    assert(sd_io_begin());
    assert(ui_set_source_mode(3)==0);
    assert(!sd_io_begin());
    srcswitch_tick(); assert(test_sends==0 && g_sd_phase==SD_DRAIN);
    sd_io_end(); srcswitch_tick();
    assert(g_sd_phase==SD_WAIT_HOST && test_export_sends==1);
    srcswitch_tick(); assert(g_sd_phase==SD_WAIT_HOST && !sd_io_allowed());
    test_mounted=0; test_exported=1; test_host=1; srcswitch_tick();
    assert(g_sd_phase==SD_HOST && !sd_io_begin());
    assert(ui_set_source_mode(0)==0);
    test_host=0; test_mounted=1; srcswitch_tick();
    assert(g_sd_phase==SD_WAIT_LOCAL && !sd_io_allowed()); /* LUN still attached */
    test_exported=0; srcswitch_tick();
    assert(g_sd_phase==SD_LOCAL && sd_io_begin()); sd_io_end();
    assert(sd_write_begin()); sd_write_end();
    assert(test_seed==1 && access(SD_EXPORT_MARKER,F_OK)!=0);
    puts("PASS drain, export acknowledgement, detach, mount and cache resume");

    reset(); test_guarded=0;
    assert(ui_set_source_mode(3)<0 && test_sends==0 && sd_io_allowed());
    test_guarded=1; test_scan=1;
    assert(ui_set_source_mode(3)<0 && test_sends==0 && sd_io_allowed());
    puts("PASS unprotected player and active scan reject before pausing media");

    reset(); assert(ui_set_source_mode(3)==0); test_fail=1; srcswitch_tick();
    assert(g_sd_phase==SD_UNKNOWN && !sd_io_allowed());
    test_fail=0; test_clock=50000; srcswitch_tick();
    assert(!sd_io_allowed() && access(SD_EXPORT_MARKER,F_OK)==0);
    puts("PASS failed IPC cannot reopen the card");

    reset(); assert(ui_set_source_mode(3)==0); srcswitch_tick();
    test_clock=25000; srcswitch_tick();
    assert(g_sd_phase==SD_UNKNOWN && !sd_io_allowed());
    puts("PASS elapsed time is not export completion");

    reset(); assert(ui_set_source_mode(3)==0); srcswitch_tick();
    g_sd_phase=SD_LOCAL; srcswitch_init();
    assert(g_sd_phase==SD_WAIT_HOST && !sd_io_allowed());
    test_mounted=0; test_exported=1; test_host=1; srcswitch_tick();
    assert(g_sd_phase==SD_HOST);
    g_sd_phase=SD_LOCAL; srcswitch_init();
    assert(g_sd_phase==SD_HOST && !sd_io_allowed());
    assert(ui_set_source_mode(0)==0);
    g_sd_phase=SD_LOCAL; srcswitch_init();
    assert(g_sd_phase==SD_WAIT_LOCAL && !sd_io_allowed());
    test_mounted=1; test_exported=0; test_host=0; srcswitch_tick();
    assert(g_sd_phase==SD_LOCAL && sd_io_allowed());
    puts("PASS restart restores pending, exported and returning ownership");

    reset(); assert(storage_mark('E',3));
    srcswitch_init(); assert(g_sd_phase==SD_WAIT_HOST && !sd_io_allowed());
    srcswitch_tick(); assert(test_export_sends==1 && !g_sd_reissue);
    test_mounted=0; test_exported=1; test_host=1; srcswitch_tick();
    assert(g_sd_phase==SD_HOST && storage_mark('R',0));
    srcswitch_init(); int sent=test_sends; srcswitch_tick();
    assert(test_sends>sent && !g_sd_reissue && g_sd_phase==SD_WAIT_LOCAL);
    puts("PASS crash between marker publication and IPC reissues the saved request");

    reset(); assert(storage_mark('D',3)); test_mounted=0;
    srcswitch_init(); assert(g_sd_phase==SD_LOCAL && !sd_io_allowed());
    test_mounted=1; srcswitch_tick(); assert(sd_io_allowed());
    puts("PASS interrupted drain waits for coldplug without a permanent hold");

    reset(); enter_host();
    assert(ui_set_source_mode(3)==0 && test_export_sends==1);
    assert(ui_set_source_mode(0)==0);
    assert(ui_set_source_mode(3)<0 && test_export_sends==1);
    puts("PASS repeated requests cannot queue a late export behind return");

    reset(); FILE *f=fopen(SD_EXPORT_MARKER,"w"); assert(f); fclose(f);
    srcswitch_init(); assert(g_sd_phase==SD_UNKNOWN && !sd_io_allowed());
    puts("PASS old or malformed handoff marker remains held");
    /* Leaving USB DAC with a computer on the cable: 0642 0 first, wait for the gadget to go (+1.5 s), only then the switch. */
    reset(); session_set_source_mode(1); test_attached=1; test_gadget=1;
    assert(ui_set_source_mode(0)==0 && g_dacx.active && session_source_mode()==1 && test_sends==1);
    assert(ui_set_source_mode(2)<0 && test_sends==1);                      /* a second request while it waits is refused */
    test_clock=5000; srcswitch_tick(); assert(g_dacx.active && session_source_mode()==1);   /* gadget still bound */
    test_gadget=0; test_clock=6000; srcswitch_tick(); assert(g_dacx.active);       /* seen gone: margin starts */
    test_clock=7400; srcswitch_tick(); assert(g_dacx.active && session_source_mode()==1);
    test_clock=7500; srcswitch_tick(); assert(!g_dacx.active && session_source_mode()==0 && test_sends>1);
    puts("PASS leaving USB DAC waits for the gadget, then switches");

    reset(); session_set_source_mode(1); test_attached=1; test_gadget=1;
    assert(ui_set_source_mode(0)==0);
    test_clock=30001; srcswitch_tick();
    assert(!g_dacx.active && session_source_mode()==1);
    puts("PASS a gadget that never goes away ends the exit without switching");

    /* USB DAC volume: the Local level is carried over once the switch has settled (4.5 s), never before. */
    reset(); test_local_vol=40;
    assert(ui_set_source_mode(1)==0 && session_source_mode()==1);
    test_clock=4499; srcswitch_tick(); assert(test_volume_set==0 && !srcswitch_dac_level_settled());
    test_clock=4500; srcswitch_tick(); assert(test_volume_set==1);
    test_clock=4600; srcswitch_tick(); assert(test_volume_set==1);          /* one-shot */
    test_clock=8001; assert(srcswitch_dac_level_settled());
    puts("PASS the Local volume is carried into USB DAC once, after the switch settled");

    reset(); test_local_vol=40;
    assert(ui_set_source_mode(1)==0); assert(ui_set_source_mode(0)==0);     /* gone before the carry is due */
    test_clock=5000; srcswitch_tick(); assert(test_volume_set==0);
    puts("PASS no volume carry when USB DAC was left before it settled");

    reset(); enter_host(); assert(ui_set_source_mode(0)==0);
    test_host=0; test_mounted=1; test_exported=0; srcswitch_tick();
    assert(g_sd_phase==SD_LOCAL && test_restarts==1);
    puts("PASS returning from USB Storage restarts the device once");

    reset(); assert(ui_set_source_mode(3)==0); sd_io_fault(); srcswitch_tick();
    assert(g_sd_phase==SD_UNKNOWN && test_export_sends==0 && !sd_io_resume());
    srcswitch_init(); assert(!sd_io_resume() && !sd_local_playback_allowed());
    puts("PASS unreaped decoder prevents export and survives UI restart");
    puts("HARNESS COMPLETE");
    return 0;
}
'''
    c = root / "test.c"
    c.write_text(headers + tests)
    exe = root / "test"
    subprocess.run([os.environ.get("CC", "cc"), "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g", "-pthread", '-DSD_IO_FAULT_MARKER="' + str(root / "fault") + '"', '-DSD_EXPORT_MARKER="' + str(root / "handoff") + '"', "-I", str(app),
                    str(c), str(app / "outseq.c"), str(app / "session.c"), "-o", str(exe)], check=True)
    run_harness(exe, 10)

print('COMPLETE storage_test.py')
