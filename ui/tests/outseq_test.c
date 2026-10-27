/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Host test for outseq.c: golden frame lists, failure injected at every step, settle timing.
 * outseq.c's only seam is ipc_send_cmd(); this file supplies a recording fake of it. */
#include "outseq.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAXF 16
static struct { char frame[48]; double t; } g_log[MAXF];
static int g_n, g_fail_at = -1;   /* g_fail_at: index of the call that returns -1 */

static double now(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
int ipc_send_cmd(const char *frame){
    assert(g_n < MAXF);
    strncpy(g_log[g_n].frame, frame, sizeof g_log[g_n].frame - 1);
    g_log[g_n].t = now();
    return g_n++ == g_fail_at ? -1 : 0;
}
static void reset(int fail_at){ g_n = 0; g_fail_at = fail_at; memset(g_log, 0, sizeof g_log); }

/* frames sent so far must equal the first n entries of want */
static void expect_frames(const char *what, const char *const *want, int n){
    if(g_n != n){ fprintf(stderr, "%s: %d frames sent, want %d\n", what, g_n, n); assert(0); }
    for(int i = 0; i < n; i++)
        if(strcmp(g_log[i].frame, want[i])){ fprintf(stderr, "%s: frame %d is %s, want %s\n", what, i, g_log[i].frame, want[i]); assert(0); }
}

#define R6 "0666000C0006"
#define R4 "0666000C0004"
#define R3 "0666000C0003"
#define G0 "0642000C0000"
#define G5 "0642000C0005"
#define W8 "0657000C0008"
#define G2 "0642000C0002"
#define G1 "0642000C0001"
#define P0 "0818000C0000"
#define W1 "0657000C0001"
#define W6 "0657000C0006"

typedef struct {
    const char *name;
    int (*run)(void);                  /* returns the outseq_rc as int */
    const char *frames[5]; int n;      /* the golden list */
    int soft_from;                     /* frames at index >= soft_from are SOFT (n if none) */
    int moved_idx;                     /* index of the route-moved frame, -1 if none */
    int settle_after;                  /* index of the frame followed by a 65 ms settle, -1 if none */
} golden_t;

static int r_full_int(void){ return outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL); }
static int r_full_spdif(void){ return outseq_route(OUTSEQ_SPDIF, OUTSEQ_FULL); }
static int r_full_usb(void){ return outseq_route(OUTSEQ_USB, OUTSEQ_FULL); }
static int r_quick_int(void){ return outseq_route(OUTSEQ_INTERNAL, OUTSEQ_QUICK); }
static int r_quick_spdif(void){ return outseq_route(OUTSEQ_SPDIF, OUTSEQ_QUICK); }
static int r_quick_usb(void){ return outseq_route(OUTSEQ_USB, OUTSEQ_QUICK); }
static int r_boot_int(void){ return outseq_local_boot(OUTSEQ_INTERNAL); }
static int r_boot_spdif(void){ return outseq_local_boot(OUTSEQ_SPDIF); }
static int r_boot_usb(void){ return outseq_local_boot(OUTSEQ_USB); }
static int r_src_local(void){ return outseq_source(OUTSEQ_SRC_LOCAL); }
static int r_src_usbdac(void){ return outseq_source(OUTSEQ_SRC_USB_DAC); }
static int r_src_btrx(void){ return outseq_source(OUTSEQ_SRC_BT_RECEIVING); }
static int r_src_storage(void){ return outseq_source(OUTSEQ_SRC_USB_STORAGE); }
static int r_analog(void){ return outseq_analog(); }
static int r_pre_int(void){ outseq_local_preamble(OUTSEQ_INTERNAL); return 0; }
static int r_pre_spdif(void){ outseq_local_preamble(OUTSEQ_SPDIF); return 0; }
static int r_pre_usb(void){ outseq_local_preamble(OUTSEQ_USB); return 0; }

/* from the doc's golden table (modes.c as of 9b3a7c7): MUST frames first, SOFT frames from soft_from */
static const golden_t G[] = {
    /*  name                 run            frames             n  soft_from  moved  settle_after */
    { "route INTERNAL full", r_full_int,    { R6, G0, W8 },    3, 3,         0,     0  },
    { "route SPDIF full",    r_full_spdif,  { R4, G0, W8 },    3, 3,         0,     0  },
    { "route USB full",      r_full_usb,    { R3, G5, W8 },    3, 3,         0,    -1  },
    { "route INTERNAL quick", r_quick_int,  { R6, W8 },        2, 2,         0,    -1  },
    { "route SPDIF quick",   r_quick_spdif, { R4, W8 },        2, 2,         0,    -1  },
    { "route USB quick",     r_quick_usb,   { R3, W8 },        2, 2,         0,    -1  },
    { "boot INTERNAL",       r_boot_int,    { G0, R6, W8 },    3, 2,        -1,    -1  },
    { "boot SPDIF",          r_boot_spdif,  { G0, R4, W8 },    3, 2,        -1,    -1  },
    { "boot USB",            r_boot_usb,    { R3, G5, W8 },    3, 1,        -1,    -1  },
    { "source LOCAL",        r_src_local,   { R6, G0, W8 },    3, 3,         0,    -1  },
    { "source USB DAC",      r_src_usbdac,  { R6, G2, W1 },    3, 3,         0,    -1  },
    { "source BT receiving", r_src_btrx,    { R6, P0, G0, W6 }, 4, 4,        0,    -1  },
    { "source USB storage",  r_src_storage, { R6, G1, W8 },    3, 3,         0,    -1  },
    { "analog (leave BT)",   r_analog,      { R6, W8 },        2, 2,        -1,    -1  },
    { "preamble INTERNAL",   r_pre_int,     { R6, W8 },        2, 0,        -1,    -1  },
    { "preamble SPDIF",      r_pre_spdif,   { R4, W8 },        2, 0,        -1,    -1  },
    { "preamble USB",        r_pre_usb,     { R3, G5, W8 },    3, 0,        -1,    -1  },
};
#define NG ((int)(sizeof G / sizeof G[0]))

static void check_golden(const golden_t *g){
    reset(-1);
    int rc = g->run();
    expect_frames(g->name, g->frames, g->n);
    assert(rc == OUTSEQ_OK);
    if(g->settle_after >= 0){
        double gap = g_log[g->settle_after + 1].t - g_log[g->settle_after].t;
        assert(gap >= 0.065);                    /* the 65 ms settle sits between the route frame and the gadget frame */
    }
    for(int i = 0; i + 1 < g->n; i++)
        if(i != g->settle_after) assert(g_log[i + 1].t - g_log[i].t < 0.040);   /* and nowhere else */
}

/* the doc's rule: a MUST failure stops (REFUSED up to the route-moved step, MOVED after it; no moved step: REFUSED);
 * a SOFT failure is remembered and the rest still goes out (result REFUSED, or MOVED once the route moved) */
static void check_fail_each(const golden_t *g){
    for(int k = 0; k < g->n; k++){
        reset(k);
        int rc = g->run();
        int soft = k >= g->soft_from;
        int sent = soft ? g->n : k + 1;
        expect_frames(g->name, g->frames, sent);
        if(g->name[0] == 'p') continue;          /* preamble returns void: only the frames sent are observable */
        int moved_before = g->moved_idx >= 0 && g->moved_idx < k;
        int want = moved_before ? OUTSEQ_MOVED : OUTSEQ_REFUSED;
        if(soft && g->moved_idx >= 0) want = OUTSEQ_MOVED;
        if(rc != want){ fprintf(stderr, "%s: fail at %d gives %d, want %d\n", g->name, k, rc, want); assert(0); }
    }
}

/* Bluetooth sink: only 0666 6, 0666 2 and the codec frame must land; 0666 2 is the route-moved step (index 4) */
static void check_bt_sink(void){
    const char *codec = "06b3001D0000AA:BB:CC:DD:EE:FF";
    const char *want[] = { R6, G0, W8, "06c1000C0000", "0666000C0002", W8, codec };
    reset(-1);
    assert(outseq_bt_sink(codec) == OUTSEQ_OK);
    expect_frames("bt sink", want, 7);
    static const struct { int sent, rc; } F[7] = {
        { 1, OUTSEQ_REFUSED },   /* 0666 6: nothing moved */
        { 7, OUTSEQ_OK }, { 7, OUTSEQ_OK }, { 7, OUTSEQ_OK },   /* 0642, 0657, 06c1: ignored */
        { 5, OUTSEQ_REFUSED },   /* 0666 2 itself refused: still not moved */
        { 7, OUTSEQ_OK },        /* the second 0657: ignored */
        { 7, OUTSEQ_MOVED },     /* codec frame: the route already moved */
    };
    for(int k = 0; k < 7; k++){
        reset(k);
        int rc = outseq_bt_sink(codec);
        if(rc != F[k].rc){ fprintf(stderr, "bt sink: fail at %d gives %d, want %d\n", k, rc, F[k].rc); assert(0); }
        expect_frames("bt sink fail", want, F[k].sent);
    }
    reset(-1);
    assert(outseq_bt_sink(NULL) == OUTSEQ_REFUSED && g_n == 0);
    assert(outseq_bt_sink("") == OUTSEQ_REFUSED && g_n == 0);
}

/* work-mode re-assert: exactly 0657 8, and a failed send changes nothing else */
static void check_assert_workmode(void){
    const char *want[] = { W8 };
    reset(-1); outseq_assert_workmode(); expect_frames("assert workmode", want, 1);
    reset(0);  outseq_assert_workmode(); expect_frames("assert workmode fail", want, 1);
}

int main(void){
    check_bt_sink();
    check_assert_workmode();
    for(int i = 0; i < NG; i++){ check_golden(&G[i]); check_fail_each(&G[i]); }
    /* an out-of-range route sends nothing */
    reset(-1);
    assert(outseq_route((outseq_route_t)3, OUTSEQ_FULL) == OUTSEQ_REFUSED && g_n == 0);
    assert(outseq_route((outseq_route_t)-1, OUTSEQ_QUICK) == OUTSEQ_REFUSED && g_n == 0);
    assert(outseq_source((outseq_src_t)4) == OUTSEQ_REFUSED && g_n == 0);
    assert(outseq_source((outseq_src_t)-1) == OUTSEQ_REFUSED && g_n == 0);
    printf("outseq_test: %d sequences + bt sink ok\n", NG);
    return 0;
}
