/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "fwcaps.h"
#include "screens.h"
#include "command_spawn.h"
#include <fcntl.h>
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "session.h"
#include "radio.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include "ui_round.h"

static lv_obj_t *g_sw, *g_list, *g_info_list;
static lv_timer_t *g_scan_timer;
static lv_timer_t *g_bt_autoroute_timer;
static char g_sel_mac[20];
static char g_bt_autorouted[20];

static void start_scan(void);
static void scan_timer_cb(lv_timer_t *t);
static void scan_abort(void);
static void scan_kick(void);
static void bt_autoroute_start(void);
static void bt_autoroute_stop(void);

static lv_obj_t *g_scan_icon;
static void spin_anim_cb(void *o, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t*)o, v, 0); }
static void scan_stop(void){
    if(g_scan_icon){ lv_anim_delete(g_scan_icon, spin_anim_cb); g_scan_icon = NULL; }
}

static int run_cap(const char *cmd, char *out, int cap){
    out[0] = 0;
    FILE *p = popen(cmd, "r");
    if(!p){ fprintf(stderr,"bt run_cap popen failed: %s (%s)\n", cmd, strerror(errno)); return 0; }
    int n = fread(out, 1, cap-1, p);
    if(n < 0) n = 0;
    out[n] = 0;
    pclose(p);
    return n;
}

#define BT_STUCK_MAX 8
static pid_t g_bt_stuck[BT_STUCK_MAX];

static void bt_reap_stuck(void){
    for(int i = 0; i < BT_STUCK_MAX; i++)
        if(g_bt_stuck[i] > 0 && waitpid(g_bt_stuck[i], NULL, WNOHANG) != 0) g_bt_stuck[i] = 0;
}

static int run_cap_bounded(const char *cmd, char *out, int cap, int timeout_ms){
    out[0] = 0;
    if(cap < 1) return 0;
    bt_reap_stuck();
    int slot = -1;
    for(int i = 0; i < BT_STUCK_MAX; i++) if(g_bt_stuck[i] <= 0){ slot = i; break; }
    if(slot < 0) return 0;
    int fds[2];
    if(pipe(fds) != 0) return 0;
    pid_t pid = fork();
    if(pid < 0){ close(fds[0]); close(fds[1]); return 0; }
    if(pid == 0){
        close(fds[0]);
        dup2(fds[1], 1);
        close(fds[1]);
        if(setpgid(0, 0) != 0 && getpgrp() != getpid()) _exit(126);
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        _exit(127);
    }
    setpgid(pid, pid);
    close(fds[1]);
    int n = 0;
    uint32_t start = lv_tick_get();
    for(;;){
        int left = timeout_ms - (int)lv_tick_elaps(start);
        if(left <= 0) break;
        struct pollfd pf = { fds[0], POLLIN, 0 };
        int pr = poll(&pf, 1, left);
        if(pr <= 0) break;
        int r = read(fds[0], out + n, cap - 1 - n);
        if(r <= 0) break;
        n += r;
        if(n >= cap - 1) break;
    }
    out[n] = 0;
    close(fds[0]);
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    for(int i = 0; i < 20; i++){
        if(waitpid(pid, NULL, WNOHANG) != 0) return n;
        usleep(5000);
    }
    g_bt_stuck[slot] = pid;
    return n;
}
int ui_run_cap_bounded(const char *cmd, char *out, int cap, int timeout_ms){ return run_cap_bounded(cmd, out, cap, timeout_ms); }

static void list_msg(const char *m){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *e = lv_label_create(g_list);
    lv_label_set_text(e, m);
    lv_obj_set_style_text_color(e, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(e, TF(UI_14), 0);
}
static void list_msg_scanning(void){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *row = lv_obj_create(g_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, "Scanning");
    lv_obj_set_style_text_color(t, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(t, TF(UI_14), 0);
    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, LV_SYMBOL_REFRESH);
    lv_obj_set_size(ic, 24, 24);
    lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ic, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(ic, TF(UI_14), 0);
    g_scan_icon = ic;
    lv_obj_set_style_transform_pivot_x(ic, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(ic, lv_pct(50), 0);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, ic);
    lv_anim_set_exec_cb(&a, spin_anim_cb);
    lv_anim_set_values(&a, 0, 3600); lv_anim_set_time(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static int bt_on(void){
    char b[64]; run_cap("pidof bluetoothd 2>/dev/null", b, sizeof b);
    if(!b[0]) return 0;
    char s[2048]; run_cap_bounded("bluetoothctl show 2>/dev/null", s, sizeof s, 250);
    return strstr(s, "Powered: yes") != NULL;
}

int bt_radio_on(void){
    int found = 0;
    for(int i = 0; i < 12; i++){
        char p[64]; snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/type", i);
        FILE *f = fopen(p, "r"); if(!f) continue;
        char t[16] = {0}; char *r = fgets(t, sizeof t, f); fclose(f);
        if(!r || strncmp(t, "bluetooth", 9) != 0) continue;
        found = 1;
        snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/soft", i);
        f = fopen(p, "r"); if(!f) continue;
        int soft = 1; if(fscanf(f, "%d", &soft) != 1) soft = 1; fclose(f);
        if(soft != 0) return 0;
    }
    return found ? 1 : cfg_get_int("bt_on", 0);
}

static void bt_ensure_services(void){
    system("pidof bluealsa >/dev/null 2>&1 || bluealsa -S --device=hci0 --profile=a2dp-source "
           "--sbc-quality=medium --ldac-abr --ldac-quality=standard "
           "--codec=sbc --initial-volume=48 >/tmp/bluealsa.log 2>&1 &");
}
const char *const BT_CODEC_LABEL[BT_CODEC_N] = { "SBC", "AAC", "LDAC Mobile", "LDAC Standard", "LDAC High" };
static int bt_mac_valid(const char *mac);
static int bt_codec_frame_value(const char *mac, int v, char *out, int cap);
#define BTC_AAC  2u
#define BTC_LDAC 4u
static unsigned bt_codec_need(int v){ return v == 1 ? BTC_AAC : (v >= 2 && v <= 4) ? BTC_LDAC : 0u; }
int bt_codec_pick(int choice, unsigned avail, int avail_known){
    if(choice < 0 || choice >= BT_CODEC_N) return 0;
    unsigned need = bt_codec_need(choice);
    if(need && !(avail_known && (avail & need))) return 0;
    return choice;
}
unsigned bt_codec_parse_avail(const char *info, int *known){
    unsigned m = 0;
    if(known) *known = 0;
    const char *h = info ? strstr(info, "Available codecs:") : NULL;
    if(!h) return 0;
    if(known) *known = 1;
    const char *p = h + 17;
    for(int first = 1; *p;){
        const char *e = strchr(p, '\n'); size_t n = e ? (size_t)(e - p) : strlen(p);
        if(!first && memchr(p, ':', n)) break;
        first = 0;
        for(size_t i = 0; i < n;){
            size_t j = i;
            while(j < n && ((p[j]>='A'&&p[j]<='Z')||(p[j]>='a'&&p[j]<='z')||(p[j]>='0'&&p[j]<='9'))) j++;
            if(j - i == 3 && !strncasecmp(p + i, "AAC", 3)) m |= BTC_AAC;
            else if(j - i == 4 && !strncasecmp(p + i, "LDAC", 4)) m |= BTC_LDAC;
            i = j > i ? j : i + 1;
        }
        if(!e) break;
        p = e + 1;
    }
    return m;
}
static int bt_codec_query_cmd(const char *mac, char *out, int cap){
    if(!bt_mac_valid(mac)) return -1;
    char d[24]; int n = 0;
    for(int i = 0; i < 17; i++) d[n++] = mac[i] == ':' ? '_' : mac[i];
    d[n] = 0;
    int r = snprintf(out, cap, "p=$(bluealsa-cli list-pcms 2>/dev/null | grep -i 'dev_%s/a2dpsrc' | head -n 1); "
                               "[ -n \"$p\" ] && { echo \"PCM=$p\"; bluealsa-cli info \"$p\" 2>/dev/null; }", d);
    return (r > 0 && r < cap) ? 0 : -1;
}
static int bt_probe_run(const char *cmd, char *out, int cap, int timeout_ms){
    out[0] = 0;
    int fds[2];
    if(pipe2(fds, O_CLOEXEC) != 0) return 0;
    pid_t pid;
    char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    if(command_spawn_io(&pid, argv, fds[1], -1) != 0){ close(fds[0]); close(fds[1]); return 0; }
    close(fds[1]);
    int n = 0, left = timeout_ms;
    while(left > 0 && n < cap - 1){
        struct pollfd pf = { fds[0], POLLIN, 0 };
        struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
        if(poll(&pf, 1, left) <= 0) break;
        int r = read(fds[0], out + n, cap - 1 - n);
        if(r <= 0) break;
        n += r;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        left -= (int)((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
    }
    out[n] = 0;
    close(fds[0]);
    kill(-pid, SIGKILL); kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    return n;
}
static struct { char mac[18]; int state; int pcm, known; unsigned avail;
                int choice, misses, armed, applied; unsigned gen; } g_cq;
static pthread_mutex_t g_cq_mu = PTHREAD_MUTEX_INITIALIZER;
#define BT_PCM_TRIES 15
struct bt_probe_arg { unsigned gen; char mac[18]; };
static void *bt_codec_probe_thread(void *arg){
    struct bt_probe_arg *pa = arg;
    char cmd[240], info[1536];
    info[0] = 0;
    if(bt_codec_query_cmd(pa->mac, cmd, sizeof cmd) == 0) bt_probe_run(cmd, info, sizeof info, 700);
    int known = 0; unsigned avail = bt_codec_parse_avail(info, &known);
    pthread_mutex_lock(&g_cq_mu);
    if(pa->gen == g_cq.gen){
        g_cq.pcm = strstr(info, "PCM=") != NULL; g_cq.known = known; g_cq.avail = avail; g_cq.state = 2;
    }
    pthread_mutex_unlock(&g_cq_mu);
    free(pa);
    return NULL;
}
int ui_bt_codec_frame(const char *mac, char *out, int cap){
    return bt_codec_frame_value(mac, 0, out, cap);
}
static int bt_codec_frame_value(const char *mac, int v, char *out, int cap){
    if(!bt_mac_valid(mac)) return -1;
    int r = snprintf(out, cap, "06b3%04X%04X%s", (unsigned)(12 + 17), (unsigned)v, mac);
    return (r > 0 && r < cap) ? 0 : -1;
}
int bt_codec_upgrade_step(void){
    char mac[18], f[48]; int send = -1, done = 0; struct bt_probe_arg *spawn = NULL;
    pthread_mutex_lock(&g_cq_mu);
    snprintf(mac, sizeof mac, "%s", g_cq.mac);
    if(!g_cq.armed || strcmp(ui_route_mac(), mac)) done = 1;
    else if(g_cq.state == 2){
        if(g_cq.pcm){
            int v = bt_codec_pick(g_cq.choice, g_cq.avail, g_cq.known);
            if(v != 0) send = v;
            done = 1; g_cq.applied = 1;
        } else if(++g_cq.misses >= BT_PCM_TRIES){ done = 1; g_cq.applied = 1; }
        g_cq.state = 0;
    }
    if(!done && g_cq.state == 0 && (spawn = malloc(sizeof *spawn)) != NULL){
        spawn->gen = g_cq.gen; snprintf(spawn->mac, sizeof spawn->mac, "%s", mac); g_cq.state = 1;
    }
    if(done) g_cq.armed = 0;
    pthread_mutex_unlock(&g_cq_mu);
    if(send > 0 && bt_codec_frame_value(mac, send, f, sizeof f) == 0) ipc_send_cmd(f);
    if(spawn){ pthread_t t;
               if(pthread_create(&t, NULL, bt_codec_probe_thread, spawn) == 0) pthread_detach(t);
               else { pthread_mutex_lock(&g_cq_mu); if(spawn->gen == g_cq.gen) g_cq.state = 0; pthread_mutex_unlock(&g_cq_mu); free(spawn); } }
    return done;
}
static lv_timer_t *g_cq_timer;
static void bt_codec_upgrade_cb(lv_timer_t *t){
    if(bt_codec_upgrade_step()){ lv_timer_del(t); g_cq_timer = NULL; }
}
void ui_bt_codec_upgrade_cancel(void){
    pthread_mutex_lock(&g_cq_mu);
    g_cq.gen++; g_cq.armed = 0; g_cq.applied = 0; g_cq.state = 0;
    pthread_mutex_unlock(&g_cq_mu);
}
void ui_bt_codec_upgrade_arm(const char *mac, int fresh){
    if(!bt_mac_valid(mac)) return;
    int choice = cfg_get_int("bt_codec", 0);
    if(choice < 0 || choice >= BT_CODEC_N) choice = 0;
    if(fw_os_ver() != 257) choice = 0;
    pthread_mutex_lock(&g_cq_mu);
    int same = !strcmp(g_cq.mac, mac) && (g_cq.armed || g_cq.applied);
    if(!fresh && (same || choice == 0)){ pthread_mutex_unlock(&g_cq_mu); return; }
    g_cq.gen++; g_cq.armed = 0; g_cq.applied = 0; g_cq.state = 0; g_cq.misses = 0;
    snprintf(g_cq.mac, sizeof g_cq.mac, "%s", mac);
    if(choice > 0){ g_cq.choice = choice; g_cq.armed = 1; }
    pthread_mutex_unlock(&g_cq_mu);
    if(choice > 0 && !g_cq_timer) g_cq_timer = lv_timer_create(bt_codec_upgrade_cb, 2000, NULL);
}
static void bt_enable(void){
    if(access("/tmp/bt_enabling", F_OK) == 0) return;
    system("touch /tmp/bt_enabling 2>/dev/null");
    system(
        "( killall -9 fiio_bluetoothctl brcm_patchram_plus bluetoothd bluealsa 2>/dev/null; "
        "  hciconfig hci0 down 2>/dev/null; "
        "  rfkill block bluetooth; sleep 1; "
        "  [ -e /tmp/bt_enabling ] || exit; "
        "  rfkill unblock bluetooth; sleep 1; "
        "  [ -e /tmp/bt_enabling ] || exit; "
        "  brcm_patchram_plus --enable_lpm --enable_hci --no2bytes --tosleep 200000 --baudrate 3000000 "
        "    --patchram /lib/firmware/bt_bcm/BCM4343A1_001.002.009.1026.1055.hcd /dev/ttyS0 >/tmp/patchram.log 2>&1 & "
        "  i=0; while [ \"$i\" -lt 40 ]; do [ -e /tmp/bt_enabling ] || exit; hciconfig hci0 up 2>/dev/null; "
        "    hciconfig hci0 2>/dev/null | grep -q RUNNING && break; sleep 0.5; i=$((i+1)); done; "
        "  [ -e /tmp/bt_enabling ] || exit; "
        "  /usr/project/bluetoothd --noplugin=sap --plugin=a2dp,avrcp --mode=source >/tmp/btd.log 2>&1 & "
        "  bluealsa -S --device=hci0 --profile=a2dp-source --sbc-quality=medium "
        "    --ldac-abr --ldac-quality=standard --codec=sbc --initial-volume=48 >/tmp/bluealsa.log 2>&1 & "
        "  sleep 1; [ -e /tmp/bt_enabling ] || exit; "
        "  bluetoothctl power on; hciconfig hci0 piscan; hciconfig hci0 class 0x200414; "
        "  bluetoothctl agent on; bluetoothctl default-agent; bluetoothctl pairable on; "
        "  rm -f /tmp/bt_enabling "
        ") >/dev/null 2>&1 &");
}
static void bt_disable(void){
    scan_abort();
    bt_autoroute_stop();
    system("( rm -f /tmp/bt_enabling; "
           "killall -9 bluealsa bluetoothd brcm_patchram_plus fiio_bluetoothctl bt-agent 2>/dev/null; "
           "hciconfig hci0 down >/dev/null 2>&1; "
           "rfkill block bluetooth >/dev/null 2>&1 ) >/dev/null 2>&1 &");
}

static int bt_mac_valid(const char *mac){
    if(!mac) return 0;
    for(int i = 0; i < 17; i++){
        char c = mac[i];
        if((i % 3) == 2){ if(c != ':') return 0; }
        else if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f'))) return 0;
    }
    return mac[17] == 0;
}

static void bt_autoroute_poll_cb(lv_timer_t *t){
    (void)t;
    char path[256], mac[20];
    int found = 0;
    if(run_cap_bounded("bluealsa-cli list-pcms 2>/dev/null | grep -m1 a2dpsrc", path, sizeof path, 250) > 0){
        char *dev = strstr(path, "dev_");
        if(dev){
            dev += 4;
            char *slash = strchr(dev, '/');
            if(slash && slash - dev == 17){
                memcpy(mac, dev, 17); mac[17] = 0;
                for(int i = 0; i < 17; i++) if(mac[i] == '_') mac[i] = ':';
                found = bt_mac_valid(mac);
            }
        }
    }
    if(!found){ g_bt_autorouted[0] = 0; ui_bt_codec_upgrade_cancel(); return; }
    if(strcmp(mac, g_bt_autorouted)){
        if(ui_route_bt(mac) == 0)
            snprintf(g_bt_autorouted, sizeof g_bt_autorouted, "%s", mac);
    }
}

static void bt_autoroute_start(void){
    if(g_bt_autoroute_timer) return;
    g_bt_autoroute_timer = lv_timer_create(bt_autoroute_poll_cb, 3000, NULL);
}
static void bt_autoroute_stop(void){
    if(g_bt_autoroute_timer){ lv_timer_del(g_bt_autoroute_timer); g_bt_autoroute_timer = NULL; }
    g_bt_autorouted[0] = 0;
}
void bt_notify_player_restart(void){
    g_bt_autorouted[0] = 0;
    if(bt_on() && !g_bt_autoroute_timer) bt_autoroute_start();
}

static int bt_info_prop(const char *buf, const char *key, char *out, int cap){
    if(cap <= 0) return 0;
    size_t klen = strlen(key);
    for(const char *l = buf; l && *l; ){
        const char *p = l; while(*p==' '||*p=='\t') p++;
        if(!strncmp(p, key, klen)){
            p += klen; while(*p==' '||*p=='\t') p++;
            int i=0; while(p[i] && p[i]!='\n' && p[i]!='\r' && i<cap-1){ out[i]=p[i]; i++; }
            while(i>0 && (out[i-1]==' '||out[i-1]=='\t')) i--;
            out[i]=0; return 1;
        }
        const char *nl = strchr(l, '\n'); l = nl ? nl+1 : NULL;
    }
    out[0]=0;
    return 0;
}
static int bt_dev_connected(const char *mac){
    if(!bt_mac_valid(mac)) return 0;
    char cmd[160], buf[2048];
    snprintf(cmd, sizeof cmd, "bluetoothctl info %s 2>/dev/null", mac);
    run_cap(cmd, buf, sizeof buf);
    for(char *l = buf; l && *l; ){
        char *nl = strchr(l, '\n'); if(nl) *nl = 0;
        char *p = l; while(*p == ' ' || *p == '\t') p++;
        if(!strncmp(p, "Connected:", 10)) return strstr(p, "yes") != NULL;
        if(!nl) break; l = nl + 1;
    }
    return 0;
}

static lv_timer_t *g_bt_conn_timer;
static uint32_t    g_bt_conn_start;
static char        g_bt_conn_mac[32];
static void bt_conn_poll_cb(lv_timer_t *t){
    (void)t;
    if(bt_dev_connected(g_bt_conn_mac)){
        lv_timer_del(g_bt_conn_timer); g_bt_conn_timer = NULL;
        if(ui_route_bt(g_bt_conn_mac) == 0){
            snprintf(g_bt_autorouted, sizeof g_bt_autorouted, "%s", g_bt_conn_mac);
            ui_toast("Connected");
        } else {
            ui_toast("Paired - audio stays on player");
        }
        scan_kick();
        return;
    }
    if(lv_tick_elaps(g_bt_conn_start) > 25000){
        lv_timer_del(g_bt_conn_timer); g_bt_conn_timer = NULL;
        ui_toast("Couldn't connect");
        scan_kick();
    }
}

static void bt_connect(const char *mac){
    if(!bt_mac_valid(mac)){ ui_toast("Bad device address"); return; }
    char cmd[256];
    snprintf(cmd, sizeof cmd,
             "( bluetoothctl pair %s; bluetoothctl trust %s; bluetoothctl connect %s ) >/dev/null 2>&1 &",
             mac, mac, mac);
    system(cmd);
    snprintf(g_bt_conn_mac, sizeof g_bt_conn_mac, "%s", mac);
    ui_toast("Connecting...");
    g_bt_conn_start = lv_tick_get();
    if(g_bt_conn_timer) lv_timer_del(g_bt_conn_timer);
    g_bt_conn_timer = lv_timer_create(bt_conn_poll_cb, 1500, NULL);
}
static void bt_disconnect(const char *mac){
    if(!bt_mac_valid(mac)) return;
    if(!strcmp(mac, g_bt_autorouted)) g_bt_autorouted[0] = 0;
    ui_route_analog();
    char cmd[128];
    snprintf(cmd, sizeof cmd, "bluetoothctl disconnect %s >/dev/null 2>&1 &", mac);
    system(cmd);
    ui_toast("Disconnecting...");
}

static void info_row(const char *key, const char *val){
    lv_obj_t *r = lv_obj_create(g_info_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 250, 40);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_50, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *k = lv_label_create(r);
    lv_label_set_text(k, key);
    lv_obj_set_pos(k, 12, 11);
    lv_obj_set_style_text_font(k, TF(UI_14), 0);
    lv_obj_set_style_text_color(k, TC(TEXT_MUTED), 0);
    lv_obj_t *v = lv_label_create(r);
    lv_label_set_text(v, val && val[0] ? val : "-");
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(v, 96, 11); lv_obj_set_size(v, 142, 18);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(v, TF(USER_14), 0);
    lv_obj_set_style_text_color(v, TC(TEXT_PRIMARY), 0);
}
static void info_forget_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(bt_mac_valid(g_sel_mac)){
        bt_disconnect(g_sel_mac);
        char cmd[128];
        snprintf(cmd, sizeof cmd, "bluetoothctl remove %s >/dev/null 2>&1", g_sel_mac);
        ui_toast(system(cmd) == 0 ? "Device forgotten" : "Couldn't forget device");
    }
    screen_back();
    if(g_scan_timer) lv_timer_del(g_scan_timer);
    g_scan_timer = lv_timer_create(scan_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(g_scan_timer, 1);
}
static void info_action_row(const char *label, lv_event_cb_t cb){
    lv_obj_t *r = lv_button_create(g_info_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 250, 44);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, TC(DANGER_SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, TC(DANGER_SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(r, cb, LV_EVENT_CLICKED, NULL, "bt.cb", UI_CORE);
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, label); lv_obj_center(t);
    lv_obj_set_style_text_font(t, TF(UI_14), 0);
    lv_obj_set_style_text_color(t, TC(STATUS_DANGER), 0);
}
static void info_disc_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    bt_disconnect(g_sel_mac);
    screen_back();
    if(g_scan_timer) lv_timer_del(g_scan_timer);
    g_scan_timer = lv_timer_create(scan_timer_cb, 4000, NULL);
    lv_timer_set_repeat_count(g_scan_timer, 1);
}
static void info_back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

void bt_info_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header_cb(root, "Device", info_back_cb);
    g_info_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_info_list);
    lv_obj_set_pos(g_info_list, 55, 78); lv_obj_set_size(g_info_list, 250, 190);
    lv_obj_set_style_bg_opa(g_info_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(g_info_list, 8, 0);
    lv_obj_set_flex_flow(g_info_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_info_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(g_info_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_info_list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *db = lv_button_create(root);
    lv_obj_remove_style_all(db);
    lv_obj_set_pos(db, 110, 286); lv_obj_set_size(db, 140, 38);
    lv_obj_set_style_radius(db, 12, 0);
    lv_obj_set_style_bg_color(db, TC(DANGER_PANEL_SURFACE), 0);
    lv_obj_set_style_bg_opa(db, LV_OPA_COVER, 0);
    ui_on(db, info_disc_cb, LV_EVENT_CLICKED, NULL, "bt.info_disc", UI_CORE);
    lv_obj_t *dl = lv_label_create(db);
    lv_label_set_text(dl, "Disconnect");
    lv_obj_center(dl);
    lv_obj_set_style_text_font(dl, TF(UI_16), 0);
    lv_obj_set_style_text_color(dl, TC(STATUS_DANGER_STRONG), 0);
}

void bt_info_open(void){
    if(!g_info_list || !bt_mac_valid(g_sel_mac)) return;
    lv_obj_clean(g_info_list);
    char cmd[128], buf[2048], val[96];
    snprintf(cmd, sizeof cmd, "bluetoothctl info %s 2>/dev/null", g_sel_mac);
    run_cap(cmd, buf, sizeof buf);
    char *p = strstr(buf, "Name: ");
    if(p && sscanf(p+6, "%95[^\n]", val) == 1) info_row("Name", val);
    info_row("Address", g_sel_mac);
    { char v[96];
      info_row("Connected", (bt_info_prop(buf,"Connected:",v,sizeof v) && !strcmp(v,"yes")) ? "Yes" : "No");
      info_row("Paired",    (bt_info_prop(buf,"Paired:",   v,sizeof v) && !strcmp(v,"yes")) ? "Yes" : "No");
      if(bt_info_prop(buf,"Icon:",v,sizeof v)) info_row("Type", v); }
    info_row("Audio", "On (beta)");
    info_action_row("Forget This Device", info_forget_cb);
    screen_show(SCR_BT_INFO);
}

static void row_free_cb(lv_event_t *e){ free(lv_obj_get_user_data(lv_event_get_target(e))); }
static void dev_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    lv_obj_t *row = lv_event_get_target(e);
    const char *mac = (const char*)lv_obj_get_user_data(row);
    int connected = (intptr_t)lv_event_get_user_data(e);
    if(!mac || !mac[0]) return;
    if(connected){
        snprintf(g_sel_mac, sizeof g_sel_mac, "%s", mac);
        ui_route_bt(mac);
        bt_info_open();
    } else {
        bt_connect(mac);
        list_msg("Connecting " LV_SYMBOL_BLUETOOTH);
        if(g_scan_timer) lv_timer_del(g_scan_timer);
        g_scan_timer = lv_timer_create(scan_timer_cb, 4000, NULL);
        lv_timer_set_repeat_count(g_scan_timer, 1);
    }
}

static void add_dev_row(const char *mac, const char *name, int connected){
    lv_obj_t *r = lv_button_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 268, 46);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, (connected ? TC(CONNECTED_SURFACE) : TC(SURFACE)), 0);
    lv_obj_set_style_bg_opa(r, connected ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    char *dup = strdup(mac);
    lv_obj_set_user_data(r, dup);
    ui_on(r, dev_cb, LV_EVENT_CLICKED, (void*)(intptr_t)connected, "bt.dev", UI_CORE);
    lv_obj_add_event_cb(r, row_free_cb, LV_EVENT_DELETE, NULL);

    int tx = 12;
    if(connected){
        lv_obj_t *ck = lv_label_create(r);
        lv_label_set_text(ck, LV_SYMBOL_OK);
        lv_obj_set_pos(ck, 12, 15);
        lv_obj_set_style_text_font(ck, TF(UI_14), 0);
        lv_obj_set_style_text_color(ck, TC(STATUS_INFO), 0);
        tx = 34;
    }
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, name);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t, tx, 13); lv_obj_set_size(t, 220 - tx, 20);
    lv_obj_set_style_text_font(t, TF(USER_16), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);

    lv_obj_t *ic = lv_label_create(r);
    lv_label_set_text(ic, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_pos(ic, 240, 14);
    lv_obj_set_style_text_font(ic, TF(UI_14), 0);
    lv_obj_set_style_text_color(ic, (connected ? TC(STATUS_INFO) : TC(TEXT_MUTED)), 0);
}

typedef struct { char mac[20]; char name[128]; int connected; } bt_scan_dev_t;
static pthread_mutex_t g_scan_mu = PTHREAD_MUTEX_INITIALIZER;
static bt_scan_dev_t   g_scan_res[40];
static int             g_scan_n = 0;
static int             g_scan_done = 0;
static int             g_scan_running = 0;
static unsigned        g_scan_gen = 0;
static lv_timer_t     *g_scanpoll_timer;
static int             g_scanpoll_n = 0;
static int             g_scan_pending = 0;

static void *scan_worker(void *arg){
    unsigned my_gen = (unsigned)(intptr_t)arg;
    char buf[8192]; run_cap("bluetoothctl devices 2>/dev/null", buf, sizeof buf);
    bt_scan_dev_t res[40]; int n = 0;
    char *l = buf;
    while(l && *l && n < 40){
        char *nl = strchr(l, '\n'); if(nl) *nl = 0;
        if(!strncmp(l, "Device ", 7) && strlen(l) > 7+17){
            char mac[20]; memcpy(mac, l+7, 17); mac[17]=0;
            const char *name = l + 7 + 17 + 1;
            if(name[0] && bt_mac_valid(mac)){
                char cmd[128], info[2048];
                snprintf(cmd, sizeof cmd, "bluetoothctl info %s 2>/dev/null", mac);
                run_cap(cmd, info, sizeof info);
                char icon[64], cv[16];
                if(bt_info_prop(info, "Icon:", icon, sizeof icon) && !strncmp(icon, "audio", 5)){
                    snprintf(res[n].mac,  sizeof res[n].mac,  "%s", mac);
                    snprintf(res[n].name, sizeof res[n].name, "%s", name);
                    res[n].connected = (bt_info_prop(info,"Connected:",cv,sizeof cv) && !strcmp(cv,"yes"));
                    n++;
                }
            }
        }
        if(!nl) break;
        l = nl + 1;
    }
    pthread_mutex_lock(&g_scan_mu);
    if(my_gen == g_scan_gen){
        memcpy(g_scan_res, res, (size_t)n * sizeof(bt_scan_dev_t));
        g_scan_n = n; g_scan_done = 1;
        g_scan_running = 0;
    }
    pthread_mutex_unlock(&g_scan_mu);
    return NULL;
}

static void scan_render(void){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    static bt_scan_dev_t local[40]; int n;
    pthread_mutex_lock(&g_scan_mu);
    n = g_scan_n; if(n > 40) n = 40;
    memcpy(local, g_scan_res, (size_t)n * sizeof(bt_scan_dev_t));
    pthread_mutex_unlock(&g_scan_mu);
    for(int i = 0; i < n; i++) if(local[i].connected)  add_dev_row(local[i].mac, local[i].name, 1);
    for(int i = 0; i < n; i++) if(!local[i].connected) add_dev_row(local[i].mac, local[i].name, 0);
    if(n == 0) list_msg("No audio devices found");
}

static void scanpoll_cb(lv_timer_t *t){
    (void)t;
    int done;
    pthread_mutex_lock(&g_scan_mu); done = g_scan_done; pthread_mutex_unlock(&g_scan_mu);
    if(!done){
        if(++g_scanpoll_n < 100) return;
        pthread_mutex_lock(&g_scan_mu); g_scan_gen++; g_scan_running = 0; pthread_mutex_unlock(&g_scan_mu);
    }
    if(g_scanpoll_timer){ lv_timer_del(g_scanpoll_timer); g_scanpoll_timer = NULL; }
    if(g_scan_pending){ g_scan_pending = 0; scan_kick(); return; }
    scan_render();
}

static void scan_ensure_poll(void){
    g_scanpoll_n = 0;
    if(g_scanpoll_timer) lv_timer_del(g_scanpoll_timer);
    g_scanpoll_timer = lv_timer_create(scanpoll_cb, 300, NULL);
}

static void scan_kick(void){
    pthread_mutex_lock(&g_scan_mu);
    if(g_scan_running){
        pthread_mutex_unlock(&g_scan_mu); g_scan_pending = 1; scan_ensure_poll(); return;
    }
    g_scan_running = 1; g_scan_done = 0; g_scan_n = 0; g_scan_gen++;
    unsigned gen = g_scan_gen;
    pthread_mutex_unlock(&g_scan_mu);
    g_scan_pending = 0;
    pthread_t th;
    if(pthread_create(&th, NULL, scan_worker, (void*)(intptr_t)gen) == 0){
        pthread_detach(th);
        scan_ensure_poll();
    } else {
        pthread_mutex_lock(&g_scan_mu); g_scan_running = 0; pthread_mutex_unlock(&g_scan_mu);
        if(g_scanpoll_timer){ lv_timer_del(g_scanpoll_timer); g_scanpoll_timer = NULL; }
        scan_render();
    }
}

static void scan_timer_cb(lv_timer_t *t){
    (void)t;
    if(g_scan_timer){ lv_timer_del(g_scan_timer); g_scan_timer = NULL; }
    scan_kick();
}
static int  scanwait_stay(void){ return screen_current() == SCR_BT; }
static void bt_up_then_scan(void){ bt_ensure_services(); bt_autoroute_start(); start_scan(); }
static void scanwait_timeout_cb(void){
    list_msg(bt_radio_on() ? "Bluetooth is starting up" : "Bluetooth is off");
}
static struct radio_watch g_scanwait_watch = {
    .env = &radio_lv_env, .interval_ms = 1000, .timeout_ms = 28000, .is_up = bt_on,
    .stay = scanwait_stay, .on_up = bt_up_then_scan, .on_timeout = scanwait_timeout_cb,
};

static void scan_abort(void){
    if(g_scan_timer){ lv_timer_del(g_scan_timer); g_scan_timer = NULL; }
    if(g_scanpoll_timer){ lv_timer_del(g_scanpoll_timer); g_scanpoll_timer = NULL; }
    radio_watch_cancel(&g_scanwait_watch);
    pthread_mutex_lock(&g_scan_mu);
    g_scan_gen++; g_scan_running = 0; g_scan_n = 0; g_scan_done = 0;
    pthread_mutex_unlock(&g_scan_mu);
}
static void start_scan(void){
    if(!bt_on()){ list_msg("Bluetooth is off"); return; }
    list_msg_scanning();
    system("bluetoothctl --timeout 13 scan on >/dev/null 2>&1 &");
    if(g_scan_timer) lv_timer_del(g_scan_timer);
    g_scan_timer = lv_timer_create(scan_timer_cb, 13500, NULL);
    lv_timer_set_repeat_count(g_scan_timer, 1);
}

static void bt_set_intent(int on){ cfg_set_int("bt_on", on); }
static void bt_radio_start(void){ bt_enable(); }
static void bt_radio_cleanup(void){
    if(g_bt_conn_timer){ lv_timer_del(g_bt_conn_timer); g_bt_conn_timer = NULL; }
    scan_abort();
    ui_route_analog();
}
static void bt_radio_timeout_cb(void){
    if(g_sw) lv_obj_clear_state(g_sw, LV_STATE_CHECKED);
    list_msg("Couldn't turn on Bluetooth");
}
static struct radio g_bt_radio = {
    .watch = { .env = &radio_lv_env, .interval_ms = 1000, .timeout_ms = 28000,
               .is_up = bt_on, .on_up = bt_up_then_scan, .on_timeout = bt_radio_timeout_cb },
    .set_intent = bt_set_intent, .start = bt_radio_start, .cleanup = bt_radio_cleanup, .power_down = bt_disable,
    .is_on = bt_radio_on, .timeout = RADIO_ROLLBACK, .toggle_observes = 1,
    .excludes = &g_scanwait_watch,
};
static void sw_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    int on = lv_obj_has_state(g_sw, LV_STATE_CHECKED);
    if(on){
        radio_on(&g_bt_radio, 1);
        list_msg("Turning on " LV_SYMBOL_BLUETOOTH);
    } else {
        radio_off(&g_bt_radio);
        list_msg("Bluetooth is off");
    }
}

int bt_toggle(void){
    int on = radio_toggle(&g_bt_radio);
    if(g_sw){ if(on) lv_obj_add_state(g_sw, LV_STATE_CHECKED); else lv_obj_clear_state(g_sw, LV_STATE_CHECKED); }
    ui_status_refresh();
    return on;
}

void bt_init_intent(void){
    if(cfg_get_int("bt_on", -1) >= 0) return;
    cfg_set_int("bt_on", 0);
}
static void bootrestore_up_cb(void){
    bt_ensure_services();
    bt_autoroute_start();
}
static struct radio_watch g_bootrestore_watch = {
    .env = &radio_lv_env, .interval_ms = 1000, .timeout_ms = 28000, .is_up = bt_on, .on_up = bootrestore_up_cb,
};
static lv_timer_t *g_bootoff_timer;
static uint32_t    g_bootoff_start;
static void bootoff_poll_cb(lv_timer_t *t){
    (void)t;
    static uint32_t last_off;
    int done = cfg_get_int("bt_on", 0) == 1 || session_saved_source() == 2 || lv_tick_elaps(g_bootoff_start) > 30000;
    if(!done && bt_radio_on() && (!last_off || lv_tick_elaps(last_off) >= 6000)){
        fprintf(stderr, "bt: radio came up with intent off at boot - powering it down\n"); fflush(stderr);
        last_off = lv_tick_get() ? lv_tick_get() : 1;
        bt_disable();
    }
    if(done){ lv_timer_del(g_bootoff_timer); g_bootoff_timer = NULL; }
}
void bt_boot_restore(void){
    bt_init_intent();
    if(cfg_get_int("bt_on", 0) != 1){
        g_bootoff_start = lv_tick_get();
        if(g_bootoff_timer) lv_timer_del(g_bootoff_timer);
        g_bootoff_timer = lv_timer_create(bootoff_poll_cb, 2000, NULL);
        bootoff_poll_cb(NULL);
        return;
    }
    if(bt_on()){ bt_ensure_services(); bt_autoroute_start(); return; }
    bt_enable();
    radio_watch_begin(&g_bootrestore_watch);
}

static void rescan_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) start_scan(); }
static void back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

void bt_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header_cb(root, "Bluetooth", back_cb);

    lv_obj_t *trow = lv_obj_create(root);
    lv_obj_remove_style_all(trow);
    lv_obj_set_pos(trow, 50, 64); lv_obj_set_size(trow, 208, 48);
    lv_obj_set_style_radius(trow, 12, 0);
    lv_obj_set_style_bg_color(trow, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(trow, LV_OPA_70, 0);
    lv_obj_clear_flag(trow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tl = lv_label_create(trow);
    lv_label_set_text(tl, "Bluetooth");
    lv_obj_set_pos(tl, 16, 14);
    lv_obj_set_style_text_font(tl, TF(UI_16), 0);
    lv_obj_set_style_text_color(tl, TC(TEXT_PRIMARY), 0);

    g_sw = lv_switch_create(trow);
    lv_obj_set_size(g_sw, 46, 24);
    lv_obj_set_ext_click_area(g_sw, 10);
    lv_obj_align(g_sw, LV_ALIGN_RIGHT_MID, -14, 0);
    ui_on(g_sw, sw_cb, LV_EVENT_VALUE_CHANGED, NULL, "bt.sw.value", UI_CORE);

    lv_obj_t *rb = lv_button_create(root);
    lv_obj_remove_style_all(rb);
    lv_obj_set_pos(rb, 266, 72); lv_obj_set_size(rb, 36, 32);
    lv_obj_set_ext_click_area(rb, 8);
    lv_obj_set_style_radius(rb, 10, 0);
    lv_obj_set_style_bg_color(rb, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(rb, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(rb, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    ui_on(rb, rescan_cb, LV_EVENT_CLICKED, NULL, "bt.rescan", UI_CORE);
    lv_obj_t *rl = lv_label_create(rb);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(rl, TF(UI_16), 0);
    lv_obj_set_style_text_color(rl, TC(TEXT_SECONDARY), 0);
    lv_obj_center(rl);

    lv_obj_t *note = lv_label_create(root);
    lv_label_set_text(note, "Connect a speaker to play audio (beta)");
    lv_obj_set_pos(note, 30, 120); lv_obj_set_width(note, 300);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(note, TF(UI_12), 0);
    lv_obj_set_style_text_color(note, TC(TEXT_FOOTNOTE), 0);

    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, 40, 138); lv_obj_set_size(g_list, 280, UI_LIST_H_FROM(138));
    lv_obj_set_style_pad_bottom(g_list, UI_LIST_PAD_BOTTOM, 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(g_list, 6, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
}

void bt_open(void){
    int want = cfg_get_int("bt_on", 0) || bt_radio_on();
    if(g_sw){
        if(want) lv_obj_add_state(g_sw, LV_STATE_CHECKED);
        else     lv_obj_clear_state(g_sw, LV_STATE_CHECKED);
    }
    screen_show(SCR_BT);
    radio_watch_cancel(&g_bt_radio.watch);
    radio_watch_cancel(&g_scanwait_watch);
    if(bt_on()){
        bt_ensure_services(); bt_autoroute_start(); start_scan();
    } else if(want){
        list_msg_scanning();
        radio_watch_begin(&g_scanwait_watch);
    } else {
        list_msg("Bluetooth is off");
    }
}
