/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "admit.h"
#include "session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXF 32
static char g_log[MAXF][48];
static int g_n, g_sends, k_fail_nth, k_fail_all;
int ipc_send_cmd(const char *frame){
    g_sends++;
    if(k_fail_all || (k_fail_nth && g_sends == k_fail_nth)) return -1;
    assert(g_n < MAXF);
    snprintf(g_log[g_n++], sizeof g_log[0], "%s", frame);
    return 0;
}
static unsigned k_gen = 1;
unsigned ipc_generation(void){ return k_gen; }
static track_state_t k_state;
void ipc_get_state(track_state_t *out){ *out = k_state; }
static struct { char key[24]; int v; } g_cfg[8]; static int g_ncfg;
int cfg_get_int(const char *key, int def){ for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)) return g_cfg[i].v; return def; }
int cfg_set_int(const char *key, int v){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)){ g_cfg[i].v = v; return 0; }
    assert(g_ncfg < 8); snprintf(g_cfg[g_ncfg].key, sizeof g_cfg[0].key, "%s", key); g_cfg[g_ncfg++].v = v; return 0;
}
static void reset_log(void){ g_n = 0; g_sends = 0; k_fail_nth = 0; k_fail_all = 0; memset(g_log, 0, sizeof g_log); }

static uint32_t k_now;
static int k_allowed, k_busy, k_recent, k_book, k_playing, k_fw;
static uint32_t env_now(void){ return k_now; }
static int env_allowed(void){ return k_allowed; }
static int env_busy(void){ return k_busy; }
static int env_recent(void){ return k_recent; }
static int env_book(void){ return k_book; }
static int env_playing(void){ return k_playing; }
static int env_fw(void){ return k_fw; }
static const struct admit_env test_env = { env_now, env_allowed, env_busy, env_recent, env_book, env_playing, env_fw };

#define R6 "0666000C0006"
#define R4 "0666000C0004"
#define R3 "0666000C0003"
#define G5 "0642000C0005"
#define W8 "0657000C0008"
#define PP PLAYER_CMD_PLAYPAUSE
static const char *const PRE_FRAMES[3][3] = { { R6, W8, NULL }, { R4, W8, NULL }, { R3, G5, W8 } };

static void expect_frames(const char *what, const char *const *want, int n){
    int ok = g_n == n;
    for(int i = 0; ok && i < n; i++) ok = !strcmp(g_log[i], want[i]);
    if(!ok){
        fprintf(stderr, "%s: %d frames (want %d):", what, g_n, n);
        for(int i = 0; i < g_n; i++) fprintf(stderr, " %s", g_log[i]);
        fprintf(stderr, "\n");
        assert(0);
    }
}
static void expect_reason(const char *what, const char *got, const char *want){
    if(want ? (!got || strcmp(got, want)) : got != NULL){
        fprintf(stderr, "%s: reason '%s', want '%s'\n", what, got ? got : "(go)", want ? want : "(go)");
        assert(0);
    }
}

typedef struct {
    int allowed, busy;
    int src;
    int bt;
    int settling;
    int pending, recent;
    int book;
    int playing, fw, route;
} facts_t;

static void apply(const facts_t *f){
    admit_set_env(&test_env);
    k_gen++;
    k_now += 20000;
    (void)admit_settling();
    k_allowed = f->allowed; k_busy = f->busy; k_recent = f->pending || f->recent; k_book = f->book; k_playing = f->playing; k_fw = f->fw;
    session_set_source_mode(f->src);
    session_set_route(f->route);
    if(f->bt) session_set_bt_mac("AA:BB:CC:DD:EE:FF"); else session_clear_bt_mac();
    if(f->settling) admit_note_player_restart();
}

#define SW  "Switching output - try again"
#define LOC "Return to local playback first"
#define SD  "SD card is not ready"
#define BTA "Bluetooth output is active"
#define STA "Player is starting - try again"
#define BSY "Player is busy - try again"
#define BOK "Stop the audiobook first"
static const facts_t IDLE_LOCAL = { .allowed = 1 };

typedef struct {
    const char *name; facts_t f;
    const char *out_idle, *local, *command, *output_switch, *route_bt, *route_bt_send, *analog, *local_init;
} rule_t;
static void rules(void){
    static const rule_t T[] = {
        { "quiet Local",            IDLE_LOCAL,                                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
        { "launch verdict pending", { .allowed = 0 },                              NULL, LOC,  LOC,  SD,   LOC,  NULL, LOC,  LOC  },
        { "output switch running",  { .allowed = 1, .busy = 1 },                   SW,   NULL, SW,   NULL, SW,   NULL, SW,   SW   },
        { "USB DAC source",         { .allowed = 1, .src = 1 },                    NULL, NULL, NULL, LOC,  LOC,  NULL, NULL, LOC  },
        { "BT Receiving source",    { .allowed = 1, .src = 2 },                    NULL, NULL, NULL, LOC,  LOC,  NULL, NULL, LOC  },
        { "USB Storage source",     { .allowed = 1, .src = 3 },                    NULL, NULL, NULL, LOC,  LOC,  NULL, NULL, LOC  },
        { "Bluetooth sink routed",  { .allowed = 1, .bt = 1 },                     NULL, NULL, NULL, BTA,  NULL, NULL, NULL, NULL },
        { "after a Player restart", { .allowed = 1, .settling = 1 },               NULL, NULL, NULL, STA,  NULL, STA,  NULL, NULL },
        { "a play still starting",  { .allowed = 1, .recent = 1 },                 NULL, NULL, NULL, BSY,  NULL, NULL, NULL, NULL },
        { "a play pending",         { .allowed = 1, .pending = 1 },                NULL, NULL, NULL, BSY,  NULL, NULL, NULL, NULL },
        { "an audiobook",           { .allowed = 1, .book = 1 },                   NULL, NULL, NULL, BOK,  NULL, NULL, NULL, NULL },
        { "playing on SPDIF",       { .allowed = 1, .playing = 1, .route = 1 },    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
        { "card not ready AND busy AND USB DAC",
                                    { .allowed = 0, .busy = 1, .src = 1 },         SW,   LOC,  SW,   SD,   SW,   NULL, SW,   LOC  },
        { "USB DAC AND BT AND restart AND book",
                                    { .allowed = 1, .src = 1, .bt = 1, .settling = 1, .recent = 1, .book = 1 },
                                                                                   NULL, NULL, NULL, LOC,  LOC,  STA,  NULL, LOC  },
        { "BT AND restart AND recent AND book",
                                    { .allowed = 1, .bt = 1, .settling = 1, .recent = 1, .book = 1 },
                                                                                   NULL, NULL, NULL, BTA,  NULL, STA,  NULL, NULL },
        { "restart AND recent AND book",
                                    { .allowed = 1, .settling = 1, .recent = 1, .book = 1 },
                                                                                   NULL, NULL, NULL, STA,  NULL, STA,  NULL, NULL },
        { "recent AND book",        { .allowed = 1, .recent = 1, .book = 1 },      NULL, NULL, NULL, BSY,  NULL, NULL, NULL, NULL },
    };
    for(unsigned i = 0; i < sizeof T / sizeof T[0]; i++){
        const rule_t *r = &T[i];
        apply(&r->f);
        expect_reason(r->name, admit_check(ADMIT_OUTPUT_IDLE),      r->out_idle);
        expect_reason(r->name, admit_check(ADMIT_LOCAL_PLAYBACK),   r->local);
        expect_reason(r->name, admit_check(ADMIT_PLAYER_COMMAND),   r->command);
        expect_reason(r->name, admit_check(ADMIT_OUTPUT_SWITCH),    r->output_switch);
        expect_reason(r->name, admit_check(ADMIT_ROUTE_BT),         r->route_bt);
        expect_reason(r->name, admit_check(ADMIT_ROUTE_BT_SEND),    r->route_bt_send);
        expect_reason(r->name, admit_check(ADMIT_ROUTE_ANALOG),     r->analog);
        expect_reason(r->name, admit_check(ADMIT_LOCAL_INIT),       r->local_init);
    }
    assert(admit_check((admit_what_t)99) != NULL);
}

typedef struct { int ran, saw_playing; long saw_pos; outseq_rc rc; } step_probe_t;
static outseq_rc probe_step(const admit_held_t *held, void *arg){
    step_probe_t *p = arg;
    p->ran++; p->saw_playing = held->was_playing; p->saw_pos = held->st.position_ms;
    return p->rc;
}
static outseq_rc analog_step(const admit_held_t *held, void *arg){ (void)held; (void)arg; return outseq_analog(); }

static void dance(void){
    static const char *const seek5k = "0103001000001388";
    const facts_t quiet = IDLE_LOCAL;
    step_probe_t p;

    apply(&quiet); reset_log(); memset(&k_state, 0, sizeof k_state); p = (step_probe_t){ .rc = OUTSEQ_OK };
    assert(admit_switch(probe_step, &p, NULL) == ADMIT_SWITCH_OK && p.ran == 1 && g_n == 0);

    apply(&(facts_t){ .allowed = 1, .playing = 1 }); reset_log();
    k_state = (track_state_t){ .have_track = 1, .position_ms = 5000 };
    p = (step_probe_t){ .rc = OUTSEQ_OK };
    admit_held_t held;
    assert(admit_switch(probe_step, &p, &held) == ADMIT_SWITCH_OK && p.ran == 1 && p.saw_playing == 1 && p.saw_pos == 5000);
    assert(held.was_playing == 1 && held.st.position_ms == 5000);
    expect_frames("playing, step landed", (const char *const[]){ PP, seek5k, PP }, 3);

    reset_log(); p = (step_probe_t){ 0 };
    assert(admit_switch(analog_step, NULL, NULL) == ADMIT_SWITCH_OK);
    expect_frames("playing, analog route", (const char *const[]){ PP, R6, W8, seek5k, PP }, 5);

    apply(&quiet); reset_log();
    k_state = (track_state_t){ .have_track = 1, .position_ms = 5000 };
    assert(admit_switch(analog_step, NULL, NULL) == ADMIT_SWITCH_OK);
    expect_frames("stopped with a track", (const char *const[]){ R6, W8, seek5k }, 3);

    apply(&(facts_t){ .allowed = 1, .playing = 1 }); reset_log();
    k_state = (track_state_t){ .have_track = 1, .position_ms = 0 };
    assert(admit_switch(analog_step, NULL, NULL) == ADMIT_SWITCH_OK);
    expect_frames("playing at 0", (const char *const[]){ PP, R6, W8, PP }, 4);

    apply(&(facts_t){ .allowed = 1, .playing = 1 }); reset_log(); k_fail_all = 1;
    k_state = (track_state_t){ .have_track = 1, .position_ms = 5000 };
    p = (step_probe_t){ .rc = OUTSEQ_OK };
    assert(admit_switch(probe_step, &p, NULL) == ADMIT_SWITCH_NOT_PAUSED && p.ran == 0 && g_sends == 1 && g_n == 0);

    for(int i = 0; i < 2; i++){
        outseq_rc bad = i ? OUTSEQ_MOVED : OUTSEQ_REFUSED;
        apply(&(facts_t){ .allowed = 1, .playing = 1 }); reset_log();
        p = (step_probe_t){ .rc = bad };
        assert(admit_switch(probe_step, &p, NULL) == ADMIT_SWITCH_STEP_FAILED && p.ran == 1);
        expect_frames("step failed", (const char *const[]){ PP }, 1);
    }
    apply(&(facts_t){ .allowed = 1, .playing = 1 }); reset_log(); k_fail_nth = 3;
    assert(admit_switch(analog_step, NULL, NULL) == ADMIT_SWITCH_STEP_FAILED);
    expect_frames("analog failed part way", (const char *const[]){ PP, R6 }, 2);

    apply(&(facts_t){ .allowed = 1, .playing = 1 }); reset_log();
    k_state = (track_state_t){ .have_track = 1, .position_ms = 5000 };
    p = (step_probe_t){ .rc = OUTSEQ_OK };
    {
        struct admit_env e = test_env;
        e.local_playback_allowed = env_busy;
        admit_set_env(&e);
        assert(admit_switch(probe_step, &p, NULL) == ADMIT_SWITCH_OK);
        expect_frames("seek refused", (const char *const[]){ PP, PP }, 2);
    }
}

static void seek(void){
    apply(&IDLE_LOCAL); reset_log();
    assert(ui_seek_to(5000) == 0);
    expect_frames("seek", (const char *const[]){ "0103001000001388" }, 1);
    reset_log(); assert(ui_seek_to(-7) == 0);
    expect_frames("negative seek clamps to 0", (const char *const[]){ "0103001000000000" }, 1);
    reset_log(); k_fail_all = 1; assert(ui_seek_to(1) == -1);
    apply(&(facts_t){ .allowed = 0 }); reset_log();
    assert(ui_seek_to(5000) == -1 && g_sends == 0);
}

static void settle_window(void){
    admit_set_env(&test_env);
    k_now = 1000;
    assert(!admit_settling());
    admit_note_player_restart();
    k_now = 1000 + 8999; assert(admit_settling());
    k_now = 1000 + 9000; assert(!admit_settling());
    k_now = 1000 + 100;  assert(!admit_settling());
    k_now = UINT32_MAX - 100; admit_note_player_restart();
    k_now = 50;          assert(admit_settling());
    k_now = 8898;        assert(admit_settling());
    k_now = 8899; admit_tick(); assert(!admit_settling());
}

static const char *const W8_ONLY[] = { W8 };
static void play_start_table(void){
    typedef struct { const char *name; facts_t f; } row_t;
    #define LOCAL_FW(rt_, pl_, rc_) { .allowed = 1, .fw = 1, .route = (rt_), .playing = (pl_), .recent = (rc_) }
    static const row_t rows[] = {
        { "internal, stopped",      LOCAL_FW(0, 0, 0) }, { "internal, playing",      LOCAL_FW(0, 1, 0) }, { "internal, play recent",  LOCAL_FW(0, 0, 1) },
        { "SPDIF, stopped",         LOCAL_FW(1, 0, 0) }, { "SPDIF, playing",         LOCAL_FW(1, 1, 0) }, { "SPDIF, play recent",     LOCAL_FW(1, 0, 1) },
        { "USB audio, stopped",     LOCAL_FW(2, 0, 0) }, { "USB audio, playing",     LOCAL_FW(2, 1, 0) }, { "USB audio, play recent", LOCAL_FW(2, 0, 1) },
    };
    for(unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++){
        const row_t *r = &rows[i];
        apply(&r->f); reset_log();
        admit_sent_t got = admit_play_start(ADMIT_PLAY_LIST);
        if(r->f.playing || r->f.recent){ assert(got == ADMIT_SENT_NONE && g_n == 0); }
        else {
            const char *const *fr = PRE_FRAMES[r->f.route];
            assert(got == ADMIT_SENT_PREAMBLE);
            expect_frames(r->name, fr, fr[2] ? 3 : 2);
        }
    }
    apply(&(facts_t){ .allowed = 1, .fw = 1, .bt = 1, .route = 2 }); reset_log();
    assert(admit_play_start(ADMIT_PLAY_LIST) == ADMIT_SENT_NONE && g_n == 0);
    apply(&(facts_t){ .allowed = 1, .fw = 0 }); reset_log();
    assert(admit_play_start(ADMIT_PLAY_LIST) == ADMIT_SENT_NONE && g_n == 0);
    assert(admit_play_start(ADMIT_PLAY_RESERVED_SLOT) == ADMIT_SENT_WORKMODE);
    expect_frames("old firmware, reserved slot", W8_ONLY, 1);
    for(int src = 1; src <= 3; src++){
        apply(&(facts_t){ .allowed = 1, .fw = 1, .src = src }); reset_log();
        assert(admit_play_start(ADMIT_PLAY_LIST) == ADMIT_SENT_NONE);
        assert(admit_play_start(ADMIT_PLAY_RESERVED_SLOT) == ADMIT_SENT_NONE && g_n == 0);
    }
    for(int rt = 0; rt <= 2; rt++) for(int pl = 0; pl <= 1; pl++) for(int rc = 0; rc <= 1; rc++){
        apply(&(facts_t){ .allowed = 1, .fw = 1, .route = rt, .playing = pl, .recent = rc }); reset_log();
        assert(admit_play_start(ADMIT_PLAY_RESERVED_SLOT) == ADMIT_SENT_WORKMODE);
        expect_frames("reserved slot, local", W8_ONLY, 1);
    }
    apply(&(facts_t){ .allowed = 1, .fw = 1, .bt = 1 }); reset_log();
    assert(admit_play_start(ADMIT_PLAY_RESERVED_SLOT) == ADMIT_SENT_NONE && g_n == 0);
    #undef LOCAL_FW
}

static int legacy_preamble(const facts_t *s){ return s->fw && s->src == 0 && !s->bt && !s->playing && !(s->pending || s->recent); }
static int legacy_workmode(const facts_t *s){ return s->src == 0 && !s->bt; }
static const char *legacy_output_blocked(const facts_t *s, int settling, int recent){
    if(!s->allowed) return "SD card is not ready";
    if(s->src != 0) return "Return to local playback first";
    if(s->bt) return "Bluetooth output is active";
    if(settling) return "Player is starting - try again";
    if(recent) return "Player is busy - try again";
    if(s->book) return "Stop the audiobook first";
    return NULL;
}
static int legacy_route_bt_blocked(const facts_t *s){ if(s->busy) return 1; if(!s->allowed) return 1; if(s->src != 0) return 1; return 0; }
static int legacy_analog_blocked(const facts_t *s){ if(s->busy) return 1; if(!s->allowed) return 1; return 0; }
static int legacy_local_init_blocked(const facts_t *s){ if(!s->allowed) return 1; if(s->src != 0) return 1; if(s->busy) return 1; return 0; }
static const char *legacy_command(const facts_t *s){
    if(s->busy) return "Switching output - try again";
    if(!s->allowed) return "Return to local playback first";
    return NULL;
}
static void cross_product(void){
    int n = 0, plays = 0;
    for(int allowed = 0; allowed <= 1; allowed++) for(int busy = 0; busy <= 1; busy++) for(int src = 0; src <= 3; src++)
    for(int bt = 0; bt <= 1; bt++) for(int settling = 0; settling <= 1; settling++) for(int pending = 0; pending <= 1; pending++)
    for(int recent = 0; recent <= 1; recent++) for(int book = 0; book <= 1; book++)
    for(int playing = 0; playing <= 1; playing++) for(int fw = 0; fw <= 1; fw++) for(int route = 0; route <= 2; route++){
        facts_t f = { allowed, busy, src, bt, settling, pending, recent, book, playing, fw, route };
        apply(&f);
        const char *a;
        a = admit_check(ADMIT_OUTPUT_SWITCH);
        expect_reason("output switch vs old ui_output_blocked", a, legacy_output_blocked(&f, settling, pending || recent));
        assert((admit_check(ADMIT_ROUTE_BT) != NULL) == legacy_route_bt_blocked(&f));
        assert((admit_check(ADMIT_ROUTE_BT_SEND) != NULL) == settling);
        assert((admit_check(ADMIT_ROUTE_ANALOG) != NULL) == legacy_analog_blocked(&f));
        assert((admit_check(ADMIT_LOCAL_INIT) != NULL) == legacy_local_init_blocked(&f));
        expect_reason("command vs old toasts", admit_check(ADMIT_PLAYER_COMMAND), legacy_command(&f));
        expect_reason("source switch vs old busy toast", admit_check(ADMIT_OUTPUT_IDLE), busy ? "Switching output - try again" : NULL);
        assert((admit_check(ADMIT_LOCAL_PLAYBACK) != NULL) == !allowed);
        n++;
        {
            facts_t pf = f;
            apply(&pf); reset_log();
            admit_sent_t got = admit_play_start(ADMIT_PLAY_LIST);
            if(legacy_preamble(&pf)){ assert(got == ADMIT_SENT_PREAMBLE); expect_frames("product list", PRE_FRAMES[route], PRE_FRAMES[route][2] ? 3 : 2); }
            else { assert(got == ADMIT_SENT_NONE && g_n == 0); }
            reset_log();
            got = admit_play_start(ADMIT_PLAY_RESERVED_SLOT);
            if(legacy_workmode(&pf)){ assert(got == ADMIT_SENT_WORKMODE); expect_frames("product slot", W8_ONLY, 1); }
            else { assert(got == ADMIT_SENT_NONE && g_n == 0); }
            plays += 2;
        }
    }
    printf("  ok cross product: %d situations, %d play starts equal the old expressions\n", n, plays);
}

static void run(const char *name, void (*fn)(void)){
    fflush(stdout);
    pid_t p = fork();
    assert(p >= 0);
    if(p == 0){ fn(); fflush(stdout); _exit(0); }
    int st = 0; waitpid(p, &st, 0);
    if(!WIFEXITED(st) || WEXITSTATUS(st) != 0){ fprintf(stderr, "admit_test FAILED: %s\n", name); exit(1); }
    printf("  ok %s\n", name);
}
#define RUN(f) run(#f, f)

int main(void){
    RUN(rules);
    RUN(dance);
    RUN(seek);
    RUN(settle_window);
    RUN(play_start_table);
    run("cross_product", cross_product);
    puts("admit_test: ok");
    return 0;
}
