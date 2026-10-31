/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "wifi.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "fwcaps.h"
#include "ipc.h"
#include "radio.h"
#include "sysconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include "ui_round.h"

#define WCLI "/usr/sbin/wpa_cli -i wlan0 "

static lv_obj_t *g_sw, *g_list;
static lv_timer_t *g_scan_timer;
static lv_timer_t *g_conn_timer;
static uint32_t    g_wifi_last_start = 0;
static int         g_sanitize_pending = 1;
static unsigned    g_intent_epoch = 0;
static char g_pending_ssid[64];
static char g_cur_ssid[64];
static lv_obj_t *g_info_list;

static void start_scan(void);
static void wifi_lease_retry_reset(void);
static void scan_fill(void);
static void wifi_info_refresh(void);
extern int ui_screen_backlight_on(void);

static lv_obj_t *g_scan_icon;
static void spin_anim_cb(void *o, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t*)o, v, 0); }
static void scan_stop(void){
    if(g_scan_icon){ lv_anim_delete(g_scan_icon, spin_anim_cb); g_scan_icon = NULL; }
}

static char *run_cap_all(const char *cmd, size_t max){
    char *b = malloc(max); if(!b) return NULL;
    ui_run_cap_bounded(cmd, b, (int)max, 2000);
    return b;
}
static int run_cap(const char *cmd, char *out, int cap){
    return ui_run_cap_bounded(cmd, out, cap, 1000);
}

static int wifi_dhcp_absent(void);

static int wifi_shell(const char *cmd){
    static pid_t launch;
    if(launch > 0){
        if(waitpid(launch, NULL, WNOHANG) != 0) launch = 0;
    }
    if(strstr(cmd, "wifi_up.sh")){
        int absent = wifi_dhcp_absent();
        if(absent < 0) return -1;
        if(!absent) return wifi_shell("rfkill unblock wifi; ifconfig wlan0 up; "
            "wpa_supplicant -B -i wlan0 -c /usr/data/wpa_supplicant.conf");
        if(launch > 0) return -1;
        launch = fork();
        if(launch == 0){ execl("/bin/sh", "sh", "-c", ". /usr/bin/wifi_up.sh; wait", (char *)NULL); _exit(127); }
        return launch < 0 ? -1 : 0;
    }
    char bounded[1024], out[64];
    snprintf(bounded, sizeof bounded, "%s; printf '\nRC=%%d\n' \"$?\"", cmd);
    ui_run_cap_bounded(bounded, out, sizeof out, 2000);
    return strstr(out, "\nRC=0\n") ? 0 : -1;
}
#define system wifi_shell

static int wifi_ssid_decode(const char *in, int inlen, char *out, int cap){
    int o = 0;
    for(int i=0;i<inlen;i++){
        unsigned char c = (unsigned char)in[i], v = c;
        if(c == '\\'){
            if(++i >= inlen) return -1;
            char e = in[i];
            if(e == '\\' || e == '"') v = (unsigned char)e;
            else if(e == 'x'){
                int h = 0;
                for(int k=1;k<=2;k++){
                    if(i + k >= inlen) return -1;
                    char d = in[i+k]; int dv;
                    if(d >= '0' && d <= '9') dv = d - '0';
                    else if(d >= 'a' && d <= 'f') dv = d - 'a' + 10;
                    else if(d >= 'A' && d <= 'F') dv = d - 'A' + 10;
                    else return -1;
                    h = h * 16 + dv;
                }
                v = (unsigned char)h; i += 2;
            }
            else if(e == 'n') v = '\n';
            else if(e == 'r') v = '\r';
            else if(e == 't') v = '\t';
            else if(e == 'e') v = 27;
            else return -1;
        }
        if(v < 0x20 || v == 0x7f) return -1;
        if(o >= 32 || o >= cap - 1) return -1;
        out[o++] = (char)v;
    }
    out[o] = 0;
    return o;
}
static int wifi_ssid_field_eq(const char *fld, int flen, const char *ssid){
    char d[64]; int n = wifi_ssid_decode(fld, flen, d, sizeof d);
    return n >= 0 && (size_t)n == strlen(ssid) && memcmp(d, ssid, (size_t)n) == 0;
}

static int wifi_lease_ready(const char *status);
static int wifi_status(char *ssid, int scap, char *ip, int icap){
    (void)scap; (void)icap;
    ssid[0] = 0; ip[0] = 0;
    char buf[2048];
    if(!run_cap(WCLI "status 2>/dev/null", buf, sizeof buf)) return 0;
    int connected = (strstr(buf, "wpa_state=COMPLETED") != NULL);
    char *l = buf;
    while(l && *l){
        if(!strncmp(l, "ssid=", 5)){
            char *e = strchr(l+5, '\n'); int n = e ? (int)(e - (l+5)) : (int)strlen(l+5);
            if(wifi_ssid_decode(l+5, n, ssid, 64) < 0) ssid[0] = 0;
        }
        else if(!strncmp(l, "ip_address=", 11)){ sscanf(l+11, "%31[^\n]", ip); }
        l = strchr(l, '\n'); if(l) l++;
    }
    return connected && wifi_lease_ready(buf);
}

static int wifi_radio_on(void){
    char buf[256];
    run_cap("pidof wpa_supplicant 2>/dev/null", buf, sizeof buf);
    return buf[0] ? 1 : 0;
}

static int wpa_ready(void){
    char buf[64];
    run_cap(WCLI "ping 2>/dev/null", buf, sizeof buf);
    return strstr(buf, "PONG") != NULL;
}
static int wifi_sanitize_savenets(void){
    if(!wpa_ready()) return 0;
    system("" WCLI "set update_config 1 >/dev/null 2>&1");
    system("" WCLI "enable_network all >/dev/null 2>&1");
    char buf[4096];
    run_cap(WCLI "list_networks 2>/dev/null", buf, sizeof buf);
    char *l = strchr(buf, '\n'); if(l) l++;
    while(l && *l){
        int nid;
        if(sscanf(l, "%d", &nid) == 1){
            char c[96];
            snprintf(c, sizeof c, "" WCLI "set_network %d frequency 0 >/dev/null 2>&1", nid);
            system(c);
        }
        l = strchr(l, '\n'); if(l) l++;
    }
    system("" WCLI "save_config >/dev/null 2>&1");
    return 1;
}

void wifi_init_intent(void){
    if(cfg_get_int("wifi_on", -1) >= 0) return;
    int on = sysconfig_get_via(ui_run_cap_bounded, SC_WIFI_STATUS, 1000);
    if(on >= 0) cfg_set_int("wifi_on", on);
}

#define RECO_SETTLE_MS 9000
#define RECO_GIVEUP_MS 30000
#define RECO_WINDOW_MS 30000
#define RECO_PROBE_MS  1000
static int      g_reco_hold = 0;
static unsigned g_reco_gen  = 0xFFFFFFFFu;
static unsigned g_reco_rx0, g_reco_epoch0;
static uint32_t g_reco_armed_at, g_reco_first_rx_at, g_reco_probe_at, g_reco_eval_at;
static int      g_reco_seen_rx;

static int wifi_reco_decide(int db_wifi, int db_bt, int eff_wifi, int eff_bt, int want_wifi, int user_changed){
    return db_wifi == 0 && db_bt == 1 && eff_wifi == 0 && eff_bt == 1 && want_wifi == 1 && !user_changed;
}

static int read_saved_radios(int *wifi, int *bt){
    static const sc_col_t cols[2] = { SC_WIFI_STATUS, SC_BT_STATUS };
    int v[2];
    if(sysconfig_get_many_via(ui_run_cap_bounded, cols, 2, v, RECO_PROBE_MS) != 2) return 0;
    *wifi = v[0]; *bt = v[1];
    return 1;
}
static int wifi_rfkill_blocked(void){
    for(int i = 0; i < 12; i++){
        char p[64]; snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/type", i);
        FILE *f = fopen(p, "r"); if(!f) continue;
        char t[16] = {0}; char *r = fgets(t, sizeof t, f); fclose(f);
        if(!r || strncmp(t, "wlan", 4) != 0) continue;
        snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/soft", i);
        f = fopen(p, "r"); if(!f) return -1;
        int soft = -1; if(fscanf(f, "%d", &soft) != 1) soft = -1; fclose(f);
        snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/hard", i);
        f = fopen(p, "r"); if(!f) return -1;
        int hard = -1; if(fscanf(f, "%d", &hard) != 1) hard = -1; fclose(f);
        return soft == 1 || hard == 1 ? 1 : (soft == 0 && hard == 0 ? 0 : -1);
    }
    return -1;
}
static int wifi_effective(void){
    char buf[64] = {0};
    int rc = ui_run_cap_bounded("pidof wpa_supplicant 2>/dev/null; echo done", buf, sizeof buf, RECO_PROBE_MS);
    if(rc <= 0 || !strstr(buf, "done")) return -1;
    int daemon = (buf[0] >= '0' && buf[0] <= '9');
    int blocked = wifi_rfkill_blocked();
    if(daemon || blocked == 0) return 1;
    return blocked == 1 ? 0 : -1;
}

static void wifi_reconcile_poll(void){
    if(!fw_has_radio_boot_arbitration()){ g_reco_hold = 0; return; }
    unsigned gen = ipc_generation(), rx = ipc_rx_frames();
    uint32_t now = lv_tick_get();
    int first_generation = 0;
    if(gen != g_reco_gen){
        first_generation = (g_reco_gen == 0xFFFFFFFFu);
        g_reco_gen = gen; g_reco_hold = 1; g_reco_rx0 = rx; g_reco_epoch0 = g_intent_epoch;
        g_reco_armed_at = now; g_reco_seen_rx = 0; g_reco_probe_at = now - 1000; g_reco_eval_at = now - RECO_PROBE_MS;
    }
    if(!g_reco_hold) return;
    if(g_intent_epoch != g_reco_epoch0){ g_reco_hold = 0; return; }
    if(cfg_get_int("wifi_on", 1) == 0){ g_reco_hold = 0; return; }
    if(first_generation){
        int dbw, dbb;
        if(read_saved_radios(&dbw, &dbb) && dbb == 0){
            g_reco_hold = 0;
            fprintf(stderr, "radio reconcile (gen %u): saved Bluetooth off, released boot hold\n", gen);
            return;
        }
    }
    if(!g_reco_seen_rx){
        if(rx == g_reco_rx0){
            if(now - g_reco_armed_at >= RECO_GIVEUP_MS){ g_reco_hold = 0; return; }
            if(now - g_reco_probe_at >= 1000){ g_reco_probe_at = now; ipc_send_probe("02020008"); }
            return;
        }
        g_reco_seen_rx = 1; g_reco_first_rx_at = now;
    }
    if(now - g_reco_first_rx_at < RECO_SETTLE_MS) return;
    if(now - g_reco_eval_at < 1000) return;
    g_reco_eval_at = now;
    int dbw = -1, dbb = -1;
    if(!read_saved_radios(&dbw, &dbb)) dbw = dbb = -1;
    int effw = wifi_effective(), effb = bt_radio_on();
    if(lv_tick_get() - g_reco_first_rx_at >= RECO_WINDOW_MS){ g_reco_hold = 0; return; }
    if(g_intent_epoch != g_reco_epoch0){ g_reco_hold = 0; return; }
    if(!cfg_get_int(CFG_PDBG, 0) && wifi_reco_decide(dbw, dbb, effw, effb, cfg_get_int("wifi_on", 1), 0)){
        cfg_set_int("wifi_on", 0);
        ui_toast("Wi-Fi off: Bluetooth was on");
        fprintf(stderr, "radio reconcile (gen %u): accepted stock's Bluetooth-priority boot, wifi_on=0\n", gen);
        g_reco_hold = 0;
        return;
    }
    if(now - g_reco_first_rx_at >= RECO_WINDOW_MS) g_reco_hold = 0;
}

struct wifi_proc { int pid; unsigned long long start; };
struct wifi_obs { struct wifi_proc wpa, dhcp; int id, ip, idle, sockets, allowed; char ssid[64]; };
static struct {
    struct wifi_obs net; unsigned epoch, intent, player; uint32_t since;
    int phase, seen_empty, release_evidence, released, resume;
} g_lease;
enum { LEASE_NONE, LEASE_RELEASE, LEASE_ACQUIRE, LEASE_READY, LEASE_FAILED };
static char g_lease_dir[64];
static long g_lease_offset;
static pid_t g_lease_helper;
static uint32_t g_lease_check, g_lease_retry_ms = 100;
static int g_lease_settled;

static int wifi_proc_stat(int pid, struct wifi_proc *p, int *parent){
    char path[64], buf[1024]; snprintf(path, sizeof path, "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r"); if(!f) return 0;
    char *r = fgets(buf, sizeof buf, f); fclose(f);
    char *q = r ? strrchr(buf, ')') : NULL; if(!q) return 0;
    char *save, *t = strtok_r(q + 2, " ", &save); unsigned long long start = 0;
    for(int field = 3; t && field <= 22; field++, t = strtok_r(NULL, " ", &save)){
        if(field == 4) *parent = atoi(t);
        if(field == 22) start = strtoull(t, NULL, 10);
    }
    p->pid = pid; p->start = start; return start != 0;
}
static int wifi_proc_match(int pid, const char *name){
    char path[64], buf[2048]; snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r"); if(!f) return errno == ENOENT ? 0 : -1;
    size_t n = fread(buf, 1, sizeof buf - 1, f); int err = ferror(f); fclose(f); buf[n] = 0;
    if(err || n == sizeof buf - 1) return -1;
    if(!n) return 0;
    const char *first = strrchr(buf, '/'); first = first ? first + 1 : buf;
    int app = 0, iface = 0;
    for(size_t i = 0; i < n; ){
        const char *arg = buf + i, *base = strrchr(arg, '/'); base = base ? base + 1 : arg;
        if((i == 0 || (i == strlen(buf) + 1 && !strcmp(first, "busybox"))) && !strcmp(base, name)) app = 1;
        if(!strcmp(arg, "-iwlan0")) iface = 1;
        if(!strcmp(arg, "-i") && i + strlen(arg) + 1 < n && !strcmp(arg + strlen(arg) + 1, "wlan0")) iface = 1;
        i += strlen(arg) + 1;
    }
    return app && iface;
}
static int wifi_proc_find(const char *name, struct wifi_proc *p){
    DIR *d = opendir("/proc"); if(!d) return 0;
    struct dirent *e; int count = 0, parent; struct wifi_proc found = {0};
    while((e = readdir(d))){
        int pid = atoi(e->d_name); if(pid <= 0) continue;
        int match = wifi_proc_match(pid, name);
        if(match < 0){ count += 2; continue; }
        if(!match) continue;
        count++;
        if(wifi_proc_stat(pid, &found, &parent)) *p = found; else count += 2;
    }
    closedir(d); if(count != 1) memset(p, 0, sizeof *p); return count == 1;
}
static int wifi_dhcp_absent(void){
    DIR *d = opendir("/proc"); if(!d) return -1;
    struct dirent *e; int absent = 1;
    while((e = readdir(d))){
        int pid = atoi(e->d_name); if(pid <= 0) continue;
        int match = wifi_proc_match(pid, "udhcpc");
        if(match < 0) absent = -1;
        else if(match){ absent = 0; break; }
    }
    closedir(d); return absent;
}
static int wifi_proc_eq(struct wifi_proc a, struct wifi_proc b){ return a.pid && a.pid == b.pid && a.start == b.start; }
static int wifi_lease_signal_context(int sig);
static int wifi_dhcp_signal(struct wifi_proc expected, int sig){
    struct wifi_proc current = {0};
    return wifi_lease_signal_context(sig) && wifi_proc_find("udhcpc", &current) && wifi_proc_eq(expected, current) && kill(current.pid, sig) == 0;
}
static int wifi_dhcp_idle(struct wifi_proc p, int *sockets){
    char path[64], link[128]; int idle = 1; *sockets = 0;
    DIR *d = opendir("/proc"); if(!d) return 0;
    struct dirent *e; struct wifi_proc child; int parent;
    while((e = readdir(d))){
        int pid = atoi(e->d_name);
        if(pid > 0 && (!wifi_proc_stat(pid, &child, &parent) || parent == p.pid)) idle = 0;
    }
    closedir(d); snprintf(path, sizeof path, "/proc/%d/fd", p.pid);
    d = opendir(path); if(!d) return 0;
    while((e = readdir(d))){
        if(e->d_name[0] == '.') continue;
        char fd[128]; snprintf(fd, sizeof fd, "%s/%.32s", path, e->d_name);
        ssize_t n = readlink(fd, link, sizeof link - 1); if(n < 0){ idle = 0; continue; }
        link[n] = 0; if(!strncmp(link, "socket:[", 8)) (*sockets)++;
    }
    closedir(d); return idle;
}
static int wifi_ipv4(void){
    int fd = socket(AF_INET, SOCK_DGRAM, 0); if(fd < 0) return -1;
    struct ifreq req; memset(&req, 0, sizeof req); strcpy(req.ifr_name, "wlan0");
    int flags = ioctl(fd, SIOCGIFFLAGS, &req);
    int up = flags == 0 && (req.ifr_flags & IFF_UP);
    int rc = ioctl(fd, SIOCGIFADDR, &req), err = errno; close(fd);
    if(!up) return -1;
    if(rc < 0) return err == EADDRNOTAVAIL ? 0 : -1;
    return ((struct sockaddr_in *)&req.ifr_addr)->sin_addr.s_addr != 0;
}
static void wifi_lease_observe(struct wifi_obs *o, const char *status){
    memset(o, 0, sizeof *o); o->id = -1; o->ip = wifi_ipv4();
    wifi_proc_find("wpa_supplicant", &o->wpa); wifi_proc_find("udhcpc", &o->dhcp);
    for(const char *l = status; l && *l; ){
        if(!strncmp(l, "id=", 3)) o->id = atoi(l + 3);
        if(!strncmp(l, "ssid=", 5)){
            const char *end = strchr(l + 5, '\n');
            if(wifi_ssid_decode(l + 5, end ? end - l - 5 : strlen(l + 5), o->ssid, sizeof o->ssid) < 0) o->ssid[0] = 0;
        }
        l = strchr(l, '\n'); if(l) l++;
    }
    o->allowed = !g_reco_hold && cfg_get_int("wifi_on", 1) && wifi_rfkill_blocked() == 0 &&
        o->wpa.pid && o->dhcp.pid && o->ip >= 0 && o->id >= 0 && o->ssid[0] &&
        (strstr(status, "\nwpa_state=COMPLETED\n") || !strncmp(status, "wpa_state=COMPLETED\n", 20));
    if(o->dhcp.pid) o->idle = wifi_dhcp_idle(o->dhcp, &o->sockets);
}
static int wifi_lease_matches(const struct wifi_obs *o){
    return wifi_proc_eq(o->wpa, g_lease.net.wpa) && wifi_proc_eq(o->dhcp, g_lease.net.dhcp) &&
        o->id == g_lease.net.id && !strcmp(o->ssid, g_lease.net.ssid) &&
        g_lease.intent == g_intent_epoch && g_lease.player == ipc_generation();
}
static int wifi_lease_signal_context(int sig){
    char status[2048]; run_cap(WCLI "status 2>/dev/null", status, sizeof status);
    struct wifi_obs o; wifi_lease_observe(&o, status);
    if(!o.allowed || !wifi_lease_matches(&o)) return 0;
    if(sig == SIGUSR2) g_lease.release_evidence |= o.ip == 1 || o.sockets || !o.idle;
    return sig != SIGUSR1 || (o.ip == 0 && o.idle && !o.sockets);
}
static void wifi_lease_advance(const struct wifi_obs *o, uint32_t now){
    int same_owner = wifi_proc_eq(o->dhcp, g_lease.net.dhcp);
    if(o->dhcp.pid && !same_owner) g_lease.released = g_lease.release_evidence = 0;
    if(same_owner && g_lease.release_evidence &&
       (g_lease.phase == LEASE_RELEASE || g_lease.phase == LEASE_FAILED) &&
       now != g_lease.since && o->ip == 0 && o->idle && !o->sockets)
        g_lease.released = 1;
    if(o->ip != 0 || !o->idle || o->sockets) g_lease.released = 0;
    if(!o->allowed){
        if(o->ip == 0 && !g_lease.epoch) g_lease.seen_empty = 1;
        if(g_lease.phase == LEASE_RELEASE || g_lease.phase == LEASE_ACQUIRE){
            g_lease.resume = g_lease.phase; g_lease.phase = LEASE_FAILED;
        }
        if(g_lease.epoch && o->dhcp.pid && !same_owner) g_lease.phase = LEASE_NONE;
        return;
    }
    if(g_lease.phase == LEASE_FAILED && g_lease.resume == LEASE_ACQUIRE &&
       wifi_lease_matches(o) && now - g_lease.since <= 30000){
        g_lease.phase = LEASE_ACQUIRE; g_lease.resume = LEASE_NONE;
    }
    if(!wifi_lease_matches(o) || g_lease.phase == LEASE_NONE ||
       (g_lease.phase == LEASE_FAILED && now - g_lease.since >= 1000)){
        int first = !g_lease.epoch && g_lease.seen_empty && o->ip == 0;
        g_lease.net = *o; g_lease.intent = g_intent_epoch; g_lease.player = ipc_generation();
        g_lease.epoch++; g_lease.since = now; g_lease.seen_empty = first; g_lease.resume = LEASE_NONE;
        if(first) g_lease.phase = LEASE_ACQUIRE;
        else if(g_lease.released){
            g_lease.seen_empty = 1;
            g_lease.phase = wifi_dhcp_signal(o->dhcp, SIGUSR1) ? LEASE_ACQUIRE : LEASE_FAILED;
            if(g_lease.phase == LEASE_ACQUIRE) g_lease.released = g_lease.release_evidence = 0;
        } else {
            g_lease.release_evidence |= o->ip == 1 || o->sockets || !o->idle;
            g_lease.phase = wifi_dhcp_signal(o->dhcp, SIGUSR2) ? LEASE_RELEASE : LEASE_FAILED;
        }
        return;
    }
    if(g_lease.phase == LEASE_READY){ if(!o->ip) g_lease.phase = LEASE_FAILED; return; }
    if(now - g_lease.since > 30000){ g_lease.phase = LEASE_FAILED; return; }
    if(g_lease.phase == LEASE_RELEASE){
        if(o->ip == 1 || o->sockets || !o->idle) g_lease.release_evidence = 1;
        if(g_lease.released){
            g_lease.seen_empty = 1; g_lease.since = now;
            g_lease.phase = wifi_dhcp_signal(o->dhcp, SIGUSR1) ? LEASE_ACQUIRE : LEASE_FAILED;
            if(g_lease.phase == LEASE_ACQUIRE) g_lease.released = g_lease.release_evidence = 0;
        }
    } else if(g_lease.phase == LEASE_ACQUIRE && g_lease.seen_empty && o->ip == 1 && o->idle)
        g_lease.phase = LEASE_READY;
}
static int wifi_lease_ready(const char *status){
    if(g_lease.phase != LEASE_READY) return 0;
    struct wifi_obs o; wifi_lease_observe(&o, status);
    return o.allowed && o.ip == 1 && o.idle && wifi_lease_matches(&o);
}
static void wifi_lease_events(void){
    if(g_lease_helper && waitpid(g_lease_helper, NULL, WNOHANG) == 0) return;
    g_lease_helper = 0;
    if(!g_lease_dir[0]){
        strcpy(g_lease_dir, "/tmp/diskos-wifi-XXXXXX");
        if(!mkdtemp(g_lease_dir)){ g_lease_dir[0] = 0; return; }
        char path[96]; snprintf(path, sizeof path, "%s/action", g_lease_dir);
        FILE *f = fopen(path, "w"); if(!f) return;
        fprintf(f, "#!/bin/sh\nprintf '%%s %%s\\n' \"$$\" \"$2\" >> %s/event\n", g_lease_dir);
        fclose(f); if(chmod(path, 0700)) return;
    }
    char path[96]; snprintf(path, sizeof path, "%s/action", g_lease_dir);
    pid_t owner = getpid(); g_lease_helper = fork();
    if(g_lease_helper == 0){
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != owner) _exit(127);
        int fd = open("/dev/null", O_RDWR);
        if(fd >= 0){ dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); if(fd > 2) close(fd); }
        execl("/usr/sbin/wpa_cli", "wpa_cli", "-i", "wlan0", "-a", path, (char *)NULL); _exit(127);
    }
    if(g_lease_helper < 0) g_lease_helper = 0;
}
static int wifi_lease_slow(void){
    return g_lease.phase == LEASE_FAILED && !g_conn_timer && !g_scan_timer &&
        !(g_lease.resume == LEASE_ACQUIRE && lv_tick_elaps(g_lease.since) <= 30000);
}
static void wifi_lease_step(void){
    static unsigned intent, player;
    static int want_prev = -1, lit_prev, screen_prev = -1;
    static uint32_t input_prev;
    int want_now = cfg_get_int("wifi_on", 1), lit = ui_screen_backlight_on(), screen = screen_current();
    unsigned gen = ipc_generation();
    uint32_t tick, idle;
    do { tick = lv_tick_get(); idle = lv_display_get_inactive_time(NULL); } while(lv_tick_get() != tick);
    uint32_t input = tick - idle;
    if(intent != g_intent_epoch || player != gen || want_prev != want_now ||
       (lit && !lit_prev) || screen_prev != screen || input != input_prev)
        wifi_lease_retry_reset();
    intent = g_intent_epoch; player = gen; want_prev = want_now;
    lit_prev = lit; screen_prev = screen; input_prev = input;
    if(!wifi_lease_slow()) g_lease_retry_ms = 100;
    else if(lv_tick_elaps(g_lease_check) < g_lease_retry_ms) return;
    g_lease_check = lv_tick_get();
    if(!cfg_get_int("wifi_on", 1)) {
        g_lease.phase = LEASE_FAILED; g_lease.resume = LEASE_NONE;
        if(g_lease_helper > 0){
            if(waitpid(g_lease_helper, NULL, WNOHANG) == 0) kill(g_lease_helper, SIGTERM);
            else g_lease_helper = 0;
        }
        if(screen_current() == SCR_WIFI_INFO) wifi_info_refresh();
        g_lease_settled = 1;
        return;
    }
    wifi_lease_events();
    char status[2048];
    run_cap(WCLI "status 2>/dev/null", status, sizeof status);
    struct wifi_obs o; wifi_lease_observe(&o, status); wifi_lease_advance(&o, lv_tick_get());
    g_lease.net.allowed = o.allowed && o.ip == 1 && o.idle;
    if(!o.allowed || g_lease.phase == LEASE_READY || g_lease.phase == LEASE_FAILED) g_lease_settled = 1;
    if(wifi_lease_slow()){
        g_lease_retry_ms *= 2;
        if(g_lease_retry_ms > 4000) g_lease_retry_ms = 4000;
    } else g_lease_retry_ms = 100;
    if(ui_screen_backlight_on()){
        const char *cur = o.allowed && g_lease.phase == LEASE_READY ? o.ssid : "";
        if(screen_current() == SCR_WIFI && !g_scan_timer && strcmp(cur, g_cur_ssid)) scan_fill();
        if(screen_current() == SCR_WIFI_INFO) wifi_info_refresh();
    }
}

static void wifi_lease_retry_reset(void){
    g_lease_retry_ms = 100; g_lease_check = lv_tick_get() - 100;
}

static int wifi_lease_consume(void){
    if(!g_lease_dir[0]) return 0;
    char path[96], event[128]; snprintf(path, sizeof path, "%s/event", g_lease_dir);
    FILE *f = fopen(path, "r"); if(!f) return 0;
    if(fseek(f, g_lease_offset, SEEK_SET)){ fclose(f); return 0; }
    int consumed = 0;
    while(fgets(event, sizeof event, f)){
        if(!strchr(event, '\n')) break;
        g_lease_offset = ftell(f);
        if(strstr(event, "DISCONNECTED")){
            g_lease.net.allowed = 0;
            if(g_lease.phase == LEASE_RELEASE || g_lease.phase == LEASE_ACQUIRE){
                g_lease.resume = g_lease.phase; g_lease.phase = LEASE_FAILED;
            }
        } else if(!strstr(event, "CONNECTED")) continue;
        consumed = 1;
        wifi_lease_retry_reset();
        wifi_lease_step();
    }
    fclose(f); return consumed;
}

void wifi_supervise(void){
    wifi_reconcile_poll();
    if(g_reco_hold) return;
    static uint32_t lease_check;
    int event = wifi_lease_consume();
    if(!event && lv_tick_elaps(lease_check) >= 100 &&
       (g_lease.phase == LEASE_RELEASE || g_lease.phase == LEASE_ACQUIRE ||
        g_lease.phase == LEASE_FAILED)){
        lease_check = lv_tick_get(); wifi_lease_step();
    }
    static uint32_t last_check = 0;
    static int      first = 1;
    if(!first && lv_tick_elaps(last_check) < 5000) return;
    first = 0; last_check = lv_tick_get();

    wifi_lease_step();
    int up   = wifi_radio_on();
    int want = cfg_get_int("wifi_on", 1);

    if(up && g_sanitize_pending && !g_conn_timer && wifi_sanitize_savenets()) g_sanitize_pending = 0;

    if(!up && want){
        if(!g_wifi_last_start || lv_tick_elaps(g_wifi_last_start) > 30000){
            g_wifi_last_start = lv_tick_get();
            g_sanitize_pending = 1;
            system("/usr/bin/wifi_up.sh >/dev/null 2>&1 &");
        }
    } else if(up && !want){
        system("/usr/bin/wifi_down.sh >/dev/null 2>&1");
    }
}

int wifi_connectivity(void){
    if(!cfg_get_int("wifi_on", 1)) return 0;
    if(g_reco_hold || !g_lease_settled) return -1;
    return g_lease.net.allowed && g_lease.phase == LEASE_READY &&
           g_lease.intent == g_intent_epoch && g_lease.player == ipc_generation();
}

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

static int get_gateway(char *out, int cap){
    out[0] = 0;
    FILE *f = fopen("/proc/net/route", "r");
    if(!f) return 0;
    char line[256]; int got = 0;
    if(!fgets(line, sizeof line, f)){ fclose(f); return 0; }
    while(fgets(line, sizeof line, f)){
        char iface[32]; unsigned long dest, gw;
        if(sscanf(line, "%31s %lx %lx", iface, &dest, &gw) == 3 && dest == 0 && gw != 0){
            snprintf(out, cap, "%lu.%lu.%lu.%lu",
                     gw & 0xFF, (gw>>8)&0xFF, (gw>>16)&0xFF, (gw>>24)&0xFF);
            got = 1; break;
        }
    }
    fclose(f);
    return got;
}

static void wpa_q(const char *v, char *out, int cap){
    int o = 0;
    #define WQ_PUT(ch) do{ if(o < cap-1) out[o++] = (char)(ch); }while(0)
    WQ_PUT('\'');
    WQ_PUT('"');
    for(; *v && o < cap-8; v++){
        if(*v == '\'') { WQ_PUT('\''); WQ_PUT('\\'); WQ_PUT('\''); WQ_PUT('\''); }
        else WQ_PUT(*v);
    }
    WQ_PUT('"');
    WQ_PUT('\'');
    out[o] = 0;
    #undef WQ_PUT
}
static void wpa_hex(const char *v, char *out, int cap){
    int o = 0;
    for(; *v && o + 2 < cap; v++){ snprintf(out + o, 3, "%02x", (unsigned char)*v); o += 2; }
    out[o] = 0;
}

static uint32_t    g_conn_start;
static char        g_conn_ssid[64];
static int         g_conn_id = -1;

static int wifi_net_temp_disabled(const char *list, int id){
    const char *l = strchr(list, '\n');
    l = l ? l + 1 : "";
    while(*l){
        const char *nl = strchr(l, '\n'); size_t len = nl ? (size_t)(nl - l) : strlen(l);
        char *end; long v = strtol(l, &end, 10);
        if(end != l && *end == '\t' && v == id && (size_t)(end - l) < len){
            const char *f = end;
            int tabs = 0;
            while(f < l + len && tabs < 3){ if(*f == '\t') tabs++; if(tabs < 3) f++; }
            if(tabs == 3){
                f++;
                size_t rest = (size_t)(l + len - f);
                for(size_t i = 0; i + 15 <= rest; i++)
                    if(!strncmp(f + i, "[TEMP-DISABLED]", 15)) return 1;
            }
        }
        if(!nl) break;
        l = nl + 1;
    }
    return 0;
}

static void conn_fail(const char *why){
    lv_timer_del(g_conn_timer); g_conn_timer = NULL;
    if(wifi_sanitize_savenets()) g_sanitize_pending = 0;
    ui_toast(why);
    start_scan();
}
static void conn_poll_cb(lv_timer_t *t){
    (void)t;
    char ss[64], ip[32];
    if(wifi_status(ss, sizeof ss, ip, sizeof ip) && ip[0] && g_lease.net.id == g_conn_id && !strcmp(ss, g_conn_ssid)){
        lv_timer_del(g_conn_timer); g_conn_timer = NULL;
        snprintf(g_cur_ssid, sizeof g_cur_ssid, "%s", g_conn_ssid);
        if(wifi_sanitize_savenets()) g_sanitize_pending = 0;
        ui_toast("Connected");
        start_scan();
        return;
    }
    char nl[2048]; run_cap(WCLI "list_networks 2>/dev/null", nl, sizeof nl);
    if(wifi_net_temp_disabled(nl, g_conn_id)){ conn_fail("Couldn't connect - check the password"); return; }
    if(lv_tick_elaps(g_conn_start) > 75000)
        conn_fail("Couldn't connect");
}

static int wifi_reply_ok(const char *out){
    return out[0] == 'O' && out[1] == 'K' && (out[2] == 0 || out[2] == '\n' || out[2] == '\r');
}
static int wifi_step(const char *cmd, const char *step){
    char out[64]; run_cap(cmd, out, sizeof out);
    if(wifi_reply_ok(out)) return 0;
    fprintf(stderr, "wifi connect: %s failed (reply '%.20s')\n", step, out);
    return -1;
}
static int wifi_psk_len_ok(const char *key){ size_t n = strlen(key); return n >= 8 && n <= 63; }

static const char *wifi_apply_net(int id, const char *ssid, const char *key){
    char cmd[512], qssid[160], qkey[300];
    wpa_hex(ssid, qssid, sizeof qssid);
    snprintf(cmd, sizeof cmd, WCLI "set_network %d ssid %s 2>/dev/null", id, qssid);
    if(wifi_step(cmd, "set ssid")) return "Couldn't configure Wi-Fi";
    if(key && key[0]){
        wpa_q(key, qkey, sizeof qkey);
        snprintf(cmd, sizeof cmd, WCLI "set_network %d key_mgmt WPA-PSK 2>/dev/null", id);
        if(wifi_step(cmd, "set key_mgmt")) return "Couldn't configure Wi-Fi";
        snprintf(cmd, sizeof cmd, WCLI "set_network %d psk %s 2>/dev/null", id, qkey);
        if(wifi_step(cmd, "set psk")) return "Couldn't set Wi-Fi password";
    } else {
        snprintf(cmd, sizeof cmd, WCLI "set_network %d key_mgmt NONE 2>/dev/null", id);
        if(wifi_step(cmd, "set open")) return "Couldn't configure Wi-Fi";
    }
    snprintf(cmd, sizeof cmd, WCLI "enable_network %d 2>/dev/null", id);
    if(wifi_step(cmd, "enable_network")) return "Couldn't configure Wi-Fi";
    snprintf(cmd, sizeof cmd, WCLI "select_network %d 2>/dev/null", id);
    if(wifi_step(cmd, "select_network")) return "Couldn't connect to Wi-Fi";
    return NULL;
}

static void wifi_connect(const char *ssid, const char *key){
    wifi_lease_retry_reset();
    char buf[2048];
    if(key && key[0] && !wifi_psk_len_ok(key)){ ui_toast("Password must be 8 to 63 characters"); return; }
    int id = -1;
    run_cap(WCLI "list_networks 2>/dev/null", buf, sizeof buf);
    char *l = strchr(buf, '\n'); if(l) l++;
    while(l && *l){
        char *nl = strchr(l, '\n');
        char *t1 = strchr(l, '\t');
        if(t1 && (!nl || t1 < nl)){
            char *sfld = t1 + 1;
            char *t2 = strchr(sfld, '\t');
            if(t2 && (!nl || t2 < nl) && wifi_ssid_field_eq(sfld, (int)(t2 - sfld), ssid)){
                id = atoi(l); break;
            }
        }
        l = nl ? nl + 1 : NULL;
    }
    if(id < 0){
        char idbuf[32];
        run_cap(WCLI "add_network 2>/dev/null", idbuf, sizeof idbuf);
        char *end = idbuf;
        long nid = strtol(idbuf, &end, 10);
        if(end == idbuf || nid < 0){
            fprintf(stderr, "wifi add_network failed: '%s'\n", idbuf);
            ui_toast("Couldn't add network");
            return;
        }
        id = (int)nid;
    }
    g_intent_epoch++;
    const char *why = wifi_apply_net(id, ssid, key);
    if(why){ ui_toast(why); return; }
    if(g_conn_timer){ lv_timer_del(g_conn_timer); g_conn_timer = NULL; }
    if(system("" WCLI "save_config >/dev/null 2>&1")) fprintf(stderr, "wifi connect: save_config failed\n");
    snprintf(g_conn_ssid, sizeof g_conn_ssid, "%s", ssid);
    g_conn_id = id;
    ui_toast("Connecting...");
    g_conn_start = lv_tick_get();
    g_conn_timer = lv_timer_create(conn_poll_cb, 1000, NULL);
}

static void psk_done(const char *text){
    if(text && text[0]) wifi_connect(g_pending_ssid, text);
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
    lv_obj_set_pos(v, 110, 11); lv_obj_set_size(v, 128, 18);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(v, TF(USER_14), 0);
    lv_obj_set_style_text_color(v, TC(TEXT_PRIMARY), 0);
}
static void info_back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

void wifi_info_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header_cb(root, "Network", info_back_cb);
    g_info_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_info_list);
    lv_obj_set_pos(g_info_list, 55, 84); lv_obj_set_size(g_info_list, 250, 230);
    lv_obj_set_style_pad_bottom(g_info_list, 44, 0);
    lv_obj_set_style_bg_opa(g_info_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(g_info_list, 8, 0);
    lv_obj_set_flex_flow(g_info_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_info_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(g_info_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_info_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_info_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
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
    ui_on(r, cb, LV_EVENT_CLICKED, NULL, "wifi.cb", UI_CORE);
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, label); lv_obj_center(t);
    lv_obj_set_style_text_font(t, TF(UI_14), 0);
    lv_obj_set_style_text_color(t, TC(STATUS_DANGER), 0);
}
static char g_info_ssid[64];
static int wifi_remove_ssid(const char *ssid){
    wifi_lease_retry_reset();
    if(!wpa_ready() || !ssid || !ssid[0]) return 0;
    char buf[4096]; run_cap(WCLI "list_networks 2>/dev/null", buf, sizeof buf);
    char *l = strchr(buf, '\n'); if(l) l++;
    int removed = 0, failed = 0;
    while(l && *l){
        char *nl = strchr(l, '\n');
        char *t1 = strchr(l, '\t');
        if(t1 && (!nl || t1 < nl)){
            char *sfld = t1 + 1;
            char *t2 = strchr(sfld, '\t');
            if(t2 && (!nl || t2 < nl) && wifi_ssid_field_eq(sfld, (int)(t2 - sfld), ssid)){
                int nid = atoi(l);
                char cmd[80]; snprintf(cmd, sizeof cmd, WCLI "remove_network %d 2>/dev/null", nid);
                char out[64]; run_cap(cmd, out, sizeof out);
                if(strstr(out, "OK")) removed = 1; else failed = 1;
            }
        }
        l = nl ? nl + 1 : NULL;
    }
    if(removed){
        char so[64]; run_cap(WCLI "save_config 2>/dev/null", so, sizeof so);
        if(!strstr(so, "OK")) failed = 1;
    }
    return removed && !failed;
}
static void wifi_forget_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    ui_toast(wifi_remove_ssid(g_info_ssid) ? "Network forgotten" : "Couldn't forget network");
    wifi_open();
}
static void wifi_info_refresh(void){
    if(!g_info_list) return;
    lv_obj_clean(g_info_list);
    if(!cfg_get_int("wifi_on", 1)){
        g_info_ssid[0] = 0;
        info_row("Network", ""); info_row("IP Address", "");
        return;
    }
    char ssid[64], ip[32], buf[2048], val[64], gw[32];
    int ready = wifi_status(ssid, sizeof ssid, ip, sizeof ip);
    if(!ready) ip[0] = 0;
    snprintf(g_info_ssid, sizeof g_info_ssid, "%s", ssid);
    info_row("Network", ssid);
    info_row("IP Address", ip);
    if(ready && get_gateway(gw, sizeof gw)) info_row("Router", gw);
    run_cap(WCLI "signal_poll 2>/dev/null", buf, sizeof buf);
    char *p = strstr(buf, "RSSI=");
    if(p){ int r; if(sscanf(p+5, "%d", &r)==1){ snprintf(val,sizeof val,"%d dBm", r); info_row("Signal", val);} }
    run_cap(WCLI "status 2>/dev/null", buf, sizeof buf);
    p = strstr(buf, "key_mgmt=");
    if(p && sscanf(p+9, "%63[^\n]", val)==1){ info_row("Security", strstr(val,"NONE")?"Open":val); }
    p = strstr(buf, "\nfreq=");
    if(p){ int fr; if(sscanf(p+6,"%d",&fr)==1){ snprintf(val,sizeof val,"%d MHz", fr); info_row("Frequency", val);} }
    p = strstr(buf, "bssid=");
    if(p && sscanf(p+6, "%63[^\n]", val)==1){ info_row("BSSID", val); }
    info_action_row("Forget This Network", wifi_forget_cb);
}
void wifi_info_open(void){ wifi_lease_retry_reset(); wifi_info_refresh(); screen_show(SCR_WIFI_INFO); }

static void row_free_cb(lv_event_t *e){ free(lv_obj_get_user_data(lv_event_get_target(e))); }
static void net_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    lv_obj_t *row = lv_event_get_target(e);
    const char *ssid = (const char*)lv_obj_get_user_data(row);
    intptr_t flags = (intptr_t)lv_event_get_user_data(e);
    int secured   = flags & 1;
    int connected = flags & 2;
    if(!ssid || !ssid[0]) return;
    if(connected){
        wifi_info_open();
    } else if(secured){
        snprintf(g_pending_ssid, sizeof g_pending_ssid, "%s", ssid);
        char prompt[80]; snprintf(prompt, sizeof prompt, "Password for %s", ssid);
        kbinput_open_password(prompt, "", psk_done);
    } else {
        wifi_connect(ssid, NULL);
    }
}

static void add_signal_bars(lv_obj_t *parent, int x, int y, int sig){
    int bars = sig >= -55 ? 4 : sig >= -67 ? 3 : sig >= -78 ? 2 : 1;
    static const int h[4] = {5, 9, 13, 17};
    for(int i=0;i<4;i++){
        lv_obj_t *b = lv_obj_create(parent);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 4, h[i]);
        lv_obj_set_pos(b, x + i*6, y + (17 - h[i]));
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, (i < bars ? TC(SIGNAL_ACTIVE) : TC(SIGNAL_INACTIVE)), 0);
    }
}

static void add_net_row(const char *ssid, int signal, int secured, int connected){
    lv_obj_t *r = lv_button_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 268, 46);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, (connected ? TC(CONNECTED_SURFACE) : TC(SURFACE)), 0);
    lv_obj_set_style_bg_opa(r, connected ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    char *dup = strdup(ssid);
    lv_obj_set_user_data(r, dup);
    intptr_t flags = (secured?1:0) | (connected?2:0);
    ui_on(r, net_cb, LV_EVENT_CLICKED, (void*)flags, "wifi.net", UI_CORE);
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
    lv_label_set_text(t, ssid);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t, tx, 13); lv_obj_set_size(t, 190 - tx, 20);
    lv_obj_set_style_text_font(t, TF(USER_16), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);

    if(secured){
        lv_obj_t *shk = lv_obj_create(r);
        lv_obj_remove_style_all(shk);
        lv_obj_clear_flag(shk, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(shk, 8, 8); lv_obj_set_pos(shk, 217, 15);
        lv_obj_set_style_radius(shk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(shk, 2, 0);
        lv_obj_set_style_border_color(shk, TC(TEXT_MUTED), 0);
        lv_obj_t *body = lv_obj_create(r);
        lv_obj_remove_style_all(body);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(body, 10, 8); lv_obj_set_pos(body, 216, 20);
        lv_obj_set_style_radius(body, 2, 0);
        lv_obj_set_style_bg_color(body, TC(TEXT_MUTED), 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    }
    add_signal_bars(r, 232, 14, signal);
}

struct wnet { char ssid[64]; int sig; int secured; int connected; };
#define WNET_MAX 40

#define WNET_NO_RSSI (-1000)
static int wifi_scan_order(char *buf, const char *cur, int cur_rssi, int cur_secured, struct wnet *out){
    int n = 0;
    char *l = strchr(buf, '\n'); if(l) l++;
    while(l && *l){
        char *nl = strchr(l, '\n');
        if(!nl) break;
        *nl = 0;
        char *f[5] = {0,0,0,0,0}; int nf = 0; char *p = l;
        f[nf++] = p;
        while(nf < 5 && (p = strchr(p, '\t'))){ *p++ = 0; f[nf++] = p; }
        char ssid[64];
        if(nf == 5 && f[4][0] && wifi_ssid_decode(f[4], (int)strlen(f[4]), ssid, sizeof ssid) > 0){
            int sig = atoi(f[2]), dup = -1;
            for(int i=0;i<n;i++) if(!strcmp(out[i].ssid, ssid)){ dup = i; break; }
            if(dup >= 0){
                if(sig > out[dup].sig) out[dup].sig = sig;
            } else if(n < WNET_MAX){
                snprintf(out[n].ssid, sizeof out[n].ssid, "%s", ssid);
                out[n].sig = sig;
                out[n].secured = (strstr(f[3], "WPA") || strstr(f[3], "PSK") || strstr(f[3], "WEP")) != 0;
                out[n].connected = 0;
                n++;
            }
        }
        l = nl + 1;
    }
    if(!cur || !cur[0]) return n;
    int at = -1;
    for(int i=0;i<n;i++) if(!strcmp(out[i].ssid, cur)){ at = i; break; }
    struct wnet c;
    if(at >= 0){ c = out[at]; memmove(&out[1], &out[0], sizeof(struct wnet) * at); }
    else{
        memset(&c, 0, sizeof c);
        snprintf(c.ssid, sizeof c.ssid, "%s", cur);
        c.sig = (cur_rssi == WNET_NO_RSSI) ? -100 : cur_rssi;
        c.secured = cur_secured;
        if(n >= WNET_MAX) n--;
        memmove(&out[1], &out[0], sizeof(struct wnet) * n);
        n++;
    }
    c.connected = 1;
    out[0] = c;
    return n;
}

static int wifi_parse_rssi(const char *b){
    const char *r = strstr(b, "RSSI=");
    if(!r) return WNET_NO_RSSI;
    r += 5;
    char *end; long v = strtol(r, &end, 10);
    if(end == r || (*end && *end != '\n' && *end != '\r') || v > 0 || v < -200) return WNET_NO_RSSI;
    return (int)v;
}
static int wifi_status_secured(const char *st){
    for(const char *l = st; l && *l; ){
        if(!strncmp(l, "key_mgmt=", 9)){
            const char *k = l + 9;
            return !(!strncmp(k, "NONE", 4) && (k[4] == 0 || k[4] == '\n' || k[4] == '\r'));
        }
        l = strchr(l, '\n'); if(l) l++;
    }
    return 0;
}
static int wifi_cur_rssi(void){
    char b[512];
    if(!run_cap(WCLI "signal_poll 2>/dev/null", b, sizeof b)) return WNET_NO_RSSI;
    return wifi_parse_rssi(b);
}

static void scan_fill(void){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char cs[64], cip[32];
    if(!(wifi_radio_on() && wifi_status(cs, sizeof cs, cip, sizeof cip))) cs[0]=0;
    snprintf(g_cur_ssid, sizeof g_cur_ssid, "%s", cs);

    char *buf = run_cap_all(WCLI "scan_results 2>/dev/null", 1 << 20);
    if(!buf){ list_msg("Couldn't scan Wi-Fi"); return; }
    struct wnet nets[WNET_MAX];
    int rssi = WNET_NO_RSSI, sec = 0;
    if(g_cur_ssid[0]){
        char st[2048]; run_cap(WCLI "status 2>/dev/null", st, sizeof st);
        sec = wifi_status_secured(st);
        rssi = wifi_cur_rssi();
    }
    int n = wifi_scan_order(buf, g_cur_ssid, rssi, sec, nets);
    free(buf);
    for(int i=0;i<n;i++) add_net_row(nets[i].ssid, nets[i].sig, nets[i].secured, nets[i].connected);
    if(n == 0) list_msg("No networks found");
}

static void scan_timer_cb(lv_timer_t *t){
    (void)t;
    scan_fill();
    if(g_scan_timer){ lv_timer_del(g_scan_timer); g_scan_timer = NULL; }
}
static void start_scan(void){
    wifi_lease_retry_reset();
    if(!wifi_radio_on()){ list_msg("Wi-Fi is off"); return; }
    if(system("" WCLI "scan >/dev/null 2>&1") != 0){ list_msg("Couldn't scan Wi-Fi"); return; }
    list_msg_scanning();
    if(g_scan_timer) lv_timer_del(g_scan_timer);
    g_scan_timer = lv_timer_create(scan_timer_cb, 2500, NULL);
    lv_timer_set_repeat_count(g_scan_timer, 1);
}

static void wifi_set_intent(int on){ cfg_set_int("wifi_on", on); }
static int  wifi_intent_on(void){ return cfg_get_int("wifi_on", 1); }
static void wifi_radio_start(void){
    system("/usr/bin/wifi_up.sh >/dev/null 2>&1 &");
    g_wifi_last_start = lv_tick_get();
    g_sanitize_pending = 1;
}
static void wifi_radio_cleanup(void){
    if(g_scan_timer){ lv_timer_del(g_scan_timer); g_scan_timer = NULL; }
    if(g_conn_timer){ lv_timer_del(g_conn_timer); g_conn_timer = NULL; }
    wifi_lease_step();
}
static void wifi_radio_down(void){ system("/usr/bin/wifi_down.sh >/dev/null 2>&1"); }
static void wifi_radio_up_cb(void){ start_scan(); }
static void wifi_radio_timeout_cb(void){ list_msg("Couldn't turn on Wi-Fi"); }
static struct radio g_wifi_radio = {
    .watch = { .env = &radio_lv_env, .interval_ms = 700, .timeout_ms = 8000, .is_up = wifi_radio_on,
               .on_up = wifi_radio_up_cb, .on_timeout = wifi_radio_timeout_cb },
    .set_intent = wifi_set_intent, .start = wifi_radio_start, .cleanup = wifi_radio_cleanup, .power_down = wifi_radio_down,
    .is_on = wifi_intent_on, .timeout = RADIO_REPORT_ONLY, .toggle_observes = 0,
};
static void sw_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    wifi_lease_retry_reset();
    int on = lv_obj_has_state(g_sw, LV_STATE_CHECKED);
    g_intent_epoch++;
    if(on){
        radio_on(&g_wifi_radio, 1);
        list_msg("Turning on Wi-Fi...");
    } else {
        radio_off(&g_wifi_radio);
        list_msg("Wi-Fi is off");
    }
}

int wifi_toggle(void){
    g_intent_epoch++;
    int on = radio_toggle(&g_wifi_radio);
    if(g_sw){ if(on) lv_obj_add_state(g_sw, LV_STATE_CHECKED); else lv_obj_clear_state(g_sw, LV_STATE_CHECKED); }
    return on;
}

static void rescan_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) start_scan(); }
static void back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

void wifi_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header_cb(root, "Wi-Fi", back_cb);

    lv_obj_t *trow = lv_obj_create(root);
    lv_obj_remove_style_all(trow);
    lv_obj_set_pos(trow, 50, 64); lv_obj_set_size(trow, 208, 48);
    lv_obj_set_style_radius(trow, 12, 0);
    lv_obj_set_style_bg_color(trow, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(trow, LV_OPA_70, 0);
    lv_obj_clear_flag(trow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tl = lv_label_create(trow);
    lv_label_set_text(tl, "Wi-Fi");
    lv_obj_set_pos(tl, 16, 14);
    lv_obj_set_style_text_font(tl, TF(UI_16), 0);
    lv_obj_set_style_text_color(tl, TC(TEXT_PRIMARY), 0);

    g_sw = lv_switch_create(trow);
    lv_obj_set_size(g_sw, 46, 24);
    lv_obj_set_ext_click_area(g_sw, 10);
    lv_obj_align(g_sw, LV_ALIGN_RIGHT_MID, -14, 0);
    ui_on(g_sw, sw_cb, LV_EVENT_VALUE_CHANGED, NULL, "wifi.sw.value", UI_CORE);

    lv_obj_t *rb = lv_button_create(root);
    lv_obj_remove_style_all(rb);
    lv_obj_set_pos(rb, 266, 72); lv_obj_set_size(rb, 36, 32);
    lv_obj_set_ext_click_area(rb, 8);
    lv_obj_set_style_radius(rb, 10, 0);
    lv_obj_set_style_bg_color(rb, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(rb, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(rb, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    ui_on(rb, rescan_cb, LV_EVENT_CLICKED, NULL, "wifi.rescan", UI_CORE);
    lv_obj_t *rl = lv_label_create(rb);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(rl, TF(UI_16), 0);
    lv_obj_set_style_text_color(rl, TC(TEXT_SECONDARY), 0);
    lv_obj_center(rl);

    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, 40, 126); lv_obj_set_size(g_list, 280, UI_LIST_H_FROM(126));
    lv_obj_set_style_pad_bottom(g_list, UI_LIST_PAD_BOTTOM, 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(g_list, 6, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
}

void wifi_open(void){
    wifi_lease_retry_reset();
    if(g_sw){
        if(wifi_radio_on()) lv_obj_add_state(g_sw, LV_STATE_CHECKED);
        else                lv_obj_clear_state(g_sw, LV_STATE_CHECKED);
    }
    screen_show(SCR_WIFI);
    start_scan();
}
