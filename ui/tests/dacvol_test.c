/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "dacvol.h"
#include "ipc.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct { int vol; unsigned seq; } g_p;
void ipc_get_state(track_state_t *out){ memset(out, 0, sizeof *out); out->volume = g_p.vol; out->volume_seq = g_p.seq; }
static struct { char key[24]; int v; } g_cfg[8]; static int g_ncfg;
int cfg_get_int(const char *key, int def){ for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)) return g_cfg[i].v; return def; }
int cfg_set_int(const char *key, int v){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)){ g_cfg[i].v = v; return 0; }
    assert(g_ncfg < 8); snprintf(g_cfg[g_ncfg].key, sizeof g_cfg[0].key, "%s", key); g_cfg[g_ncfg].v = v; g_ncfg++; return 0;
}

static uint32_t g_now;
static int g_ready, g_card, g_probe, g_mix_busy_polls, g_mix_state, g_mix_fail_on_set;
static dacvol_ctl_t g_ctl;
static int g_keys_vol, g_single_vol, g_cap, g_key_active, g_player_rc, g_maxvol_set, g_maxvol_restored;
static int g_keyq[32][2], g_nkey, g_keyi;
static char g_log[32][160]; static int g_nlog;
enum { OP_MIXSET, OP_PLAYER, OP_MAXVOL, OP_RESTORE };
static struct { int op, val; } g_ops[256]; static int g_nops;
static void op(int o, int v){ assert(g_nops < 256); g_ops[g_nops].op = o; g_ops[g_nops].val = v; g_nops++; }

static uint32_t e_now(void){ return g_now; }
static int e_ready(void){ return g_ready; }
static int e_card(void){ return g_card; }
static int e_probe(int card){ (void)card; return 0; }
static int e_probe_poll(dacvol_ctl_t *o){
    if(g_probe == DACVOL_PROBE_PENDING) return DACVOL_PROBE_PENDING;
    if(g_probe == DACVOL_PROBE_OK){ *o = g_ctl; }
    return g_probe;
}
static int e_mix_set(int raw){ op(OP_MIXSET, raw); g_mix_busy_polls = 2; if(g_mix_fail_on_set) g_mix_state = DACVOL_MIX_FAILED; return 0; }
static int e_mix_state(void){
    if(g_mix_state == DACVOL_MIX_FAILED) return DACVOL_MIX_FAILED;
    if(g_mix_busy_polls > 0){ g_mix_busy_polls--; return DACVOL_MIX_BUSY; }
    return DACVOL_MIX_IDLE;
}
static int e_player(int v){ op(OP_PLAYER, v); return g_player_rc; }
static int e_maxvol(int v){ op(OP_MAXVOL, v); g_maxvol_set++; return 0; }
static void e_restore(void){ op(OP_RESTORE, 0); g_maxvol_restored++; }
static void e_key_active(int on){ g_key_active = on; }
static int e_key_poll(int *c, int *v){ if(g_keyi >= g_nkey) return 0; *c = g_keyq[g_keyi][0]; *v = g_keyq[g_keyi][1]; g_keyi++; return 1; }
static int e_keys_vol(void){ return g_keys_vol; }
static int e_single_vol(void){ return g_single_vol; }
static int e_cap(void){ return g_cap; }
static void e_log(const char *m){ if(g_nlog < 32) snprintf(g_log[g_nlog++], sizeof g_log[0], "%s", m); }
static const struct dacvol_env env = {
    .now = e_now, .usb_ready = e_ready, .usb_card = e_card, .mixer_probe = e_probe, .mixer_probe_poll = e_probe_poll,
    .mixer_set = e_mix_set, .mixer_state = e_mix_state, .player_send = e_player, .player_maxvol = e_maxvol,
    .player_maxvol_restore = e_restore, .key_active = e_key_active, .key_poll = e_key_poll,
    .keys_adjust_volume = e_keys_vol, .single_click_volume = e_single_vol, .user_cap = e_cap, .log = e_log,
};

static void reset(void){
    memset(&g_p, 0, sizeof g_p); memset(g_cfg, 0, sizeof g_cfg); g_ncfg = 0;
    g_now = 1000; g_ready = 0; g_card = 1; g_probe = DACVOL_PROBE_OK; g_mix_busy_polls = 0; g_mix_state = DACVOL_MIX_IDLE; g_mix_fail_on_set = 0;
    memset(&g_ctl, 0, sizeof g_ctl);
    g_ctl.numid = 6; g_ctl.channels = 2; g_ctl.min = 0; g_ctl.max = 100; g_ctl.cur = 100; g_ctl.db_min_cdb = -5000; g_ctl.db_max_cdb = 0;
    snprintf(g_ctl.name, sizeof g_ctl.name, "PCM Playback Volume");
    g_keys_vol = 1; g_single_vol = 1; g_cap = VOL_MAX; g_key_active = 0; g_player_rc = 0; g_maxvol_set = g_maxvol_restored = 0;
    g_nkey = g_keyi = 0; g_nlog = 0; g_nops = 0;
    dacvol_init(&env);
}
static void player_report(int v){ g_p.vol = v; g_p.seq++; }
static void run(int ms){ for(int t = 0; t < ms; t += 20){ g_now += 20; dacvol_tick(); } }
static int count_ops(int o){ int n = 0; for(int i = 0; i < g_nops; i++) if(g_ops[i].op == o) n++; return n; }
static int last_op(int o){ for(int i = g_nops - 1; i >= 0; i--) if(g_ops[i].op == o) return g_ops[i].val; return -1; }
static int first_index(int o){ for(int i = 0; i < g_nops; i++) if(g_ops[i].op == o) return i; return -1; }
static int log_has(const char *needle){ for(int i = 0; i < g_nlog; i++) if(strstr(g_log[i], needle)) return 1; return 0; }
static void press(int code, int val){ assert(g_nkey < 32); g_keyq[g_nkey][0] = code; g_keyq[g_nkey][1] = val; g_nkey++; }

static void go_active(int prev_vol, int expect_level){
    g_p.vol = prev_vol; g_p.seq = prev_vol >= 0 ? 1 : 0;
    g_ready = 1;
    run(400);
    assert(dacvol_owns_volume());
    player_report(VOL_MAX);
    run(100);
    assert(last_op(OP_PLAYER) == (expect_level == 0 ? 0 : VOL_MAX));
}

static const char DISC_CONTENTS[] =
"numid=5,iface=MIXER,name='PCM Playback Switch'\n"
"  ; type=BOOLEAN,access=rw------,values=1\n"
"  : values=on\n"
"numid=6,iface=MIXER,name='PCM Playback Volume'\n"
"  ; type=INTEGER,access=rw---R--,values=2,min=0,max=100,step=0\n"
"  : values=100,100\n"
"  | dBminmax-min=-50.00dB,max=0.00dB\n"
"numid=3,iface=MIXER,name='Mic Capture Switch'\n"
"  ; type=BOOLEAN,access=rw------,values=1\n"
"  : values=on\n"
"numid=4,iface=MIXER,name='Mic Capture Volume'\n"
"  ; type=INTEGER,access=rw---R--,values=1,min=0,max=100,step=0\n"
"  : values=100\n"
"  | dBminmax-min=-50.00dB,max=0.00dB\n"
"numid=1,iface=PCM,name='Capture Channel Map'\n"
"  ; type=INTEGER,access=r----R--,values=1,min=0,max=36,step=0\n"
"  : values=0\n";

static void test_parse(void){
    dacvol_ctl_t c;
    assert(dacvol_parse_contents(DISC_CONTENTS, &c) == 0);
    assert(c.numid == 6 && c.channels == 2 && c.min == 0 && c.max == 100 && c.cur == 100);
    assert(c.db_min_cdb == -5000 && c.db_max_cdb == 0 && !strcmp(c.name, "PCM Playback Volume"));

    const char *other =
        "numid=2,iface=MIXER,name='Headset Playback Volume'\n  ; type=INTEGER,access=rw---R--,values=2,min=-128,max=0,step=1\n  : values=-20,-20\n"
        "numid=9,iface=MIXER,name='USBStreamer Output Playback Volume'\n  ; type=INTEGER,access=rw---R--,values=1,min=0,max=255,step=0\n  : values=200\n";
    assert(dacvol_parse_contents(other, &c) == 0 && c.numid == 2 && c.min == -128 && c.cur == -20 && c.db_min_cdb == DACVOL_NO_DB);

    const char *pcm_later =
        "numid=2,iface=MIXER,name='Master Playback Volume'\n  ; type=INTEGER,access=rw---R--,values=2,min=0,max=64,step=1\n  : values=64,64\n"
        "numid=7,iface=MIXER,name='PCM Playback Volume'\n  ; type=INTEGER,access=rw---R--,values=2,min=0,max=100,step=0\n  : values=100,100\n";
    assert(dacvol_parse_contents(pcm_later, &c) == 0 && c.numid == 7);

    const char *readonly = "numid=6,iface=MIXER,name='PCM Playback Volume'\n  ; type=INTEGER,access=r----R--,values=2,min=0,max=100,step=0\n  : values=100,100\n";
    assert(dacvol_parse_contents(readonly, &c) != 0);
    const char *capture_only = "numid=4,iface=MIXER,name='Mic Capture Volume'\n  ; type=INTEGER,access=rw---R--,values=1,min=0,max=100,step=0\n  : values=100\n";
    assert(dacvol_parse_contents(capture_only, &c) != 0);
    const char *flat = "numid=6,iface=MIXER,name='PCM Playback Volume'\n  ; type=INTEGER,access=rw---R--,values=2,min=5,max=5,step=0\n  : values=5,5\n";
    assert(dacvol_parse_contents(flat, &c) != 0);
    assert(dacvol_parse_contents("", &c) != 0);
    assert(dacvol_parse_contents(NULL, &c) != 0);
    puts("ok parse");
}

static void test_map(void){
    dacvol_ctl_t c = { .min = 0, .max = 100 };
    assert(dacvol_map(&c, 0) == 0 && dacvol_map(&c, VOL_MAX) == 100 && dacvol_map(&c, 60) == 50 && dacvol_map(&c, 72) == 60);
    assert(dacvol_map(&c, -5) == 0 && dacvol_map(&c, 999) == 100);
    dacvol_ctl_t n = { .min = -128, .max = 0 };
    assert(dacvol_map(&n, 0) == -128 && dacvol_map(&n, VOL_MAX) == 0 && dacvol_map(&n, 60) == -64);
    dacvol_ctl_t o = { .min = 10, .max = 265 };
    assert(dacvol_map(&o, 0) == 10 && dacvol_map(&o, VOL_MAX) == 265);
    dacvol_ctl_t bad = { .min = 4, .max = 4 };
    assert(dacvol_map(&bad, 50) == 0 && dacvol_map(NULL, 50) == 0);
    int prev = -1;
    for(int l = 0; l <= VOL_MAX; l++){ int r = dacvol_map(&c, l); assert(r >= prev && r >= 0 && r <= 100); prev = r; }
    puts("ok map");
}

static void test_safe_start_and_order(void){
    reset();
    go_active(90, 72);
    assert(count_ops(OP_MIXSET) == 1 && last_op(OP_MIXSET) == 60);
    assert(first_index(OP_MIXSET) < first_index(OP_PLAYER));
    assert(last_op(OP_PLAYER) == VOL_MAX);
    assert(cfg_get_int("dacvol_prev", -9) == 90);
    assert(g_key_active == 1);
    assert(log_has("safe-start cap applied") && log_has("'PCM Playback Volume'"));
    int sends = count_ops(OP_PLAYER);
    run(3000);
    assert(count_ops(OP_PLAYER) == sends && count_ops(OP_MIXSET) == 1);

    reset();
    go_active(40, 40);
    assert(last_op(OP_MIXSET) == 33);
    assert(!log_has("safe-start cap applied"));

    reset();
    cfg_set_int("usb_audio_vol", 100);
    go_active(30, 72);
    assert(last_op(OP_MIXSET) == 60);

    reset();
    cfg_set_int("usb_audio_vol", 100);
    cfg_set_int("dac_safe_start", 100);
    go_active(30, 100);
    assert(last_op(OP_MIXSET) == 83);

    reset();
    cfg_set_int("usb_audio_vol", 50);
    go_active(110, 50);
    assert(last_op(OP_MIXSET) == 42);

    reset();
    go_active(-1, 72);
    assert(last_op(OP_MIXSET) == 60 && cfg_get_int("dacvol_prev", -9) == 72);
    puts("ok safe start and ordering");
}

static void test_unity_bypasses_cap(void){
    reset();
    g_cap = 80;
    go_active(70, 70);
    assert(last_op(OP_PLAYER) == VOL_MAX);
    assert(first_index(OP_MAXVOL) >= 0 && last_op(OP_MAXVOL) == VOL_MAX);
    assert(first_index(OP_MAXVOL) < first_index(OP_PLAYER));
    press(DACVOL_KEY_UP, 1);
    for(int i = 0; i < 20; i++){ press(DACVOL_KEY_UP, 2); }
    run(40);
    int level;
    assert(dacvol_take_change(&level) && level == 80);
    assert(last_op(OP_MIXSET) == dacvol_map(&g_ctl, 80));
    assert(dacvol_set_user(110) == 1);
    assert(last_op(OP_MIXSET) == dacvol_map(&g_ctl, 80));
    puts("ok unity bypasses the cap, level obeys it");
}

static void test_keys(void){
    reset();
    go_active(90, 72);
    int sends = count_ops(OP_PLAYER), mix = count_ops(OP_MIXSET);
    press(DACVOL_KEY_DOWN, 1);
    run(20);
    int level;
    assert(dacvol_take_change(&level) && level == 71);
    assert(count_ops(OP_MIXSET) == mix + 1 && last_op(OP_MIXSET) == 59);
    player_report(119);
    run(500);
    assert(count_ops(OP_MIXSET) == mix + 1);
    assert(count_ops(OP_PLAYER) > sends && last_op(OP_PLAYER) == VOL_MAX);
    player_report(VOL_MAX);
    int after = count_ops(OP_PLAYER);
    run(2000);
    assert(count_ops(OP_PLAYER) == after && count_ops(OP_MIXSET) == mix + 1);
    assert(!dacvol_take_change(&level));
    assert(dacvol_effective_volume(VOL_MAX) == 71);

    press(DACVOL_KEY_UP, 1); press(DACVOL_KEY_UP, 1);
    run(20);
    assert(dacvol_take_change(&level) && level == 73);
    assert(last_op(OP_MIXSET) == 61);
    assert(dacvol_effective_volume(55) == 73);

    int before = count_ops(OP_MIXSET);
    press(DACVOL_KEY_DOWN, 1); press(DACVOL_KEY_DOWN, 2);
    run(20);
    player_report(119); player_report(118);
    run(20);
    player_report(VOL_MAX);
    run(1500);
    assert(count_ops(OP_MIXSET) == before + 2);
    assert(dacvol_take_change(&level) && level == 71);

    press(DACVOL_KEY_DOWN, 0);
    press(265, 1);
    press(265, 1);
    run(20);
    assert(!dacvol_take_change(&level));
    assert(log_has("key code 265 seen"));
    int n265 = 0; for(int i = 0; i < g_nlog; i++) if(strstr(g_log[i], "key code 265")) n265++;
    assert(n265 == 1);
    puts("ok keys");
}

static void test_keys_as_track_buttons(void){
    reset();
    go_active(90, 72);
    g_keys_vol = 0; g_single_vol = 0;
    int mix = count_ops(OP_MIXSET);
    press(DACVOL_KEY_DOWN, 1);
    run(20);
    int level;
    assert(!dacvol_take_change(&level) && count_ops(OP_MIXSET) == mix);
    puts("ok key actions set to switch track are ignored");
}

static void test_single_click_volume_with_double_click_as_track(void){
    reset();
    go_active(90, 72);
    g_keys_vol = 1; g_single_vol = 1;
    int level, mix = count_ops(OP_MIXSET);
    press(DACVOL_KEY_UP, 1);
    run(20);
    assert(dacvol_take_change(&level) && level == 73);
    assert(count_ops(OP_MIXSET) == mix + 1 && last_op(OP_MIXSET) == 61);
    player_report(VOL_MAX);
    run(1000);

    mix = count_ops(OP_MIXSET);
    press(DACVOL_KEY_DBL_UP, 1);
    press(DACVOL_KEY_UP, 1);
    run(20);
    assert(!dacvol_take_change(&level) && count_ops(OP_MIXSET) == mix);
    assert(!log_has("not a volume key"));
    run(800);
    press(DACVOL_KEY_DOWN, 1);
    run(20);
    assert(dacvol_take_change(&level) && level == 72);
    assert(count_ops(OP_MIXSET) == mix + 1);

    mix = count_ops(OP_MIXSET);
    press(DACVOL_KEY_DBL_DOWN, 1);
    run(300);
    press(DACVOL_KEY_DOWN, 1);
    run(20);
    assert(!dacvol_take_change(&level) && count_ops(OP_MIXSET) == mix);
    puts("ok a single click adjusts the DAC while a double click stays a track skip");
}

static void test_single_click_as_track_keeps_player_driven_volume(void){
    reset();
    go_active(90, 72);
    g_keys_vol = 1; g_single_vol = 0;
    int level, mix = count_ops(OP_MIXSET);
    press(DACVOL_KEY_UP, 1);
    run(20);
    assert(!dacvol_take_change(&level) && count_ops(OP_MIXSET) == mix);
    player_report(119);
    run(20);
    assert(dacvol_take_change(&level) && level == 71);
    puts("ok single click set to track ignores the key but a Player volume move still reaches the DAC");
}

static void test_inference_without_key_events(void){
    reset();
    go_active(90, 72);
    run(1000);
    int mix = count_ops(OP_MIXSET), sends = count_ops(OP_PLAYER);
    player_report(119);
    run(20);
    int level;
    assert(dacvol_take_change(&level) && level == 71);
    assert(count_ops(OP_MIXSET) == mix + 1 && last_op(OP_MIXSET) == 59);
    assert(log_has("no key event"));
    run(600);
    assert(count_ops(OP_PLAYER) > sends && last_op(OP_PLAYER) == VOL_MAX);
    player_report(VOL_MAX);
    run(2000);
    assert(count_ops(OP_MIXSET) == mix + 1 && !dacvol_take_change(&level));
    player_report(118);
    player_report(117);
    run(20);
    assert(dacvol_take_change(&level) && level == 68);
    puts("ok drops without a key event are applied to the DAC");
}

static void test_echo_of_our_own_assert_is_ignored(void){
    reset();
    g_p.vol = 90; g_p.seq = 1; g_ready = 1;
    run(100);
    assert(dacvol_owns_volume());
    int mix = count_ops(OP_MIXSET);
    player_report(80);
    run(20);
    player_report(100);
    run(20);
    player_report(VOL_MAX);
    run(2000);
    assert(count_ops(OP_MIXSET) == mix);
    int level;
    assert(!dacvol_take_change(&level));
    puts("ok ramped echoes of the unity assert do not move the DAC");
}

static void test_zero_level_mutes_player(void){
    reset();
    cfg_set_int("usb_audio_vol", 2);
    go_active(90, 2);
    press(DACVOL_KEY_DOWN, 1); press(DACVOL_KEY_DOWN, 1); press(DACVOL_KEY_DOWN, 1);
    run(20);
    int level;
    assert(dacvol_take_change(&level) && level == 0);
    assert(last_op(OP_MIXSET) == 0);
    run(20);
    assert(last_op(OP_PLAYER) == 0);
    player_report(0);
    run(1000);
    press(DACVOL_KEY_UP, 1);
    run(20);
    assert(dacvol_take_change(&level) && level == 1);
    assert(last_op(OP_MIXSET) == 1 && last_op(OP_PLAYER) == VOL_MAX);
    puts("ok level 0 silences the Player, level 1 restores unity");
}

static void test_player_refuses_unity(void){
    reset();
    g_p.vol = 90; g_p.seq = 1; g_ready = 1;
    run(400);
    player_report(100);
    run(3000);
    assert(count_ops(OP_MAXVOL) == 1);
    assert(log_has("not bit-perfect"));
    int sends = count_ops(OP_PLAYER);
    run(5000);
    assert(count_ops(OP_PLAYER) == sends);
    puts("ok a Player that refuses unity is accepted after bounded retries");
}

static void test_leave_restores(void){
    reset();
    g_cap = 90;
    go_active(85, 72);
    press(DACVOL_KEY_DOWN, 1);
    run(20);
    g_ready = 0;
    int sends = count_ops(OP_PLAYER);
    run(60);
    assert(count_ops(OP_PLAYER) == sends + 1 && last_op(OP_PLAYER) == 85);
    assert(cfg_get_int("dacvol_prev", -9) == -1);
    assert(cfg_get_int("usb_audio_vol", -9) == 71);
    assert(last_op(OP_MIXSET) == 100);
    assert(g_maxvol_restored == 1);
    assert(g_key_active == 0);
    assert(!dacvol_owns_volume());
    assert(dacvol_set_user(50) == 0);
    int ops = g_nops;
    run(3000);
    assert(g_nops == ops);
    g_ready = 1;
    run(400);
    assert(dacvol_owns_volume());
    puts("ok leaving restores the Player volume and the DAC");
}

static void test_release_now_before_a_synchronous_route_change(void){
    reset();
    go_active(85, 72);
    assert(dacvol_release_now(VOL_MAX) == 85);
    assert(last_op(OP_PLAYER) == 85 && cfg_get_int("dacvol_prev", -9) == -1 && g_maxvol_restored == 1);
    assert(!dacvol_owns_volume());
    g_ready = 0;
    run(100);
    assert(dacvol_release_now(55) == 55);

    reset();
    assert(dacvol_release_now(77) == 77 && g_nops == 0);

    reset();
    go_active(85, 72);
    g_player_rc = -1;
    assert(dacvol_release_now(VOL_MAX) == 85);
    g_ready = 0;
    g_player_rc = 0;
    run(600);
    assert(last_op(OP_PLAYER) == 85 && cfg_get_int("dacvol_prev", -9) == -1);
    puts("ok release_now hands back the previous volume at once and finishes in the background");
}

static void test_leave_when_card_is_gone(void){
    reset();
    go_active(85, 72);
    g_card = -1; g_ready = 0;
    int mix = count_ops(OP_MIXSET);
    run(100);
    assert(last_op(OP_PLAYER) == 85);
    assert(count_ops(OP_MIXSET) == mix);
    puts("ok an unplugged DAC is not written to on leave");
}

static void test_leave_retry_and_giveup(void){
    reset();
    go_active(85, 72);
    g_ready = 0; g_player_rc = -1;
    run(2200);
    assert(cfg_get_int("dacvol_prev", -9) == 85);
    int tries = 0; for(int i = 0; i < g_nops; i++) if(g_ops[i].op == OP_PLAYER && g_ops[i].val == 85) tries++;
    assert(tries >= 3 && tries <= 6);
    g_player_rc = 0;
    run(600);
    assert(cfg_get_int("dacvol_prev", -9) == -1 && last_op(OP_PLAYER) == 85);

    reset();
    go_active(85, 72);
    g_ready = 0; g_player_rc = -1;
    run(16000);
    assert(log_has("not restored"));
    assert(cfg_get_int("dacvol_prev", -9) == 85);
    g_player_rc = 0;
    run(1200);
    assert(cfg_get_int("dacvol_prev", -9) == -1 && last_op(OP_PLAYER) == 85 && log_has("repaired"));
    puts("ok leave retries, then the idle repair finishes the job");
}

static void test_recover_after_unclean_exit(void){
    reset();
    cfg_set_int("dacvol_prev", 64);
    g_player_rc = -1;
    run(100);
    int tries = count_ops(OP_PLAYER);
    assert(tries >= 1 && cfg_get_int("dacvol_prev", -9) == 64);
    run(3000);
    assert(count_ops(OP_PLAYER) >= tries + 2 && count_ops(OP_PLAYER) <= tries + 4);
    g_player_rc = 0;
    run(1100);
    assert(last_op(OP_PLAYER) == 64 && cfg_get_int("dacvol_prev", -9) == -1);
    assert(log_has("repaired"));
    int n = count_ops(OP_PLAYER);
    run(3000);
    assert(count_ops(OP_PLAYER) == n);

    reset();
    cfg_set_int("dacvol_prev", 64);
    g_p.vol = VOL_MAX; g_p.seq = 5; g_ready = 1;
    run(400);
    assert(dacvol_owns_volume());
    assert(cfg_get_int("dacvol_prev", -9) == 64);
    assert(log_has("prev 64"));
    puts("ok an unclean exit is repaired, a restart on USB keeps the original previous volume");
}

static void test_fallbacks(void){
    reset();
    g_probe = DACVOL_PROBE_NOCTL;
    g_p.vol = 90; g_p.seq = 1; g_ready = 1;
    run(1000);
    assert(!dacvol_owns_volume() && count_ops(OP_PLAYER) == 0 && count_ops(OP_MIXSET) == 0);
    assert(cfg_get_int("dacvol_prev", -9) == -9);
    assert(log_has("no usable playback volume control"));
    assert(dacvol_set_user(50) == 0);
    int n = g_nlog;
    run(5000);
    assert(g_nlog == n);
    assert(g_key_active == 0);

    reset();
    g_probe = DACVOL_PROBE_ERROR;
    g_ready = 1;
    run(1000);
    assert(!dacvol_owns_volume() && count_ops(OP_PLAYER) == 0 && log_has("amixer failed"));

    reset();
    g_probe = DACVOL_PROBE_PENDING;
    g_ready = 1;
    run(3900);
    assert(g_nlog == 0);
    run(300);
    assert(log_has("probe timed out") && count_ops(OP_PLAYER) == 0 && !dacvol_owns_volume());

    reset();
    g_ctl.max = g_ctl.min;
    g_ready = 1;
    run(300);
    assert(log_has("unusable") && count_ops(OP_PLAYER) == 0);

    reset();
    g_card = -1;
    g_ready = 1;
    run(300);
    assert(log_has("fallback") && count_ops(OP_PLAYER) == 0);

    reset();
    g_mix_fail_on_set = 1;
    g_p.vol = 90; g_p.seq = 1; g_ready = 1;
    run(1000);
    assert(log_has("mixer write failed") && count_ops(OP_PLAYER) == 0 && !dacvol_owns_volume());
    assert(cfg_get_int("dacvol_prev", -9) == -1);
    assert(g_maxvol_restored == 1);
    g_ready = 0;
    run(100);
    g_mix_fail_on_set = 0; g_mix_state = DACVOL_MIX_IDLE;
    g_ready = 1;
    run(400);
    assert(dacvol_owns_volume());

    reset();
    cfg_set_int("usb_hw_volume", 0);
    g_ready = 1;
    run(1000);
    assert(g_nops == 0 && g_nlog == 0 && !dacvol_owns_volume());
    puts("ok every failure falls back to the Player volume and logs once");
}

static void test_mixer_failure_while_active(void){
    reset();
    go_active(85, 72);
    g_mix_state = DACVOL_MIX_FAILED;
    run(60);
    assert(last_op(OP_PLAYER) == 72);
    assert(log_has("falling back to the Player volume 72"));
    assert(!dacvol_owns_volume());
    assert(cfg_get_int("dacvol_prev", -9) == -1);
    run(2000);
    assert(!dacvol_owns_volume());
    g_mix_state = DACVOL_MIX_IDLE;
    g_ready = 0;
    run(100);
    g_ready = 1;
    run(400);
    assert(dacvol_owns_volume());
    puts("ok a failing mixer hands the volume back to the Player at the same level");
}

static void test_user_set_during_entry(void){
    reset();
    g_p.vol = 90; g_p.seq = 1; g_ready = 1;
    g_probe = DACVOL_PROBE_PENDING;
    run(40);
    assert(dacvol_set_user(30) == 0);
    g_probe = DACVOL_PROBE_OK;
    run(20);
    assert(dacvol_set_user(33) == 1);
    run(200);
    assert(last_op(OP_MIXSET) == dacvol_map(&g_ctl, 33));
    player_report(VOL_MAX);
    run(100);
    assert(dacvol_set_user(50) == 1 && last_op(OP_MIXSET) == 42);
    assert(dacvol_set_user(500) == 1 && last_op(OP_MIXSET) == 100);
    puts("ok user volume during entry and while active");
}

static void test_persist_debounce(void){
    reset();
    go_active(90, 72);
    assert(cfg_get_int("usb_audio_vol", -9) == -9);
    dacvol_set_user(40); dacvol_set_user(45);
    run(1000);
    assert(cfg_get_int("usb_audio_vol", -9) == -9);
    run(1000);
    assert(cfg_get_int("usb_audio_vol", -9) == 45);
    puts("ok the level is persisted after it settles");
}

static void test_card_loss_mid_entry(void){
    reset();
    g_p.vol = 90; g_p.seq = 1; g_ready = 1;
    g_mix_busy_polls = 0;
    g_probe = DACVOL_PROBE_PENDING;
    run(100);
    g_ready = 0;
    run(100);
    assert(count_ops(OP_PLAYER) == 0 && !dacvol_owns_volume());
    g_probe = DACVOL_PROBE_OK; g_ready = 1;
    run(60);
    g_ready = 0;
    run(300);
    assert(cfg_get_int("dacvol_prev", -9) == -1);
    puts("ok losing the DAC mid-entry leaves the Player alone");
}

int main(void){
    test_parse();
    test_map();
    test_safe_start_and_order();
    test_unity_bypasses_cap();
    test_keys();
    test_keys_as_track_buttons();
    test_single_click_volume_with_double_click_as_track();
    test_single_click_as_track_keeps_player_driven_volume();
    test_inference_without_key_events();
    test_echo_of_our_own_assert_is_ignored();
    test_zero_level_mutes_player();
    test_player_refuses_unity();
    test_leave_restores();
    test_release_now_before_a_synchronous_route_change();
    test_leave_when_card_is_gone();
    test_leave_retry_and_giveup();
    test_recover_after_unclean_exit();
    test_fallbacks();
    test_mixer_failure_while_active();
    test_user_set_during_entry();
    test_persist_debounce();
    test_card_loss_mid_entry();
    puts("PASS (0 failures)");
    return 0;
}
