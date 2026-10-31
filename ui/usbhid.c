#include "usbhid.h"
#include "screens.h"
#include "outroute.h"
#include "power.h"
#include "clicks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <linux/input.h>

#define MAXFD 4
#define SCAN_MS 1500
#define DEBOUNCE_MS 150
#define CLICK_DEBOUNCE_MS 40
#define VOL_STEP 2
#define VOL_SETTLE_MS 600
#define CMD_NEXT "0201000C0001"
#define CMD_PREVIOUS "0201000C0002"

static struct { int fd, idx; } g_dev[MAXFD];
static int g_open_n, g_was_usb, g_vol = -1, g_click_dev;
static uint32_t g_last_scan, g_vol_ms, g_key_ms[0x300];
static clicks_t g_clicks;

static int already_open(int idx){
    for(int i = 0; i < g_open_n; i++) if(g_dev[i].idx == idx) return 1;
    return 0;
}
static void drop(int i){
    fprintf(stderr, "usbhid: closed event%d\n", g_dev[i].idx); fflush(stderr);
    close(g_dev[i].fd);
    g_dev[i] = g_dev[--g_open_n];
}
static void close_all(void){ while(g_open_n) drop(g_open_n - 1); }

static void scan(void){
    for(int idx = 2; idx < 32 && g_open_n < MAXFD; idx++){
        char p[96], real[PATH_MAX], name[64] = "";
        if(already_open(idx)) continue;
        snprintf(p, sizeof p, "/sys/class/input/event%d/device", idx);
        if(!realpath(p, real) || !strstr(real, "/usb")) continue;
        snprintf(p, sizeof p, "/sys/class/input/event%d/device/name", idx);
        FILE *f = fopen(p, "r");
        if(f){ if(!fgets(name, sizeof name, f)) name[0] = 0; fclose(f); name[strcspn(name, "\n")] = 0; }
        snprintf(p, sizeof p, "/dev/input/event%d", idx);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if(fd < 0) continue;
        g_dev[g_open_n].fd = fd; g_dev[g_open_n++].idx = idx;
        fprintf(stderr, "usbhid: opened event%d \"%s\" (%s)\n", idx, name, real); fflush(stderr);
    }
}
static int step_volume(int dir){
    uint32_t now = lv_tick_get();
    if(g_vol < 0 || (uint32_t)(now - g_vol_ms) > VOL_SETTLE_MS){
        track_state_t st; ipc_get_state(&st); g_vol = st.volume;
    }
    int v = g_vol + dir * VOL_STEP;
    if(v < 0) v = 0;
    if(v > VOL_MAX) v = VOL_MAX;
    g_vol = v; g_vol_ms = now;
    ui_set_volume(v);
    return v;
}
static int is_click_key(int c){ return c == KEY_PLAYPAUSE || c == KEY_PLAYCD || c == KEY_PAUSECD; }
static void send_transport(int idx, int c, const char *cmd, const char *what){
    int rc = ui_transport_command(cmd);
    fprintf(stderr, "usbhid: event%d key %d -> %s (rc=%d)\n", idx, c, what, rc);
    fflush(stderr);
}
static void send_clicks(int idx, int c, int count){
    if(count == 1) send_transport(idx, c, PLAYER_CMD_PLAYPAUSE, "play/pause");
    else if(count == 2) send_transport(idx, c, CMD_NEXT, "next (double click)");
    else if(count == 3) send_transport(idx, c, CMD_PREVIOUS, "previous (triple click)");
}
int usbhid_poll(int screen_lit){
    uint32_t now = lv_tick_get();
    int handled = 0, quiet = chargemode_active() || kbinput_active();
    if(outroute_route() != OUT_USB){ clicks_reset(&g_clicks); if(g_was_usb){ close_all(); g_was_usb = 0; } return 0; }
    g_was_usb = 1;
    if(quiet) clicks_reset(&g_clicks);
    else {
        int due = clicks_poll(&g_clicks, now);
        if(due){ send_clicks(g_click_dev, KEY_PLAYPAUSE, due); power_note_input(); handled = 1; }
    }
    if(!quiet && (!g_open_n || (uint32_t)(now - g_last_scan) >= SCAN_MS)){ g_last_scan = now; scan(); }
    for(int i = 0; i < g_open_n; i++){
        struct input_event ev[16]; ssize_t n;
        while((n = read(g_dev[i].fd, ev, sizeof ev)) > 0){
            if(quiet) continue;
            for(int k = 0; k < (int)(n / (ssize_t)sizeof ev[0]); k++){
                int c = ev[k].code, vol = c == KEY_VOLUMEUP || c == KEY_VOLUMEDOWN;
                const char *cmd = NULL, *what = NULL;
                if(ev[k].type != EV_KEY || ev[k].value < 1 || c < 0 || c >= 0x300) continue;
                if(ev[k].value == 2 && !vol) continue;
                if(is_click_key(c)){
                    if((uint32_t)(now - g_key_ms[c]) < CLICK_DEBOUNCE_MS) continue;
                    g_key_ms[c] = now;
                    g_click_dev = g_dev[i].idx;
                    int done = clicks_press(&g_clicks, now);
                    if(done) send_clicks(g_click_dev, c, done);
                    power_note_input();
                    handled = 1;
                    continue;
                }
                if(c == KEY_NEXTSONG){ cmd = CMD_NEXT; what = "next"; }
                else if(c == KEY_PREVIOUSSONG){ cmd = CMD_PREVIOUS; what = "previous"; }
                else if(!vol) continue;
                if((uint32_t)(now - g_key_ms[c]) < DEBOUNCE_MS) continue;
                g_key_ms[c] = now;
                if(cmd){
                    int rc = ui_transport_command(cmd);
                    fprintf(stderr, "usbhid: event%d key %d -> %s (rc=%d)\n", g_dev[i].idx, c, what, rc);
                } else {
                    int v = step_volume(c == KEY_VOLUMEUP ? 1 : -1);
                    fprintf(stderr, "usbhid: event%d key %d -> volume %d\n", g_dev[i].idx, c, v);
                    if(screen_lit) ui_show_volume(v);
                }
                fflush(stderr);
                power_note_input();
                handled = 1;
            }
        }
        if(n < 0 && errno == ENODEV) drop(i--);
    }
    return handled;
}
