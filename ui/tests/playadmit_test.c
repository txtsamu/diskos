/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Host test for playadmit.c. Characterises the play-start gates exactly as main.c wrote them before the module existed:
 *   1. a named table (route x play state) of the frames that go out today;
 *   2. the whole situation cross product against LEGACY_* oracles, which are the old gate expressions verbatim.
 * playadmit.c links the real outseq.c; the only seam is ipc_send_cmd(), faked here with a recorder. */
#include "playadmit.h"
#include "outseq.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MAXF 16
static char g_log[MAXF][48];
static int g_n;
int ipc_send_cmd(const char *frame){
    assert(g_n < MAXF);
    strncpy(g_log[g_n], frame, sizeof g_log[g_n] - 1);
    g_n++;
    return 0;
}
static void reset(void){ g_n = 0; memset(g_log, 0, sizeof g_log); }

#define R6 "0666000C0006"
#define R4 "0666000C0004"
#define R3 "0666000C0003"
#define G5 "0642000C0005"
#define W8 "0657000C0008"

/* the frames outseq_local_preamble sends per route */
static const char *const PRE_FRAMES[3][3] = { { R6, W8, NULL }, { R4, W8, NULL }, { R3, G5, W8 } };

/* The gates as main.c had them (ui_play_list_ex / ui_queue_jump, and the four reserved-slot sites), verbatim apart from
 * the accessors becoming fields. Do not "simplify" these: they are the behaviour being preserved. */
static int legacy_preamble(const playadmit_situation_t *s){
    return s->fw_needs_local_init && s->source_mode == 0 && !s->bt_route && !s->playing && !s->play_recent;
}
static int legacy_workmode(const playadmit_situation_t *s){
    return s->source_mode == 0 && !s->bt_route;
}

static void expect(const char *what, playadmit_sent_t got, playadmit_sent_t want_sent, const char *const *want, int n){
    int ok = got == want_sent && g_n == n;
    for(int i = 0; ok && i < n; i++) ok = !strcmp(g_log[i], want[i]);
    if(!ok){
        fprintf(stderr, "%s: sent=%d (want %d), %d frames (want %d):", what, got, want_sent, g_n, n);
        for(int i = 0; i < g_n; i++) fprintf(stderr, " %s", g_log[i]);
        fprintf(stderr, "\n");
        assert(0);
    }
}

/* named situations. paused and stopped are the same input today: playing == 0 (playstate cannot tell them apart) */
typedef struct { const char *name; playadmit_situation_t s; } row_t;
#define LOCAL(route, playing, recent) { 1, 0, 0, (route), (playing), (recent) }

int main(void){
    int cases = 0;

    /* 1. LIST plays: the table the user cares about (firmware needs the init, Local source, no BT) */
    static const row_t rows[] = {
        { "internal, stopped",          LOCAL(OUTSEQ_INTERNAL, 0, 0) },
        { "internal, paused",           LOCAL(OUTSEQ_INTERNAL, 0, 0) },
        { "internal, playing",          LOCAL(OUTSEQ_INTERNAL, 1, 0) },
        { "internal, play recent",      LOCAL(OUTSEQ_INTERNAL, 0, 1) },
        { "SPDIF, stopped",             LOCAL(OUTSEQ_SPDIF, 0, 0) },
        { "SPDIF, paused",              LOCAL(OUTSEQ_SPDIF, 0, 0) },
        { "SPDIF, playing",             LOCAL(OUTSEQ_SPDIF, 1, 0) },
        { "SPDIF, play recent",         LOCAL(OUTSEQ_SPDIF, 0, 1) },
        { "USB audio, stopped",         LOCAL(OUTSEQ_USB, 0, 0) },
        { "USB audio, paused",          LOCAL(OUTSEQ_USB, 0, 0) },
        { "USB audio, playing",         LOCAL(OUTSEQ_USB, 1, 0) },
        { "USB audio, play recent",     LOCAL(OUTSEQ_USB, 0, 1) },
    };
    for(unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++){
        const row_t *r = &rows[i];
        reset();
        playadmit_sent_t got = playadmit_start(PLAYADMIT_LIST, &r->s);
        if(r->s.playing || r->s.play_recent) expect(r->name, got, PLAYADMIT_NONE, NULL, 0);
        else {
            const char *const *f = PRE_FRAMES[r->s.output_route];
            int n = f[2] ? 3 : 2;
            expect(r->name, got, PLAYADMIT_PREAMBLE, f, n);
        }
        cases++;
    }
    {   /* Bluetooth route, stopped: never 0666 under an A2DP sink (output_route is stale there, whatever it holds) */
        playadmit_situation_t s = { 1, 0, 1, OUTSEQ_USB, 0, 0 };
        reset(); expect("BT route, stopped", playadmit_start(PLAYADMIT_LIST, &s), PLAYADMIT_NONE, NULL, 0); cases++;
        s.output_route = OUTSEQ_INTERNAL;
        reset(); expect("BT route (internal recorded), stopped", playadmit_start(PLAYADMIT_LIST, &s), PLAYADMIT_NONE, NULL, 0); cases++;
    }
    {   /* firmware without the drift (V2.09/V2.28): LIST sends nothing, RESERVED_SLOT still asserts the work mode */
        playadmit_situation_t s = { 0, 0, 0, OUTSEQ_INTERNAL, 0, 0 };
        static const char *const w[] = { W8 };
        reset(); expect("old firmware, list", playadmit_start(PLAYADMIT_LIST, &s), PLAYADMIT_NONE, NULL, 0); cases++;
        reset(); expect("old firmware, reserved slot", playadmit_start(PLAYADMIT_RESERVED_SLOT, &s), PLAYADMIT_WORKMODE, w, 1); cases++;
    }
    for(int src = 1; src <= 3; src++){   /* USB DAC / Bluetooth receiving / USB storage own the Player */
        playadmit_situation_t s = { 1, src, 0, OUTSEQ_INTERNAL, 0, 0 };
        reset(); expect("other source, list", playadmit_start(PLAYADMIT_LIST, &s), PLAYADMIT_NONE, NULL, 0); cases++;
        reset(); expect("other source, reserved slot", playadmit_start(PLAYADMIT_RESERVED_SLOT, &s), PLAYADMIT_NONE, NULL, 0); cases++;
    }
    {   /* RESERVED_SLOT ignores the play state, the recent latch and the route: 0657 8 alone, never 0666 */
        static const char *const w[] = { W8 };
        for(int rt = OUTSEQ_INTERNAL; rt <= OUTSEQ_USB; rt++)
            for(int pl = 0; pl <= 1; pl++)
                for(int rc = 0; rc <= 1; rc++){
                    playadmit_situation_t s = { 1, 0, 0, rt, pl, rc };
                    reset(); expect("reserved slot, local", playadmit_start(PLAYADMIT_RESERVED_SLOT, &s), PLAYADMIT_WORKMODE, w, 1); cases++;
                }
        playadmit_situation_t bt = { 1, 0, 1, OUTSEQ_INTERNAL, 0, 0 };
        reset(); expect("reserved slot, BT", playadmit_start(PLAYADMIT_RESERVED_SLOT, &bt), PLAYADMIT_NONE, NULL, 0); cases++;
    }
    reset(); expect("NULL situation", playadmit_start(PLAYADMIT_LIST, NULL), PLAYADMIT_NONE, NULL, 0); cases++;

    /* 2. the full cross product against the old gate expressions */
    int product = 0;
    for(int fw = 0; fw <= 1; fw++) for(int src = 0; src <= 3; src++) for(int bt = 0; bt <= 1; bt++)
    for(int rt = OUTSEQ_INTERNAL; rt <= OUTSEQ_USB; rt++) for(int pl = 0; pl <= 1; pl++) for(int rc = 0; rc <= 1; rc++){
        playadmit_situation_t s = { fw, src, bt, rt, pl, rc };
        static const char *const w[] = { W8 };
        reset();
        playadmit_sent_t got = playadmit_start(PLAYADMIT_LIST, &s);
        if(legacy_preamble(&s)) expect("product list", got, PLAYADMIT_PREAMBLE, PRE_FRAMES[rt], PRE_FRAMES[rt][2] ? 3 : 2);
        else                    expect("product list", got, PLAYADMIT_NONE, NULL, 0);
        reset();
        got = playadmit_start(PLAYADMIT_RESERVED_SLOT, &s);
        if(legacy_workmode(&s)) expect("product slot", got, PLAYADMIT_WORKMODE, w, 1);
        else                    expect("product slot", got, PLAYADMIT_NONE, NULL, 0);
        product += 2;
    }

    printf("playadmit_test: %d named cases + %d cross-product cases ok\n", cases, product);
    return 0;
}
