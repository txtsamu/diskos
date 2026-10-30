/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>

LV_FONT_DECLARE(font_weather16)
static lv_font_t s_appwfont;

#define WI_SUN   "\xEF\x86\x85"
#define WI_MOON  "\xEF\x86\x86"
#define WI_CLOUD "\xEF\x83\x82"
#define WI_RAIN  "\xEF\x83\xA9"
#define WI_BOLT  "\xEF\x83\xA7"
#define WI_SNOW  "\xEF\x8B\x9C"

static char g_wbuf[160];
static char g_loc[160];
static pthread_mutex_t g_wx_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_wready = 0;
static int g_inflight = 0;
static int g_have = 0;
static uint32_t g_last = 0;
static int g_wfail = 0;
static int g_wgen = 0;
static int g_wjobgen = 0;
static int g_wwant = 0;
static int g_wjobnet = 0;
static uint32_t g_wback = 0;
static int g_wskip = 0;

static void wx_urlenc(const char *s, char *out, int cap)
{
    static const char *hex = "0123456789ABCDEF";
    int o = 0;
    for(; *s && o < cap - 4; s++){
        unsigned char c = (unsigned char)*s;
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'||c==',')
            out[o++] = (char)c;
        else { out[o++]='%'; out[o++]=hex[c>>4]; out[o++]=hex[c&15]; }
    }
    out[o] = 0;
}

static void *weather_thread(void *arg)
{
    (void)arg;
    int fail = 1;
    char result[160] = {0};
    char line[256] = {0};
    char cmd[320];
    if (g_loc[0])
        snprintf(cmd, sizeof cmd, "wget -qO- -T 8 'https://wttr.in/%s?format=%%t~%%C~%%l' 2>/dev/null", g_loc);
    else
        snprintf(cmd, sizeof cmd, "wget -qO- -T 8 'https://wttr.in/?format=%%t~%%C~%%l' 2>/dev/null");
    FILE *f = popen(cmd, "r");
    if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; pclose(f); }
    else fprintf(stderr, "weather popen failed: %s\n", strerror(errno));
    if (line[0] && strchr(line, '~')) {
        char temp[32] = {0}, cond[128] = {0};
        char *t1 = strchr(line, '~');
        int tl = t1 - line; if (tl > 31) tl = 31;
        memcpy(temp, line, tl); temp[tl] = 0;
        char *c = t1 + 1;
        char *t2 = strchr(c, '~');
        int full = t2 ? (int)(t2 - c) : (int)strlen(c);
        while (full > 0 && (c[full-1] == '\n' || c[full-1] == '\r')) full--;
        char low[128]; int li = 0;
        for (int i = 0; i < full && li < 127; i++) {
            char ch = c[i]; if (ch >= 'A' && ch <= 'Z') ch += 32; low[li++] = ch;
        }
        low[li] = 0;
        const char *ic = WI_CLOUD;
        if      (strstr(low,"thunder") || strstr(low,"storm"))                       ic = WI_BOLT;
        else if (strstr(low,"snow") || strstr(low,"sleet") || strstr(low,"blizzard") ||
                 strstr(low,"ice"))                                                  ic = WI_SNOW;
        else if (strstr(low,"rain") || strstr(low,"shower") || strstr(low,"drizzle"))ic = WI_RAIN;
        else if (strstr(low,"fog") || strstr(low,"mist") || strstr(low,"haze") ||
                 strstr(low,"cloud") || strstr(low,"overcast"))                      ic = WI_CLOUD;
        else if (strstr(low,"sun") || strstr(low,"clear"))                           ic = WI_SUN;
        int cl = full;
        char *comma = memchr(c, ',', cl); if (comma) cl = (int)(comma - c);
        if (cl > 120) cl = 120;
        memcpy(cond, c, cl); cond[cl] = 0;
        char *tp = temp; if (*tp == '+') tp++;
        snprintf(result, sizeof result, "%s  %s  %s", ic, tp, cond);
        { size_t n = strlen(result);
          while(n && (result[n - 1] == ' ' || result[n - 1] == '\t' || result[n - 1] == '\r' || result[n - 1] == '\n')) result[--n] = 0; }
        fail = 0;
    }
    if (fail) fprintf(stderr, "weather: no usable data (resp='%.48s')\n", line);
    pthread_mutex_lock(&g_wx_mu);
    if (!fail) memcpy(g_wbuf, result, sizeof g_wbuf);
    g_wfail = fail; g_wready = 1; g_inflight = 0;
    pthread_mutex_unlock(&g_wx_mu);
    return NULL;
}

static void weather_start(int net)
{
    if (net < 0) return;
    int go = 0;
    pthread_mutex_lock(&g_wx_mu);
    if (!g_inflight && !g_wready) { g_inflight = 1; go = 1; }
    pthread_mutex_unlock(&g_wx_mu);
    if (!go) return;
    g_wwant = 0;
    g_wjobgen = g_wgen;
    g_wjobnet = net;
    wx_urlenc(cfg_get_str("weather_loc", ""), g_loc, sizeof g_loc);
    g_last = lv_tick_get();
    if (!net) { g_wskip = 1; return; }
    pthread_t th;
    if (pthread_create(&th, NULL, weather_thread, NULL) == 0) pthread_detach(th);
    else {
        pthread_mutex_lock(&g_wx_mu);
        g_inflight = 0; g_wfail = 1; g_wready = 1;
        pthread_mutex_unlock(&g_wx_mu);
    }
}

void weather_fetch_async(void)
{
    g_wwant = 1;
    weather_start(wifi_connectivity());
}

static void weather_app_refresh(void);

void weather_set_enabled(int on)
{
    if (on) {
        g_wwant = 1; g_wback = 0;
        pthread_mutex_lock(&g_wx_mu);
        home_set_weather(g_have ? g_wbuf : "Weather unavailable");
        pthread_mutex_unlock(&g_wx_mu);
    }
    else    { home_set_weather(""); saver_set_weather(""); }
}

void weather_poll(lv_timer_t *t)
{
    (void)t;
    static int known = -1;
    int net = wifi_connectivity();
    int on = cfg_get_int("weather_on", 1);
    int reconnect = net > 0 && known == 0;
    if (net >= 0) known = net;
    if (on && reconnect) { g_wback = 0; g_wwant = 1; }
    if (on && g_wwant) weather_start(net);
    if (g_wskip) {
        g_wskip = 0;
        pthread_mutex_lock(&g_wx_mu);
        g_inflight = 0; g_wfail = 1; g_wready = 1;
        pthread_mutex_unlock(&g_wx_mu);
    }
    if (!on) {
        pthread_mutex_lock(&g_wx_mu);
        g_wready = 0;
        pthread_mutex_unlock(&g_wx_mu);
        return;
    }
    int ready = 0, fail = 0, inflight;
    pthread_mutex_lock(&g_wx_mu);
    if (g_wready) { g_wready = 0; ready = 1; fail = g_wfail; }
    inflight = g_inflight;
    pthread_mutex_unlock(&g_wx_mu);
    if (ready) {
        if (g_wjobgen != g_wgen) {
            g_wwant = 1;
        } else if (!fail) {
            g_have = 1;
            g_wback = 0;
            home_set_weather(g_wbuf);
            saver_set_weather(g_wbuf);
        } else if (g_wjobnet > 0 && !g_have) {
            g_wback = g_wback ? g_wback * 2 : 20000u;
            if (g_wback > 600000u) g_wback = 600000u;
        }
        weather_app_refresh();
    }
    if (!g_have) home_set_weather("Weather unavailable");
    uint32_t interval = g_have ? 600000u : net > 0 && g_wback ? g_wback : 20000u;
    if (!inflight && (g_wwant || lv_tick_get() - g_last > interval)) weather_start(net);
}

static lv_obj_t *g_app_w;
static lv_obj_t *g_app_loc;

static void weather_app_refresh(void)
{
    if (g_app_w) {
        char snap[160]; int wfail;
        pthread_mutex_lock(&g_wx_mu);
        snprintf(snap, sizeof snap, "%s", g_wbuf);
        wfail = g_wfail;
        pthread_mutex_unlock(&g_wx_mu);
        lv_label_set_text(g_app_w, g_have ? snap
                                          : (wfail ? "Weather unavailable" : "Fetching..."));
    }
    if (g_app_loc) {
        const char *l = cfg_get_str("weather_loc", "");
        char b[128];
        snprintf(b, sizeof b, "Location:  %s", (l && l[0]) ? l : "Auto (by IP)");
        lv_label_set_text(g_app_loc, b);
    }
}

void weather_app_open(void) { weather_app_refresh(); screen_show(SCR_WEATHER); }

static void loc_done(const char *text)
{
    if(!text) return;
    cfg_set_str("weather_loc", text);
    g_wgen++;
    g_have = 0;
    g_wback = 0;
    weather_fetch_async();
    weather_app_refresh();
}
static void set_loc_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    kbinput_open("City / location", cfg_get_str("weather_loc", ""), loc_done);
}
static void auto_loc_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    loc_done("");
}

static lv_obj_t *wapp_btn(lv_obj_t *root, int y, const char *label, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, 50, y); lv_obj_set_size(b, 260, 52);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, "weather.cb", UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
    return b;
}

void weather_app_create(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    ui_header(root, "Weather");

    s_appwfont = *TF(UI_22);
    s_appwfont.fallback = &font_weather16;
    g_app_w = lv_label_create(root);
    lv_obj_set_pos(g_app_w, 0, 96); lv_obj_set_width(g_app_w, 360);
    lv_obj_set_style_text_align(g_app_w, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_app_w, &s_appwfont, 0);
    lv_obj_set_style_text_color(g_app_w, TC(TEXT_PRIMARY), 0);

    g_app_loc = lv_label_create(root);
    lv_obj_set_pos(g_app_loc, 0, 140); lv_obj_set_width(g_app_loc, 360);
    lv_obj_set_style_text_align(g_app_loc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_app_loc, TF(UI_14), 0);
    lv_obj_set_style_text_color(g_app_loc, TC(TEXT_MUTED), 0);

    wapp_btn(root, 186, "Set Location", set_loc_cb);
    wapp_btn(root, 248, "Auto (by IP)", auto_loc_cb);

    weather_app_refresh();
}
