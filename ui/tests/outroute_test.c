/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "outroute.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#if !defined(OUT_USB_CARD) || !defined(OUT_USB_ANY_CARD) || !defined(ROUTE_STATE_DIR) || !defined(ROUTE_STATE_PATH) || !defined(ROUTE_PROC_ROOT) || !defined(ROUTE_TEST_ROOT)
#error "the Makefile points the card, route state and proc paths at a temp dir"
#endif

#define MAXF 64
static char g_log[MAXF][20];
static int g_n, g_fail_at = -1, g_fail_from = -1, g_playing;
static unsigned g_gen = 1;
#define PAUSE PLAYER_CMD_PLAYPAUSE
int ipc_send_cmd(const char *frame){
    assert(g_n < MAXF);
    snprintf(g_log[g_n], sizeof g_log[0], "%s", frame);
    int idx = g_n++;
    if(idx == g_fail_at || (g_fail_from >= 0 && idx >= g_fail_from)) return -1;
    if(!strcmp(frame, PAUSE)) g_playing = !g_playing;
    return 0;
}
unsigned ipc_generation(void){ return g_gen; }
int ipc_is_ready(void){ return 1; }
static struct { int have_track; long pos; int volume; unsigned volume_seq; char path[64]; } g_trk;
void ipc_get_state(track_state_t *out){
    memset(out, 0, sizeof *out);
    out->have_track = g_trk.have_track; out->position_ms = g_trk.pos; out->volume = g_trk.volume; out->volume_seq = g_trk.volume_seq;
    snprintf(out->path, sizeof out->path, "%s", g_trk.have_track ? g_trk.path : "");
}
static struct { char key[24]; int v; } g_cfg[8]; static int g_ncfg;
int cfg_get_int(const char *key, int def){ for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)) return g_cfg[i].v; return def; }
int cfg_set_int(const char *key, int v){
    for(int i = 0; i < g_ncfg; i++) if(!strcmp(g_cfg[i].key, key)){ g_cfg[i].v = v; return 0; }
    assert(g_ncfg < 8); snprintf(g_cfg[g_ncfg].key, sizeof g_cfg[0].key, "%s", key); g_cfg[g_ncfg++].v = v; return 0;
}

static uint32_t g_now;
static const char *g_blocked;
static int g_seek_rc, g_nseek; static long g_seek_ms;
static char g_toast[32][96]; static int g_ntoast;
static int g_nchanged;
static uint32_t env_now(void){ return g_now; }
static int env_playing(void){ return g_playing; }
static const char *env_blocked(void){ return g_blocked; }
static void env_toast(const char *s){ assert(g_ntoast < 32); snprintf(g_toast[g_ntoast++], sizeof g_toast[0], "%s", s); }
static int env_seek(long ms){ g_nseek++; g_seek_ms = ms; return g_seek_rc; }
static void on_changed(void){ g_nchanged++; }
static const struct outroute_env env = { .now = env_now, .playing = env_playing, .switch_blocked = env_blocked, .toast = env_toast, .seek_to = env_seek };

#define R6 "0666000C0006"
#define R4 "0666000C0004"
#define R3 "0666000C0003"
#define RBT "0666000C0002"
#define G0 "0642000C0000"
#define G5 "0642000C0005"
#define W8 "0657000C0008"
#define BTINIT "06c1000C0000"
#define CODEC "0818000C0001"
#define MAC "AA:BB:CC:DD:EE:FF"

static void sh(const char *fmt, const char *a){ char c[600]; snprintf(c, sizeof c, fmt, a); assert(system(c) == 0); }
static void sh2(const char *fmt, const char *a, const char *b){ char c[900]; snprintf(c, sizeof c, fmt, a, b); assert(system(c) == 0); }
static void fake_player(long pid, unsigned long long start){
    char d[300], f[320];
    snprintf(d, sizeof d, "%s/%ld", ROUTE_PROC_ROOT, pid); mkdir(d, 0755);
    snprintf(f, sizeof f, "%s/stat", d);
    FILE *fp = fopen(f, "w"); assert(fp);
    fprintf(fp, "%ld (mq_player) S", pid);
    for(int i = 0; i < 18; i++) fprintf(fp, " 0");
    fprintf(fp, " %llu 0 0\n", start);
    fclose(fp);
}
static void fake_uptime(double secs){
    char f[300]; snprintf(f, sizeof f, "%s/uptime", ROUTE_PROC_ROOT);
    FILE *fp = fopen(f, "w"); assert(fp); fprintf(fp, "%.2f 0.00\n", secs); fclose(fp);
}
static void usb_card(int present){
    char p[160]; snprintf(p, sizeof p, "%s/usbid", OUT_USB_CARD);
    if(present){ int fd = open(p, O_WRONLY | O_CREAT, 0600); assert(fd >= 0); close(fd); } else unlink(p);
}
static void setup(void){
    sh("rm -rf '%s'", ROUTE_TEST_ROOT);
    sh2("mkdir -p '%s' '%s'", OUT_USB_CARD, ROUTE_PROC_ROOT);
    usb_card(0);
    g_now = 1000; g_n = 0; g_fail_at = g_fail_from = -1; g_playing = 0; g_blocked = NULL; g_seek_rc = 0; g_nseek = 0; g_ntoast = 0; g_nchanged = 0;
    g_trk.have_track = 1; g_trk.pos = 0; g_trk.volume = 40; g_trk.volume_seq = 0; snprintf(g_trk.path, sizeof g_trk.path, "/music/a.flac");
    g_ncfg = 0;
    outroute_init(&env);
    outroute_set_listener(on_changed);
}
static void reset_log(void){ g_n = 0; }
static void run(int ms){ for(int t = 0; t < ms; t += 100){ g_now += 100; outroute_tick(); } }
static int idx(const char *frame, int from){ for(int i = from; i < g_n; i++) if(!strcmp(g_log[i], frame)) return i; return -1; }
static int toasted(const char *s){ for(int i = 0; i < g_ntoast; i++) if(!strcmp(g_toast[i], s)) return 1; return 0; }
static void expect_frames(const char *what, const char *const *want, int n){
    if(g_n != n){ fprintf(stderr, "%s: %d frames sent, want %d:", what, g_n, n); for(int i = 0; i < g_n; i++) fprintf(stderr, " %s", g_log[i]); fprintf(stderr, "\n"); assert(0); }
    for(int i = 0; i < n; i++)
        if(strcmp(g_log[i], want[i])){ fprintf(stderr, "%s: frame %d is %s, want %s\n", what, i, g_log[i], want[i]); assert(0); }
}
static void read_route_file(char *buf, size_t cap){
    FILE *f = fopen(ROUTE_STATE_PATH, "r"); if(!f){ buf[0] = 0; return; }
    size_t n = fread(buf, 1, cap - 1, f); buf[n] = 0; fclose(f);
}
static int route_file_exists(void){ return access(ROUTE_STATE_PATH, F_OK) == 0; }
static void put_route_file(const char *content, mode_t mode){
    mkdir(ROUTE_STATE_DIR, 0700); chmod(ROUTE_STATE_DIR, 0700);
    int fd = open(ROUTE_STATE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600); assert(fd >= 0);
    assert(write(fd, content, strlen(content)) == (ssize_t)strlen(content));
    fchmod(fd, mode); close(fd);
}

static void route_state_and_spdif_mirror(void){
    setup();
    assert(outroute_route() == OUT_INTERNAL && !outroute_spdif_chosen());
    outroute_set(OUT_SPDIF);
    assert(outroute_route() == OUT_SPDIF && cfg_get_int("spdif", 0) == 1 && outroute_spdif_chosen());
    outroute_set(OUT_USB);
    assert(outroute_route() == OUT_USB && cfg_get_int("spdif", 1) == 0);
    outroute_reset();
    assert(outroute_route() == OUT_INTERNAL);
    outroute_set(OUT_SPDIF); outroute_clear_spdif_choice();
    assert(cfg_get_int("spdif", 1) == 0 && outroute_route() == OUT_SPDIF);
}
static void restart_invalidates_the_route(void){
    setup();
    outroute_set(OUT_USB);
    g_gen++;
    assert(outroute_route() == OUT_INTERNAL);
    outroute_set(OUT_SPDIF); assert(cfg_get_int("spdif", 0) == 1);
    g_gen++; assert(outroute_route() == OUT_INTERNAL && cfg_get_int("spdif", 1) == 0);
    outroute_set(OUT_INTERNAL); g_gen++;
    assert(outroute_route() == OUT_INTERNAL);
}
static void bt_sink_is_a_fact(void){
    setup();
    assert(!outroute_bt_routed() && !strcmp(outroute_bt_mac(), ""));
    outroute_set_bt(MAC);
    assert(outroute_bt_routed() && !strcmp(outroute_bt_mac(), MAC));
    outroute_set_bt("11:22:33:44:55:66");
    assert(!strcmp(outroute_bt_mac(), "11:22:33:44:55:66"));
    outroute_set(OUT_SPDIF);
    assert(outroute_bt_routed());
    outroute_set_bt(NULL);
    assert(!outroute_bt_routed() && !strcmp(outroute_bt_mac(), ""));
    outroute_set_bt("AA:BB:CC:DD:EE:FF:00:11:22:33");
    assert(strlen(outroute_bt_mac()) == 19);
}
static void saved_usb_audio_preference(void){
    setup();
    assert(!outroute_saved_usb_audio());
    outroute_save_usb_audio(1);
    assert(outroute_saved_usb_audio() && cfg_get_int("usb_audio_on", 0) == 1);
    outroute_save_usb_audio(0);
    assert(!outroute_saved_usb_audio() && cfg_get_int("usb_audio_on", 1) == 0);
}
static void mac_normalize(void){
    char out[OUTROUTE_MAC_LEN];
    assert(outroute_mac_normalize("aa:bb:cc:dd:ee:ff", out) == 0 && !strcmp(out, MAC));
    assert(outroute_mac_normalize(MAC, out) == 0 && !strcmp(out, MAC));
    assert(outroute_mac_normalize("00:11:22:33:44:5f", out) == 0 && !strcmp(out, "00:11:22:33:44:5F"));
    assert(outroute_mac_normalize(NULL, out) < 0);
    assert(outroute_mac_normalize("", out) < 0);
    assert(outroute_mac_normalize("AA:BB:CC:DD:EE", out) < 0);
    assert(outroute_mac_normalize("AA:BB:CC:DD:EE:FF:00", out) < 0);
    assert(outroute_mac_normalize("AA-BB-CC-DD-EE-FF", out) < 0);
    assert(outroute_mac_normalize("AA:BB:CC:DD:EE:GG", out) < 0);
    assert(outroute_mac_normalize("AAA:B:CC:DD:EE:FF", out) < 0);
}

static void route_file_roundtrip(void){
    setup(); fake_player(4242, 777);
    outroute_set(OUT_USB);
    g_playing = 0;
    assert(outroute_to_bt(MAC, CODEC, NULL) == OUTROUTE_OK);
    assert(outroute_bt_routed() && !strcmp(outroute_bt_mac(), MAC) && outroute_route() == OUT_INTERNAL);
    char buf[200]; read_route_file(buf, sizeof buf);
    assert(!strcmp(buf, "pid=4242 start=777 gen=1 mac=" MAC "\n"));
    struct stat st; assert(stat(ROUTE_STATE_PATH, &st) == 0 && (st.st_mode & 07777) == 0600);
    assert(stat(ROUTE_STATE_DIR, &st) == 0 && (st.st_mode & 07777) == 0700);
    outroute_init(&env);
    assert(!outroute_bt_routed());
    outroute_restore();
    assert(outroute_bt_routed() && !strcmp(outroute_bt_mac(), MAC) && route_file_exists());
}
static void route_file_survives_a_ui_restart_not_a_player_restart(void){
    setup(); fake_player(4242, 777);
    assert(outroute_to_bt(MAC, CODEC, NULL) == OUTROUTE_OK);
    outroute_init(&env);
    fake_player(4242, 778);
    outroute_restore();
    assert(!outroute_bt_routed() && !route_file_exists());
}
static void route_file_forgotten(void){
    setup(); fake_player(4242, 777);
    assert(outroute_to_bt(MAC, CODEC, NULL) == OUTROUTE_OK && route_file_exists());
    outroute_bt_forget();
    assert(!outroute_bt_routed() && !route_file_exists());
    outroute_bt_forget();
}
static void route_file_rejections(void){
    typedef struct { const char *name, *content; mode_t mode; } row_t;
    static const row_t T[] = {
        { "wrong file mode",        "pid=4242 start=777 gen=1 mac=" MAC "\n", 0644 },
        { "wrong pid",              "pid=4243 start=777 gen=1 mac=" MAC "\n", 0600 },
        { "wrong start",            "pid=4242 start=1 gen=1 mac=" MAC "\n", 0600 },
        { "zero start",             "pid=4242 start=0 gen=1 mac=" MAC "\n", 0600 },
        { "extra field",            "pid=4242 start=777 gen=1 mac=" MAC " x\n", 0600 },
        { "no newline",             "pid=4242 start=777 gen=1 mac=" MAC, 0600 },
        { "non-canonical spacing",  "pid=4242  start=777 gen=1 mac=" MAC "\n", 0600 },
        { "garbage",                "hello\n", 0600 },
        { "short mac",              "pid=4242 start=777 gen=1 mac=AA:BB:CC:DD:EE\n", 0600 },
        { "non-hex mac",            "pid=4242 start=777 gen=1 mac=AA:BB:CC:DD:EE:GG\n", 0600 },
        { "bad separators",         "pid=4242 start=777 gen=1 mac=AA-BB-CC-DD-EE-FF\n", 0600 },
    };
    for(unsigned i = 0; i < sizeof T / sizeof T[0]; i++){
        setup(); fake_player(4242, 777);
        put_route_file(T[i].content, T[i].mode);
        outroute_restore();
        if(outroute_bt_routed() || route_file_exists()){ fprintf(stderr, "route file case '%s' was accepted or kept\n", T[i].name); assert(0); }
    }
    setup(); fake_player(4242, 777);
    put_route_file("pid=4242 start=777 gen=9 mac=aa:bb:cc:dd:ee:ff\n", 0600);
    outroute_restore();
    assert(outroute_bt_routed() && !strcmp(outroute_bt_mac(), MAC));
    setup(); fake_player(4242, 777);
    put_route_file("pid=4242 start=777 gen=1 mac=" MAC "\n", 0600);
    chmod(ROUTE_STATE_DIR, 0755);
    outroute_restore();
    assert(!outroute_bt_routed());
    setup(); fake_player(4242, 777);
    outroute_restore();
    assert(!outroute_bt_routed());
    setup(); fake_player(4242, 777); fake_player(4243, 5);
    put_route_file("pid=4242 start=777 gen=1 mac=" MAC "\n", 0600);
    outroute_restore();
    assert(!outroute_bt_routed() && !route_file_exists());
    setup(); fake_player(4242, 777);
    put_route_file("pid=4242 start=777 gen=1 mac=" MAC "\n", 0600);
    {
        char target[300]; snprintf(target, sizeof target, "%s/target", ROUTE_STATE_DIR);
        assert(rename(ROUTE_STATE_PATH, target) == 0);
        assert(symlink(target, ROUTE_STATE_PATH) == 0);
        outroute_restore();
        assert(!outroute_bt_routed() && !route_file_exists() && access(target, F_OK) == 0);
    }
}
static void route_file_not_written_without_one_player(void){
    setup();
    assert(outroute_to_bt(MAC, CODEC, NULL) == OUTROUTE_OK);
    assert(outroute_bt_routed() && !route_file_exists());
    setup(); fake_player(1, 5); fake_player(2, 6);
    assert(outroute_to_bt(MAC, CODEC, NULL) == OUTROUTE_OK);
    assert(!route_file_exists());
    setup(); fake_player(4242, 777);
    put_route_file("stale\n", 0600);
    sh("rm -rf '%s'", ROUTE_PROC_ROOT);
    assert(outroute_to_bt(MAC, CODEC, NULL) == OUTROUTE_OK);
    assert(!route_file_exists());
}
static void analog_removes_the_file_and_the_sink(void){
    setup(); fake_player(4242, 777);
    assert(outroute_to_bt(MAC, CODEC, NULL) == OUTROUTE_OK && route_file_exists());
    reset_log();
    outroute_held_t held;
    assert(outroute_to_analog(0, &held) == 1);
    const char *want[] = { R6, W8 };
    expect_frames("analog", want, 2);
    assert(!outroute_bt_routed() && !route_file_exists());
}
static void analog_without_a_sink_is_a_cleanup_unless_forced(void){
    setup(); fake_player(4242, 777);
    put_route_file("pid=4242 start=777 gen=1 mac=" MAC "\n", 0600);
    outroute_held_t held;
    assert(outroute_to_analog(0, &held) == 0 && g_n == 0 && !route_file_exists());
    assert(outroute_to_analog(1, &held) == 1);
    const char *want[] = { R6, W8 };
    expect_frames("forced analog", want, 2);
}
static void player_age(void){
    setup();
    long hz = sysconf(_SC_CLK_TCK);
    assert(outroute_player_age_s() < 0);
    fake_player(4242, (unsigned long long)(100 * hz));
    assert(outroute_player_age_s() < 0);
    fake_uptime(130.0);
    double age = outroute_player_age_s();
    assert(age > 29.9 && age < 30.1);
    fake_player(4243, 5);
    assert(outroute_player_age_s() < 0);
}

static void leave_paused_frames(void){
    setup();
    outroute_held_t held;
    assert(outroute_to_analog(1, &held) == 1 && g_n == 2 && g_nseek == 0);
    const char *quiet[] = { R6, W8 };
    expect_frames("stopped, no position", quiet, 2);
    assert(!held.was_playing);

    setup(); g_playing = 1; g_trk.pos = 5000;
    assert(outroute_to_analog(1, &held) == 1);
    const char *playing[] = { PAUSE, R6, W8, PAUSE };
    expect_frames("playing with a track", playing, 4);
    assert(g_nseek == 1 && g_seek_ms == 5000 && held.was_playing && held.st.position_ms == 5000);

    setup(); g_trk.pos = 5000;
    assert(outroute_to_analog(1, &held) == 1);
    expect_frames("stopped with a track", quiet, 2);
    assert(g_nseek == 1 && g_seek_ms == 5000);

    setup(); g_playing = 1; g_trk.pos = 0;
    assert(outroute_to_analog(1, &held) == 1);
    expect_frames("playing at 0", playing, 4);
    assert(g_nseek == 0);

    setup(); g_playing = 1; g_trk.pos = 5000; g_seek_rc = -1;
    assert(outroute_to_analog(1, &held) == 1);
    expect_frames("seek refused", playing, 4);
    assert(g_nseek == 1);

    setup(); g_trk.have_track = 0; g_trk.pos = 5000;
    assert(outroute_to_analog(1, &held) == 1 && g_nseek == 0);
}
static void leave_paused_not_paused(void){
    setup(); g_playing = 1; g_trk.pos = 5000;
    g_fail_from = 0;
    outroute_held_t held;
    assert(outroute_to_analog(1, &held) == OUTROUTE_NOT_PAUSED);
    assert(g_n == 1 && !strcmp(g_log[0], PAUSE) && g_nseek == 0 && held.was_playing);
}
static void leave_paused_policy_on_step_failure(void){
    outroute_held_t held;
    setup(); g_playing = 1; g_trk.pos = 5000; g_fail_at = 2;
    assert(outroute_to_analog(1, &held) == OUTROUTE_STEP_FAILED);
    const char *part[] = { PAUSE, R6, W8 };
    expect_frames("analog failed part way", part, 3);
    assert(!g_playing && g_nseek == 0 && g_ntoast == 0);

    setup(); g_playing = 1; g_trk.pos = 5000; g_fail_at = 1;
    assert(outroute_to_analog(1, &held) == OUTROUTE_STEP_FAILED);
    assert(g_n == 2 && !g_playing && g_nseek == 0 && g_ntoast == 0);

    setup(); outroute_set_bt(MAC); g_playing = 1; g_fail_at = 2;
    assert(outroute_to_analog(1, &held) == OUTROUTE_STEP_FAILED);
    assert(outroute_bt_routed() && !g_playing);
}
static void bt_sink_sequence(void){
    outroute_held_t held;
    setup(); outroute_set(OUT_USB); g_playing = 1; g_trk.pos = 5000; g_trk.volume = 33; g_trk.volume_seq = 4;
    assert(outroute_to_bt(MAC, CODEC, &held) == OUTROUTE_OK);
    const char *want[] = { PAUSE, R6, G0, W8, BTINIT, RBT, W8, CODEC, "0715000C0021", PAUSE };
    expect_frames("bt sink", want, 10);
    assert(g_nseek == 1 && g_seek_ms == 5000 && held.was_playing);
    assert(outroute_route() == OUT_INTERNAL && !strcmp(outroute_bt_mac(), MAC));

    setup(); g_trk.volume_seq = 0;
    assert(outroute_to_bt(MAC, CODEC, NULL) == OUTROUTE_OK && idx("0715000C0028", 0) < 0 && g_n == 7);
}
static void bt_sink_failures(void){
    outroute_held_t held;
    setup(); outroute_set(OUT_USB); g_playing = 1; g_fail_at = 1;
    assert(outroute_to_bt(MAC, CODEC, &held) == OUTROUTE_STEP_FAILED);
    assert(outroute_route() == OUT_USB && !outroute_bt_routed() && g_n == 2 && !g_playing && !route_file_exists());

    setup(); outroute_set(OUT_USB); g_playing = 1; fake_player(4242, 777); g_fail_at = 7;
    assert(outroute_to_bt(MAC, CODEC, &held) == OUTROUTE_STEP_FAILED);
    assert(g_n == 8 && !strcmp(g_log[7], CODEC) && outroute_route() == OUT_INTERNAL && !outroute_bt_routed() && !g_playing && !route_file_exists());

    setup(); outroute_set(OUT_USB); g_playing = 1; g_fail_from = 0;
    assert(outroute_to_bt(MAC, CODEC, &held) == OUTROUTE_NOT_PAUSED && outroute_route() == OUT_USB && g_n == 1);

    setup();
    assert(outroute_to_bt(MAC, "", &held) == OUTROUTE_STEP_FAILED && !outroute_bt_routed());
}

static void pick_quick_spdif(void){
    setup(); g_playing = 1; g_trk.pos = 7000;
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) == 0);
    assert(g_n == 1 && !strcmp(g_log[0], PAUSE) && outroute_busy() && outroute_pick_pending());
    assert(outroute_pick(OUT_INTERNAL, OUTROUTE_SETTING) < 0 && toasted("Switching..."));
    run(300); assert(g_n == 1);
    run(300);
    const char *want[] = { PAUSE, R4, W8, PAUSE };
    expect_frames("quick spdif", want, 4);
    assert(outroute_route() == OUT_SPDIF && !outroute_busy() && g_nseek == 1 && g_seek_ms == 7000 && g_nchanged > 0);
    assert(cfg_get_int("usb_audio_on", 1) == 0 && cfg_get_int("spdif", 0) == 1);
}
static void pick_full_form_by_mode_and_when_leaving_usb(void){
    setup();
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_MODE) == 0);
    run(700);
    const char *mode[] = { R4, G0, W8 };
    expect_frames("mode form", mode, 3);
    setup(); outroute_set(OUT_USB);
    assert(outroute_pick(OUT_INTERNAL, OUTROUTE_SETTING) == 0);
    run(700);
    const char *leave[] = { R6, G0, W8 };
    expect_frames("leaving usb is full", leave, 3);
}
static void pick_refusals(void){
    setup(); g_blocked = "Player is busy - try again";
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) < 0 && toasted("Player is busy - try again") && g_n == 0 && !outroute_busy());
    g_blocked = NULL;
    assert(outroute_pick(7, OUTROUTE_SETTING) < 0 && outroute_pick(-1, OUTROUTE_SETTING) < 0 && g_n == 0);
    setup(); g_playing = 1; g_fail_from = 0;
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) < 0 && toasted("Player is busy - try again") && !outroute_busy());
}
static void pick_gives_up_when_the_world_moves(void){
    setup(); g_playing = 1;
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) == 0);
    g_gen++; run(200);
    assert(toasted("Player restarted - try again") && !outroute_busy() && g_n == 1);

    setup(); g_playing = 1;
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) == 0);
    snprintf(g_trk.path, sizeof g_trk.path, "/music/b.flac"); run(200);
    assert(toasted("Track changed - try again") && !outroute_busy() && g_n == 1);

    setup(); g_playing = 1;
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) == 0);
    g_playing = 1;
    for(int t = 0; t < 3200; t += 100){ g_now += 100; g_playing = 1; outroute_tick(); }
    assert(toasted("Player is busy - try again") && !outroute_busy() && outroute_route() == OUT_INTERNAL);

    setup(); g_playing = 1;
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) == 0);
    g_blocked = "SD card is not ready"; run(700);
    assert(toasted("SD card is not ready") && !outroute_busy() && g_n == 2 && !strcmp(g_log[1], PAUSE) && g_playing);
}
static void pick_stay_paused(void){
    setup(); g_playing = 1;
    assert(outroute_pick(OUT_INTERNAL, OUTROUTE_MODE_STAY_PAUSED) == 0);
    run(700);
    const char *want[] = { PAUSE, R6, G0, W8 };
    expect_frames("stay paused", want, 4);
    assert(!g_playing && outroute_route() == OUT_INTERNAL);
}
static void pick_policy_recover_on_step_failure(void){
    setup(); g_playing = 1; g_trk.pos = 7000;
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) == 0);
    g_fail_at = 2;
    run(700);
    const char *want[] = { PAUSE, R4, W8, R6, G0, W8, PAUSE };
    expect_frames("failed pick restores the internal DAC", want, 7);
    assert(toasted("Output switch failed - internal DAC restored") && outroute_route() == OUT_INTERNAL && !outroute_busy());
    assert(g_playing && g_nseek == 1 && g_seek_ms == 7000);
}
static void pick_policy_refused_keeps_the_route_and_resumes(void){
    setup(); g_playing = 1; outroute_set(OUT_SPDIF);
    assert(outroute_pick(OUT_INTERNAL, OUTROUTE_SETTING) == 0);
    g_fail_at = 1;
    run(700);
    assert(toasted("Couldn't switch output") && outroute_route() == OUT_SPDIF && g_playing && !outroute_busy());
}
static void pick_policy_recovery_timer(void){
    setup(); g_playing = 1;
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) == 0);
    g_fail_from = 2;
    run(700);
    assert(toasted("Output state uncertain - recovering") && outroute_busy() && !outroute_pick_pending() && outroute_route() == OUT_INTERNAL);
    int before = g_n;
    run(1000); assert(g_n > before && outroute_busy());
    g_fail_from = -1; reset_log();
    run(600);
    const char *want[] = { R6, G0, W8 };
    expect_frames("recovery", want, 3);
    assert(!outroute_busy() && toasted("Internal DAC restored"));
}
static void recovery_stops_after_a_player_restart(void){
    setup(); g_playing = 1;
    assert(outroute_pick(OUT_SPDIF, OUTROUTE_SETTING) == 0);
    g_fail_from = 2; run(700);
    assert(outroute_busy());
    g_gen++; run(600);
    assert(!outroute_busy() && outroute_route() == OUT_INTERNAL);
}
static void usb_audio_enter_and_unplug(void){
    setup(); usb_card(1);
    assert(outroute_pick(OUT_USB, OUTROUTE_MODE) == 0);
    run(700);
    const char *to_usb[] = { R3, G5, W8 };
    expect_frames("to USB", to_usb, 3);
    assert(toasted("Powering USB DAC...") && outroute_route() == OUT_USB && outroute_busy());
    run(1500);
    assert(toasted("USB DAC connected") && !outroute_busy() && cfg_get_int("usb_audio_on", 0) == 1);
    usb_card(0); run(2500);
    run(700);
    assert(toasted("USB DAC disconnected - internal DAC") && outroute_route() == OUT_INTERNAL);
    const char *back[] = { R3, G5, W8, R6, G0, W8 };
    expect_frames("back to internal", back, 6);
    assert(cfg_get_int("usb_audio_on", 1) == 0);
}
static void usb_audio_without_a_dac_falls_back(void){
    setup();
    assert(outroute_pick(OUT_USB, OUTROUTE_MODE) == 0);
    run(21000);
    assert(toasted("No USB DAC found - internal DAC restored") && outroute_route() == OUT_INTERNAL && !outroute_busy());
    const char *want[] = { R3, G5, W8, R6, G0, W8 };
    expect_frames("fallback", want, 6);
}
static void usb_audio_on_another_card(void){
    setup();
    char d[260]; char *slash;
    snprintf(d, sizeof d, "%s", OUT_USB_CARD); slash = strrchr(d, '/'); *slash = 0;
    char p[300]; snprintf(p, sizeof p, "%s/card2", d); mkdir(p, 0700);
    snprintf(p, sizeof p, "%s/card2/usbid", d); close(open(p, O_WRONLY | O_CREAT, 0600));
    assert(outroute_pick(OUT_USB, OUTROUTE_MODE) == 0);
    run(21000);
    assert(toasted("USB DAC not on card1 - internal DAC restored") && outroute_route() == OUT_INTERNAL);
}
static void usb_audio_restart_while_waiting(void){
    setup(); assert(outroute_pick(OUT_USB, OUTROUTE_MODE) == 0);
    run(700); g_gen++; run(300);
    assert(toasted("Player restarted - try again") && !outroute_busy() && outroute_route() == OUT_INTERNAL);
}
static void usb_audio_unplug_gives_up_after_three_tries(void){
    setup(); usb_card(1);
    assert(outroute_pick(OUT_USB, OUTROUTE_MODE) == 0);
    run(2500);
    usb_card(0);
    for(int t = 0; t < 60000; t += 100){ g_now += 100; g_blocked = outroute_pick_pending() ? "SD card is not ready" : NULL; outroute_tick(); }
    assert(toasted("USB DAC disconnected - internal DAC") && toasted("Couldn't leave the USB DAC - pick the internal DAC in Settings"));
    assert(outroute_route() == OUT_USB && !outroute_busy());
    int quiet = g_ntoast;
    for(int t = 0; t < 20000; t += 100){ g_now += 100; g_blocked = outroute_pick_pending() ? "SD card is not ready" : NULL; outroute_tick(); }
    assert(g_ntoast == quiet);
}
static void adopt_watches_a_usb_route(void){
    setup(); usb_card(1);
    outroute_set(OUT_USB);
    outroute_adopt();
    assert(g_nchanged == 1);
    run(2500);
    assert(g_n == 0 && outroute_route() == OUT_USB);
    usb_card(0);
    run(3000);
    assert(toasted("USB DAC disconnected - internal DAC"));
}

static void scenario(const char *name, void (*fn)(void)){
    fflush(stdout);
    pid_t p = fork();
    if(p == 0){ fn(); _exit(0); }
    int st = 0; waitpid(p, &st, 0);
    if(!WIFEXITED(st) || WEXITSTATUS(st) != 0){ fprintf(stderr, "FAIL %s\n", name); exit(1); }
    printf("  ok %s\n", name);
}
#define S(f) scenario(#f, f)
int main(void){
    S(route_state_and_spdif_mirror);
    S(restart_invalidates_the_route);
    S(bt_sink_is_a_fact);
    S(saved_usb_audio_preference);
    S(mac_normalize);
    S(route_file_roundtrip);
    S(route_file_survives_a_ui_restart_not_a_player_restart);
    S(route_file_forgotten);
    S(route_file_rejections);
    S(route_file_not_written_without_one_player);
    S(analog_removes_the_file_and_the_sink);
    S(analog_without_a_sink_is_a_cleanup_unless_forced);
    S(player_age);
    S(leave_paused_frames);
    S(leave_paused_not_paused);
    S(leave_paused_policy_on_step_failure);
    S(bt_sink_sequence);
    S(bt_sink_failures);
    S(pick_quick_spdif);
    S(pick_full_form_by_mode_and_when_leaving_usb);
    S(pick_refusals);
    S(pick_gives_up_when_the_world_moves);
    S(pick_stay_paused);
    S(pick_policy_recover_on_step_failure);
    S(pick_policy_refused_keeps_the_route_and_resumes);
    S(pick_policy_recovery_timer);
    S(recovery_stops_after_a_player_restart);
    S(usb_audio_enter_and_unplug);
    S(usb_audio_without_a_dac_falls_back);
    S(usb_audio_on_another_card);
    S(usb_audio_restart_while_waiting);
    S(usb_audio_unplug_gives_up_after_three_tries);
    S(adopt_watches_a_usb_route);
    puts("outroute_test: ok");
    return 0;
}
