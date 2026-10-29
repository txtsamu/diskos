/* Charging screen. The Disc wakes by itself when a charger is plugged into a switched-off unit (stock does the same),
 * so diskOS comes up fully. If it starts with a plain wall charger attached it shows this screen instead of the
 * normal UI: battery level, "Tap to continue". Unplugging while it is still showing powers the Disc off again.
 * A computer on the cable is NOT a charger: the USB gadget (USB DAC / Audio / Storage) state is "configured"
 * or similar rather than "not attached", and a remembered USB mode is never interrupted, so none of this ever
 * touches USB Audio. Charger presence comes from the SGM41513 charger IC (i2c-2, 0x1a, REG08 VBUS_STAT). */
#include "screens.h"
#include "theme_kit.h"
#include "theme.h"
#include "config.h"
#include "modes.h"
#include "session.h"
#include "power.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <sys/ioctl.h>

#define I2C_SLAVE_FORCE_ 0x0706
#define CHG_BUS  "/dev/i2c-2"
#define CHG_ADDR 0x1a
#define CHG_REG_STATUS 0x08

static lv_obj_t *g_ov, *g_pct, *g_sub;
static int g_gone_polls;
static uint32_t g_last_poll;

/* 1 = charger IC reports VBUS present, 0 = not, -1 = could not read */
static int vbus_present(void){
    int fd = open(CHG_BUS, O_RDWR | O_CLOEXEC);
    if(fd < 0) return -1;
    unsigned char reg = CHG_REG_STATUS, val = 0;
    int ok = ioctl(fd, I2C_SLAVE_FORCE_, CHG_ADDR) == 0 && write(fd, &reg, 1) == 1 && read(fd, &val, 1) == 1;
    close(fd);
    return ok ? ((val >> 5) & 7) != 0 : -1;
}
/* 1 only while no USB host is on the cable (UDC "not attached"): a wall charger, not a computer */
static int plain_charger(void){
    glob_t g; int plain = 1;
    if(glob("/sys/class/udc/*/state", 0, NULL, &g) != 0) return 1;
    for(size_t i = 0; i < g.gl_pathc; i++){
        FILE *f = fopen(g.gl_pathv[i], "r"); char s[32] = "";
        if(!f) continue;
        if(fgets(s, sizeof s, f) && strncmp(s, "not attached", 12) != 0) plain = 0;
        fclose(f);
    }
    globfree(&g);
    return plain;
}
/* a USB mode is in use or was the remembered one: never show or act */
static int usb_mode_involved(void){
    int sm = ui_get_source_mode();
    return modes_output_route() == OUT_USB || sm == 1 || sm == 3 ||
           session_saved_usb_audio() || session_saved_source() == 1 || session_saved_source() == 3;
}
static int battery_pct(void){
    FILE *f = fopen("/sys/class/power_supply/cw221X-bat/capacity", "r");
    int v = -1;
    if(f){ if(fscanf(f, "%d", &v) != 1) v = -1; fclose(f); }
    return v;
}
static void close_ov(void){ if(g_ov){ lv_obj_delete(g_ov); g_ov = NULL; g_pct = g_sub = NULL; } }
static void ov_click(lv_event_t *e){ (void)e; close_ov(); }
static void paint(void){
    if(!g_pct) return;
    int p = battery_pct();
    char b[16]; if(p >= 0) snprintf(b, sizeof b, "%d%%", p); else snprintf(b, sizeof b, "--");
    lv_label_set_text(g_pct, b);
}

int chargemode_active(void){ return g_ov != NULL; }

void chargemode_boot(void){
    if(g_ov || usb_mode_involved() || vbus_present() != 1 || !plain_charger()) return;
    g_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_ov);
    lv_obj_set_size(g_ov, 360, 360);
    lv_obj_set_style_bg_color(g_ov, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_ov, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_ov, ov_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ic = lv_label_create(g_ov);
    lv_label_set_text(ic, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(ic, TF(UI_36), 0);
    lv_obj_set_style_text_color(ic, ui_current_accent(), 0);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -70);
    g_pct = lv_label_create(g_ov);
    lv_obj_set_style_text_font(g_pct, TF(UI_36), 0);
    lv_obj_set_style_text_color(g_pct, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_pct, LV_ALIGN_CENTER, 0, -10);
    lv_obj_t *t = lv_label_create(g_ov);
    lv_label_set_text(t, "Charging");
    lv_obj_set_style_text_font(t, TF(UI_16), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_SECONDARY), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 40);
    g_sub = lv_label_create(g_ov);
    lv_label_set_text(g_sub, "Tap to continue");
    lv_obj_set_style_text_font(g_sub, TF(UI_16), 0);
    lv_obj_set_style_text_color(g_sub, TC(TEXT_SECONDARY), 0);
    lv_obj_align(g_sub, LV_ALIGN_CENTER, 0, 70);
    g_gone_polls = 0; g_last_poll = lv_tick_get();
    paint();
    lv_refr_now(NULL);
    fprintf(stderr, "chargemode: charger at boot, showing the charging screen\n"); fflush(stderr);
}

/* main loop, every iteration: refresh the level; unplug while showing = power off, unless a USB mode is involved */
void chargemode_tick(void){
    if(!g_ov || lv_tick_elaps(g_last_poll) < 1000) return;
    g_last_poll = lv_tick_get();
    if(usb_mode_involved() || !plain_charger()){ close_ov(); return; }   /* a computer/USB mode took over: just a normal UI */
    paint();
    int v = vbus_present();
    if(v == 0){
        if(++g_gone_polls >= 3){ close_ov(); fprintf(stderr, "chargemode: unplugged, powering off\n"); fflush(stderr); power_shutdown_request(); }
    } else g_gone_polls = 0;
}
