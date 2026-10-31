/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "outroute.h"
#include "config.h"

#ifndef OUT_USB_CARD
#define OUT_USB_CARD "/proc/asound/card1"
#endif
#ifndef OUT_USB_ANY_CARD
#define OUT_USB_ANY_CARD "/proc/asound/card*/usbid"
#endif
#ifndef ROUTE_STATE_PATH
#define ROUTE_STATE_PATH "/tmp/.diskos-ui/route"
#endif
#ifndef ROUTE_STATE_DIR
#define ROUTE_STATE_DIR "/tmp/.diskos-ui"
#endif
#ifndef ROUTE_STATE_UID
#define ROUTE_STATE_UID 0
#endif
#ifndef ROUTE_PROC_ROOT
#define ROUTE_PROC_ROOT "/proc"
#endif
#define OUT_QUIET_MS 500
#define OUT_WAIT_MS  3000
#define OUT_USB_WAIT_MS   20000
#define OUT_USB_SETTLE_MS 1000
#define USBGONE_TRIES 3
#define PREF_USB "usb_audio_on"
#define PREF_SPDIF "spdif"
#define MAC_TEXT_LEN 17

static const struct outroute_env *E;
static void (*g_listener)(void);
static uint32_t now_ms(void){ return E->now(); }
static uint32_t elapsed(uint32_t t0){ return now_ms() - t0; }
static void notify(void){ if(g_listener) g_listener(); }
void outroute_set_listener(void (*cb)(void)){ g_listener = cb; }

typedef struct { int on; uint32_t period, last; } tm_t;
static void tm_start(tm_t *t, uint32_t period){ t->on = 1; t->period = period; t->last = now_ms(); }
static void tm_stop(tm_t *t){ t->on = 0; }
static int  tm_due(tm_t *t){
    if(!t->on || elapsed(t->last) < t->period) return 0;
    t->last = now_ms(); return 1;
}

static int g_route = OUT_INTERNAL;
static unsigned g_route_gen = 0;
static char g_bt_mac[OUTROUTE_MAC_LEN] = "";
static struct { int target, was_playing, full; unsigned gen; char path[256]; long pos; uint32_t t0, quiet0; tm_t tm; } g_osw;
static int g_rec = 0; static unsigned g_rec_gen = 0; static tm_t g_rec_tm;
static struct { tm_t tm; uint32_t t0, seen; } g_usbw;
static tm_t g_usbgone; static int g_usbgone_miss, g_usbgone_tries;

void outroute_init(const struct outroute_env *env){
    E = env;
    g_route = OUT_INTERNAL; g_route_gen = 0;
    g_bt_mac[0] = 0;
    memset(&g_osw, 0, sizeof g_osw);
    g_rec = 0; g_rec_gen = 0;
    memset(&g_rec_tm, 0, sizeof g_rec_tm);
    memset(&g_usbw, 0, sizeof g_usbw);
    memset(&g_usbgone, 0, sizeof g_usbgone);
    g_usbgone_miss = g_usbgone_tries = 0;
}

void outroute_set(int route){
    g_route = route; g_route_gen = ipc_generation();
    if(cfg_get_int(PREF_SPDIF, 0) != (route == OUT_SPDIF)) cfg_set_int(PREF_SPDIF, route == OUT_SPDIF);
}
int outroute_route(void){
    if(g_route != OUT_INTERNAL && g_route_gen != ipc_generation()) outroute_set(OUT_INTERNAL);
    return g_route;
}
void outroute_reset(void){ outroute_set(OUT_INTERNAL); }
int  outroute_spdif_chosen(void){ return cfg_get_int(PREF_SPDIF, 0); }
void outroute_clear_spdif_choice(void){ if(cfg_get_int(PREF_SPDIF, 0)) cfg_set_int(PREF_SPDIF, 0); }

int  outroute_saved_usb_audio(void){ return cfg_get_int(PREF_USB, 0) == 1; }
void outroute_save_usb_audio(int on){ cfg_set_int(PREF_USB, on ? 1 : 0); }

const char *outroute_bt_mac(void){ return g_bt_mac; }
int  outroute_bt_routed(void){ return g_bt_mac[0] != 0; }
void outroute_set_bt(const char *mac){ snprintf(g_bt_mac, sizeof g_bt_mac, "%s", mac ? mac : ""); }

int outroute_mac_normalize(const char *in, char out[OUTROUTE_MAC_LEN]){
    if(!in) return -1;
    int j = 0;
    for(int i = 0; in[i] && j < OUTROUTE_MAC_LEN - 1; i++){ char c = in[i]; if(c >= 'a' && c <= 'z') c -= 32; out[j++] = c; }
    out[j] = 0;
    if(j != MAC_TEXT_LEN) return -1;
    for(int i = 0; i < MAC_TEXT_LEN; i++){
        if((i+1)%3 == 0){ if(out[i] != ':') return -1; }
        else if(!((out[i] >= '0' && out[i] <= '9') || (out[i] >= 'A' && out[i] <= 'F'))) return -1;
    }
    return 0;
}

static int player_identity(long *pid, unsigned long long *start){
    DIR *dir = opendir(ROUTE_PROC_ROOT);
    if(!dir) return -1;
    int found = 0;
    struct dirent *ent;
    while((ent = readdir(dir))){
        char *end; errno = 0;
        long p = strtol(ent->d_name, &end, 10);
        if(errno || p <= 0 || *end) continue;
        char path[256];
        if(snprintf(path, sizeof path, "%s/%ld/stat", ROUTE_PROC_ROOT, p) >= (int)sizeof path) continue;
        FILE *f = fopen(path, "r");
        if(!f) continue;
        char line[1024]; int ok = fgets(line, sizeof line, f) != NULL && strchr(line, '\n') != NULL;
        fclose(f);
        if(!ok) continue;
        char *lp = strchr(line, '('), *rp = strrchr(line, ')');
        if(!lp || !rp || rp <= lp || (size_t)(rp-lp-1) != strlen("mq_player") ||
           strncmp(lp+1, "mq_player", rp-lp-1) || rp[1] != ' ') continue;
        char *save = NULL, *tok = strtok_r(rp+2, " \n", &save);
        if(!tok || tok[0] == 'Z' || tok[0] == 'X') continue;
        for(int field=3; field<22 && tok; field++) tok = strtok_r(NULL, " \n", &save);
        if(!tok) continue;
        errno = 0;
        unsigned long long ticks = strtoull(tok, &end, 10);
        if(errno || !ticks || *end) continue;
        *pid = p; *start = ticks;
        found++;
        if(found > 1) break;
    }
    closedir(dir);
    return found == 1 ? 0 : -1;
}
double outroute_player_age_s(void){
    long pid, hz = sysconf(_SC_CLK_TCK);
    unsigned long long start;
    if(hz <= 0 || player_identity(&pid, &start) < 0) return -1;
    char path[sizeof ROUTE_PROC_ROOT + 16];
    if(snprintf(path, sizeof path, "%s/uptime", ROUTE_PROC_ROOT) >= (int)sizeof path) return -1;
    FILE *f = fopen(path, "r");
    if(!f) return -1;
    double uptime = 0;
    int ok = fscanf(f, "%lf", &uptime) == 1;
    fclose(f);
    return ok && uptime >= 0 ? uptime - (double)start / (double)hz : -1;
}

static int route_state_dir(int create){
    struct stat st;
    if(create && mkdir(ROUTE_STATE_DIR, 0700) < 0 && errno != EEXIST) return 0;
    return lstat(ROUTE_STATE_DIR, &st) == 0 && S_ISDIR(st.st_mode) &&
           st.st_uid == ROUTE_STATE_UID && (st.st_mode & 07777) == 0700;
}
static void route_state_remove(void){ if(route_state_dir(0)) (void)unlink(ROUTE_STATE_PATH); }
static void route_state_save(const char *mac){
    if(!route_state_dir(1)) return;
    long pid; unsigned long long start;
    if(player_identity(&pid, &start) < 0){ route_state_remove(); return; }
    char data[128];
    int n = snprintf(data, sizeof data, "pid=%ld start=%llu gen=%u mac=%s\n",
                     pid, start, ipc_generation(), mac);
    if(n <= 0 || n >= (int)sizeof data){ route_state_remove(); return; }
    char tmp[sizeof ROUTE_STATE_PATH + 16];
    if(snprintf(tmp, sizeof tmp, "%s.XXXXXX", ROUTE_STATE_PATH) >= (int)sizeof tmp){ route_state_remove(); return; }
    int fd = mkstemp(tmp);
    if(fd < 0){ route_state_remove(); return; }
    size_t pos = 0;
    while(pos < (size_t)n){ ssize_t w = write(fd, data+pos, (size_t)n-pos);
        if(w < 0 && errno == EINTR) continue;
        if(w <= 0) break;
        pos += (size_t)w;
    }
    int ok = pos == (size_t)n && fchmod(fd, 0600) == 0;
    close(fd);
    if(!ok || rename(tmp, ROUTE_STATE_PATH) != 0){ unlink(tmp); route_state_remove(); }
}
void outroute_restore(void){
    if(!route_state_dir(0)) return;
    int fd = open(ROUTE_STATE_PATH, O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd < 0){ if(errno != ENOENT) route_state_remove(); return; }
    struct stat st;
    char data[128]; ssize_t n = -1;
    if(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == ROUTE_STATE_UID &&
       (st.st_mode & 07777) == 0600 && st.st_size > 0 && st.st_size < (off_t)sizeof data)
        n = read(fd, data, sizeof data);
    close(fd);
    if(n <= 0 || n >= (ssize_t)sizeof data || n != st.st_size || memchr(data, 0, (size_t)n)){
        route_state_remove(); return;
    }
    data[n] = 0;
    long pid = -1, current_pid; unsigned long long start = 0, current_start;
    unsigned gen; char mac[OUTROUTE_MAC_LEN], canonical[128];
    if(sscanf(data, "pid=%ld start=%llu gen=%u mac=%19s", &pid, &start, &gen, mac) != 4 ||
       snprintf(canonical, sizeof canonical, "pid=%ld start=%llu gen=%u mac=%s\n", pid, start, gen, mac) <= 0 ||
       strcmp(data, canonical) || pid <= 0 || !start ||
       player_identity(&current_pid, &current_start) < 0 ||
       pid != current_pid || start != current_start){ route_state_remove(); return; }
    char norm[OUTROUTE_MAC_LEN];
    if(outroute_mac_normalize(mac, norm) < 0){ route_state_remove(); return; }
    outroute_set_bt(norm);
}
void outroute_bt_forget(void){
    g_bt_mac[0] = 0;
    route_state_remove();
}

static void out_resume(int moved){
    if(!g_osw.was_playing) return;
    track_state_t st; ipc_get_state(&st);
    if(moved && (ipc_generation() != g_osw.gen || !st.have_track || strcmp(st.path, g_osw.path) != 0)){ E->toast("Output switched - press play"); return; }
    if(moved && g_osw.pos > 0 && E->seek_to(g_osw.pos) < 0){ E->toast("Couldn't resume - press play"); return; }
    if(ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0) E->toast("Couldn't resume - press play");
}

static int out_usb_card_ok(void){
    char p[96]; snprintf(p, sizeof p, "%s/usbid", OUT_USB_CARD);
    return access(p, F_OK) == 0;
}
static int out_usb_any_card(void){
    glob_t g; int n = 0;
    if(glob(OUT_USB_ANY_CARD, 0, NULL, &g) == 0){ n = (int)g.gl_pathc; globfree(&g); }
    return n > 0;
}
static void out_rec_clear(void){ tm_stop(&g_rec_tm); g_rec = 0; }
static void out_recover_tick(void){
    if(ipc_generation() != g_rec_gen){ out_rec_clear(); outroute_reset(); notify(); return; }
    if(!ipc_is_ready()) return;
    if(outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL) == OUTSEQ_OK){
        out_rec_clear(); outroute_reset();
        E->toast("Internal DAC restored"); notify();
    }
}
static void out_rec_enter(void){
    outroute_reset();
    if(g_rec) return;
    g_rec = 1; g_rec_gen = ipc_generation();
    tm_start(&g_rec_tm, 500);
}
static void usbgone_start(void){
    g_usbgone_miss = 0; g_usbgone_tries = 0;
    if(!g_usbgone.on) tm_start(&g_usbgone, 1000);
}
static void usbgone_tick(void){
    if(outroute_route() != OUT_USB){ tm_stop(&g_usbgone); return; }
    if(outroute_busy()) return;
    if(out_usb_card_ok()){ g_usbgone_miss = 0; g_usbgone_tries = 0; return; }
    if(++g_usbgone_miss < 2) return;
    g_usbgone_miss = 0;
    if(g_usbgone_tries >= USBGONE_TRIES){
        tm_stop(&g_usbgone);
        E->toast("Couldn't leave the USB DAC - pick the internal DAC in Settings");
        return;
    }
    fprintf(stderr, "usb audio: DAC gone - back to the internal DAC (paused)\n"); fflush(stderr);
    if(outroute_pick(OUT_INTERNAL, OUTROUTE_MODE_STAY_PAUSED) == 0){
        if(!g_usbgone_tries++) E->toast("USB DAC disconnected - internal DAC");
    }
}
static void usbw_tick(void){
    if(ipc_generation() != g_osw.gen){ tm_stop(&g_usbw.tm); outroute_reset(); E->toast("Player restarted - try again"); notify(); return; }
    if(out_usb_card_ok()){
        if(!g_usbw.seen) g_usbw.seen = now_ms() ? now_ms() : 1;
        if(elapsed(g_usbw.seen) < OUT_USB_SETTLE_MS) return;
        tm_stop(&g_usbw.tm); E->toast("USB DAC connected"); out_resume(1); usbgone_start(); notify();
        outroute_save_usb_audio(1);
        return;
    }
    if(elapsed(g_usbw.t0) < OUT_USB_WAIT_MS) return;
    tm_stop(&g_usbw.tm);
    if(outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL) == OUTSEQ_OK){
        outroute_reset();
        E->toast(out_usb_any_card() ? "USB DAC not on card1 - internal DAC restored" : "No USB DAC found - internal DAC restored");
        out_resume(1);
    } else {
        out_rec_enter();
        E->toast("Output state uncertain - recovering");
    }
    notify();
}
static void usbw_start(void){
    g_usbw.t0 = now_ms(); g_usbw.seen = 0;
    tm_start(&g_usbw.tm, 250);
    E->toast("Powering USB DAC...");
}
static int out_run(void){
    int target = g_osw.target;
    outseq_rc rc = outseq_route(target, g_osw.full ? OUTSEQ_FULL : OUTSEQ_QUICK);
    if(rc == OUTSEQ_REFUSED){ outroute_set(outroute_route()); E->toast("Couldn't switch output"); out_resume(0); notify(); return -1; }
    if(rc == OUTSEQ_OK && target == OUT_USB){ outroute_set(target); usbw_start(); notify(); return 0; }
    if(rc == OUTSEQ_OK){ outroute_set(target); out_resume(1); outroute_save_usb_audio(0); notify(); return 0; }
    if(outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL) == OUTSEQ_OK){
        outroute_reset();
        E->toast("Output switch failed - internal DAC restored");
        out_resume(1); notify();
        return -1;
    }
    out_rec_enter();
    E->toast("Output state uncertain - recovering");
    notify();
    return -1;
}
static void osw_stop(const char *why, int resume){
    tm_stop(&g_osw.tm);
    outroute_set(outroute_route());
    E->toast(why);
    if(resume) out_resume(0);
    notify();
}
static void osw_tick(void){
    track_state_t st; ipc_get_state(&st);
    if(ipc_generation() != g_osw.gen){ osw_stop("Player restarted - try again", 0); return; }
    if(strcmp(st.path, g_osw.path) != 0){ osw_stop("Track changed - try again", 0); return; }
    if(elapsed(g_osw.t0) >= OUT_WAIT_MS){ osw_stop("Player is busy - try again", 0); return; }
    if(E->playing()) g_osw.quiet0 = 0;
    else if(!g_osw.quiet0) g_osw.quiet0 = now_ms() ? now_ms() : 1;
    if(!g_osw.quiet0 || elapsed(g_osw.quiet0) < OUT_QUIET_MS){
        return;
    }
    const char *why = E->switch_blocked();
    if(why){ osw_stop(why, g_osw.was_playing); return; }
    tm_stop(&g_osw.tm);
    out_run();
}

int outroute_pick(int target, outroute_how_t how){
    if(target < OUT_INTERNAL || target > OUT_USB) return -1;
    if(outroute_busy()){ E->toast("Switching..."); return -1; }
    const char *why = E->switch_blocked();
    if(why){ outroute_set(outroute_route()); E->toast(why); return -1; }
    int was_playing = E->playing();
    track_state_t st; ipc_get_state(&st);
    memset(&g_osw, 0, sizeof g_osw);
    g_osw.target = target; g_osw.was_playing = was_playing;
    g_osw.full = how != OUTROUTE_SETTING || target == OUT_USB || outroute_route() == OUT_USB;
    g_osw.gen = ipc_generation(); g_osw.pos = st.have_track ? st.position_ms : 0;
    snprintf(g_osw.path, sizeof g_osw.path, "%s", st.have_track ? st.path : "");
    if(was_playing && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0){ outroute_set(outroute_route()); E->toast("Player is busy - try again"); return -1; }
    g_osw.t0 = now_ms();
    tm_start(&g_osw.tm, 100);
    if(how == OUTROUTE_MODE_STAY_PAUSED) g_osw.was_playing = 0;
    return 0;
}
int outroute_busy(void){ return g_osw.tm.on || g_rec || g_usbw.tm.on; }
int outroute_pick_pending(void){ return g_osw.tm.on; }
void outroute_adopt(void){
    notify();
    if(outroute_route() == OUT_USB) usbgone_start();
}
void outroute_tick(void){
    if(tm_due(&g_osw.tm)) osw_tick();
    if(tm_due(&g_rec_tm)) out_recover_tick();
    if(tm_due(&g_usbw.tm)) usbw_tick();
    if(tm_due(&g_usbgone)) usbgone_tick();
}

typedef outseq_rc (*step_fn)(const outroute_held_t *held, void *arg);
static int leave_paused_switch(step_fn step, void *arg, outroute_held_t *held){
    outroute_held_t h;
    ipc_get_state(&h.st);
    h.was_playing = E->playing();
    if(held) *held = h;
    if(h.was_playing && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0) return OUTROUTE_NOT_PAUSED;
    if(step(&h, arg) != OUTSEQ_OK) return OUTROUTE_STEP_FAILED;
    if(h.st.have_track && h.st.position_ms > 0) E->seek_to(h.st.position_ms);
    if(h.was_playing) ipc_send_cmd(PLAYER_CMD_PLAYPAUSE);
    return OUTROUTE_OK;
}
static outseq_rc bt_step(const outroute_held_t *held, void *frame){
    outseq_rc rc = outseq_bt_sink((const char *)frame);
    if(rc != OUTSEQ_REFUSED) outroute_reset();
    if(rc == OUTSEQ_OK && held->st.volume_seq){ char v[16]; snprintf(v, sizeof v, "0715000C%04X", held->st.volume); ipc_send_cmd(v); }
    return rc;
}
static outseq_rc analog_step(const outroute_held_t *held, void *arg){ (void)held; (void)arg; return outseq_analog(); }

int outroute_to_bt(const char *mac, const char *codec_frame, outroute_held_t *held){
    int rc = leave_paused_switch(bt_step, (void *)codec_frame, held);
    if(rc < 0) return rc;
    outroute_set_bt(mac);
    route_state_save(mac);
    return OUTROUTE_OK;
}
int outroute_to_analog(int force, outroute_held_t *held){
    if(!force && !outroute_bt_routed()){ route_state_remove(); return 0; }
    int rc = leave_paused_switch(analog_step, NULL, held);
    if(rc < 0) return rc;
    outroute_bt_forget();
    return 1;
}
