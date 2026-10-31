/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <ucontext.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <glob.h>
#include <dirent.h>
#include <linux/input.h>
#include <net/if.h>
#include "command_spawn.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/core/lv_refr_private.h"
#include "theme.h"
#include "theme_kit.h"
#include "fontft.h"
#include "fb_pan.h"
#include "controls.h"
#include "screens.h"
#include "modes.h"
#include "usbhid.h"
#include "outseq.h"
#include "session.h"
#include "srcswitch.h"
#include "admit.h"
#include "timekeep.h"
#include "bootsel.h"
#include "rmguard.h"
#include "launcher.h"
#include "transport.h"
#include "booksess.h"
#include "handoff.h"
#include "anim.h"
#include "ipc.h"
#include "opustitle_worker.h"
#include "fwcaps.h"
#include "musicdb.h"
#include "sysconfig.h"
#include "prof.h"
#include "lvgl/src/drivers/evdev/lv_evdev.h"
#include "config.h"
#include "phonehome.h"
#include "scanner.h"
#include "playstate.h"
#include "sdio.h"
#include "art.h"
#include "power.h"

void mclock_sync(int backlight_on);
void ui_np_title_scroll(int visible);
void library_jump_cancel(void);

static lv_indev_t *g_touch = NULL;
static int         g_screen_off = 0;
static int         g_touch_raw = -1;

static volatile int g_manual_sleep = 0;
static uint32_t g_manual_sleep_at = 0;
void ui_request_sleep(void){ g_manual_sleep = 1; g_manual_sleep_at = lv_tick_get(); }
int ui_screen_backlight_on(void){ return !g_screen_off; }
void ui_backlight_hold(void);
void ui_backlight_release(int fallback);
static int g_draw_dark;
static void draw_set_dark(int dark){
    if(dark == g_draw_dark) return;
    lv_display_t *d = lv_display_get_default();
    if(!d) return;
    g_draw_dark = dark;
    if(dark){
        lv_inv_area(d, NULL);
        lv_display_enable_invalidation(d, 0);
        lv_timer_pause(lv_display_get_refr_timer(d));
        ui_backlight_hold();
    } else {
        lv_display_enable_invalidation(d, 1);
        lv_obj_invalidate(lv_display_get_screen_active(d));
        lv_obj_invalidate(lv_display_get_layer_bottom(d));
        lv_obj_invalidate(lv_display_get_layer_top(d));
        lv_obj_invalidate(lv_display_get_layer_sys(d));
        lv_refr_now(d);
        ui_backlight_release(ui_effective_brightness());
    }
}
static void wake_backlight(int v){ ui_backlight(v); }
void albumwall_poll_sync(int lit);
static lv_obj_t   *g_dbgdot = NULL;
static int         g_dbg = 0;

#define SWIPE_THRESH_DEFAULT 60
#define BACK_START_MAX_X 64
#define NP_NAV_DIST 95
#define NP_NAV_STRAIGHT 3
static int g_swipe_thresh = SWIPE_THRESH_DEFAULT;

int ui_get_swipe_thresh(void){ return g_swipe_thresh; }
static int g_press_sx, g_press_sy, g_press_lx, g_press_ly;
int ui_press_travel(void){
    lv_indev_t *in = lv_indev_active();
    if(in){ lv_point_t p; lv_indev_get_point(in, &p); if(p.x >= 0 && p.x < 360 && p.y >= 0 && p.y < 360){ g_press_lx = p.x; g_press_ly = p.y; } }
    int dx = g_press_lx - g_press_sx, dy = g_press_ly - g_press_sy;
    if(dx < 0) dx = -dx;
    if(dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

static _Atomic int g_bl_idle = 0;
int ui_main_is_idle(void){ return g_bl_idle; }
void ui_apply_swipe_thresh(int px){
    if(px < 20) px = 20; if(px > 200) px = 200;
    g_swipe_thresh = px;
}
void ui_set_swipe_thresh(int px){
    ui_apply_swipe_thresh(px);
    cfg_set_int("swipe_thresh", g_swipe_thresh);
}
static void swipe_thresh_load(void){
    ui_apply_swipe_thresh(cfg_get_int("swipe_thresh", SWIPE_THRESH_DEFAULT));
}

static void dbgdot_init(void){
    g_dbgdot = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_dbgdot);
    lv_obj_set_size(g_dbgdot, 26, 26);
    lv_obj_set_style_radius(g_dbgdot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_dbgdot, TC(FIXED_DEBUG_TOUCH), 0);
    lv_obj_set_style_bg_opa(g_dbgdot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_dbgdot, TC(OUTLINE_BRIGHT), 0);
    lv_obj_set_style_border_width(g_dbgdot, 2, 0);
    lv_obj_add_flag(g_dbgdot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_dbgdot, LV_OBJ_FLAG_CLICKABLE);
}

static void go_settings(void){ screen_show(SCR_SETTINGS); }

static int read_int_file(const char *path){
    FILE *f = fopen(path, "r"); if(!f) return -1;
    int v = -1; if(fscanf(f, "%d", &v) != 1) v = -1; fclose(f); return v;
}

typedef struct { int seen; int player_off; unsigned off_seq; } powerkey_t;
enum { PK_NONE, PK_OFF, PK_WAKE, PK_RELEVEL };
static int powerkey_step(powerkey_t *k, int cbr, int ours, unsigned ours_seq, int dark){
    if(cbr < 0 || cbr == k->seen) return PK_NONE;
    k->seen = cbr;
    if(cbr == 0){ k->player_off = 1; k->off_seq = ours_seq; return dark ? PK_NONE : PK_OFF; }
    if(k->player_off){
        if(cbr == ours && ours_seq != k->off_seq) return PK_NONE;
        k->player_off = 0; return PK_WAKE;
    }
    if(cbr == ours) return PK_NONE;
    return PK_RELEVEL;
}
static int powerkey_defer_startup(int brightness){
    if(brightness != 0 || fw_power_key_driveable()) return 0;
    double up = -1; FILE *f = fopen("/proc/uptime", "r");
    if(f){ if(fscanf(f, "%lf", &up) != 1) up = -1; fclose(f); }
    return up > 150.0;
}
enum { SF_NONE, SF_OFF, SF_WAKE, SF_INJECT };
static int screen_frame_step(int on, int bl_state, int *player_off, int poweroff_inhibit){
    if(on){ *player_off = 0; return SF_WAKE; }
    int own_off = bl_state == 2 && !*player_off;
    *player_off = 1;
    if(own_off) return poweroff_inhibit ? SF_NONE : SF_INJECT;
    return SF_OFF;
}
static int power_key_inject(void){
    char path[64], name[32];
    for(int i = 0; i < 8; i++){
        snprintf(path, sizeof path, "/sys/class/input/event%d/device/name", i);
        FILE *f = fopen(path, "r"); if(!f) continue;
        int got = fgets(name, sizeof name, f) != NULL; fclose(f);
        if(!got || strncmp(name, "x2000_key", 9) || (name[9] && name[9] != '\n')) continue;
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if(fd < 0) return 0;
        struct input_event ev[4]; memset(ev, 0, sizeof ev);
        ev[0].type = EV_KEY; ev[0].code = 265; ev[0].value = 1;  ev[1].type = EV_SYN; ev[1].code = SYN_REPORT;
        ev[2].type = EV_KEY; ev[2].code = 265; ev[2].value = 0;  ev[3].type = EV_SYN; ev[3].code = SYN_REPORT;
        int ok = write(fd, ev, sizeof ev) == (ssize_t)sizeof ev;
        close(fd);
        fprintf(stderr, "power key: %s a press to wake the player's screen\n", ok ? "sent" : "could not send");
        return ok;
    }
    return 0;
}
static int dim_level(void){
    int dim = ui_effective_brightness();
    if(dim > 6 && !(screen_current() == SCR_SAVER && saver_wants_bright())) dim = 6;
    return dim;
}

static void clock_tick(lv_timer_t *t){
    (void)t;
    time_t now = time(NULL);
    struct tm lt; localtime_r(&now, &lt);
    char hm[12], date[24];
    int h24 = cfg_get_int("time_24h", 1);
    if(h24){
        strftime(hm, sizeof hm, "%H:%M", &lt);
    } else {
        strftime(hm, sizeof hm, "%I:%M %p", &lt);
        if(hm[0]=='0') memmove(hm, hm+1, strlen(hm));
    }
    strftime(date, sizeof date, "%a %d %b", &lt);
    home_set_clock(hm, date);
    saver_set_clock(hm, date);
}
void ui_clock_refresh(void){ clock_tick(NULL); }

static void status_poll_cb(lv_timer_t *t){
    (void)t;
    int batt = -1, charging = 0, wifi = 0, bt;

    FILE *bf = fopen("/sys/class/power_supply/cw221X-bat/capacity", "r");
    if(bf){ if(fscanf(bf, "%d", &batt) != 1) batt = -1; fclose(bf); }
    long cur = 0;
    FILE *cf = fopen("/sys/class/power_supply/cw221X-bat/current_now", "r");
    if(cf){ if(fscanf(cf, "%ld", &cur) != 1) cur = 0; fclose(cf); }
    charging = (cur > 0);

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if(s >= 0){
        struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
        strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ-1);
        if(ioctl(s, SIOCGIFADDR, &ifr) == 0) wifi = 1;
        close(s);
    }
    if(wifi){
        char op[16] = {0};
        FILE *of = fopen("/sys/class/net/wlan0/operstate", "r");
        if(of){ if(!fgets(op, sizeof op, of)) op[0] = 0; fclose(of); }
        wifi = (strncmp(op, "up", 2) == 0);
    }

    bt = bt_radio_on();

    home_set_status(batt, charging, wifi, bt);
}
void ui_status_refresh(void){ status_poll_cb(NULL); }

static int run_bounded(char *const argv[], int timeout_ms);
static void hwclock_save_tick(lv_timer_t *t);

int ui_apply_eq(int preset){
    if(preset < 0) preset = 0; if(preset > 20) preset = 20;
    char f[16]; snprintf(f, sizeof f, "0689000C%04X", preset);
    int rc = ipc_send_cmd(f);
    fprintf(stderr,"eq %d -> %s\n", preset, f); fflush(stderr);
    return rc;
}
int ui_eq_select(int preset){
    if(preset < 0) preset = 0; if(preset > 20) preset = 20;
    if(ui_apply_eq(preset) < 0) return -1;
    if(preset > 0) cfg_set_int_deferred("eq_last", preset);
    cfg_set_int_deferred("eq_preset", preset);
    cfg_flush();
    return 0;
}

void ui_set_dre(int on){        if(on<0) return; ipc_send_cmd(on   ? "0812000C0000" : "0812000C0001"); }
void ui_set_gain(int high){     if(high<0) return; const char *tag=fw_gain_tag(); if(!tag){ fprintf(stderr,"gain: unverified firmware (MAIN_OS_VER=%d) -> not sending\n", fw_os_ver()); return; } char f[16]; snprintf(f,sizeof f,"%s000C%04X", tag, high?1:0); ipc_send_cmd(f); }
void ui_set_output(int spdif){  if(spdif<0) return; modes_output_switch(spdif ? OUT_SPDIF : OUT_INTERNAL); }
void ui_set_dac_filter(int idx){ if(idx<0||idx>5) return; char f[16]; snprintf(f,sizeof f,"0653000C%04X",idx); ipc_send_cmd(f); }
void ui_set_replay_gain(int v){ if(v<0||v>2) return; char f[16]; snprintf(f,sizeof f,"0718000C%04X",(unsigned)v); ipc_send_cmd(f); }
void ui_set_gapless(int on){   if(on<0) return; ipc_send_cmd(on ? "0647000C0001" : "0647000C0000"); }
int ui_set_artist_class(int album_artist){
    if(!fw_artist_class_settable()){ fprintf(stderr, "artist class: unverified firmware (MAIN_OS_VER=%d)\n", fw_os_ver()); return -1; }
    if(ipc_send_cmd(album_artist ? "0648000C0001" : "0648000C0000") < 0) return -1;
    mdb_set_artist_mode(album_artist);
    library_artist_mode_changed();
    return 0;
}
int ui_set_folder_jump(int on){
    if(!fw_folder_jump_settable()){ fprintf(stderr, "folder jump: unverified firmware (MAIN_OS_VER=%d)\n", fw_os_ver()); return -1; }
    return ipc_send_cmd(on ? "0687000C0001" : "0687000C0000") < 0 ? -1 : 0;
}
void ui_set_track_display(int on){ if(fw_track_display_settable()) ipc_send_cmd(on ? "064d000C0001" : "064d000C0000"); }
void ui_set_memory(int mode){  if(mode<0||mode>2) return; char f[16]; snprintf(f,sizeof f,"0684000C%04X",mode); ipc_send_cmd(f); }
void ui_set_maxvol(int v){     if(v<0) v=0; if(v>120) v=120; char f[16]; snprintf(f,sizeof f,"0711000C%04X",v); ipc_send_cmd(f); }
void ui_set_balance(int v){ if(v < -10 || v > 10) return;
                            int fv = (v>0) ? (v & 0xFF) : (v<0) ? (0x0100 | ((-v) & 0xFF)) : 0;
                            char f[16]; snprintf(f,sizeof f,"0713000C%04X",fv); ipc_send_cmd(f); }

static void route_uppercase(const char *in, char *out, int cap);
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
static int route_player_identity(long *pid, unsigned long long *start){
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
    if(route_player_identity(&pid, &start) < 0){ route_state_remove(); return; }
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
static int route_state_restore(void){
    if(!route_state_dir(0)) return 0;
    int fd = open(ROUTE_STATE_PATH, O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd < 0){ if(errno != ENOENT) route_state_remove(); return 0; }
    struct stat st;
    char data[128]; ssize_t n = -1;
    if(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == ROUTE_STATE_UID &&
       (st.st_mode & 07777) == 0600 && st.st_size > 0 && st.st_size < (off_t)sizeof data)
        n = read(fd, data, sizeof data);
    close(fd);
    if(n <= 0 || n >= (ssize_t)sizeof data || n != st.st_size || memchr(data, 0, (size_t)n)){
        route_state_remove(); return 0;
    }
    data[n] = 0;
    long pid = -1, current_pid; unsigned long long start = 0, current_start;
    unsigned gen; char mac[20], canonical[128];
    if(sscanf(data, "pid=%ld start=%llu gen=%u mac=%19s", &pid, &start, &gen, mac) != 4 ||
       snprintf(canonical, sizeof canonical, "pid=%ld start=%llu gen=%u mac=%s\n", pid, start, gen, mac) <= 0 ||
       strcmp(data, canonical) || pid <= 0 || !start ||
       route_player_identity(&current_pid, &current_start) < 0 ||
       pid != current_pid || start != current_start){ route_state_remove(); return 0; }
    char norm[20]; route_uppercase(mac, norm, sizeof norm);
    if(strlen(norm) != 17){ route_state_remove(); return 0; }
    for(int i=0;i<17;i++){
        if((i+1)%3 == 0 ? norm[i] != ':' :
           !((norm[i]>='0'&&norm[i]<='9') || (norm[i]>='A'&&norm[i]<='F'))){
            route_state_remove(); return 0;
        }
    }
    session_set_bt_mac(norm);
    return 1;
}
static int g_playing = 0;
static void route_uppercase(const char *in, char *out, int cap){
    int j=0; for(int i=0; in[i] && j<cap-1; i++){ char c=in[i]; if(c>='a'&&c<='z') c-=32; out[j++]=c; } out[j]=0;
}
const char *ui_route_mac(void){ return session_bt_mac(); }
static outseq_rc bt_route_step(const admit_held_t *held, void *frame){
    outseq_rc rc = outseq_bt_sink((const char *)frame);
    if(rc != OUTSEQ_REFUSED) modes_output_reset();
    if(rc == OUTSEQ_OK && held->st.volume_seq){ char v[16]; snprintf(v, sizeof v, "0715000C%04X", held->st.volume); ipc_send_cmd(v); }
    return rc;
}
int ui_route_bt(const char *mac){
    if(admit_check(ADMIT_ROUTE_BT)) return -1;
    if(!mac) return -1;
    char norm[20]; route_uppercase(mac, norm, sizeof norm);
    if((int)strlen(norm) != 17) return -1;
    for(int i=0;i<17;i++){
        if((i+1)%3 == 0){ if(norm[i] != ':') return -1; }
        else if(!((norm[i]>='0'&&norm[i]<='9') || (norm[i]>='A'&&norm[i]<='F'))) return -1;
    }
    if(!strcmp(norm, session_bt_mac())){ ui_bt_codec_upgrade_arm(norm, 0); return 0; }
    if(admit_check(ADMIT_ROUTE_BT_SEND)) return -1;
    char f[48];
    if(ui_bt_codec_frame(norm, f, sizeof f) < 0) return -1;
    admit_held_t held;
    if(admit_switch(bt_route_step, f, &held) < 0) return -1;
    session_set_bt_mac(norm);
    route_state_save(norm);
    session_startup_route_settled();
    ui_bt_codec_upgrade_arm(norm, 1);
    fprintf(stderr,"route BT %s (playing=%d pos=%ldms)\n", norm, held.was_playing, held.st.position_ms); fflush(stderr);
    return 0;
}
static outseq_rc analog_route_step(const admit_held_t *held, void *arg){ (void)held; (void)arg; return outseq_analog(); }
static int route_analog(int force){
    if(admit_check(ADMIT_ROUTE_ANALOG)) return -1;
    if(!force && !session_bt_routed()){ route_state_remove(); return 0; }
    admit_held_t held;
    if(admit_switch(analog_route_step, NULL, &held) < 0) return -1;
    session_clear_bt_mac();
    route_state_remove();
    ui_bt_codec_upgrade_cancel();
    fprintf(stderr,"route analog (playing=%d)\n", held.was_playing); fflush(stderr);
    return 0;
}
int ui_route_analog(void){ return route_analog(0); }

int ui_get_source_mode(void){ return session_source_mode(); }


static int sd_exported_to_host(void);
static int coldplug_mounted(void);
static int storage_host_confirmed(void);

static int storage_player_guarded(void){
    DIR *d = opendir("/proc"); if(!d) return 0;
    struct dirent *e; int found = 0, ok = 1;
    while(ok && (e = readdir(d))){
        if(e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p[320], comm[64]; snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        char *got = fgets(comm, sizeof comm, f); fclose(f);
        if(!got || strcmp(comm, "mq_player\n")) continue;
        found++;
        snprintf(p, sizeof p, "/proc/%s/environ", e->d_name);
        f = fopen(p, "r"); if(!f){ ok = 0; break; }
        char env[8192]; size_t n = fread(env, 1, sizeof env - 1, f);
        int err = ferror(f); fclose(f); env[n] = 0;
        if(err || n == sizeof env - 1){ ok = 0; break; }
        int guarded = 0;
        for(size_t i = 0; i < n; i += strlen(env + i) + 1){
            if(strncmp(env + i, "PATH=", 5)) continue;
            char *path = env + i + 5, *sep = strchr(path, ':');
            if(sep) *sep = 0;
            guarded = path[0] == '/' && rmguard_dir_ok(path);
            break;
        }
        if(!guarded) ok = 0;
    }
    closedir(d);
    return found > 0 && ok;
}
static int usb_host_attached(void){
    glob_t g; int on = 0;
    if(glob("/sys/class/udc/*/state", 0, NULL, &g) != 0) return 0;
    for(size_t i = 0; i < g.gl_pathc && !on; i++){
        FILE *f = fopen(g.gl_pathv[i], "r"); char s[32] = "";
        if(!f) continue;
        if(fgets(s, sizeof s, f)) on = s[0] && strncmp(s, "not attached", 12) != 0;
        fclose(f);
    }
    globfree(&g);
    return on;
}
static int env_dac_gadget_present(void){ return access("/sys/kernel/config/usb_gadget/uac_demo", F_OK) == 0; }
static int env_playing(void){ return g_playing; }
static void env_scan_if_library_empty(void){ if(mdb_total_song_count() == 0 && !mdb_load_failed()) scanner_start(); }
static void env_art_cancel(void){ art_cancel(); }
static void env_card_returned(void){ albumwall_prewarm_seed(); }
void settings_restart_now(void);
static void env_restart(void){ settings_restart_now(); }
static void env_set_volume(int v){ ui_set_volume(v); }
static const struct srcswitch_env g_srcswitch_env = {
    .now = lv_tick_get, .media_mounted = coldplug_mounted, .exported_to_host = sd_exported_to_host,
    .host_confirmed = storage_host_confirmed, .host_attached = usb_host_attached,
    .dac_gadget_present = env_dac_gadget_present, .player_guarded = storage_player_guarded,
    .playing = env_playing, .scan_active = scanner_active, .scan_if_library_empty = env_scan_if_library_empty,
    .art_cancel = env_art_cancel, .card_returned = env_card_returned, .restart = env_restart,
    .toast = ui_toast, .set_volume = env_set_volume,
    .seek_to = ui_seek_to,
};
static void srcswitch_tick_cb(lv_timer_t *t){ (void)t; srcswitch_tick(); }


static int book_session_active(void);

static int g_mode_applied = -2;
static int send_play_mode(int mode){
    char m[16]; snprintf(m, sizeof m, "0102000C%04X", mode & 0xFFFF);
    int rc = ipc_send_cmd(m);
    g_mode_applied = (rc == 0 && mode >= 0 && mode <= 4) ? mode : -1;
    return rc;
}
int ui_play_mode_now(void){
    if(g_mode_applied >= 0) return g_mode_applied;
    if(book_session_active()) return -1;
    int wm = cfg_get_int("work_mode", -1);
    if(wm < 0) return sysconfig_get(SC_PLAY_MODE);
    if(g_mode_applied == -2 && wm <= 4) return wm;
    return -1;
}
static void reapply_audio_body(void);
static void reapply_sends(void);
static int g_reapply_tries;
static lv_timer_t *g_reapply_tmr;
static void reapply_retry_cb(lv_timer_t *t);
static void reapply_later(void){ g_reapply_tmr = lv_timer_create(reapply_retry_cb, 1500, NULL); if(g_reapply_tmr) lv_timer_set_repeat_count(g_reapply_tmr, 1); }
static void reapply_send_all(void){
    ipc_quiet_begin();
    reapply_sends();
    int fails = ipc_quiet_end();
    if(fails && g_reapply_tries < 3){ g_reapply_tries++; reapply_later(); }
    else g_reapply_tries = 0;
    if(fails) fprintf(stderr, "reapply: %d sends failed (try %d)\n", fails, g_reapply_tries);
}
void ui_reapply_audio(void){
    if(g_reapply_tmr){ lv_timer_delete(g_reapply_tmr); g_reapply_tmr = NULL; }
    g_reapply_tries = 0;
    reapply_audio_body();
    reapply_send_all();
}
static void reapply_retry_cb(lv_timer_t *t){
    (void)t; g_reapply_tmr = NULL;
    if(admit_check(ADMIT_OUTPUT_IDLE)){ reapply_later(); return; }
    reapply_send_all();
}
static void reapply_audio_body(void){
    g_mode_applied = -1;
    modes_output_reset();
}
static void reapply_sends(void){
    ui_set_dre(cfg_get_int("audio_dre",    1));
    ui_set_gain(cfg_get_int("audio_gain",  0));
    ui_set_dac_filter(cfg_get_int("audio_filter", 1));
    ui_set_replay_gain(cfg_get_int("replay_gain", 0));
    {
        int wm = cfg_get_int("work_mode", -1);
        if(book_session_active()) send_play_mode(4);
        else if(wm >= 0 && wm <= 4) ui_set_workmode(wm);
        int mp = cfg_get_int("memory_play", -1); if(mp >= 0)             ui_set_memory(mp);
        int gp = cfg_get_int("gapless", -1);     if(gp >= 0)             ui_set_gapless(gp);
        int mv = cfg_get_int("max_vol", -1);     if(mv >= 10)            ui_set_maxvol(mv);
        int bl = cfg_get_int("balance", -100);   if(bl >= -10 && bl<=10) ui_set_balance(bl);
    }
}
static void opus_title_tick(void){
    static opus_title_worker *worker;
    static int attempted, active;
    static uint64_t token=UINT64_MAX;
    ipc_title_event event; ipc_get_title_event(&event);
    if(!attempted){ worker=otw_create(NULL,NULL); attempted=1; }
    if(!worker){
        if(event.state.player_confirmed && !event.state.title_ready)
            ipc_publish_title(&event,event.player_title,1);
        return;
    }
    otw_library_commit(worker,ipc_library_epoch());
    for(unsigned polls=0;;polls++){
        track_state_t *s=&event.state;
        if(token!=s->playback_token){ otw_identity_end(worker); token=s->playback_token; active=0; }
        otw_player_generation(worker,s->player_generation);
        if(s->player_confirmed && (active || !s->title_ready)){
            active=1;
            unsigned flags=((s->is_cue_valid && s->is_sacd_valid) ||
                (s->is_cue_valid && s->is_cue) || (s->is_sacd_valid && s->is_sacd) ? OTW_KIND_KNOWN : 0) |
                (s->is_cue_valid && s->is_cue ? OTW_CUE : 0) |
                (s->is_sacd_valid && s->is_sacd ? OTW_SACD : 0);
            otw_identity id={s->player_generation,s->song_id_raw,s->pos_id_raw,s->song_id_valid,s->pos_id_valid};
            otw_submit(worker,id,s->path,event.player_title,flags);
        }
        if(polls==2) return;
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        otw_commit commit;
        if(!otw_poll(worker,(uint64_t)now.tv_sec*1000+now.tv_nsec/1000000,&commit)) return;
        int freeze=commit.result.outcome!=OPUS_TITLE_NOT_APPLICABLE;
        if(ipc_publish_title(&event,commit.result.title,freeze)){
            if(!freeze) active=0;
            return;
        }
        otw_reject_commit(worker);
    }
}
static uint32_t g_play_pending = 0;
#ifndef PLAY_PCM_GLOB
#define PLAY_PCM_GLOB "/proc/asound/card*/pcm*p/sub*/status"
#endif
static int play_pcm_running(void){
    glob_t paths;
    if(glob(PLAY_PCM_GLOB, 0, NULL, &paths) != 0){
        globfree(&paths);
        return 0;
    }
    int running = 0;
    for(size_t i = 0; i < paths.gl_pathc && i < 16 && !running; i++){
        char buf[512];
        int fd = open(paths.gl_pathv[i],
                      O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if(fd < 0) continue;
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if(n > 0){
            buf[n] = 0;
            running = strstr(buf, "state: RUNNING") != NULL;
        }
    }
    globfree(&paths);
    return running;
}

static struct {
    uint32_t request, at;
    unsigned serial, pos_seq, generation, transport;
    uint64_t token;
    long position, duration;
    int have_track, playing, provisional;
} g_play_pre;

static void play_request_arm(const track_state_t *cur, int was_playing){
    g_play_pending = lv_tick_get();
    if(!g_play_pending) g_play_pending = 1;

    g_play_pre.serial++;
    g_play_pre.request = g_play_pre.at = g_play_pending;
    g_play_pre.pos_seq = cur->pos_seq;
    g_play_pre.position = cur->position_ms;
    g_play_pre.generation = cur->player_generation;
    g_play_pre.transport = ipc_transport_seq();
    g_play_pre.token = cur->playback_token;
    g_play_pre.duration = cur->duration_ms;
    g_play_pre.have_track = cur->have_track;
    g_play_pre.playing = was_playing;
    g_play_pre.provisional = cur->provisional;
}

static int play_pcm_seen(uint32_t now, int fresh){
    static uint32_t at;
    static int valid, running;
    if(!valid || now - at >= 500 || (fresh && at != now)){
        running = play_pcm_running();
        at = now;
        valid = 1;
    }
    return running;
}

static int play_start_failed(const track_state_t *st){
    static uint32_t request, progress, pos_at, adv_at;
    static unsigned serial, pos_seq, generation, transport;
    static uint64_t token;
    static long position, duration;
    static int active, had_track, seed, advances;
    static int attrib, started, possible, may_start, seen;

    uint32_t now = lv_tick_get();
    if(!g_play_pending){
        active = 0;
        return 0;
    }
    if(!active || request != g_play_pending ||
       serial != g_play_pre.serial){
        active = 1;
        request = g_play_pending;
        serial = g_play_pre.serial;
        progress = request;
        advances = attrib = started = possible = seen = 0;
        may_start = seed = 0;

        if(g_play_pre.request == request){
            pos_at = g_play_pre.at;
            pos_seq = g_play_pre.pos_seq;
            position = g_play_pre.position;
            generation = g_play_pre.generation;
            transport = g_play_pre.transport;
            token = g_play_pre.token;
            duration = g_play_pre.duration;
            had_track = g_play_pre.have_track;
            seed = g_play_pre.provisional;
            may_start = !g_play_pre.playing &&
                        (!had_track || position <= 1500);
        } else {
            pos_at = now;
            pos_seq = st->pos_seq;
            position = st->position_ms;
            generation = st->player_generation;
            transport = ipc_transport_seq();
            token = st->playback_token;
            duration = st->duration_ms;
            had_track = st->have_track;
        }
    }

    if(generation != st->player_generation ||
       transport != ipc_transport_seq()){
        g_play_pending = 0;
        active = 0;
        return 0;
    }

    if(seed && st->player_confirmed){
        seed = 0;
        attrib = 0;
        advances = 0;
        progress = now;
        token = st->playback_token;
        duration = st->duration_ms;
        had_track = st->have_track;
        pos_seq = st->pos_seq;
        position = st->position_ms;
        pos_at = now;
    } else if(token != st->playback_token ||
              had_track != st->have_track){
        progress = now;
        advances = 0;
        attrib = !seed && st->have_track &&
                 st->player_confirmed && !st->provisional;
        token = st->playback_token;
        duration = st->duration_ms;
        had_track = st->have_track;
        pos_seq = st->pos_seq;
        position = st->position_ms;
        pos_at = now;
    } else if(duration != st->duration_ms){
        progress = now;
        advances = 0;
        duration = st->duration_ms;
        pos_seq = st->pos_seq;
        position = st->position_ms;
        pos_at = now;
    }

    if(st->pos_seq != pos_seq){
        long long delta = (long long)st->position_ms - position;
        uint32_t gap = now - pos_at;
        long long slack = (long long)gap + 1500;
        int fresh = st->have_track && st->player_confirmed &&
                    !st->provisional &&
                    (int32_t)(st->pos_seq - st->path_seq) > 0;
        int wrap = delta < 0 && duration > 0 &&
                   position >= 0 && position <= duration &&
                   st->position_ms >= 0 &&
                   st->position_ms <= duration &&
                   (long long)duration - position +
                       st->position_ms <= slack;

        if(fresh && delta != 0) progress = now;
        if(fresh && ((delta > 0 && delta <= slack) || wrap)){
            advances++;
            seen = 1;
            adv_at = now;
            if(attrib) started = 1;
            else if(may_start) possible = 1;
        } else {
            advances = 0;
            if(fresh && !wrap && delta < -1500)
                attrib = 1;
        }

        pos_seq = st->pos_seq;
        position = st->position_ms;
        pos_at = now;
        if(advances >= 2 && (attrib || possible)){
            g_play_pending = 0;
            if(attrib){
                void system_ota_note_playback(void);
                system_ota_note_playback();
            }
            active = 0;
            return 0;
        }
    }

    uint32_t age = now - request;
    if(age < 45000 && (age < 15000 || now - progress < 6000))
        return 0;

    if(started || possible){
        g_play_pending = 0;
        active = 0;
        return 0;
    }

    int final = age >= 45000;
    int out = seen && now - adv_at < 6000;
    if(!out) out = play_pcm_seen(now, final);
    if(!out && !final) out = play_pcm_seen(now, 1);
    if(out){
        if(final){
            g_play_pending = 0;
            active = 0;
        }
        return 0;
    }
    return 1;
}
void ui_note_transport_sent(void){ session_note_transport_sent(ipc_generation(), lv_tick_get()); }
static int startup_player_old(void){
    long pid, hz = sysconf(_SC_CLK_TCK);
    unsigned long long start;
    if(hz <= 0 || route_player_identity(&pid, &start) < 0) return 0;
    char path[sizeof ROUTE_PROC_ROOT + 16];
    if(snprintf(path, sizeof path, "%s/uptime", ROUTE_PROC_ROOT) >= (int)sizeof path) return 0;
    FILE *f = fopen(path, "r");
    if(!f) return 0;
    double uptime = 0;
    int ok = fscanf(f, "%lf", &uptime) == 1;
    fclose(f);
    return ok && uptime >= 0 && uptime - (double)start / (double)hz >= 12.0;
}
static int startup_saved_bt_route(void){
    int dev = sysconfig_get_via(ui_run_cap_bounded, SC_OUT_DEV, 250);
    if(dev == 2) return 1;
    if(dev == 6 || dev == 4) return 0;
    return -1;
}
static int startup_connected_sink(const char *mac){
    if(cfg_get_int("bt_on", 0) != 1) return 0;
    char buf[2048] = {0};
    ui_run_cap_bounded("if ! pidof bluealsa >/dev/null 2>&1; then echo DISKOS_NO_BLUEALSA; "
                       "else bluealsa-cli list-pcms 2>/dev/null && printf '\\nDISKOS_LIST_DONE\\n'; fi",
                       buf, sizeof buf, 250);
    if(strstr(buf, "DISKOS_NO_BLUEALSA\n")) return 0;
    if(!strstr(buf, "DISKOS_LIST_DONE\n")) return -1;
    if(!mac || !mac[0]) return strstr(buf, "a2dpsrc") != NULL;
    char needle[32]; snprintf(needle, sizeof needle, "dev_%s/a2dpsrc", mac);
    for(char *p=needle; *p; p++) if(*p == ':') *p = '_';
    route_uppercase(needle, needle, sizeof needle);
    route_uppercase(buf, buf, sizeof buf);
    return strstr(buf, needle) != NULL;
}
static void boot_autoplay_pause_cb(lv_timer_t *t){
    static uint32_t t0;
    if(!t0) t0 = lv_tick_get() ? lv_tick_get() : 1;
    if(ipc_transport_seq() != 0 || lv_tick_elaps(t0) > 60000){ lv_timer_del(t); return; }
    if(admit_check(ADMIT_OUTPUT_IDLE) || !ui_is_playing()) return;
    if(ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) == 0){ fprintf(stderr, "boot: paused the player's resume autoplay\n"); fflush(stderr); }
    lv_timer_del(t);
}
static int startup_admitted(void){ return admit_check(ADMIT_LOCAL_INIT) == NULL; }
static int startup_play_starting(void){ return g_play_pending || book_session_active(); }
static int startup_playing(void){ return g_playing; }
static int startup_have_track(void){ track_state_t st; ipc_get_state(&st); return st.have_track; }
static int startup_route_analog(void){ return route_analog(1); }
static const struct session_startup_env g_startup_env = {
    .admitted = startup_admitted, .rx_frames = ipc_rx_frames, .play_starting = startup_play_starting,
    .playing = startup_playing, .have_track = startup_have_track, .fw_needs_init = fw_needs_localplayer_init,
    .player_old = startup_player_old, .saved_bt_route = startup_saved_bt_route,
    .connected_sink = startup_connected_sink, .route_analog = startup_route_analog,
};
static void localplayer_workmode_cb(lv_timer_t *t){
    switch(session_startup_tick(lv_tick_get())){
    case SESSION_STARTUP_PROBE: ipc_send_probe("02020008"); break;
    case SESSION_STARTUP_DONE:  lv_timer_del(t); break;
    case SESSION_STARTUP_WAIT:  break;
    }
}
static int g_books_migrated = 0;
static int books_ensure_migrated(void){
    if(g_books_migrated) return 1;
    if(mdb_migrate_books()){ g_books_migrated = 1; return 1; }
    return 0;
}
static void migrate_books_retry_cb(lv_timer_t *t){
    if(books_ensure_migrated()) lv_timer_del(t);
}
static void playlist_port_cb(lv_timer_t *t){
    static int tries;
    if(mdb_playlist_sync_registry() == 2 || ++tries >= 60) lv_timer_del(t);
}
static void scanner_poll(lv_timer_t *t){
    (void)t;
    if(scanner_take_finished()){
        int oc = scanner_outcome();
        if(scanner_take_committed() || mdb_load_failed()){
            mdb_load();
            library_ensure_capacity();
            library_refresh();
            albumwall_prewarm_seed();
        }
        int done=0, total=0; scanner_progress(&done, &total);
        int skipped = scanner_skipped();
        int unsup = scanner_unsupported();
        char b[80];
        if(mdb_load_failed()) snprintf(b, sizeof b, "Library busy - reopen to refresh");
        else if(oc == SCAN_OUT_CANCELLED) snprintf(b, sizeof b, "Scan cancelled");
        else if(oc == SCAN_OUT_NO_SD)  snprintf(b, sizeof b, "Insert an SD card to scan");
        else if(oc == SCAN_OUT_FAILED) snprintf(b, sizeof b, "Scan failed - library kept");
        else if(total>0 && skipped>0) snprintf(b, sizeof b, "Scanned %d song%s; %d files skipped", total, total==1?"":"s", skipped);
        else if(total>0 && unsup>0)
                             snprintf(b, sizeof b, "Scanned %d song%s (%d unsupported)", total, total==1?"":"s", unsup);
        else if(total>0)     snprintf(b, sizeof b, "Scanned %d song%s", total, total==1?"":"s");
        else if(done>0)      snprintf(b, sizeof b, "Scan failed - library kept");
        else if(skipped>0)   snprintf(b, sizeof b, "Couldn't read %d files - library kept", skipped);
        else if(unsup>0)     snprintf(b, sizeof b, "No music found (%d unsupported file%s)", unsup, unsup==1?"":"s");
        else                 snprintf(b, sizeof b, "No music found");
        ui_toast(b);
    }
}
int ui_is_playing(void){ return g_playing; }
static int env_play_pending(void){ return g_play_pending != 0; }
static int env_book_active(void){ return book_session_active(); }
static const struct admit_env g_admit_env = {
    .now = lv_tick_get, .local_playback_allowed = ui_local_playback_allowed, .output_busy = modes_output_busy,
    .play_pending = env_play_pending, .book_active = env_book_active, .playing = env_playing,
    .fw_needs_local_init = fw_needs_localplayer_init,
};

int ui_set_volume(int vol){
    if(vol < 0) vol = 0; if(vol > VOL_MAX) vol = VOL_MAX;
    { int mv = cfg_get_int("max_vol", -1); if(mv >= 10 && vol > mv) vol = mv; }
    char f[16]; snprintf(f, sizeof f, "0715000C%04X", vol);
    int rc = ipc_send_cmd(f);
    fprintf(stderr,"set volume %d -> %s (rc=%d)\n", vol, f, rc); fflush(stderr);
    return rc;
}
void ui_disarm_book_eoc(void);
static int g_book_single_mode = 0;
static int ui_play_list_ex(int list_type, const char *name, int pos1, int build_idx){
    { const char *why = admit_check(ADMIT_PLAYER_COMMAND); if(why){ ui_toast(why); return 0; } }
    if(list_type != 5 && !books_ensure_migrated()){ ui_toast("Library is finishing setup - try again"); return 0; }
    ui_cancel_book_resume();
    ui_disarm_book_eoc();
    booksess_hold_off_adopt();
    if(list_type != 5 && g_book_single_mode){
        send_play_mode(cfg_get_int("work_mode", 0));
        g_book_single_mode = 0;
    }
    if(pos1 < 1) pos1 = 1;
    if(!name) name = "";
    if(strlen(name) >= 380){ ui_toast("Couldn't play that list"); return 0; }
    char f[420];
    int sent = 0;
    ui_note_transport_sent();
    (void)admit_play_start(ADMIT_PLAY_LIST);
    {
        int datalen = 8 + (int)strlen(name);
        snprintf(f, sizeof f, "0100%04X%04X%04X%s",
                 8 + datalen, (build_idx >= 0 ? build_idx : pos1-1) & 0xFFFF, list_type & 0xFFFF, name);
        track_state_t cur; ipc_get_state(&cur);
        if(ipc_send_cmd(f) == 0){
            sent = 1;
            admit_note_play_sent();
            fprintf(stderr,"play(build) type=%d pos=%d name='%s' -> %s\n", list_type, pos1, name, f);
            play_request_arm(&cur, g_playing);
            ui_toast("Starting...");
        } else {
            ui_toast("Couldn't start playback");
        }
    }
    fflush(stderr);
    return sent;
}
int ui_play_list(int list_type, const char *name, int pos1){ return ui_play_list_ex(list_type, name, pos1, -1); }
int ui_play_favorite(int love_id, int pos1){
    if(love_id <= 0 || pos1 < 1){ ui_toast("Couldn't find that song"); return 0; }
    char lpath[256];
    if(!mdb_love_path(love_id, lpath, sizeof lpath)){ ui_toast("Couldn't find that song"); return 0; }
    return ui_play_list_ex(6, "", pos1, love_id);
}
int ui_play_playlist(long pid, int pos){
    int n = mdb_reserved_slot_set_playlist(pid);
    if(n <= 0){ ui_toast("Playlist is empty"); return 0; }
    g_book_single_mode = 0;
    send_play_mode(cfg_get_int("work_mode", 0));
    (void)admit_play_start(ADMIT_PLAY_RESERVED_SLOT);
    if(pos < 1) pos = 1;
    int ppos = mdb_reserved_slot_player_pos(pid, pos);
    if(ppos < 1){ ui_toast("Couldn't find that song"); return 0; }
    pos = ppos;
    return ui_play_list(5, "", pos);
}
int ui_queue_jump(int ord1){
    { const char *why = admit_check(ADMIT_PLAYER_COMMAND); if(why){ ui_toast(why); return -1; } }
    if(g_play_pending){ ui_toast("Queue is updating - try again"); return -1; }
    if(ord1 < 1 || ord1 > 0xFFFF) return -1;
    ui_cancel_book_resume();
    ui_disarm_book_eoc();
    (void)admit_play_start(ADMIT_PLAY_LIST);
    ui_note_transport_sent();
    char f[24]; snprintf(f, sizeof f, "0100%04X%04X0000", 16, (ord1 - 1) & 0xFFFF);
    int rc = ipc_send_cmd(f);
    if(rc == 0) admit_note_play_sent();
    fprintf(stderr, "queue jump pos=%d -> %s (rc=%d)\n", ord1, f, rc); fflush(stderr);
    return rc;
}
int ui_play_plan(mdb_plan_t *plan, int song_id){
    if(!plan || !plan->ids || plan->count <= 0){ ui_toast("Couldn't play that list"); return 0; }
    if(plan->list_type == 5){
        if(!mdb_plan_materialize(plan)){ ui_toast("Couldn't play that list"); return 0; }
        int pos = mdb_plan_pos(plan, song_id);
        if(pos < 1){ ui_toast("Couldn't find that song"); return 0; }
        g_book_single_mode = 0;
        send_play_mode(cfg_get_int("work_mode", 0));
        (void)admit_play_start(ADMIT_PLAY_RESERVED_SLOT);
        return ui_play_list_ex(5, "", pos, -1);
    }
    if(plan->list_type != 2 && plan->list_type != 3 && plan->list_type != 7){ ui_toast("Couldn't play that list"); return 0; }
    int pos = mdb_plan_pos(plan, song_id);
    if(pos < 1){ ui_toast("Couldn't find that song"); return 0; }
    return ui_play_list_ex(plan->list_type, plan->name, pos, -1);
}
void ui_set_favorite(int on){ ipc_send_cmd(on ? "0104000C0001" : "0104000C0000"); }
static int on_song_play(int id){
    int lt = 0; char name[256] = "";
    if(!library_drill_context(&lt, name, sizeof name)){
        lt = 1; name[0] = 0;
    }
    if(lt == 2 && mdb_artist_mode()){
        mdb_plan_t plan; int ok = 0;
        if(mdb_artist_plan(name, &plan)){ ok = ui_play_plan(&plan, id); mdb_plan_free(&plan); }
        else ui_toast("Couldn't play that song");
        return ok;
    }
    int pos = mdb_play_pos(id, lt, name);
    if(pos < 1 && lt != 1){
        lt = 1; name[0] = 0;
        pos = mdb_play_pos(id, 1, "");
    }
    if(pos < 1){ ui_toast("Couldn't find that song"); return 0; }
    return ui_play_list(lt, name, pos);
}
int ui_play_song_by_path(const char *path){
    if(!path || !*path) return 0;
    int id = mdb_song_id_by_path(path);
    if(id <= 0){ ui_toast("Not in library"); return 0; }
    int pos = mdb_play_pos(id, 1, "");
    if(pos < 1){ ui_toast("Couldn't find that song"); return 0; }
    return ui_play_list(1, "", pos);
}

static int book_session_active(void){ return ui_book_active(); }

int ui_play_book(const char *path, long resume_ms){
    { const char *why = admit_check(ADMIT_PLAYER_COMMAND); if(why){ ui_toast(why); return 0; } }
    if(!path || !*path) return 0;
    if(strlen(path) >= BOOKSESS_PATH_MAX - 4){ ui_toast("Path too long"); return 0; }
    if(!mdb_reserved_slot_set(path)){ ui_toast("Couldn't start book"); return 0; }
    (void)admit_play_start(ADMIT_PLAY_RESERVED_SLOT);
    if(send_play_mode(4) != 0){ ui_toast("Couldn't start book"); return 0; }
    g_book_single_mode = 1;
    if(!ui_play_list(5, "", 1)) return 0;
    g_play_pending = 0;
    booksess_begin(path, resume_ms);
    return 1;
}

static const char *book_env_admit(void){ return admit_check(ADMIT_LOCAL_PLAYBACK); }
static void book_env_get_state(track_state_t *out){ ipc_get_state(out); }
static int book_env_playpause(void){ return ipc_send_cmd(PLAYER_CMD_PLAYPAUSE); }
static int book_env_progress(const char *key, long *position_ms){ return mdb_book_progress(key, NULL, 0, position_ms, NULL, NULL); }
static int book_env_save(const char *key, long position_ms){ return mdb_book_save(key, key, position_ms, 0); }
static void book_env_adopted(void){ g_book_single_mode = 1; send_play_mode(4); }
static int book_env_sleep_action(void){ return cfg_get_int("sleep_action", POWER_SLEEP_PAUSE); }
static void book_env_sleep_ended(void){ cfg_set_int("sleep_idx", 0); }
static void book_env_shutdown(void){ power_shutdown_request(); }
static const struct booksess_env g_booksess_env = {
    .now = lv_tick_get, .admit = book_env_admit, .get_state = book_env_get_state, .playing = ui_is_playing,
    .seek_to = ui_seek_to, .playpause = book_env_playpause, .is_book_path = mdb_is_book_path,
    .book_progress = book_env_progress, .book_save = book_env_save, .adopted = book_env_adopted,
    .sleep_action = book_env_sleep_action, .sleep_ended = book_env_sleep_ended, .shutdown = book_env_shutdown,
};
static void book_tick(lv_timer_t *t){ (void)t; booksess_tick(); }

static int on_search_play(int id){
    int pos = mdb_play_pos(id, 1, "");
    if(pos < 1){ ui_toast("Couldn't find that song"); return 0; }
    return ui_play_list(1, "", pos);
}
void ui_set_workmode(int mode){
    if(mode < 0 || mode > 4) mode = 0;
    cfg_set_int("work_mode", mode);
    int rc = send_play_mode(mode);
    fprintf(stderr,"workmode %d -> 0102000C%04X (rc=%d)\n", mode, mode, rc); fflush(stderr);
}

static char g_app_exec[256];
static int  g_app_pending = 0;
void app_launch(const char *exec){ snprintf(g_app_exec, sizeof g_app_exec, "%s", exec); g_app_pending = 1; }
static volatile uint32_t *gpio_gpb_pin(void){
    static volatile uint32_t *pin = NULL;
    static int tried = 0;
    if(!tried){
        tried = 1;
        int mem = open("/dev/mem", O_RDONLY | O_SYNC);
        if(mem >= 0){
            void *map = mmap(NULL, 4096, PROT_READ, MAP_SHARED, mem, 0x10010000);
            if(map != MAP_FAILED) pin = (volatile uint32_t *)((char *)map + 0x100);
            close(mem);
        }
    }
    return pin;
}
static int volup_held(void){
    volatile uint32_t *pin = gpio_gpb_pin();
    return pin ? (((*pin >> 13) & 1u) ? 0 : 1) : 0;
}
static void app_run(const char *exec){
    if(access(exec, X_OK) != 0){
        lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL);
        ui_toast("Can't launch app");
        return;
    }
    if(!gpio_gpb_pin()) ui_toast("Note: hold-to-close is unavailable");
    int failed = 0, killed = 0;
    pid_t pid = -1;
    char *argv[] = { (char *)exec, NULL };
    if(command_spawn(&pid, argv) != 0) pid = -1;
    if(pid > 0){
        int status = 0;
        int grouped = 1;
        struct timespec t0 = {0,0}; int holding = 0;
        for(;;){
            pid_t w = waitpid(pid, &status, WNOHANG);
            if(w == pid) break;
            if(w < 0 && errno != EINTR){ failed = 1; break; }
            if(volup_held()){
                struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
                if(!holding){ holding = 1; t0 = now; }
                else if(now.tv_sec - t0.tv_sec >= 3){
                    if(grouped) kill(-pid, SIGKILL);
                    kill(pid, SIGKILL);
                    for(int k = 0; k < 20; k++){
                        if(waitpid(pid, &status, WNOHANG) == pid) break;
                        usleep(100000);
                    }
                    killed = 1; break;
                }
            } else holding = 0;
            usleep(200000);
        }
        if(!failed && !killed && WIFEXITED(status) && WEXITSTATUS(status) == 127) failed = 1;
    } else {
        failed = 1;
    }
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    if(killed)      ui_toast("App force-closed");
    else if(failed) ui_toast("Launch failed");
}

static void state_sync_retry_cb(lv_timer_t *t){
    (void)t;
    ipc_send_probe("02020008");
}

static void advance_probe_tick(int visible){
    static uint64_t token;
    static unsigned transport;
    static unsigned pos_seq;
    static uint32_t started, sent, pos_at;
    static long position;
    static int active, used, have_pos, was_visible = 1;
    uint32_t now = lv_tick_get();
    unsigned tx = ipc_transport_seq();
    track_state_t s; ipc_get_state(&s);
    int wake = visible && !was_visible;
    was_visible = visible;
    int changed = s.playback_token != token;
    if(changed){
        token = s.playback_token; active = used = have_pos = 0; pos_seq = 0;
    }
    if(!changed && tx != transport){
        if(active){ active = used = 0; }
        have_pos = 0; pos_seq = s.pos_seq;
    }
    if(g_play_pending || book_session_active()){
        active = 0; used = 1;
    }
    transport = tx;
    int forward = 0;
    if(s.pos_seq != pos_seq && (int32_t)(s.pos_seq - s.path_seq) > 0){
        forward = have_pos && (uint32_t)(now - pos_at) <= 3000 && s.position_ms > position;
        if(have_pos && s.position_ms < position) active = 0;
        position = s.position_ms; pos_at = now; pos_seq = s.pos_seq; have_pos = 1;
    }
    if(active && (uint32_t)(now - started) >= 60000) active = 0;
    if(!visible) return;
    if(wake){ ipc_send_probe("02020008"); sent = now; }
    if(!s.have_track || !s.player_confirmed || s.provisional){ active = 0; return; }
    if(!used && forward && s.duration_ms > 1000 && s.position_ms >= s.duration_ms - 1000){
        used = active = 1; started = now;
        if(!wake){ ipc_send_probe("02020008"); sent = now; }
    }
    if(active && (uint32_t)(now - sent) >= 2000){
        ipc_send_probe("02020008"); sent = now;
    }
}

static int run_bounded(char *const argv[], int timeout_ms);

static void ipc_connect_retry_cb(lv_timer_t *t){
    if(ipc_start()==0){
        fprintf(stderr,"ipc connected (deferred)\n"); fflush(stderr);
        ipc_send_probe("02020008");
        lv_timer_del(t);
    }
}

static void ipc_health_cb(lv_timer_t *t){
    (void)t;
    if(!ipc_is_ready()) ipc_start();
    else ipc_health_check();
}

static void crash_log(int sig, siginfo_t *si, void *ucv){
    const char *m = "=== diskos CAUGHT FATAL SIGNAL ";
    int fd = open("/usr/data/diskos_boot.log", O_WRONLY|O_APPEND);
    if(fd>=0){ (void)si; (void)ucv;
        char b[8]; int n=sig, i=0; b[i++]=' ';
        if(n>=10){ b[i++]='0'+(n/10); } b[i++]='0'+(n%10); b[i++]='\n';
        write(fd,m,30); write(fd,b,i); close(fd); }
    signal(sig, SIG_DFL); raise(sig);
}

static volatile sig_atomic_t g_bounded_pgid = 0;
static volatile sig_atomic_t g_bounded_pid = 0;

static void boot_alarm_handler(int sig){
    (void)sig;
    if(g_bounded_pgid > 0) kill(-(pid_t)g_bounded_pgid, SIGKILL);
    if(g_bounded_pid > 0) kill((pid_t)g_bounded_pid, SIGKILL);
    signal(SIGALRM, SIG_DFL);
    { sigset_t s; sigemptyset(&s); sigaddset(&s, SIGALRM); sigprocmask(SIG_UNBLOCK, &s, NULL); }
    alarm(5);
    static const char m[] = "=== diskos BOOT WATCHDOG timeout -> exit for respawn ===\n";
    (void)write(2, m, sizeof m - 1);
    _exit(124);
}

#define DEFER_REAP_MAX 16
static pid_t g_defer_reap[DEFER_REAP_MAX];
static void defer_reap_sweep(void){
    for(int i = 0; i < DEFER_REAP_MAX; i++){
        if(g_defer_reap[i] > 0){
            pid_t w = waitpid(g_defer_reap[i], NULL, WNOHANG);
            if(w == g_defer_reap[i] || (w < 0 && errno == ECHILD)) g_defer_reap[i] = 0;
        }
    }
}
static int defer_reap_has_slot(void){
    for(int i = 0; i < DEFER_REAP_MAX; i++) if(g_defer_reap[i] == 0) return 1;
    return 0;
}
static void defer_reap_add(pid_t p){
    for(int i = 0; i < DEFER_REAP_MAX; i++) if(g_defer_reap[i] == 0){ g_defer_reap[i] = p; return; }
}

static int run_bounded(char *const argv[], int timeout_ms){
    defer_reap_sweep();
    if(!defer_reap_has_slot()) return -1;
    sigset_t alrm, prev;
    sigemptyset(&alrm); sigaddset(&alrm, SIGALRM);
    sigprocmask(SIG_BLOCK, &alrm, &prev);
    pid_t pid;
    int spawn_rc=command_spawn(&pid,argv);
    if(spawn_rc){
        sigprocmask(SIG_SETMASK,&prev,NULL);
        fprintf(stderr,"command spawn failed: %s (%s)\n",argv[0],strerror(spawn_rc));
        return -1;
    }
    int grouped=1;
    g_bounded_pid = pid;
    g_bounded_pgid = grouped ? pid : 0;
    sigprocmask(SIG_SETMASK, &prev, NULL);
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = -1;
    for(;;){
        int status = 0;
        sigprocmask(SIG_BLOCK, &alrm, NULL);
        pid_t w = waitpid(pid, &status, WNOHANG);
        if(w == pid){
            g_bounded_pid = 0; g_bounded_pgid = 0;
            sigprocmask(SIG_SETMASK, &prev, NULL);
            rc = (WIFEXITED(status) && WEXITSTATUS(status)==0) ? 0 : -1; break;
        }
        sigprocmask(SIG_SETMASK, &prev, NULL);
        if(w < 0 && errno != EINTR && errno != ECHILD){ rc = -1; break; }
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (now.tv_sec - t0.tv_sec)*1000 + (now.tv_nsec - t0.tv_nsec)/1000000;
        if(ms >= timeout_ms){
            if(grouped) kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            int reaped = 0;
            sigprocmask(SIG_BLOCK, &alrm, NULL);
            for(int k = 0; k < 100; k++){ if(waitpid(pid, &status, WNOHANG) == pid){ reaped = 1; break; } usleep(10000); }
            if(!reaped) defer_reap_add(pid);
            g_bounded_pid = 0; g_bounded_pgid = 0;
            sigprocmask(SIG_SETMASK, &prev, NULL);
            rc = -1; break;
        }
        usleep(10000);
    }
    sigprocmask(SIG_BLOCK, &alrm, NULL);
    g_bounded_pid = 0; g_bounded_pgid = 0;
    sigprocmask(SIG_SETMASK, &prev, NULL);
    return rc;
}

static lv_timer_t *g_t_art, *g_t_lyr, *g_t_wx, *g_t_status;
static void polls_set_paused(int paused){
    if(!g_t_art) return;
    if(paused){ lv_timer_pause(g_t_art);  lv_timer_pause(g_t_lyr);  lv_timer_pause(g_t_wx);  }
    else      { lv_timer_resume(g_t_art); lv_timer_resume(g_t_lyr); lv_timer_resume(g_t_wx); }
}

#define RTC_SAVE_TIMEOUT_MS 3000
static pid_t g_rtc_pid;
static int g_rtc_grouped;
static uint32_t g_rtc_t0;
static void rtc_save_poll(lv_timer_t *t){
    int status = 0;
    pid_t w = waitpid(g_rtc_pid, &status, WNOHANG);
    if(w == g_rtc_pid || (w < 0 && errno == ECHILD)){ g_rtc_pid = 0; lv_timer_del(t); return; }
    if(lv_tick_elaps(g_rtc_t0) < RTC_SAVE_TIMEOUT_MS) return;
    if(g_rtc_grouped) kill(-g_rtc_pid, SIGKILL);
    kill(g_rtc_pid, SIGKILL);
    if(waitpid(g_rtc_pid, &status, WNOHANG) != g_rtc_pid) defer_reap_add(g_rtc_pid);
    fprintf(stderr, "rtc save: timed out after %d ms, killed\n", RTC_SAVE_TIMEOUT_MS);
    g_rtc_pid = 0; lv_timer_del(t);
}
static void hwclock_save_tick(lv_timer_t *t){
    (void)t;
    if(g_rtc_pid) return;
    defer_reap_sweep();
    if(!defer_reap_has_slot()) return;
    pid_t pid = 0;
    char *a[] = { "hwclock", "-w", NULL };
    if(command_spawn(&pid, a) != 0 || pid <= 0) return;
    g_rtc_grouped = (setpgid(pid, pid) == 0 || (errno == EACCES && getpgid(pid) == pid));
    g_rtc_pid = pid;
    g_rtc_t0 = lv_tick_get();
    if(!lv_timer_create(rtc_save_poll, 100, NULL)){
        if(g_rtc_grouped) kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        if(waitpid(pid, NULL, WNOHANG) != pid) defer_reap_add(pid);
        g_rtc_pid = 0;
    }
}

void ui_rtc_save_now(void){ hwclock_save_tick(NULL); }

#define TIME_FILE        "/usr/data/diskos_time"
#define RTC_DEV          "/dev/rtc0"
#define TK_TICK_MS       30000
#define TK_SAVE_EVERY_S  600
#define TK_STEP_S        5
static time_t g_tk_wall; static long g_tk_mono, g_tk_save_mono = -1;
static long mono_s(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (long)ts.tv_sec; }
void ui_timekeep_save_now(void){
    if(timekeep_save(TIME_FILE, time(NULL)) == 0) g_tk_save_mono = mono_s();
}
static void timekeep_tick(lv_timer_t *t){
    (void)t;
    time_t now = time(NULL); long m = mono_s();
    long step = (long)(now - g_tk_wall) - (m - g_tk_mono);
    g_tk_wall = now; g_tk_mono = m;
    int stepped = step > TK_STEP_S || step < -TK_STEP_S;
    if(stepped){ fprintf(stderr, "clock: stepped by %ld s (now %ld)\n", step, (long)now); fflush(stderr); }
    if(stepped || g_tk_save_mono < 0 || m - g_tk_save_mono >= TK_SAVE_EVERY_S) ui_timekeep_save_now();
}
static void timekeep_boot(void){
    time_t now = time(NULL), rtc = timekeep_rtc_epoch(RTC_DEV);
    if(rtc && (rtc > now ? rtc - now : now - rtc) > TIMEKEEP_TOL_S){
        struct timeval tv = { .tv_sec = rtc, .tv_usec = 0 };
        fprintf(stderr, "clock: system %ld vs RTC %ld, following the RTC\n", (long)now, (long)rtc); fflush(stderr);
        if(settimeofday(&tv, NULL) == 0) now = rtc;
    }
    time_t saved = timekeep_load(TIME_FILE), fix = timekeep_pick(now, saved);
    if(fix){
        struct timeval tv = { .tv_sec = fix, .tv_usec = 0 };
        fprintf(stderr, "clock: %ld is behind the saved %ld, restoring the saved time\n", (long)now, (long)saved); fflush(stderr);
        if(settimeofday(&tv, NULL) == 0) ui_rtc_save_now();
    } else {
        fprintf(stderr, "clock: %ld ok (saved %ld)\n", (long)now, (long)saved); fflush(stderr);
    }
    g_tk_wall = time(NULL); g_tk_mono = mono_s();
    lv_timer_create(timekeep_tick, TK_TICK_MS, NULL);
    ui_timekeep_save_now();
}

int ui_run_bounded(char *const argv[], int timeout_ms){ return run_bounded(argv, timeout_ms); }

#define RIM_R_MIN       146
#define RIM_R_MAX       182
#define RIM_REL_R_MIN   122
#define RIM_COMMIT_TANG 22.0f
#define RIM_PX_PER_RAD  180.0f
#define RIM_ALB_PER_RAD 1.5f
#define RIM_MAX_OVER    34
enum { RIM_IDLE, RIM_CAND, RIM_OWN };
static int       rim_state = RIM_IDLE;
static float     rim_last_ang = 0;
static int       rim_sx = 0, rim_sy = 0;
static float     rim_accum = 0;
static uint32_t  rim_last_ms = 0;
static lv_obj_t *rim_scroller = NULL;
static int       rim_cover = 0;

static lv_obj_t *ui_active_scroller(void){
    switch(screen_current()){
        case SCR_LIBRARY: return library_scroller();
        case SCR_PLVIEW:  return playlistview_scroller();
        case SCR_SEARCH:  return search_scroller();
        case SCR_APPS:    return apps_scroller();
        default:          return NULL;
    }
}
static float rim_ang(int x, int y){ return atan2f((float)(y-180), (float)(x-180)); }
static float rim_wrap(float d){ if(d>(float)M_PI) d-=2.0f*(float)M_PI; else if(d<-(float)M_PI) d+=2.0f*(float)M_PI; return d; }

static void rim_press(int x, int y){
    rim_state = RIM_IDLE; rim_scroller = NULL; rim_cover = 0;
    int dx=x-180, dy=y-180; float r=sqrtf((float)(dx*dx+dy*dy));
    if(r < RIM_R_MIN || r > RIM_R_MAX) return;
    if(screen_current()==SCR_ALBUMWALL){
        rim_cover=1; rim_sx=x; rim_sy=y; rim_last_ang=rim_ang(x,y);
        rim_accum=0; rim_last_ms=lv_tick_get(); rim_state=RIM_CAND; return;
    }
    lv_obj_t *sc = ui_active_scroller();
    if(!sc) return;
    lv_anim_delete(sc, NULL);
    rim_scroller=sc; rim_sx=x; rim_sy=y; rim_last_ang=rim_ang(x,y);
    rim_accum=0; rim_last_ms=lv_tick_get(); rim_state=RIM_CAND;
}
static int rim_move(int x, int y){
    if(rim_state==RIM_IDLE) return 0;
    int rdx=x-180, rdy=y-180; float r=sqrtf((float)(rdx*rdx+rdy*rdy)); if(r<1) r=1;
    if(rim_state==RIM_CAND){
        int dx=x-rim_sx, dy=y-rim_sy;
        float radial = (dx*rdx + dy*rdy)/r;
        float tang   = fabsf((float)dx*rdy - (float)dy*rdx)/r;
        if(tang < RIM_COMMIT_TANG || tang < fabsf(radial)) return 0;
        if(r < RIM_REL_R_MIN){ rim_state=RIM_IDLE; rim_scroller=NULL; return 0; }
        lv_indev_wait_release(g_touch);
        if(rim_scroller && rim_scroller == library_scroller()) library_jump_cancel();
        if(rim_scroller) lv_obj_scroll_to_y(rim_scroller, lv_obj_get_scroll_y(rim_scroller), LV_ANIM_OFF);
        rim_state=RIM_OWN; rim_last_ang=rim_ang(x,y); rim_last_ms=lv_tick_get(); rim_accum=0;
        return 1;
    }
    if(rim_cover){
        if(screen_current()!=SCR_ALBUMWALL) return 1;
        if(r < RIM_REL_R_MIN) return 1;
        float ang=rim_ang(x,y); float dAng=rim_wrap(ang-rim_last_ang); rim_last_ang=ang; rim_last_ms=lv_tick_get();
        if(dAng > 1.2f || dAng < -1.2f) return 1;
        albumwall_scroll_rel(dAng * RIM_ALB_PER_RAD);
        return 1;
    }
    if(rim_scroller != ui_active_scroller()) return 1;
    if(r < RIM_REL_R_MIN) return 1;
    float ang=rim_ang(x,y); float dAng=rim_wrap(ang-rim_last_ang); rim_last_ang=ang;
    uint32_t now=lv_tick_get(); uint32_t dt=now-rim_last_ms; rim_last_ms=now;
    if(dAng > 1.2f || dAng < -1.2f) return 1;
    float omega = dt>0 ? fabsf(dAng)/((float)dt/1000.0f) : 0.0f;
    float gain  = 1.0f + omega/4.0f; if(gain>6.0f) gain=6.0f;
    rim_accum += dAng * RIM_PX_PER_RAD * gain;
    int step=(int)rim_accum; rim_accum -= (float)step;
    if(step){
        int dy  = -step;
        int top = lv_obj_get_scroll_top(rim_scroller);
        int bot = lv_obj_get_scroll_bottom(rim_scroller);
        int roomUp = top>0?top:0, roomDn = bot>0?bot:0;
        int inb = dy;
        if(dy>0 && inb >  roomUp) inb =  roomUp;
        if(dy<0 && inb < -roomDn) inb = -roomDn;
        if(inb) lv_obj_scroll_by(rim_scroller, 0, inb, LV_ANIM_OFF);
        int excess = dy - inb;
        if(excess){
            int over = excess>0 ? (top<0?-top:0) : (bot<0?-bot:0);
            int remain = RIM_MAX_OVER - over;
            if(remain > 0){
                float damp = 1.0f - (float)over/(float)RIM_MAX_OVER;
                int add = (int)((float)excess * 0.40f * damp);
                if(add==0) add = excess>0?1:-1;
                if(add >  remain) add =  remain;
                if(add < -remain) add = -remain;
                lv_obj_scroll_by(rim_scroller, 0, add, LV_ANIM_OFF);
            }
        }
    }
    if(rim_scroller == library_scroller()) library_scroll_letter_tick();
    return 1;
}
static void rim_spring_back(lv_obj_t *sc){
    if(!sc || !lv_obj_is_valid(sc)) return;
    int top = lv_obj_get_scroll_top(sc);
    int bot = lv_obj_get_scroll_bottom(sc);
    int dy = 0;
    if(top < 0)      dy = top;
    else if(bot < 0) dy = -bot;
    if(dy) lv_obj_scroll_by(sc, 0, dy, LV_ANIM_ON);
}
static int rim_release(void){
    int owned=(rim_state==RIM_OWN);
    if(owned && rim_cover)          albumwall_settle();
    else if(owned && rim_scroller)  rim_spring_back(rim_scroller);
    rim_state=RIM_IDLE; rim_scroller=NULL; rim_cover=0; return owned;
}

static int coldplug_mounted(void){
    FILE *m = fopen("/proc/mounts", "r");
    if(!m) return 0;
    char line[512], dev[128], target[128]; int ok = 0;
    while(fgets(line, sizeof line, m)){
        if(sscanf(line, "%127s %127s", dev, target) != 2) continue;
        if(!strcmp(target, "/tmp/sdcard") &&
           (!strcmp(dev, "/dev/mmcblk0p1") || !strcmp(dev, "/dev/mmcblk0"))){ ok = 1; break; }
    }
    fclose(m);
    return ok;
}
static int sd_mount_utf8(const char *fstype){
    if(mount("/dev/mmcblk0p1", "/tmp/sdcard", fstype, 0, "iocharset=utf8") == 0) return 0;
    if(errno != EINVAL) return -1;
    return mount("/dev/mmcblk0p1", "/tmp/sdcard", fstype, 0, NULL);
}
static void coldplug_log(const char *msg){
    FILE *f = fopen("/usr/data/coldplug.log", "a");
    if(f){ fprintf(f, "%ld %s\n", (long)time(NULL), msg); fclose(f); }
}
static int sd_exported_to_host(void){
    const char *udcp = "/sys/kernel/config/usb_gadget/storage_demo/UDC";
    const char *fndir = "/sys/kernel/config/usb_gadget/storage_demo/functions";
    struct stat sb;
    errno = 0; int have_udc = (stat(udcp, &sb) == 0); int udc_err = (!have_udc && errno != ENOENT);
    if(udc_err) return 1;
    if(have_udc){
        FILE *f = fopen(udcp, "r");
        if(!f) return 1;
        char b[64] = {0}; size_t n = fread(b, 1, sizeof b - 1, f); int rerr = ferror(f); fclose(f);
        if(rerr) return 1;
        for(size_t i = 0; i < n; i++) if(b[i] > ' ') return 1;
    }
    errno = 0;
    DIR *d = opendir(fndir);
    if(!d) return (errno == ENOENT) ? 0 : 1;
    int exported = 0; struct dirent *e;
    while(!exported && (e = readdir(d))){
        if(strncmp(e->d_name, "mass_storage.", 13) != 0) continue;
        char lunp[300]; snprintf(lunp, sizeof lunp, "%s/%s/lun.0/file", fndir, e->d_name);
        FILE *g = fopen(lunp, "r");
        if(!g){ exported = 1; break; }
        char l[300] = {0}; size_t m = fread(l, 1, sizeof l - 1, g); int rerr = ferror(g); fclose(g);
        if(rerr || (m && strstr(l, "mmcblk0"))) exported = 1;
    }
    closedir(d);
    return exported;
}
static int storage_host_confirmed(void){
    const char *base = "/sys/kernel/config/usb_gadget/storage_demo";
    char p[512], b[320];
    snprintf(p, sizeof p, "%s/UDC", base);
    FILE *f = fopen(p, "r"); if(!f) return 0;
    size_t n = fread(b, 1, sizeof b - 1, f); int err = ferror(f); fclose(f); b[n] = 0;
    int bound = 0;
    for(size_t i = 0; i < n; i++) if(b[i] > ' ') bound = 1;
    if(err || !bound) return 0;
    snprintf(p, sizeof p, "%s/functions", base);
    DIR *d = opendir(p); if(!d) return 0;
    struct dirent *e; int backed = 0;
    while((e = readdir(d))){
        if(strncmp(e->d_name, "mass_storage.", 13)) continue;
        snprintf(p, sizeof p, "%s/functions/%s/lun.0/file", base, e->d_name);
        f = fopen(p, "r"); if(!f) continue;
        n = fread(b, 1, sizeof b - 1, f); err = ferror(f); fclose(f); b[n] = 0;
        while(n && (b[n-1] == '\n' || b[n-1] == '\r')) b[--n] = 0;
        if(!err && (!strcmp(b, "/dev/mmcblk0") || !strcmp(b, "/dev/mmcblk0p1"))) backed = 1;
    }
    closedir(d);
    return backed;
}
static int uac_bound(void){
    const char *udc = "/sys/kernel/config/usb_gadget/uac_demo/UDC";
    FILE *f = fopen(udc, "r");
    if(!f) return 0;
    char b[64] = {0}; size_t n = fread(b, 1, sizeof b - 1, f); int rerr = ferror(f); fclose(f);
    if(rerr) return 0;
    for(size_t i = 0; i < n; i++) if(b[i] > ' ') return 1;
    return 0;
}
int ui_detect_source_mode(void){
    if(sd_exported_to_host()) return 3;
    if(uac_bound())           return 1;
    return 0;
}
static int sd_cold_mount_allowed(void){
    double up = -1.0; FILE *pu = fopen("/proc/uptime", "r");
    if(pu){ if(fscanf(pu, "%lf", &up) != 1) up = -1.0; fclose(pu); }
    return (up >= 0.0 && up <= 150.0 && !sd_exported_to_host() && srcswitch_sd_writable());
}
static void *coldplug_thread(void *arg){
    (void)arg;
    const int direct_sd_mount = fw_needs_direct_sd_mount();
    if(coldplug_mounted()) return NULL;
    coldplug_log("coldplug worker start");
    for(int w = 0; ; w++){
        int v = launch_status_get();
        if(v == 1) break;
        if(v == 0){ coldplug_log("launcher reported unprotected/unconfirmed - SD left untouched this boot"); return NULL; }
        if(w >= 40){ coldplug_log("no launcher verdict within 20s - SD left untouched this boot"); return NULL; }
        struct timespec hs = { 0, 500L*1000*1000 }; nanosleep(&hs, NULL);
    }
    int nudged = 0;
    for(int i = 0; i < 120; i++){
        if(coldplug_mounted()){ coldplug_log("SD mounted - done"); return NULL; }
        pthread_mutex_lock(srcswitch_mode_mutex());
        if(ui_get_source_mode() == 0 && !sd_exported_to_host() && !coldplug_mounted() && srcswitch_sd_writable()){
            if(!direct_sd_mount || nudged || i >= 10){
                if(direct_sd_mount && !nudged) coldplug_log("direct mount failed -> falling back to the player's uevent listener (one-way: no more direct mounts)");
                nudged = 1;
                int fd = open("/sys/block/mmcblk0/uevent", O_WRONLY | O_CLOEXEC);
                if(fd >= 0){ ssize_t w = write(fd, "add\n", 4); (void)w; close(fd); }
            }
            else {
                mkdir("/tmp/sdcard", 0755);
                if(sd_cold_mount_allowed() && sd_mount_utf8("exfat") == 0){
                    if(sd_exported_to_host()){
                        if(umount("/tmp/sdcard") != 0 && umount2("/tmp/sdcard", MNT_DETACH) != 0)
                            coldplug_log("WARNING: post-mount export + unmount BOTH failed (card busy) - possible transient dual-access");
                        else
                            coldplug_log("post-mount export detected -> unmounted, host owns card (one-shot gives up)");
                        pthread_mutex_unlock(srcswitch_mode_mutex()); return NULL;
                    }
                    coldplug_log("SD direct-mounted (V2.40 exfat)"); pthread_mutex_unlock(srcswitch_mode_mutex()); return NULL;
                }
                else if(sd_cold_mount_allowed() && sd_mount_utf8("vfat") == 0){
                    if(sd_exported_to_host()){
                        if(umount("/tmp/sdcard") != 0 && umount2("/tmp/sdcard", MNT_DETACH) != 0)
                            coldplug_log("WARNING: post-mount export + unmount BOTH failed (card busy) - possible transient dual-access");
                        else
                            coldplug_log("post-mount export detected -> unmounted, host owns card (one-shot gives up)");
                        pthread_mutex_unlock(srcswitch_mode_mutex()); return NULL;
                    }
                    coldplug_log("SD direct-mounted (V2.40 vfat)"); pthread_mutex_unlock(srcswitch_mode_mutex()); return NULL;
                }
                if(!sd_cold_mount_allowed()){
                    pthread_mutex_unlock(srcswitch_mode_mutex());
                    coldplug_log("V2.40 SD one-shot: window elapsed or card exported (SD not mounted)");
                    return NULL;
                }
                { char b[96]; snprintf(b, sizeof b, "SD direct-mount failed errno=%d(%s)", errno, strerror(errno)); coldplug_log(b); }
            }
        }
        pthread_mutex_unlock(srcswitch_mode_mutex());
        struct timespec ts = { 3, 0 }; nanosleep(&ts, NULL);
    }
    coldplug_log("coldplug TIMEOUT - SD never mounted");
    return NULL;
}
static int coldplug_should_run(void){
    double up = -1.0;
    FILE *u = fopen("/proc/uptime", "r");
    if(u){ if(fscanf(u, "%lf", &up) != 1) up = -1.0; fclose(u); }
    if(up < 0.0 || up > 150.0){ coldplug_log("skip: uptime unreadable or late (not a verified cold boot)"); return 0; }
    if(ui_get_source_mode() != 0){ coldplug_log("skip: not in Local mode (card may be exported to a host)"); return 0; }
    return 1;
}
static void coldplug_start(void){
    if(!coldplug_should_run()) return;
    pthread_t th;
    if(pthread_create(&th, NULL, coldplug_thread, NULL) == 0) pthread_detach(th);
}

static void launcher_kill_bounded(void){
    if(g_bounded_pgid > 0) kill(-(pid_t)g_bounded_pgid, SIGKILL);
    if(g_bounded_pid > 0) kill((pid_t)g_bounded_pid, SIGKILL);
}
static const struct launcher_env g_launcher_env = {
    .sd_exported_to_host = sd_exported_to_host, .run_bounded = run_bounded,
    .rmguard_ensure = rmguard_ensure, .kill_bounded = launcher_kill_bounded,
};

static void sd_degraded_notice_cb(lv_timer_t *t){
    (void)t;
    if(srcswitch_held()) ui_toast("SD protection unavailable - library and artwork are off");
}
static void sd_features_gate_cb(lv_timer_t *t){
    static int waited = 0, started = 0;
    int v = launch_status_get();
    if(v < 0 && !started){
        if(++waited < 60) return;
        v = 0;
    }
    if(v == 1){
        if(!started){
            started = 1;
            srcswitch_set_launch_verdict(1);
            coldplug_start();
            ui_art_retry();
            ui_start_art_prewarm();
            albumwall_prewarm_seed();
            lv_timer_set_period(t, 5000);
        }
        return;
    }
    srcswitch_revoke();
    scanner_abort();
    art_kill_all();
    g_play_pending = 0;
    ui_toast("SD protection unavailable - library and artwork are off");
    lv_timer_create(sd_degraded_notice_cb, 300000, NULL);
    lv_timer_del(t);
}

int ui_theme_reload(const char *screen){
    if(scanner_active()){ ui_toast("Library scan running - try again when it finishes"); return -1; }
    if(modes_output_busy()){ ui_toast("Output is switching - try again in a moment"); return -1; }
    if(modes_source_switch_busy()){ ui_toast("Mode is switching - try again in a moment"); return -1; }
    art_kill_all();
    if(g_bounded_pgid > 0) kill(-(pid_t)g_bounded_pgid, SIGKILL);
    if(g_bounded_pid > 0) kill((pid_t)g_bounded_pid, SIGKILL);
    for(int fd = 3; fd < 1024; fd++){ int fl = fcntl(fd, F_GETFD); if(fl >= 0) fcntl(fd, F_SETFD, fl | FD_CLOEXEC); }
    fflush(stderr);
    handoff_signal_reset();
    session_handoff_export();
    char *ua[3]; ua[0] = "mq_ui"; ua[1] = (char *)(screen ? screen : "home"); ua[2] = NULL;
    execv("/proc/self/exe", ua);
    session_handoff_cancel();
    alarm(0);
    ui_toast("Couldn't apply the theme now - it applies next start");
    return -1;
}
static void pdbg_boot_cb(lv_timer_t *t){ (void)t; debug_boot_restore(); }
int main(int argc, char **argv){
    { struct sigaction al; memset(&al,0,sizeof al);
      al.sa_handler = boot_alarm_handler; sigemptyset(&al.sa_mask); al.sa_flags = 0;
      sigaction(SIGALRM,&al,NULL); alarm(45); }
#ifdef DISKOS_DIAG_FBMARK
    system("/bin/sh /etc/diag_fb.sh magenta");
#endif
    {   const char *a0 = (argv[0] && argv[0][0]) ? argv[0] : "mq_ui";
        const char *slash = strrchr(a0,'/');
        const char *base = slash ? slash+1 : a0;
        if(strcmp(base,"mq_player")==0){
            if(strcmp(a0,"mq_player") != 0){
                size_t old_len = strlen(argv[0]);
                memmove(argv[0], "mq_player", sizeof "mq_player");
                memset(argv[0] + sizeof "mq_player", 0, old_len + 1 - sizeof "mq_player");
            }
            launcher_set_env(&g_launcher_env);
            player_launch_prepare();
            exec_stock_player();
        }
        if(strcmp(a0,"mq_ui")!=0){
            char *ua[3]; ua[0]="mq_ui"; ua[1]=(argc>1?argv[1]:NULL); ua[2]=NULL;
            handoff_signal_reset();
            execv("/proc/self/exe", ua);
            alarm(45);
        }
    }
    {
        FILE *bl = (access("/usr/data/volup_dbg", F_OK) == 0) ? fopen("/usr/data/volup_boot.log", "a") : NULL;
        if(bl){
            fprintf(bl, "boot select record run_diskos=%d\n", boot_select_is_diskos("/tmp/.diskos_boot_select"));
            fclose(bl);
        }
        boot_select_enforce("/tmp/.diskos_boot_select");
    }
    { struct stat lb; if(stat("/usr/data/diskos_boot.log", &lb) == 0 && lb.st_size > 256*1024)
          freopen("/usr/data/diskos_boot.log", "w", stderr);
      else freopen("/usr/data/diskos_boot.log", "a", stderr); }
    dup2(fileno(stderr), fileno(stdout));
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    { static char altstk[32768];
      stack_t ss; ss.ss_sp = altstk; ss.ss_size = sizeof altstk; ss.ss_flags = 0; sigaltstack(&ss, NULL);
      struct sigaction sa; memset(&sa, 0, sizeof sa);
      sa.sa_sigaction = crash_log; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
      sigaction(SIGSEGV,&sa,NULL); sigaction(SIGBUS,&sa,NULL);
      sigaction(SIGABRT,&sa,NULL); sigaction(SIGFPE,&sa,NULL); }
    #define BOOT_DEADLINE_S 45
    { struct sigaction al; memset(&al,0,sizeof al);
      al.sa_handler = boot_alarm_handler; sigemptyset(&al.sa_mask); al.sa_flags = 0;
      sigaction(SIGALRM,&al,NULL); alarm(BOOT_DEADLINE_S); }
    fprintf(stderr,"=== diskos main start argc=%d ===\n", argc); fflush(stderr);
    sysconfig_work_mode_reset_via(run_bounded, 8000);
    if(sysconfig_work_mode_confirm_via(run_bounded, 5000) != 0){ srcswitch_hold(); fprintf(stderr,"WORK_MODE reset UNCONFIRMED -> diskOS SD activity held this boot\n"); fflush(stderr); }
    if(!rmguard_ensure()){ srcswitch_hold(); fprintf(stderr,"SD guard could NOT be established -> diskOS SD activity held this boot\n"); fflush(stderr); }
    system("[ -x /usr/data/sshd/start-ssh.sh ] && /usr/data/sshd/start-ssh.sh >/dev/null 2>&1 &");
    (void)fw_os_ver();
    srcswitch_set_env(&g_srcswitch_env);
    admit_set_env(&g_admit_env);
    booksess_set_env(&g_booksess_env);
    srcswitch_init();
    { unsigned seed=0; FILE *r=fopen("/dev/urandom","rb"); if(r){ if(fread(&seed,1,sizeof seed,r)!=sizeof seed) seed=(unsigned)time(NULL); fclose(r);} else seed=(unsigned)time(NULL); srand(seed); }
    lv_init();
    int fbok=0;
    for(int i=0;i<20;i++){ if(fbpan_create("/dev/fb0")){ fbok=1; break; }
        fprintf(stderr,"fbpan try %d failed, retry\n",i); fflush(stderr); usleep(250000); }
    if(!fbok){ fprintf(stderr,"fbpan failed (gave up)\n"); return 1; }
    cfg_load();
    phonehome_apply();
    (void)route_state_restore();
    fontft_init();
    theme_init();
    swipe_thresh_load();
    int boot_br = read_int_file("/sys/class/backlight/backlight/brightness");
    int startup_deferred = powerkey_defer_startup(boot_br);
    settings_apply_startup(!startup_deferred);
    if(boot_br == 0 && !startup_deferred && !fw_power_key_driveable())
        (void)ui_backlight_take_foreign(NULL, NULL);
    wifi_init_intent();
    if(access("/tmp/.diskos_radios_reset", F_OK) != 0){
        cfg_set_int("wifi_on", cfg_get_int(CFG_PDBG, 0) ? 1 : 0);
        cfg_set_int("bt_on", 0);
        int fd = open("/tmp/.diskos_radios_reset", O_WRONLY | O_CREAT, 0600);
        if(fd >= 0) close(fd);
    }
    fprintf(stderr,"step:screens_init\n");fflush(stderr); screens_init();
    home_set_settings_click_cb(go_settings);
    library_set_song_click_cb(on_song_play);
    search_set_song_click_cb(on_search_play);
    { char *a[] = { "hwclock", "-s", NULL };
      run_bounded(a, 6000); }
    timekeep_boot();
    lv_timer_create(hwclock_save_tick, 300000, NULL);
    lv_timer_create(clock_tick, 10000, NULL);
    lv_timer_create(book_tick, 400, NULL);
    clock_tick(NULL);
    g_t_status = lv_timer_create(status_poll_cb, 3000, NULL);
    status_poll_cb(NULL);
    if(cfg_get_int(CFG_PDBG, 0)) lv_timer_set_repeat_count(lv_timer_create(pdbg_boot_cb, 6000, NULL), 1);
    bt_boot_restore();
    g_t_wx  = lv_timer_create(weather_poll, 1000, NULL);
    if(cfg_get_int("weather_on", 1)) weather_fetch_async();
    g_t_lyr = lv_timer_create(lyrics_poll, 500, NULL);
    lv_timer_create(scanner_poll, 500, NULL);
    if(!books_ensure_migrated())
        lv_timer_create(migrate_books_retry_cb, 3000, NULL);
    lv_timer_create(playlist_port_cb, 5000, NULL);
    lv_timer_create(sd_features_gate_cb, 500, NULL);
    g_t_art = lv_timer_create(ui_art_poll, 120, NULL);
    session_init();
    session_startup_set_env(&g_startup_env);
    lv_timer_create(localplayer_workmode_cb, 500, NULL);
    modes_output_boot_restore();
    if(session_entry() == SESSION_COLD_BOOT && cfg_get_int("memory_play", -1) != 0)
        lv_timer_create(boot_autoplay_pause_cb, 500, NULL);
    if(ipc_start()!=0){
        fprintf(stderr,"ipc not ready, retrying in background\n"); fflush(stderr);
        lv_timer_create(ipc_connect_retry_cb, 1000, NULL);
    }
    ipc_send_probe("02020008");
    lv_timer_t *sync_t = lv_timer_create(state_sync_retry_cb, 2000, NULL);
    lv_timer_set_repeat_count(sync_t, 5);
    lv_timer_create(ipc_health_cb, 30000, NULL);

    if(access("/usr/data/touch_off", 0)!=0){
        g_touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event1");
        g_touch_raw = open("/dev/input/event1", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if(g_touch){
            fbpan_bind_touch(g_touch);
            lv_indev_set_scroll_limit(g_touch, 18);
            g_dbg = (access("/usr/data/touch_dbg", 0)==0);
            if(g_dbg) dbgdot_init();
            fprintf(stderr,"touch ENABLED indev=%p dbg=%d thresh=%d\n",(void*)g_touch,g_dbg,g_swipe_thresh);
        } else fprintf(stderr,"touch create FAILED\n");
    } else fprintf(stderr,"touch OFF (no /usr/data/touch_on)\n");
    fflush(stderr);

    int start = SCR_HOME;
    if(argc>1){
        if(!strcmp(argv[1],"home"))    start = SCR_HOME;
        else if(!strcmp(argv[1],"library")) start = SCR_LIBRARY;
        else if(!strcmp(argv[1],"nowplaying")) start = SCR_NOWPLAYING;
        else if(!strcmp(argv[1],"settings")) start = SCR_SETTINGS;
        else if(!strcmp(argv[1],"workmode")) start = SCR_WORKMODE;
        else if(!strcmp(argv[1],"search")) start = SCR_SEARCH;
        else if(!strcmp(argv[1],"quick")) start = SCR_QUICK;
        else if(!strcmp(argv[1],"songinfo")) start = SCR_SONGINFO;
        else if(!strcmp(argv[1],"tune")) start = SCR_TUNE;
        else if(!strcmp(argv[1],"ctx")) start = SCR_NPMENU;
        else if(!strcmp(argv[1],"eq")) start = SCR_EQ;
        else if(!strcmp(argv[1],"apps")) start = SCR_APPS;
        else if(!strcmp(argv[1],"weather")) start = SCR_WEATHER;
        else if(!strcmp(argv[1],"lyrics")) start = SCR_LYRICS;
        else if(!strcmp(argv[1],"saver")) start = SCR_SAVER;
        else if(!strcmp(argv[1],"hub")) start = SCR_NPHUB;
        else if(!strcmp(argv[1],"upnext")) start = SCR_UPNEXT;
        else if(!strcmp(argv[1],"datetime")) start = SCR_DATETIME;
        else if(!strcmp(argv[1],"pet")) start = SCR_TAMA;
        else if(!strcmp(argv[1],"dac")) start = SCR_DAC;
    }
    screen_show(start);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    if(argc>1 && !strcmp(argv[1],"wifi")) wifi_open();
    if(argc>1 && !strcmp(argv[1],"bt"))   bt_open();
    if(argc>1 && !strcmp(argv[1],"lyrics")) lyrics_open();
    printf("diskOS up (screen %d)\n", start); fflush(stdout);

    track_state_t st; unsigned last=~0u;
    lv_indev_state_t prev_ts = LV_INDEV_STATE_RELEASED;
    int sx=0, sy=0, lastx=0, lasty=0; uint32_t sms=0; int s_press_scr=SCR_HOME;
    int cover_tap=0;
    int fsart_touch=0;
    int sleep_touch=0;
    playstate_t g_ps = { -1, 0 }; int last_playing=-1;
    char np_last_path[256] = "";
    char np_last_title[sizeof st.title] = "", np_last_artist[sizeof st.artist] = "";
    static const int TMAP[5] = {0,30,60,120,300};
    uint32_t last_activity = lv_tick_get();
    int woke = 0;
    int bl_state = 0;
    uint32_t last_blpoll = 0;
    powerkey_t pk = { boot_br, boot_br == 0, 0 };
    if(boot_br == 0 && !startup_deferred){
        pk.seen = ui_backlight_written(); pk.player_off = 0;
    }
    if(startup_deferred){ ui_backlight(0); bl_state = 2; g_screen_off = 1; }
    unsigned scr_seq_seen = 0, aa03_seq_seen = 0, screen_gen_seen = 0, inject_for = 0, inject_gen = 0;
    int poweroff_inhibit = 0, inject_pending = 0; uint32_t inject_due = 0;
    unsigned last_vol_seq = 0;
    srcswitch_restart_deadline();
    lv_timer_create(srcswitch_tick_cb, 100, NULL);
    { void system_ota_health_init(void); system_ota_health_init(); }
    if(session_entry() == SESSION_RELAUNCH){
        if(session_adopt(ipc_generation())){
            modes_output_adopt();
            fprintf(stderr, "theme reload: kept route %d, source mode %d (gen %u)\n", session_route(), session_source_mode(), ipc_generation()); fflush(stderr);
        }
    }
    else splash_start();
    if(session_adopt_detected(ipc_generation(), ui_detect_source_mode())){
        modes_output_adopt();
        fprintf(stderr, "startup: Player already in USB DAC mode - adopted, no restore (gen %u)\n", ipc_generation()); fflush(stderr);
    }
    lv_refr_now(NULL);
    if(bl_state == 2) draw_set_dark(1);
    if(srcswitch_held()){
        ui_toast("SD protection unavailable - library and artwork are off");
        lv_timer_create(sd_degraded_notice_cb, 300000, NULL);
    }
    { static const char okm[] = "=== diskos reached main loop (first frame painted; boot watchdog disarmed) ===\n";
      (void)write(2, okm, sizeof okm - 1);
      sigset_t as; sigemptyset(&as); sigaddset(&as, SIGALRM);
      sigprocmask(SIG_BLOCK, &as, NULL);
      alarm(0);
      signal(SIGALRM, SIG_IGN);
      sigprocmask(SIG_UNBLOCK, &as, NULL); }
    for(;;){
        if(ipc_take_reconnected()){
            admit_note_player_restart();
            booksess_player_restarted();
            session_clear_bt_mac();
            route_state_remove();
            bt_notify_player_restart();
            ui_reapply_audio();
        }
        admit_tick();
        opus_title_tick();
        ipc_get_state(&st);
        wifi_supervise();
        if(st.volume_seq != last_vol_seq){
            last_vol_seq = st.volume_seq;
            power_note_input();
            if(srcswitch_dac_level_settled() && st.volume > 0) cfg_set_int("dac_volume", st.volume);
            if(bl_state == 0){
                last_activity = lv_tick_get();
                if(screen_current()==SCR_SAVER) screen_back();
                ui_show_volume(st.volume);
            }
        }
        int playing = playstate_playing(&g_ps, st.have_track, st.position_ms, lv_tick_get(), 1600, 0);
        if(playing) ui_note_transport_sent();
        g_playing = playing;
        st.state = playing ? 2 : 1;
        if(g_play_pending && ipc_tx_waiting()){
            g_play_pending = lv_tick_get(); if(!g_play_pending) g_play_pending = 1;
            g_play_pre.request = g_play_pre.at = g_play_pending;
        }
        if(play_start_failed(&st)){
            if(g_play_pending && lv_tick_elaps(g_play_pending) > 6000){
                g_play_pending = 0;
                ui_toast("Couldn't start playback");
            }
        }
        ui_np_title_scroll(bl_state != 2);
        if(bl_state != 2 && (g_draw_dark || st.seq != last || playing != last_playing || ui_take_art_force())){
            last = st.seq; last_playing = playing;
            ui_update(&st);
            home_set_now_playing(st.have_track?ipc_display_title(&st):NULL,
                                 st.have_track?st.artist:NULL,
                                 ui_current_accent(), playing);
            { void home_set_progress(int, int); int ui_np_progress(void);
              home_set_progress(ui_np_progress(), st.have_track); }
            { void saver_set_playing(int); saver_set_playing(playing); }
            const char *idnow = st.have_track ? st.path : "";
            const char *tnow = st.have_track ? ipc_display_title(&st) : "", *anow = st.have_track ? st.artist : "";
            int art_changed = strcmp(idnow, np_last_path) != 0;
            int song_changed = strcmp(tnow, np_last_title) != 0 || strcmp(anow, np_last_artist) != 0;
            if(art_changed){
                snprintf(np_last_path, sizeof np_last_path, "%s", idnow);
                home_set_art_src(ui_current_thumb_src());
                home_set_backdrop(ui_current_backdrop_src());
            }
            if(art_changed || song_changed){
                snprintf(np_last_title, sizeof np_last_title, "%s", tnow);
                snprintf(np_last_artist, sizeof np_last_artist, "%s", anow);
                saver_set_track(st.have_track?ipc_display_title(&st):NULL,
                                st.have_track?st.artist:NULL,
                                ui_current_backdrop_src(), st.have_track?st.path:NULL);
            }
            quicksettings_refresh(playing);
            songinfo_set(&st);
            npmenu_set(st.have_track ? &st : NULL, playing, ui_current_thumb_src());
        }
        if(bl_state != 2 && g_draw_dark){
            ui_art_poll(NULL);
            lv_timer_pause(g_t_art);
            clock_tick(NULL);
        }
        if(bl_state != 2 && ui_take_art_applied()){
            home_set_art_src(ui_current_thumb_src());
            home_set_backdrop(ui_current_backdrop_src());
            saver_set_track(st.have_track?ipc_display_title(&st):NULL,
                            st.have_track?st.artist:NULL,
                            ui_current_backdrop_src(), st.have_track?st.path:NULL);
            npmenu_set(st.have_track ? &st : NULL, playing, ui_current_thumb_src());
        }
        ui_vinyl_spin(playing && screen_current()==SCR_NOWPLAYING && bl_state==0);
        saver_vinyl_spin(playing && screen_current()==SCR_SAVER && bl_state != 2);

        if(bl_state != 2){
            if(cfg_take_save_error())       ui_toast("Couldn't save settings");
            else if(ipc_take_send_error())  ui_toast("Player didn't respond");
        }
        ipc_tx_pump();
#ifdef DISKOS_PROFILE
        struct timespec pa, pb; clock_gettime(CLOCK_MONOTONIC, &pa);
#endif
        mclock_sync(bl_state != 2);
        kit_cursor_sync(bl_state != 2);
        albumwall_poll_sync(bl_state != 2);
        uint32_t wait = lv_timer_handler();
        if(bl_state != 2 && g_draw_dark){
            lv_timer_resume(g_t_art);
            draw_set_dark(0);
        }
#ifdef DISKOS_PROFILE
        clock_gettime(CLOCK_MONOTONIC, &pb);
        unsigned long long prof_h = (unsigned long long)(pb.tv_sec - pa.tv_sec) * 1000000000ULL + (unsigned long long)(pb.tv_nsec - pa.tv_nsec);
#endif

        if(g_app_pending){
            g_app_pending = 0;
            screen_back();
            app_run(g_app_exec);
            last = ~0u;
            last_activity = lv_tick_get();
        }

        if(g_touch){
            lv_indev_state_t ts = lv_indev_get_state(g_touch);
            lv_point_t p; lv_indev_get_point(g_touch, &p);
            if(kbinput_active()){
            } else if(ts==LV_INDEV_STATE_PRESSED && prev_ts==LV_INDEV_STATE_RELEASED){
                last_activity = lv_tick_get();
                if(screen_current()==SCR_SAVER){
                    if(bl_state && !pk.player_off){ wake_backlight(ui_effective_brightness()); bl_state = 0; g_screen_off = 0; }
                    screen_back();
                    woke = 1;
                } else {
                    sx=p.x; sy=p.y; lastx=p.x; lasty=p.y; sms=lv_tick_get();
                    g_press_sx = g_press_lx = p.x; g_press_sy = g_press_ly = p.y;
                    s_press_scr=screen_current();
                    cover_tap = 0;
                    fsart_touch = ui_np_fsart_active();
                    sleep_touch = ui_np_overlay_active();
                    if(fsart_touch || sleep_touch){
                    } else if(screen_current()==SCR_NOWPLAYING){
                        if(ui_np_cover_hit(sx, sy)) cover_tap = 1;
                        else ui_np_seek_press(sx, sy);
                    }
                    else {
                        rim_press(sx, sy);
                        if(rim_state==RIM_IDLE && screen_current()==SCR_ALBUMWALL && sx >= BACK_START_MAX_X)
                            albumwall_drag_begin(sx);
                    }
                    fprintf(stderr,"TAP x=%d y=%d\n",sx,sy); fflush(stderr);
                    if(g_dbg && g_dbgdot){
                        lv_obj_set_pos(g_dbgdot, sx-13, sy-13);
                        lv_obj_clear_flag(g_dbgdot, LV_OBJ_FLAG_HIDDEN);
                    }
                }
            } else if(ts==LV_INDEV_STATE_PRESSED){
                last_activity = lv_tick_get();
                lastx=p.x; lasty=p.y; g_press_lx=p.x; g_press_ly=p.y;
                if(screen_current()==SCR_NOWPLAYING && !ui_np_fsart_active() && !sleep_touch) ui_np_seek_move(p.x, p.y);
                else if(rim_move(p.x, p.y)){ }
                else if(screen_current()==SCR_ALBUMWALL && rim_state==RIM_IDLE) albumwall_drag(p.x);
            } else if(ts==LV_INDEV_STATE_RELEASED && prev_ts==LV_INDEV_STATE_PRESSED){
                if(p.x >= 0 && p.x < 360 && p.y >= 0 && p.y < 360){ lastx = p.x; lasty = p.y; g_press_lx = p.x; g_press_ly = p.y; }
                if(woke){
                    woke = 0;
                } else if(rim_release()){
                } else {
                    int dx=lastx-sx, dy=lasty-sy;
                    int adx=dx<0?-dx:dx, ady=dy<0?-dy:dy;
                    uint32_t dt=lv_tick_elaps(sms);
                    int cur=screen_current();
                    int cover_tap_gesture = cover_tap && adx < 20 && ady < 20;
                    int lvgl_owned = fsart_touch || cover_tap_gesture || sleep_touch;
                    int seek_consumed = 0;
                    if(!lvgl_owned && cur==SCR_NOWPLAYING && !ui_np_fsart_active()){
                        ui_np_seek_move(lastx, lasty);
                        seek_consumed = ui_np_seek_release(lastx, lasty);
                    }
                    int vert = ady > adx*2, horiz = adx > ady*2;
                    if(lvgl_owned || ui_np_fsart_active()){
                        if(fsart_touch && (adx >= 20 || ady >= 20)) ui_np_fsart_close();
                    } else if(!seek_consumed){
                    if(cur==SCR_QUICK && vert && dy<0 && ady>=g_swipe_thresh && dt<700){
                        screen_back();
                    } else if(cur!=SCR_QUICK && cur!=SCR_SAVER && sy<40 &&
                              vert && dy>0 && dy>=g_swipe_thresh && dt<700){
                        screen_show(SCR_QUICK);
                        quicksettings_refresh(playing);
                    } else if(cur==SCR_NOWPLAYING){
                        if(adx >= NP_NAV_DIST && adx > ady*NP_NAV_STRAIGHT && dt<900){
                            if(dx < 0) screen_show(SCR_NPHUB);
                            else       screen_back();
                        }
                    } else if(cur==SCR_HOME && horiz && dx<0 && adx>=g_swipe_thresh && dt<700){
                        apps_reload();
                        screen_show(SCR_APPS);
                    } else if(cur==SCR_ALBUMWALL && s_press_scr==SCR_ALBUMWALL){
                        if(sx < BACK_START_MAX_X && horiz && dx>0 && adx>=g_swipe_thresh && dt<700){
                            albumwall_drag_cancel();
                            screen_back();
                        } else if(adx<18 && ady<18 && sx>=100 && sx<=260 && sy>=76 && sy<=214 && dt<900){
                            albumwall_drag_cancel();
                            if(dt >= 500) albumwall_play();
                            else          albumwall_open();
                        } else {
                            albumwall_drag(lastx);
                            albumwall_drag_end();
                        }
                    } else if(horiz && dx>0 && adx>=g_swipe_thresh && dt<700 &&
                              sx < BACK_START_MAX_X){
                        fprintf(stderr,"BACK swipe sx=%d dx=%d dy=%d dt=%u\n",sx,dx,dy,dt); fflush(stderr);
                        if(!(cur==SCR_LIBRARY && library_back())) screen_back();
                    }
                    }
                }
            }
            prev_ts = ts;
        }

        booksess_sleep_tick();

        if(fw_power_key_driveable()){
            int on; unsigned sq, pq, gen; ipc_screen_snapshot(&on, &sq, &pq, &gen);
            if(gen != screen_gen_seen){
                screen_gen_seen = gen; scr_seq_seen = aa03_seq_seen = 0;
                poweroff_inhibit = inject_pending = 0; pk.player_off = 0;
            }
            if(pq != aa03_seq_seen){ aa03_seq_seen = pq; poweroff_inhibit = 1; }
            if(on >= 0 && sq != scr_seq_seen){
                scr_seq_seen = sq;
                inject_pending = 0;
                power_note_input();
                switch(screen_frame_step(on, bl_state, &pk.player_off, poweroff_inhibit)){
                case SF_INJECT: inject_pending = 1; inject_for = sq; inject_gen = gen; inject_due = lv_tick_get() + 300; break;
                case SF_OFF:
                    ui_backlight(0); bl_state = 2; g_screen_off = 1;
                    mclock_sync(0);
                    kit_cursor_sync(0);
                    if(screen_current() == SCR_SAVER) screen_back();
                    break;
                case SF_WAKE:
                    wake_backlight(ui_effective_brightness());
                    bl_state = 0; g_screen_off = 0; last_activity = lv_tick_get();
                    if(screen_current() == SCR_SAVER) screen_back();
                    break;
                default: break;
                }
            }
            if(inject_pending && (int32_t)(lv_tick_get() - inject_due) >= 0){
                inject_pending = 0;
                int on2; unsigned sq2, pq2, gen2; ipc_screen_snapshot(&on2, &sq2, &pq2, &gen2);
                if(gen2 == inject_gen && sq2 == inject_for && on2 == 0 && pq2 == aa03_seq_seen && !poweroff_inhibit) (void)power_key_inject();
            }
            if(lv_tick_elaps(last_blpoll) >= 250){
                last_blpoll = lv_tick_get();
                int cbr = read_int_file("/sys/class/backlight/backlight/brightness");
                (void)ui_backlight_take_foreign(NULL, NULL);
                if(cbr > 0 && cbr != ui_backlight_written() && !pk.player_off){
                    if(bl_state == 0) wake_backlight(ui_effective_brightness());
                    else if(bl_state == 1) wake_backlight(dim_level());
                }
            }
        } else if(lv_tick_elaps(last_blpoll) >= 250){
            last_blpoll = lv_tick_get();
            for(int pass = 0; pass < 2; pass++){
                int was_dark = bl_state == 2; unsigned at_seq = ui_backlight_write_seq();
                int cbr = pass == 0 ? ui_backlight_take_foreign(&was_dark, &at_seq) : read_int_file("/sys/class/backlight/backlight/brightness");
                if(cbr < 0) continue;
                if(pk.seen < 0){ pk.seen = cbr; pk.player_off = (cbr == 0); pk.off_seq = ui_backlight_write_seq(); continue; }
                int act = powerkey_step(&pk, cbr, pass == 0 ? -1 : ui_backlight_written(), at_seq, was_dark);
                if(act != PK_NONE) power_note_input();
                switch(act){
                case PK_OFF:
                    ui_backlight(0); bl_state = 2; g_screen_off = 1;
                    mclock_sync(0);
                    kit_cursor_sync(0);
                    if(screen_current() == SCR_SAVER) screen_back();
                    break;
                case PK_WAKE:
                    wake_backlight(ui_effective_brightness());
                    bl_state = 0; g_screen_off = 0; last_activity = lv_tick_get();
                    if(screen_current() == SCR_SAVER) screen_back();
                    break;
                case PK_RELEVEL:
                    if(bl_state == 0) wake_backlight(ui_effective_brightness());
                    else if(bl_state == 1) wake_backlight(dim_level());
                    break;
                default: break;
                }
            }
        }

        int si = cfg_get_int("saver_idx", 2), oi = cfg_get_int("screenoff_idx", 3);
        int saver_timeout   = (si>=0 && si<5) ? TMAP[si] : 60;
        int screenoff_extra = (oi>=0 && oi<5) ? TMAP[oi] : 120;
        if(g_touch_raw >= 0){
            char rb[512]; int any = 0;
            while(read(g_touch_raw, rb, sizeof rb) > 0) any = 1;
            if(any && bl_state && !pk.player_off){
                last_activity = lv_tick_get();
                wake_backlight(ui_effective_brightness()); bl_state = 0; g_screen_off = 0;
                if(screen_current() == SCR_SAVER) screen_back();
            }
        }
        if(kbinput_active()) last_activity = lv_tick_get();
        if(usbhid_poll(bl_state == 0) && bl_state == 0) last_activity = lv_tick_get();
        { static int cm_boot; if(!cm_boot){ cm_boot = 1; if(session_entry() == SESSION_COLD_BOOT) chargemode_boot(); } chargemode_tick(); }
        power_tick(chargemode_active() ? lv_tick_get() : last_activity, playing);   
        int in_saver = (screen_current()==SCR_SAVER);
        if(g_manual_sleep && !in_saver) g_manual_sleep = 0;
        int manual = g_manual_sleep;
        int sstyle    = cfg_get_int("saver_style", 0);
        int art_saver = (sstyle == 0 || sstyle == 4);
        int saver_ok  = (!art_saver || playing);
        if(saver_timeout > 0 || manual){
            uint32_t idle = manual ? lv_tick_elaps(g_manual_sleep_at) : lv_tick_elaps(last_activity);
            int past_dim = (saver_timeout > 0 && idle > (uint32_t)saver_timeout*1000);
            if(past_dim && !in_saver && bl_state==0 && (saver_ok || manual)){
                screen_show(SCR_SAVER); in_saver = 1;
            }
            if((in_saver || past_dim || manual) && bl_state==0){
                ui_backlight(dim_level()); bl_state = 1;
            }
            uint32_t off_at = manual ? (uint32_t)(screenoff_extra > 0 ? screenoff_extra : 10)*1000
                                     : (uint32_t)(saver_timeout+screenoff_extra)*1000;
            if(bl_state==1 && (manual || screenoff_extra > 0) && idle > off_at){
                ui_backlight(0); bl_state = 2; g_screen_off = 1;
                mclock_sync(0);
                kit_cursor_sync(0);
                if(screen_current()==SCR_SAVER) screen_back();
            }
        }

        mclock_sync(bl_state != 2);
        kit_cursor_sync(bl_state != 2);
        polls_set_paused(bl_state == 2);
        if(bl_state == 2) draw_set_dark(1);
        albumwall_poll_sync(bl_state != 2);
        g_screen_off = (bl_state == 2);
        advance_probe_tick(bl_state != 2);
        g_bl_idle = (bl_state >= 1);
        int busy = lv_anim_count_running() > 0 || prev_ts == LV_INDEV_STATE_PRESSED;
        if(bl_state == 2){
            wait = 200;
        } else {
            uint32_t cap = busy ? 5 : 30;
            if(wait > cap) wait = cap;
        }
#ifdef DISKOS_PROFILE
        { struct timespec pc; clock_gettime(CLOCK_MONOTONIC, &pc);
          unsigned long long body = (unsigned long long)(pc.tv_sec - pa.tv_sec) * 1000000000ULL + (unsigned long long)(pc.tv_nsec - pa.tv_nsec) - prof_h;
          prof_loop(busy, bl_state, lv_anim_count_running(), prof_h, body); }
#endif
        usleep(wait*1000);
    }
    return 0;
}
