/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "procnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <crypt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PWFILE "/usr/data/sshd/current_pw"

#define DBG "/usr/project/diskos-debug.sh"
#define KEYSSH_PORT 2222
#define KEYSSH_KEYS "/usr/data/sshd/root_home/.ssh/authorized_keys"

static lv_obj_t *g_ssh, *g_pw, *g_serial, *g_warn, *g_btnlbl, *g_keycb, *g_keysub;
static int  g_on = 0;
static char g_pwtext[16] = "";

extern int ui_run_bounded(char *const argv[], int timeout_ms);

static int rnd_alnum(char *out, int n){
    static const char cs[] = "abcdefghijkmnpqrstuvwxyz23456789";
    unsigned char b[32];
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd < 0) return -1;
    ssize_t got = read(fd, b, sizeof b);
    close(fd);
    if(got != (ssize_t)sizeof b) return -1;
    if(n > (int)sizeof b) n = sizeof b;
    for(int i = 0; i < n; i++) out[i] = cs[b[i] & 31];
    out[n] = 0;
    return 0;
}

static int wlan_ip(char *out, int n){
    int s = socket(AF_INET, SOCK_DGRAM, 0); if(s < 0) return 0;
    struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ-1);
    int ok = (ioctl(s, SIOCGIFADDR, &ifr) == 0);
    if(ok){ struct sockaddr_in *a = (struct sockaddr_in *)&ifr.ifr_addr;
            snprintf(out, n, "%s", inet_ntoa(a->sin_addr)); }
    close(s);
    return ok;
}

static int ssh_running(void){
    DIR *d = opendir("/proc"); if(!d) return 0;
    struct dirent *e; int found = 0;
    while((e = readdir(d))){
        if(e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p[64]; snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        char c[32] = ""; if(fgets(c, sizeof c, f) && strncmp(c, "dropbearmulti", 13) == 0) found = 1;
        fclose(f);
        if(found) break;
    }
    closedir(d);
    return found;
}

static int keyssh_up(void){
    static const char *const tabs[] = { "/proc/net/tcp", "/proc/net/tcp6" };
    for(int i = 0; i < 2; i++){
        FILE *f = fopen(tabs[i], "r"); if(!f) continue;
        int up = tcp_listen_in(f, KEYSSH_PORT);
        fclose(f);
        if(up) return 1;
    }
    return 0;
}

static int keyssh_has_keys(void){ struct stat st; return stat(KEYSSH_KEYS, &st) == 0 && st.st_size > 0; }

static int overlay_mounted(void){
    FILE *f = fopen("/proc/mounts", "r"); if(!f) return 1;
    char line[512]; int m = 0;
    while(fgets(line, sizeof line, f)){ if(strstr(line, " /etc/shadow ")){ m = 1; break; } }
    fclose(f);
    return m;
}

static void pw_load(void){
    g_pwtext[0] = 0;
    FILE *f = fopen(PWFILE, "r");
    if(f){ if(fgets(g_pwtext, sizeof g_pwtext, f)){ char *nl = strchr(g_pwtext, '\n'); if(nl) *nl = 0; } fclose(f); }
}
static void pw_store(void){
    FILE *f = fopen(PWFILE, "w");
    if(f){ fchmod(fileno(f), 0600); fprintf(f, "%s\n", g_pwtext); fclose(f); }
}

static void refresh_key(const char *note);

static void refresh(void){
    char b[96];
    if(g_on){
        char ip[40] = "";
        if(wlan_ip(ip, sizeof ip)) snprintf(b, sizeof b, "ssh root@%s", ip);
        else                       snprintf(b, sizeof b, "SSH: connect to WiFi first");
        lv_label_set_text(g_ssh, b);
        snprintf(b, sizeof b, "Password:  %s", g_pwtext);       lv_label_set_text(g_pw, b);
        if(access("/dev/ttyGS0", F_OK) == 0) lv_label_set_text(g_serial, "Serial: USB-C (/dev/ttyACM0)");
        else                                 lv_label_set_text(g_serial, "Serial: not available (SSH only)");
        lv_label_set_text(g_warn, "This gives ROOT access to the device. Turn it OFF when finished.");
        lv_label_set_text(g_btnlbl, "Disable Debug");
        lv_obj_remove_flag(g_pw, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_serial, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(g_ssh, "Debug access is OFF");
        lv_obj_add_flag(g_pw, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_serial, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_warn, "Enables SSH over Wi-Fi with a fresh random password.");
        lv_label_set_text(g_btnlbl, "Enable Debug");
    }
    refresh_key(NULL);
}

static void refresh_key(const char *note){
    int up = keyssh_up(), keys = keyssh_has_keys();
    lv_checkbox_set_text(g_keycb, up ? "Key SSH :2222  ON (keys only)" : "Key SSH :2222  OFF");
    if(up) lv_obj_add_state(g_keycb, LV_STATE_CHECKED); else lv_obj_remove_state(g_keycb, LV_STATE_CHECKED);
    if(!up && !keys) lv_obj_add_state(g_keycb, LV_STATE_DISABLED); else lv_obj_remove_state(g_keycb, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(g_keycb, up ? TC(STATUS_WARNING) : TC(TEXT_PRIMARY), 0);
    lv_label_set_text(g_keysub, note ? note
                      : up    ? "Disable does not stop it. Back at boot."
                      : keys  ? "Starts again at boot while a key is installed"
                              : "No key installed, so it stays off");
}

static int enable_core(const char **why){
    char pw[16], salt[16], setting[24];
    if(rnd_alnum(pw, 10) != 0 || rnd_alnum(salt, 8) != 0){ *why = "No secure random available - NOT enabled."; return -1; }
    snprintf(setting, sizeof setting, "$6$%s$", salt);
    char *h = crypt(pw, setting);
    if(!h || h[0] != '$'){ *why = "Could not hash password - NOT enabled."; return -1; }
    strncpy(g_pwtext, pw, sizeof g_pwtext - 1); g_pwtext[sizeof g_pwtext - 1] = 0;
    char *a1[] = { DBG, "ssh-on", h, NULL };     ui_run_bounded(a1, 12000);
    char *a2[] = { DBG, "serial-on", NULL };     ui_run_bounded(a2, 5000);
    if(ssh_running()){ pw_store(); g_on = 1; }
    else if(overlay_mounted()){ memset(g_pwtext, 0, sizeof g_pwtext); g_on = 1; }
    else { memset(g_pwtext, 0, sizeof g_pwtext); g_on = 0; }
    return 0;
}
static void enable_dbg(void){
    const char *why = NULL;
    if(enable_core(&why) < 0){ lv_label_set_text(g_warn, why); return; }
    refresh();
    if(g_on && !g_pwtext[0])
        lv_label_set_text(g_warn, "SSH did not start cleanly - press Disable to reset, then try again.");
    else if(!g_on)
        lv_label_set_text(g_warn, "Could not start SSH - check WiFi and try again.");
}

static void disable_dbg(void){
    char *a1[] = { DBG, "ssh-off", NULL };     ui_run_bounded(a1, 12000);
    char *a2[] = { DBG, "serial-off", NULL };  ui_run_bounded(a2, 5000);
    if(!ssh_running() && !overlay_mounted()){ unlink(PWFILE); memset(g_pwtext, 0, sizeof g_pwtext); g_on = 0; }
    else g_on = 1;
    refresh();
    if(g_on) lv_label_set_text(g_warn, "SSH did not fully stop - still ON. Try Disable again.");
}

static void btn_cb(lv_event_t *e){ (void)e; if(g_on) disable_dbg(); else enable_dbg(); }

static void key_cb(lv_event_t *e){
    int want = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    char *a[] = { DBG, want ? "key-on" : "key-off", NULL };
    ui_run_bounded(a, 8000);
    int up = keyssh_up();
    refresh_key(up == want ? NULL : want ? "Could not start Key SSH (see key-on.log)" : "Key SSH did not stop - try again");
}

static void pdbg_cb(lv_event_t *e){
    cfg_set_int(CFG_PDBG, lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED) ? 1 : 0);
}

void debug_boot_restore(void){
    if(!cfg_get_int(CFG_PDBG, 0)) return;
    if(ssh_running() || overlay_mounted()) return;
    const char *why = NULL;
    if(enable_core(&why) < 0){ fprintf(stderr, "persistent debug: %s\n", why); fflush(stderr); return; }
    fprintf(stderr, "persistent debug: SSH %s at boot\n", g_on ? "enabled" : "did not start"); fflush(stderr);
}

int debug_ssh_on(void){ return ssh_running(); }
void debug_open(void){
    g_on = ssh_running() || overlay_mounted();
    if(g_on) pw_load(); else g_pwtext[0] = 0;
    refresh();
    screen_show(SCR_DEBUG);
}

static lv_obj_t *dbg_checkbox(lv_obj_t *root, const char *text, int y){
    lv_obj_t *cb = lv_checkbox_create(root);
    lv_checkbox_set_text(cb, text);
    lv_obj_set_style_text_font(cb, TF(UI_16), 0);
    lv_obj_set_style_text_color(cb, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_pad_column(cb, 10, 0);
    lv_color_t acc = ui_current_accent();
    lv_obj_set_style_bg_color(cb, acc, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(cb, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(cb, theme_on_color(acc), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(cb, TC(TEXT_MUTED), LV_PART_INDICATOR);
    lv_obj_set_style_border_width(cb, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(cb, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(cb, 0, LV_PART_INDICATOR);
    lv_obj_align(cb, LV_ALIGN_TOP_MID, 0, y);
    return cb;
}
static lv_obj_t *pdbg_checkbox(lv_obj_t *root, const char *text, int y){
    lv_obj_t *cb = dbg_checkbox(root, text, y);
    if(cfg_get_int(CFG_PDBG, 0)) lv_obj_add_state(cb, LV_STATE_CHECKED);
    ui_on(cb, pdbg_cb, LV_EVENT_VALUE_CHANGED, NULL, "debug_ui.pdbg.value", UI_CORE);
    return cb;
}

void debug_create(lv_obj_t *root){
    ui_header(root, "Debug Mode");

    g_ssh = lv_label_create(root);
    lv_obj_set_width(g_ssh, 320); lv_obj_set_style_text_align(g_ssh, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_ssh, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_ssh, LV_ALIGN_TOP_MID, 0, 88);

    g_pw = lv_label_create(root);
    lv_obj_set_width(g_pw, 320); lv_obj_set_style_text_align(g_pw, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_pw, TF(UI_24), 0);
    lv_obj_set_style_text_color(g_pw, ui_current_accent(), 0);
    lv_obj_align(g_pw, LV_ALIGN_TOP_MID, 0, 108);

    g_serial = lv_label_create(root);
    lv_obj_set_width(g_serial, 300); lv_obj_set_style_text_align(g_serial, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_serial, TC(TEXT_MUTED), 0);
    lv_obj_align(g_serial, LV_ALIGN_TOP_MID, 0, 136);

    g_warn = lv_label_create(root);
    lv_obj_set_width(g_warn, 280); lv_label_set_long_mode(g_warn, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_warn, TC(STATUS_WARNING), 0);
    lv_obj_align(g_warn, LV_ALIGN_TOP_MID, 0, 156);

    pdbg_checkbox(root, "Persistent debug", 198);
    lv_obj_t *sub = lv_label_create(root);
    lv_label_set_text(sub, "Wi-Fi and SSH on at every boot");
    lv_obj_set_style_text_font(sub, TF(UI_12), 0);
    lv_obj_set_style_text_color(sub, TC(TEXT_MUTED), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 222);

    g_keycb = dbg_checkbox(root, "Key SSH :2222  OFF", 240);
    ui_on(g_keycb, key_cb, LV_EVENT_VALUE_CHANGED, NULL, "debug_ui.key.value", UI_CORE);
    g_keysub = lv_label_create(root);
    lv_obj_set_width(g_keysub, 300); lv_label_set_long_mode(g_keysub, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g_keysub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_keysub, TF(UI_12), 0);
    lv_obj_set_style_text_color(g_keysub, TC(TEXT_MUTED), 0);
    lv_obj_align(g_keysub, LV_ALIGN_TOP_MID, 0, 264);

    lv_obj_t *btn = lv_button_create(root);
    lv_obj_set_size(btn, 190, 40);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -38);
    ui_on(btn, btn_cb, LV_EVENT_CLICKED, NULL, "debug_ui.btn", UI_CORE);
    lv_color_t acc = ui_current_accent();
    lv_obj_set_style_bg_color(btn, acc, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    g_btnlbl = lv_label_create(btn); lv_obj_center(g_btnlbl);
    lv_obj_set_style_text_color(g_btnlbl, theme_on_color(acc), 0);

    refresh();
}
