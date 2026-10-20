#include "simple_theme.h"
#include "onboarding.h"
#include "null_theme.h"
#include "battery.h"
#include "quicksettings.h"
#include "folderbrowser.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "fwcaps.h"
#include "theme.h"
#include "theme_kit.h"
#include "screens.h"
#include "braun.h"
#include "theme.h"
#include "orbit.h"
#include "ipc.h"
#include "config.h"
#include <math.h>
#include "scanner.h"
#include "i18n.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Quick Settings, "orbit on rails": six round toggles orbit a hub showing the playing cover inside its
 * progress ring (the Ring Now Playing motif). Brightness is a control arc on the top rim; the battery is
 * an indicator arc on the bottom rim with the Home screen's logic (white, red + slow pulse at <=15%).
 * A tap on the cover goes Home, the small centre button plays/pauses. A short tap on Wi-Fi / Bluetooth
 * toggles the radio, a long press opens its settings. Background: the album's blurred backdrop, dimmed. */

LV_FONT_DECLARE(font_theme_20)
LV_FONT_DECLARE(font_theme_24)
LV_FONT_DECLARE(font_icons_28)          /* FontAwesome 28px */
#define WI_SUN "\xEF\x86\x85"           /* f185 sun */
#define QS_ARC_D     348                /* rim arcs */
#define QS_ARC_W     TH_ARC_CTL         /* brightness: a control arc, rounded ends */

enum { T_WIFI, T_BT, T_LIB, T_RESCAN, T_MODE, T_SET, T_N };
enum { Q_EQ=T_N, Q_SEARCH, Q_QUEUE, Q_UPNEXT, Q_SHORTCUTS, Q_WEATHER, Q_LYRICS, Q_INFO, Q_IMMERSIVE, Q_FOLDERS, Q_FAVS, Q_PLAYBACK, Q_DEBUG, Q_COUNT };
static const qs_item_t CATALOG[Q_COUNT]={
    {"w","Wi-Fi",LV_SYMBOL_WIFI},{"b","Bluetooth",LV_SYMBOL_BLUETOOTH},
    {"l","Library",LV_SYMBOL_DIRECTORY},{"r","Rescan",LV_SYMBOL_REFRESH},
    {"m","Modes",LV_SYMBOL_SD_CARD},{"s","Settings",LV_SYMBOL_SETTINGS},
    {"e","Equalizer","EQ"},{"f","Search",TH_IC_SEARCH},
    {"q","Queue",LV_SYMBOL_LIST},{"u","Up Next",LV_SYMBOL_NEXT},
    {"c","Shortcuts",LV_SYMBOL_BARS},{"h","Weather",WI_SUN},
    {"y","Lyrics",LV_SYMBOL_AUDIO},{"i","Song Info",LV_SYMBOL_FILE},
    {"v","Immersive",LV_SYMBOL_IMAGE},{"d","Folders",LV_SYMBOL_DIRECTORY},
    {"a","Favourites","\xEF\x80\x84"},
    {"p","Play mode",LV_SYMBOL_LOOP},
    {"x","Debug",LV_SYMBOL_SETTINGS}          /* opens Debug Mode; lit while its SSH runs */
};
int quicksettings_item_count(void){return Q_COUNT;}
int quicksettings_item_limit(void){return (th_null()||th_simple())?8:th_disco()?7:6;}
int quicksettings_playback_item(void){return Q_PLAYBACK;}
int quicksettings_equalizer_item(void){return Q_EQ;}
int quicksettings_item_active(int id){int p=simple_cfg_get_int("eq_preset",0);return id==T_WIFI?wifi_radio_live()>0:id==T_BT?bt_state()==BT_ON:id==T_RESCAN?scanner_active():id==T_MODE?ui_get_source_mode()!=0:id==Q_EQ?p>0&&p<=20:id==Q_PLAYBACK?simple_cfg_get_int("work_mode",0)!=0:id==Q_DEBUG?debug_ssh_on():0;}
static const char*playback_glyph(void){static const char*const g[]={LV_SYMBOL_RIGHT,LV_SYMBOL_SHUFFLE,LV_SYMBOL_LOOP "1",LV_SYMBOL_LOOP,LV_SYMBOL_RIGHT "1"};int v=simple_cfg_get_int("work_mode",0);return g[v>=0&&v<5?v:0];}
const qs_item_t *quicksettings_item(int id){return id>=0 && id<Q_COUNT?&CATALOG[id]:NULL;}
int quicksettings_items_load(int *out){
    const char *code=simple_cfg_get_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items",NULL);int n=0,all[Q_COUNT],limit=quicksettings_item_limit();
    if(code)for(const char *p=code;*p && n<Q_COUNT;p++)for(int id=0;id<Q_COUNT;id++)if(*p==CATALOG[id].code[0]){
        int duplicate=0;for(int k=0;k<n;k++)if(all[k]==id)duplicate=1;
        if(!duplicate)all[n++]=id;
        break;
    }
    if(!code || (code[0] && !n)){
        if(th_null()||th_simple()){static const int nd[]={T_WIFI,T_BT,T_MODE,Q_EQ,T_RESCAN,Q_SEARCH,Q_WEATHER,T_SET};memcpy(out,nd,sizeof nd);return 8;}
        if(!th_disco_family()){for(int i=0;i<T_N;i++)out[i]=i;return T_N;}
        static const int defaults[]={T_WIFI,T_BT,T_MODE,T_LIB,Q_EQ,Q_SEARCH,T_SET};
        memcpy(out,defaults,sizeof defaults);return sizeof defaults/sizeof defaults[0];
    }
    while(n>limit){                                          /* an older, longer list: drop the last that isn't pinned */
        int k=n-1;while(k>=0 && (all[k]==T_WIFI || all[k]==T_BT || all[k]==T_SET))k--;
        if(k<0)break;
        memmove(all+k,all+k+1,(size_t)(n-k-1)*sizeof *all);n--;
    }
    if(n>limit)n=limit;
    memcpy(out,all,(size_t)n*sizeof *out);
    return n;
}
int quicksettings_items_save(const int *items,int count){
    if(count<0 || count>quicksettings_item_limit())return -1;
    char codes[QS_MAX_ITEMS+1],old[48];
    for(int i=0;i<count;i++){
        if(items[i]<0 || items[i]>=Q_COUNT)return -1;
        for(int j=0;j<i;j++)if(items[j]==items[i])return -1;
        codes[i]=CATALOG[items[i]].code[0];
    }
    codes[count]=0;snprintf(old,sizeof old,"%s",simple_cfg_get_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items","wbmlefs"));
    if(simple_cfg_set_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items",codes)<0){simple_cfg_set_str_deferred(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items",old);cfg_flush();return -1;}
    return 0;
}
static int g_items[QS_MAX_ITEMS],g_nitems,g_style=-1,g_style_raw=-1;
static char g_codes[48];static lv_obj_t *g_controls,*g_root;
static orbit_t g_orb;
static lv_obj_t *g_bright, *g_batt, *g_bg, *g_dim;
static lv_obj_t *g_cover_clip, *g_cover_img, *g_cover_note, *g_prog, *g_pp, *g_pp_glyph;
static lv_timer_t *g_prog_timer;
static battery_arc_t g_battery;
static lv_obj_t *g_batt_icon, *g_sun;
static int g_scanning = -1;
static void rescan_show(int on);
static void dq_paint(void);

static void bright_change_cb(lv_event_t *e){ ui_backlight(lv_arc_get_value(lv_event_get_target(e))); }
static void bright_release_cb(lv_event_t *e){ ui_set_brightness(lv_arc_get_value(lv_event_get_target(e))); }
static void pp_cb(lv_event_t *e){ (void)e; if(ui_transport_command("0201000C0000")==0) ui_pp_glyph(g_pp_glyph,ui_pp_icon_playing(ui_is_playing())); }
static void home_cb(lv_event_t *e){ (void)e; screen_show(SCR_HOME); }            /* the cover leads Home */

/* the Mode toggle wears the icon of the mode the player is actually in (same set as Working Mode) */
static const char *mode_glyph(void){
    switch(ui_get_source_mode()){
        case 1:  return LV_SYMBOL_USB;
        case 2:  return LV_SYMBOL_BLUETOOTH;
        case 3:  return LV_SYMBOL_DRIVE;
        case 4:  return LV_SYMBOL_VOLUME_MAX;
        case 5:  return LV_SYMBOL_WIFI;
        default: return LV_SYMBOL_SD_CARD;
    }
}
const char *quicksettings_item_glyph(int id){const qs_item_t *item=quicksettings_item(id);return id==Q_PLAYBACK?playback_glyph():id==T_MODE?mode_glyph():item?item->icon:LV_SYMBOL_SETTINGS;}
static void paint_radios(void){
    if(th_disco_family()){dq_paint();return;}
    lv_color_t acc = th_ringlike()?ui_current_accent():TC(ACCENT_PRIMARY);
    int pending=-1;
    for(int i=0;i<g_nitems;i++){int id=g_items[i];if(id==T_WIFI||id==T_BT||id==Q_PLAYBACK||id==Q_EQ)orbit_set_on(&g_orb,i,quicksettings_item_active(id),acc);if(id==T_BT&&bt_state()==BT_TURNING_ON)pending=i;}
    orbit_set_pending(&g_orb,pending,acc);
}
static void rescan_go(void){ ui_rescan_library(); ui_toast("Rescan requested"); rescan_show(1); }
static void eq_toggle(void){
 int preset=0;
 if(!quicksettings_item_active(Q_EQ)){preset=simple_cfg_get_int("eq_last",1);if(preset<1||preset>20)preset=1;}
 if(ui_eq_select(preset)<0){ui_toast("Couldn't switch EQ");return;}
 int remembered=preset?preset:simple_cfg_get_int("eq_last",1);if(remembered<1||remembered>20)remembered=1;
 char message[160];snprintf(message,sizeof message,"EQ %s\nPreset: %s",preset?"On":"Off",ui_eq_name(remembered));ui_toast(message);
 quicksettings_refresh(ui_is_playing());
}
static void pick_cb(int i){                                       /* short taps */
    switch(i){
        case T_WIFI: { int on = wifi_toggle(); orbit_set_on(&g_orb,T_WIFI,on,th_ringlike()?ui_current_accent():TC(ACCENT_PRIMARY));
                       ui_toast(on ? "Turning on Wi-Fi\xE2\x80\xA6" : "Wi-Fi off"); } break;
        case T_BT:   { int on = bt_toggle(); paint_radios();
                       ui_toast(on ? "Turning on Bluetooth\xE2\x80\xA6" : "Bluetooth off"); } break;
        case T_LIB:    screen_show(SCR_LIBRARY); break;
        case T_RESCAN: if(scanner_active()){ ui_toast("Already scanning"); break; }
                       fileops_confirm("Rescan library?", "Looks for new and changed music", "Rescan", rescan_go); break;
        case T_MODE:   modes_open(); break;
        case T_SET:    screen_show(SCR_SETTINGS); break;
        case Q_EQ: eq_toggle();break;
        case Q_SEARCH: screen_show(SCR_SEARCH);break;
        case Q_QUEUE: queue_open();break;
        case Q_UPNEXT: upnext_open();break;
        case Q_SHORTCUTS: apps_reload();screen_show(SCR_APPS);break;
        case Q_WEATHER: weather_app_open();break;
        case Q_LYRICS: lyrics_open();break;
        case Q_INFO: {track_state_t st;ipc_get_state(&st);if(!st.have_track){ui_toast("No track playing");break;}songinfo_unpin();songinfo_set(&st);screen_show(SCR_SONGINFO);break;}
        case Q_IMMERSIVE: disco_open_np_immersive();break;
        case Q_FOLDERS: folderbrowser_open();break;
        case Q_FAVS: library_open_favourites();screen_show(SCR_LIBRARY);break;
        case Q_PLAYBACK:ui_np_mode_next();break;
        case Q_DEBUG: debug_open();break;   /* the SSH password is shown there, so open it rather than toggle blind */
    }
}
static void base_pick(int slot){if(slot>=0&&slot<g_nitems){pick_cb(g_items[slot]);quicksettings_refresh(ui_is_playing());}}
static void base_hold(lv_event_t*e){int slot=(int)(intptr_t)lv_event_get_user_data(e);if(slot<0||slot>=g_nitems)return;quicksettings_null_action(g_items[slot],1);}
static void base_build(void){
 g_nitems=quicksettings_items_load(g_items);
 for(int i=0;i<T_N;i++){
  if(i>=g_nitems){lv_obj_add_flag(g_orb.btn[i],LV_OBJ_FLAG_HIDDEN);if(g_orb.cap[i])lv_obj_add_flag(g_orb.cap[i],LV_OBJ_FLAG_HIDDEN);continue;}
  int id=g_items[i];lv_obj_remove_flag(g_orb.btn[i],LV_OBJ_FLAG_HIDDEN);if(g_orb.cap[i]){lv_obj_remove_flag(g_orb.cap[i],LV_OBJ_FLAG_HIDDEN);lv_label_set_text(g_orb.cap[i],id==T_MODE?"Mode":CATALOG[id].name);}
  orbit_set_glyph(&g_orb,i,quicksettings_item_glyph(id));
 }
 paint_radios();
}

/* the hub's progress ring follows the track (1 s poll; only redraws on a visible change) */
static void prog_tick(lv_timer_t *t){
    (void)t;
    if(!g_prog || screen_current() != SCR_QUICK) return;
    rescan_show(scanner_active() ? 1 : 0);                     /* also picks up a scan started elsewhere */
    paint_radios();                                             /* BT comes up in the background: follow it */
    dq_paint();
    track_state_t st; ipc_get_state(&st);
    int v = (st.have_track && st.duration_ms > 0) ? (int)((long long)st.position_ms * 1000 / st.duration_ms) : 0;
    if(abs(v - lv_arc_get_value(g_prog)) >= 3) lv_arc_set_value(g_prog, v);
}
/* rescan: while the library scan runs, the Rescan icon turns red and spins (1 turn/s) */
static void spin_exec(void *var, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t *)var, v, 0); }
static void rescan_show(int on){
    if(th_disco_family()||th_null()){g_scanning=on;return;}
    if(on == g_scanning) return;
    g_scanning = on;
    lv_obj_t *ic = g_orb.icon[T_RESCAN];
    lv_anim_delete(ic, spin_exec);
    lv_obj_set_style_text_color(ic, on ? TC(ACCENT_PRIMARY) : (th_braun() ? TC(ON_CONTROL) : TC(TEXT_PRIMARY)), 0);
    if(g_orb.cap[T_RESCAN]){ lv_label_set_text(g_orb.cap[T_RESCAN], on ? "Scanning" : "Rescan");
                            lv_obj_set_style_text_color(g_orb.cap[T_RESCAN], on ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY), 0); }
    if(on){
        lv_obj_update_layout(ic);
        lv_obj_set_style_transform_pivot_x(ic, lv_obj_get_width(ic) / 2, 0);
        lv_obj_set_style_transform_pivot_y(ic, lv_obj_get_height(ic) / 2, 0);
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, ic); lv_anim_set_exec_cb(&a, spin_exec);
        lv_anim_set_values(&a, 0, 3600); lv_anim_set_duration(&a, 1000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    } else lv_obj_set_style_transform_rotation(ic, 0, 0);
}

/* ---- Disco: preserve the music disc and rim arcs; configurable controls use the current rows
 * or an eight-button arc. The catalog and ordering are shared by both styles. */
#define DQ_CX 272                                                   /* the field's visible middle */
#define DQ_CY 180
#define DQ_OX (disco_mirror() ? 180 - DQ_CX : DQ_CX - 180)               /* the field's middle as an offset (Disco Left: on the left) */
static int g_hub_d = ORBIT_HUB;                                    /* the cover disc (Disco: bigger) */
static lv_obj_t *g_dq_row[QS_MAX_ITEMS], *g_dq_ico[QS_MAX_ITEMS], *g_dq_val[QS_MAX_ITEMS], *g_dq_name[QS_MAX_ITEMS], *g_dq_badge[QS_MAX_ITEMS];
static void set_text_if(lv_obj_t *l, const char *t){ if(l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }
static void set_col_if(lv_obj_t *l, lv_color_t c){ if(l && !lv_color_eq(lv_obj_get_style_text_color(l, 0), c)) lv_obj_set_style_text_color(l, c, 0); }
static void dq_paint(void){
    if(!th_disco_family())return;
    lv_color_t acc=ui_current_accent();
    for(int i=0;i<g_nitems;i++){
        int id=g_items[i],on=quicksettings_item_active(id);
        if(!g_dq_ico[i])continue;
        if(g_dq_row[i] && !lv_color_eq(lv_obj_get_style_border_color(g_dq_row[i],0),acc)){
            lv_obj_set_style_border_color(g_dq_row[i],acc,0);
            lv_obj_set_style_border_color(g_dq_row[i],acc,LV_STATE_PRESSED);
        }
        set_col_if(g_dq_ico[i],on?acc:TC(TEXT_PRIMARY));
        if(id==T_WIFI || id==T_BT || id==Q_EQ){
            set_text_if(g_dq_val[i],id==T_BT && bt_state()==BT_TURNING_ON?"...":on?"On":"Off");
            if(g_dq_badge[i]){
                lv_color_t color=on?acc:TC(SURFACE_RAISED);
                if(!lv_color_eq(lv_obj_get_style_bg_color(g_dq_badge[i],0),color))lv_obj_set_style_bg_color(g_dq_badge[i],color,0);
                set_col_if(g_dq_val[i],on?theme_on_color(acc):TC(TEXT_PRIMARY));
            }
        }
        if(id==T_RESCAN)set_text_if(g_dq_name[i],on?"Scanning":"Rescan");
        if(id==T_MODE||id==Q_PLAYBACK)set_text_if(g_dq_ico[i],quicksettings_item_glyph(id));
    }
}
static void dq_row_cb(lv_event_t *e){
    int slot=(int)(intptr_t)lv_event_get_user_data(e);if(slot<0 || slot>=g_nitems)return;
    int id=g_items[slot];
    if(lv_event_get_code(e)==LV_EVENT_LONG_PRESSED){
        quicksettings_null_action(id,1);
    }else if(lv_event_get_code(e)==LV_EVENT_SHORT_CLICKED){pick_cb(id);dq_paint();}
}
static const lv_font_t *row_font_list(const char *text,int width,int height){   /* the Library's row font, smaller only if needed */
    const lv_font_t *fonts[]={TH_F_LIST,TF(UI_16),TF(UI_14)};
    for(int i=0;i<3;i++){
        lv_point_t size;lv_text_get_size(&size,text,fonts[i],0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        if(size.x<=width && size.y<=height)return fonts[i];
    }
    return fonts[2];
}
/* Arc: 76 px circles; six on a circle round the screen's centre (the rim's curve, inside the brightness and battery
 * arcs), from upper right (285 degrees) round the left to lower right (75), and one in the middle beside the music */
#define RING_D 76
#define RING_R 113
#define RING_A0 75
#define RING_A1 285
#define RING_CX 150
static void controls_build(lv_obj_t *root){
    if(g_controls){lv_obj_delete(g_controls);g_controls=NULL;}
    memset(g_dq_row,0,sizeof g_dq_row);memset(g_dq_ico,0,sizeof g_dq_ico);memset(g_dq_val,0,sizeof g_dq_val);
    memset(g_dq_name,0,sizeof g_dq_name);memset(g_dq_badge,0,sizeof g_dq_badge);
    g_nitems=quicksettings_items_load(g_items);g_style_raw=simple_cfg_get_int("disco_qs_style",1)==1;g_style=g_style_raw;
    snprintf(g_codes,sizeof g_codes,"%s",simple_cfg_get_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items","wbmlefs"));
    for(int i=0;i<T_N;i++){lv_obj_add_flag(g_orb.btn[i],LV_OBJ_FLAG_HIDDEN);if(g_orb.cap[i])lv_obj_add_flag(g_orb.cap[i],LV_OBJ_FLAG_HIDDEN);}
    g_controls=lv_obj_create(root);lv_obj_remove_style_all(g_controls);lv_obj_set_size(g_controls,360,360);
    lv_obj_clear_flag(g_controls,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    if(g_sun){lv_obj_set_style_transform_scale(g_sun,176,0);lv_obj_set_style_transform_pivot_x(g_sun,lv_pct(50),0);lv_obj_set_style_transform_pivot_y(g_sun,lv_pct(50),0);
        lv_obj_align(g_sun,LV_ALIGN_CENTER,disco_mirror()?-120:120,-107);}   /* centred between the field and the end of the brightness arc */
    if(g_batt_icon){lv_obj_set_style_text_font(g_batt_icon,TF(UI_20),0);lv_obj_align(g_batt_icon,LV_ALIGN_CENTER,disco_mirror()?-120:120,107);}   /* and between the field and the end of the battery arc */
    /* Arc: Wi-Fi in the middle beside the music; the rest on a circle round the screen's centre (the rim's curve, inside
     * the brightness and battery arcs): Bluetooth first at the upper right, round the left, Settings last at the lower
     * right. List: Wi-Fi and Bluetooth on top (the fixed pair), Settings the last row. */
    int order[QS_MAX_ITEMS],no=0,centre=-1;
    for(int i=0;i<g_nitems;i++)if(g_items[i]==T_WIFI)centre=i;
    if(centre<0)for(int i=0;i<g_nitems;i++)if(g_items[i]!=T_BT && g_items[i]!=T_SET){centre=i;break;}   /* no Wi-Fi: the first free one */
    for(int i=0;i<g_nitems;i++)if(g_items[i]==T_BT)order[no++]=i;
    for(int i=0;i<g_nitems;i++)if(i!=centre && g_items[i]!=T_BT && g_items[i]!=T_SET)order[no++]=i;
    for(int i=0;i<g_nitems;i++)if(g_items[i]==T_SET)order[no++]=i;
    int arcpos[QS_MAX_ITEMS];for(int i=0;i<g_nitems;i++)arcpos[i]=-1;
    for(int k=0;k<no;k++)arcpos[order[k]]=k;
    int settings_last=-1;for(int i=0;i<g_nitems;i++)if(g_items[i]==T_SET)settings_last=i;
    int rows=0,row_index=0;
    for(int i=0;i<g_nitems;i++)if(g_items[i]!=T_WIFI && g_items[i]!=T_BT)rows++;
    /* The first gap below the fixed radios equals every inter-row gap.
     * Use integer dimensions so no one-pixel remainder gets put into a gap. */
    /* List: rows stack from the top like a Library list (4 px apart), never taller than 44 px */
    int row_gap=4,row_h=rows?(240-row_gap*rows)/rows:0;
    if(row_h>44)row_h=44;
    for(int slot=0;slot<g_nitems;slot++){
        int id=g_items[slot],radio=id==T_WIFI || id==T_BT;
        int x,y,w,h;
        if(g_style){
            w=h=RING_D;
            if(slot==centre){x=RING_CX-RING_D/2;y=180-RING_D/2;}
            else{float a=(no<2?RING_A1:RING_A1-(RING_A1-RING_A0)*(float)arcpos[slot]/(no-1))*0.0174533f;   /* even, both ends pinned */
                 x=180+(int)lroundf(RING_R*cosf(a))-RING_D/2;y=180+(int)lroundf(RING_R*sinf(a))-RING_D/2;}
            x=disco_mx(x,w);
        }
        else{
            if(radio){h=38;y=42;x=id==T_WIFI?80:184;w=98;}
            else{h=row_h;int ri=slot==settings_last?rows-1:row_index++;y=80+row_gap+ri*(row_h+row_gap);
                /* List: rows sit and curve like the Library's (curvelist Classic): a row is at most 268 px wide,
                 * centred, and narrows with the round screen; here its right end stops short of the music field. */
                int dy=abs(y+h/2-180)+h/2;
                int half=dy<176?(int)sqrtf((float)(180*180-dy*dy))-10:0;
                if(half>134)half=134;
                x=180-half;
                int right=180+half;
                if(y<270 && y+h>90){                                   /* beside the music field: clear of it */
                    int near=y>180?y:y+h<180?y+h:180;
                    int d=near-180;
                    int field_left=286-(int)sqrtf((float)(90*90-d*d));
                    if(right>field_left-6)right=field_left-6;
                }else if(right>236)right=236;                          /* above / below it: clear of the sun and battery icons */
                w=right-x;
                }
            x=disco_mx(x,w);
        }
        lv_obj_t *r=g_dq_row[slot]=lv_obj_create(g_controls);lv_obj_remove_style_all(r);lv_obj_set_pos(r,x,y);lv_obj_set_size(r,w,h);
        lv_obj_set_style_radius(r,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(r,TC(SURFACE),0);lv_obj_set_style_bg_opa(r,150,0);
        lv_obj_set_style_bg_color(r,TC(SURFACE_RAISED),LV_STATE_PRESSED);lv_obj_set_style_bg_opa(r,LV_OPA_COVER,LV_STATE_PRESSED);
        lv_obj_set_style_border_width(r,g_style?1:0,0);lv_obj_set_style_border_color(r,ui_current_accent(),0);lv_obj_set_style_border_opa(r,80,0);
        lv_obj_set_style_border_width(r,2,LV_STATE_PRESSED);lv_obj_set_style_border_color(r,ui_current_accent(),LV_STATE_PRESSED);lv_obj_set_style_border_opa(r,LV_OPA_COVER,LV_STATE_PRESSED);
        lv_obj_add_flag(r,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_USER_2);if(g_style)lv_obj_add_flag(r,LV_OBJ_FLAG_ADV_HITTEST);lv_obj_clear_flag(r,LV_OBJ_FLAG_SCROLLABLE);
        ui_on(r,dq_row_cb,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)slot,"qs.item",UI_CORE);ui_on(r,dq_row_cb,LV_EVENT_LONG_PRESSED,(void*)(intptr_t)slot,"qs.item.hold",UI_CORE);
        g_dq_ico[slot]=lv_label_create(r);lv_label_set_text(g_dq_ico[slot],quicksettings_item_glyph(id));
        int small=!g_style && h<30;
        lv_obj_set_style_text_font(g_dq_ico[slot],id==Q_SEARCH?(small?&font_theme_20:&font_theme_24):
            (id==Q_WEATHER || id==Q_FAVS)?(small?TF(ICON_20):TF(ICON_28)):
            id==Q_EQ?(g_style?TF(UI_24):TF(UI_20)):g_style?TF(UI_28):small?TF(UI_20):TF(UI_24),0);
        if(!g_style && !radio)lv_obj_set_style_text_font(g_dq_ico[slot],
            id==Q_SEARCH?&font_theme_20:(id==Q_WEATHER || id==Q_FAVS)?TF(ICON_20):id==Q_EQ?TF(UI_14):TF(UI_20),0);
        if(g_style){lv_obj_align(g_dq_ico[slot],LV_ALIGN_CENTER,0,radio?-9:0);
            if(radio){g_dq_val[slot]=lv_label_create(r);lv_obj_set_style_text_font(g_dq_val[slot],TF(UI_14),0);lv_obj_set_style_text_color(g_dq_val[slot],TC(TEXT_PRIMARY),0);lv_obj_align(g_dq_val[slot],LV_ALIGN_BOTTOM_MID,0,-4);}}
        else if(radio){
            lv_obj_align(g_dq_ico[slot],LV_ALIGN_LEFT_MID,18,0);
            lv_obj_t *badge=g_dq_badge[slot]=lv_obj_create(r);lv_obj_remove_style_all(badge);lv_obj_set_size(badge,h-8,h-8);lv_obj_align(badge,LV_ALIGN_RIGHT_MID,-4,0);
            lv_obj_set_style_radius(badge,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_opa(badge,LV_OPA_COVER,0);lv_obj_clear_flag(badge,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            g_dq_val[slot]=lv_label_create(badge);lv_obj_set_style_text_font(g_dq_val[slot],TF(UI_14),0);lv_obj_center(g_dq_val[slot]);
        }else{
            lv_obj_align(g_dq_ico[slot],LV_ALIGN_LEFT_MID,16,0);
            g_dq_name[slot]=lv_label_create(r);lv_label_set_text(g_dq_name[slot],CATALOG[id].name);
            int text_w=w-58;
            lv_obj_set_style_text_font(g_dq_name[slot],row_font_list(CATALOG[id].name,text_w,h-4),0);
            lv_obj_set_style_text_letter_space(g_dq_name[slot],0,0);
            lv_obj_set_style_text_color(g_dq_name[slot],TC(TEXT_PRIMARY),0);
            lv_label_set_long_mode(g_dq_name[slot],LV_LABEL_LONG_CLIP);
            lv_obj_set_width(g_dq_name[slot],text_w);
            lv_obj_set_style_text_align(g_dq_name[slot],LV_TEXT_ALIGN_LEFT,0);
            lv_obj_align(g_dq_name[slot],LV_ALIGN_LEFT_MID,50,0);
        }
    }
    dq_paint();
}
static void disco_layout(lv_obj_t *root){
    /* the field: the open navigation circle's shape, its right end cut by the rim */
    lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f);
    lv_obj_set_pos(f, disco_mx(196, 230), 90); lv_obj_set_size(f, 230, 180);
    lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(f, TC(SCRIM), 0); lv_obj_set_style_bg_opa(f, 204, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(f, lv_obj_get_index(g_dim) + 1);
    g_hub_d = 120;                                                   /* a bigger cover in its ring */
    lv_obj_set_size(g_cover_clip, g_hub_d, g_hub_d); lv_obj_set_size(g_prog, g_hub_d + 16, g_hub_d + 16);
    lv_obj_align(g_prog, LV_ALIGN_CENTER, DQ_OX, DQ_CY - 180);
    lv_obj_align(g_cover_clip, LV_ALIGN_CENTER, DQ_OX, DQ_CY - 180);
    /* the album-colour glow: soft outline rings, not a 24 px blurred shadow (it sits inside the progress ring, which
     * redraws every second, and an uncached shadow was recomputed with it) */
    lv_obj_set_style_outline_width(g_cover_clip, 5, 0); lv_obj_set_style_outline_opa(g_cover_clip, 70, 0); lv_obj_set_style_outline_pad(g_cover_clip, 0, 0);
    lv_obj_set_style_outline_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_align(g_pp, LV_ALIGN_CENTER, DQ_OX, DQ_CY - 180);
    controls_build(root);
}
/* ---- Halo: the ORIGINAL diskOS pull-down Quick Settings panel (tiles, brightness bar, transport row, Settings >
 * Display > Quick Settings tile picker with the qs_t_* keys), kept verbatim from the reference as halo_* statics and
 * dispatched from the public entry points below when th_halo(). Other themes never reach this code. ---- */
static void halo_qs_refresh(int playing);
LV_FONT_DECLARE(font_icons_28)          /* FontAwesome 28px - brightness sun */
LV_FONT_DECLARE(font_icons_20)          /* FontAwesome 20px - EQ + Search glyphs (match the LVGL symbols) */
#define HWI_SUN    "\xEF\x86\x85"        /* f185 sun */
#define HWI_EQ     "\xEF\x87\x9E"        /* f1de sliders = equalizer */
#define HWI_SEARCH "\xEF\x80\x82"        /* f002 magnifier = search */
#define HWI_HEART  "\xEF\x80\x84"        /* f004 heart = favourite */
#define HWI_CLOCK  "\xEF\x80\x97"        /* f017 clock = sleep timer */
#define HWI_HP     "\xEF\x80\xA5"        /* f025 headphones = DRE */
#define HWI_LINK   "\xEF\x83\x81"        /* f0c1 chain = gapless */
#define HWI_MIC    "\xEF\x84\xB0"        /* f130 microphone = lyrics */
#define HWI_TINT   "\xEF\x81\x83"        /* f043 drop = colour theme */


#define HALO_QS_MAX_TILES 6                  /* grid holds two rows of three */

/* ---- tile palette ---------------------------------------------------------------------------- */
enum { HQT_WIFI, HQT_BT, HQT_EQ, HQT_SEARCH, HQT_RESCAN, HQT_MODE, HQT_SCREENOFF,
       HQT_SHUFFLE, HQT_FAV, HQT_LYRICS, HQT_TIMER, HQT_GAPLESS, HQT_GAIN, HQT_DRE, HQT_RG, HQT_OUTDOOR, HQT_THEME, HQT_DEBUG, HQT_COUNT };

typedef struct {
    const char       *cfg;      /* per-tile enable key */
    const char       *cap;      /* caption under the circle */
    const char       *glyph;
    theme_font_role_t font;     /* a font ROLE (a table initialiser can't call theme_font) */
    int               def_on;   /* enabled by default */
    int               has_long; /* has a long-press action (opens a screen) */
} HALO_qtile_t;

static const HALO_qtile_t HALO_QTILES[HQT_COUNT] = {
    [HQT_WIFI]      = { "qs_t_wifi",      "Wi-Fi",      LV_SYMBOL_WIFI,      THEME_FONT_UI_20, 1, 1 },
    [HQT_BT]        = { "qs_t_bt",        "Bluetooth",  LV_SYMBOL_BLUETOOTH, THEME_FONT_UI_20, 1, 1 },
    [HQT_EQ]        = { "qs_t_eq",        "EQ",         HWI_EQ,               THEME_FONT_ICON_20,         1, 1 },
    [HQT_SEARCH]    = { "qs_t_search",    "Search",     HWI_SEARCH,           THEME_FONT_ICON_20,         1, 0 },
    [HQT_RESCAN]    = { "qs_t_rescan",    "Rescan",     LV_SYMBOL_REFRESH,   THEME_FONT_UI_20, 1, 0 },
    [HQT_MODE]      = { "qs_t_mode",      "Source",     LV_SYMBOL_AUDIO,     THEME_FONT_UI_20, 1, 0 },
    [HQT_SCREENOFF] = { "qs_t_screenoff", "Screen off", LV_SYMBOL_EYE_CLOSE, THEME_FONT_UI_20, 0, 0 },
    /* opt-in audio/playback tiles (default off; enable in Settings > Display > Quick Settings) */
    [HQT_SHUFFLE]   = { "qs_t_shuffle",   "Shuffle",    LV_SYMBOL_SHUFFLE,   THEME_FONT_UI_20, 0, 1 },
    [HQT_FAV]       = { "qs_t_fav",       "Favourite",  HWI_HEART,            THEME_FONT_ICON_20,         0, 0 },
    [HQT_LYRICS]    = { "qs_t_lyrics",    "Lyrics",     HWI_MIC,              THEME_FONT_ICON_20,         0, 0 },
    [HQT_TIMER]     = { "qs_t_timer",     "Timer",      HWI_CLOCK,            THEME_FONT_ICON_20,         0, 0 },
    [HQT_GAPLESS]   = { "qs_t_gapless",   "Gapless",    HWI_LINK,             THEME_FONT_ICON_20,         0, 1 },
    [HQT_GAIN]      = { "qs_t_gain",      "High Gain",  LV_SYMBOL_VOLUME_MAX,THEME_FONT_UI_20, 0, 1 },
    [HQT_DRE]       = { "qs_t_dre",       "DRE",        HWI_HP,               THEME_FONT_ICON_20,         0, 1 },
    [HQT_RG]        = { "qs_t_rg",        "ReplayGain", LV_SYMBOL_VOLUME_MID,THEME_FONT_UI_20, 0, 0 },
    [HQT_OUTDOOR]   = { "qs_t_outdoor",   "Outdoor",    HWI_SUN,              THEME_FONT_ICON_20,         0, 1 },
    [HQT_THEME]     = { "qs_t_theme",     "Theme",      HWI_TINT,             THEME_FONT_ICON_20,         0, 1 },
    [HQT_DEBUG]     = { "qs_t_debug",     "Debug",      LV_SYMBOL_SETTINGS,  THEME_FONT_UI_20, 0, 0 },   /* opens Debug Mode; lit while its SSH runs */
};

static lv_obj_t *halo_g_qs_root;
static lv_obj_t *halo_g_bright;
static lv_obj_t *halo_g_prev_glyph, *halo_g_next_glyph;
static lv_obj_t *halo_g_pp_glyph;                 /* transport play/pause glyph (NULL when transport is off) */
static lv_obj_t *halo_g_tile_dot[HQT_COUNT];       /* the circle per shown tile, recoloured on refresh */
static lv_obj_t *halo_g_tile_lbl[HQT_COUNT];       /* its caption (the ring style colours it too) */

/* the drawer shows at most this many tiles - one fewer with the transport row, so the second row never
 * pushes its outer captions off the round bezel. */
static int halo_qs_cap(void){ return cfg_get_int("qs_transport", 0) ? HALO_QS_MAX_TILES - 1 : HALO_QS_MAX_TILES; }

/* which tiles are enabled, in palette order, capped. Returns the count. */
static int halo_qs_enabled(int ids[HQT_COUNT], int cap){
    int n = 0;
    for(int i=0; i<HQT_COUNT && n<cap; i++)
        if(cfg_get_int(HALO_QTILES[i].cfg, HALO_QTILES[i].def_on)) ids[n++] = i;
    return n;
}

/* is a toggle tile currently "on"? action tiles are never lit. */
static int halo_qtile_is_on(int id){
    switch(id){
        case HQT_WIFI:    return cfg_get_int("wifi_on", 1);
        case HQT_BT:      return bt_radio_on();   /* actual radio state, not just the persisted intent */
        case HQT_EQ:      return cfg_get_int("eq_preset", 0) > 0;
        case HQT_SHUFFLE: return cfg_get_int("work_mode", 0) == 1;
        case HQT_GAPLESS: return cfg_get_int("gapless", 0) == 1;
        case HQT_GAIN:    return fw_gain_tag() && cfg_get_int("audio_gain", 0) == 1;   /* unmapped firmware: never lit */
        case HQT_DRE:     return cfg_get_int("audio_dre", 1) == 1;
        case HQT_OUTDOOR: return theme_outdoor();
        case HQT_DEBUG:   return debug_ssh_on();
        case HQT_FAV:   { track_state_t st; ipc_get_state(&st); return st.path[0] && st.is_favorite; }
        default:         return 0;   /* action tiles (Search/Rescan/Source/Screen off/Lyrics/Timer/RG) never lit */
    }
}

static void halo_tile_paint(int id, int on);
/* paint a tile, then centre its icon by INK (after the theme's final font: icon glyphs sit high or low in their line, so
 * centring the line box left them 2 px low in some themes); word tiles (terminal) keep their baseline */
static void halo_tile_recolor(int id, int on){
    halo_tile_paint(id, on);
    if(halo_g_tile_dot[id]) ui_glyph_center_ink(lv_obj_get_child(halo_g_tile_dot[id], 0));
}
static void halo_tile_paint(int id, int on){
    if(!halo_g_tile_dot[id]) return;
    lv_obj_t *c = halo_g_tile_dot[id], *g = lv_obj_get_child(c, 0);
    if(theme_kit()->qs_tile){                         /* the theme's own tile */
        int toggle = id == HQT_WIFI || id == HQT_BT || id == HQT_EQ || id == HQT_SHUFFLE || id == HQT_FAV || id == HQT_GAPLESS
                  || id == HQT_GAIN || id == HQT_DRE || id == HQT_OUTDOOR;   /* the tiles halo_qtile_is_on() can light */
        kit_keep(c);                                  /* the theme painted it: the styling pass leaves it */
        theme_kit()->qs_tile(c, g, halo_g_tile_lbl[id], on, toggle); return;
    }
    if(theme_trait(THEME_TRAIT_QS_RING)){
        /* lit = accent ring + a faint accent halo (a wide translucent outline: no blur, cheap on this CPU) with an
         * accent glyph and caption; off = a plain dark disc */
        lv_obj_set_style_bg_color(c, on ? TC(SURFACE) : TC(SURFACE_RAISED), 0);
        lv_obj_set_style_border_width(c, on ? 2 : 0, 0);
        lv_obj_set_style_border_color(c, TC(ACCENT_PRIMARY), 0);
        lv_obj_set_style_outline_width(c, on ? 5 : 0, 0);
        lv_obj_set_style_outline_color(c, TC(ACCENT_PRIMARY), 0);
        lv_obj_set_style_outline_opa(c, LV_OPA_20, 0);
        if(g) lv_obj_set_style_text_color(g, on ? TC(ACCENT_PRIMARY) : TC(TEXT_PRIMARY), 0);
        if(halo_g_tile_lbl[id]) lv_obj_set_style_text_color(halo_g_tile_lbl[id], on ? TC(ACCENT_PRIMARY) : TC(TEXT_SECONDARY), 0);
        return;
    }
    lv_obj_set_style_bg_color(c, on ? ui_current_accent() : TC(CONTROL_OFF), 0);
    if(g) lv_obj_set_style_text_color(g, on ? TC(ON_ACCENT) : TC(TEXT_PRIMARY), 0);   /* on the accent fill when lit */
}

/* ---- EQ A/B toggle (drawer): flip Off <-> the last-used preset, all persisted centrally --------- */
static void halo_eq_toggle(void){
    int cur = cfg_get_int("eq_preset", 0); if(cur < 0 || cur > 20) cur = 0;
    if(cur > 0){                                  /* on -> off; remember what was on only if the send took */
        if(ui_eq_select(0) == 0){ cfg_set_int("eq_last", cur); halo_tile_recolor(HQT_EQ, 0); ui_toast("EQ off"); }
        else ui_toast("Couldn't apply EQ");
    }else{                                        /* off -> on; restore last (Rock the first time) */
        int last = cfg_get_int("eq_last", 0);
        if(last <= 0 || last > 20) last = 2;      /* 2 = Rock: an audible default before any is chosen */
        if(ui_eq_select(last) == 0){ halo_tile_recolor(HQT_EQ, 1); char m[40]; snprintf(m, sizeof m, "EQ: %s", ui_eq_name(last)); ui_toast(m); }
        else ui_toast("Couldn't apply EQ");
    }
}

/* ---- unified tile handlers (tile id in user_data) ---------------------------------------------- */
static void halo_tile_short_cb(lv_event_t *e){
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    switch(id){
        case HQT_WIFI:      { int on = wifi_toggle(); halo_tile_recolor(HQT_WIFI, on); ui_toast(on ? "Turning on Wi-Fi..." : "Wi-Fi off"); } break;
        case HQT_BT:        { int on = bt_toggle();   halo_tile_recolor(HQT_BT,   on); ui_toast(on ? "Turning on Bluetooth..." : "Bluetooth off"); } break;
        case HQT_EQ:        halo_eq_toggle(); break;
        case HQT_SEARCH:    screen_show(SCR_SEARCH); break;
        case HQT_RESCAN:    ui_rescan_library(); break;
        case HQT_MODE:      modes_open(); break;
        case HQT_SCREENOFF: ui_request_sleep(); screen_show(SCR_SAVER); break;
        case HQT_DEBUG:     debug_open(); break;   /* the SSH password is shown there, so open it rather than toggle blind */
        case HQT_SHUFFLE: {
            if(ui_book_active()){ ui_toast("Not available in books"); break; }   /* books stay in Single */
            int on = cfg_get_int("work_mode", 0) == 1 ? 0 : 1;                    /* Shuffle <-> Sequential */
            cfg_set_int("work_mode", on); ui_set_workmode(on);
            halo_tile_recolor(HQT_SHUFFLE, on); ui_toast(on ? "Shuffle on" : "Shuffle off");
        } break;
        case HQT_FAV: {
            track_state_t st; ipc_get_state(&st);
            if(!st.path[0] || ui_book_active()){ ui_toast("No track playing"); break; }
            int nv = !st.is_favorite; ui_set_favorite(nv);
            halo_tile_recolor(HQT_FAV, nv); ui_toast(nv ? "Loved" : "Unloved");
        } break;
        case HQT_LYRICS: lyrics_open(); break;
        case HQT_TIMER:  settings_open_key("sleep_idx"); break;      /* opens the Sleep Timer chooser */
        case HQT_GAPLESS: {
            int on = cfg_get_int("gapless", 0) ? 0 : 1;
            cfg_set_int("gapless", on); ui_set_gapless(on);
            halo_tile_recolor(HQT_GAPLESS, on); ui_toast(on ? "Gapless on" : "Gapless off");
        } break;
        case HQT_GAIN: {
            if(!fw_gain_tag()){ ui_toast("Not supported on this firmware yet"); break; }   /* nothing would be sent */
            int on = cfg_get_int("audio_gain", 0) ? 0 : 1;
            cfg_set_int("audio_gain", on); ui_set_gain(on);
            halo_tile_recolor(HQT_GAIN, on); ui_toast(on ? "High gain" : "Low gain");
        } break;
        case HQT_DRE: {
            int on = cfg_get_int("audio_dre", 1) ? 0 : 1;
            cfg_set_int("audio_dre", on); ui_set_dre(on);
            halo_tile_recolor(HQT_DRE, on); ui_toast(on ? "DRE on" : "DRE off");
        } break;
        case HQT_RG:     settings_open_key("replay_gain"); break;    /* opens the ReplayGain chooser */
        case HQT_OUTDOOR: {                                          /* redraws the UI in the new palette */
            int was = cfg_get_int("outdoor", 0) == 1;
            cfg_set_int("outdoor", !was);
            if(ui_theme_reload("quick") != 0) cfg_set_int("outdoor", was);   /* refused (scan): nothing changes */
        } break;
        case HQT_THEME: {                                            /* next colour preset */
            int was = theme_preset();
            cfg_set_int("theme_preset", (was + 1) % THEME_PRESET_COUNT);
            if(ui_theme_reload("quick") != 0) cfg_set_int("theme_preset", was);
        } break;
    }
}
static void halo_tile_long_cb(lv_event_t *e){
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    switch(id){
        case HQT_WIFI: wifi_open(); break;
        case HQT_BT:   bt_open();   break;
        case HQT_EQ:   screen_show(SCR_EQ); break;
        case HQT_SHUFFLE: settings_open_key("work_mode");  break;   /* full Play Mode chooser */
        case HQT_GAPLESS: settings_open_key("gapless");    break;
        case HQT_GAIN:    settings_open_key("audio_gain"); break;
        case HQT_DRE:     settings_open_key("audio_dre");  break;
        case HQT_OUTDOOR: settings_open_key("outdoor");    break;
        case HQT_THEME:   settings_open_key("theme_preset"); break;
        default: break;
    }
}

/* one circular tile + caption, parent = root, centre at (x, y_top). Records its circle for refresh. */
static void halo_build_tile(lv_obj_t *root, int id, int x, int y){
    const HALO_qtile_t *t = &HALO_QTILES[id];
    lv_obj_t *c = lv_button_create(root);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 58, 58);
    lv_obj_align(c, LV_ALIGN_TOP_MID, x, y);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, halo_qtile_is_on(id) ? ui_current_accent() : TC(CONTROL_OFF), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(c, LV_OPA_80, LV_STATE_PRESSED);   /* gentle press dim on either state colour */
    lv_obj_set_ext_click_area(c, 4);
    char an[32], al[32];
    lv_snprintf(an, sizeof an, "qs.tile.%d", id); lv_snprintf(al, sizeof al, "qs.tile.%d.long", id);
    if(t->has_long){
        /* SHORT_CLICKED so a long press fires ONLY the long action, never the toggle too */
        ui_on(c, halo_tile_short_cb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)id, an, UI_CORE);
        ui_on(c, halo_tile_long_cb, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)id, al, UI_CORE);
    }else{
        ui_on(c, halo_tile_short_cb, LV_EVENT_CLICKED, (void*)(intptr_t)id, an, UI_CORE);
    }
    lv_obj_t *g = lv_label_create(c);
    lv_obj_clear_flag(g, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(g, t->glyph);
    lv_obj_set_style_text_font(g, theme_font(t->font), 0);
    lv_obj_set_style_text_color(g, halo_qtile_is_on(id) ? TC(ON_ACCENT) : TC(TEXT_PRIMARY), 0);
    lv_obj_center(g);

    lv_obj_t *lb = lv_label_create(root);
    lv_obj_clear_flag(lb, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(lb, tr(t->cap));
    lv_obj_set_style_text_font(lb, TF(UI_14), 0);
    lv_obj_set_style_text_color(lb, TC(TEXT_MUTED), 0);
    lv_obj_align(lb, LV_ALIGN_TOP_MID, x, y + 62);

    halo_g_tile_dot[id] = c;
    halo_g_tile_lbl[id] = lb;
    if(theme_trait(THEME_TRAIT_QS_RING)){               /* upper-case bold captions, ring styling */
        char up[24];
        lv_label_set_text(lb, theme_upper(up, sizeof up, tr(t->cap)));
        lv_obj_set_style_text_font(lb, TF(UI_12), 0);
        lv_obj_align(lb, LV_ALIGN_TOP_MID, x, y + 64);
        halo_tile_recolor(id, halo_qtile_is_on(id));
    }
    if(theme_kit()->qs_tile) halo_tile_recolor(id, halo_qtile_is_on(id));
}

/* ---- optional transport row -------------------------------------------------------------------- */
static void halo_cmd_cb(lv_event_t *e){
    const char *c = (const char*)lv_event_get_user_data(e); if(!c) return;
    if(ui_transport_command(c) == 0) halo_qs_refresh(ui_is_playing());
}
static lv_obj_t *halo_tp_btn(lv_obj_t *root, const char *sym, const lv_font_t *font, int x, int y, int sz, void *cmd){
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, sz, sz);
    lv_obj_align(b, LV_ALIGN_TOP_MID, x, y);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 6);
    char an[24];
    ui_on(b, halo_cmd_cb, LV_EVENT_CLICKED, cmd, ui_transport_action(cmd, "qs", an, sizeof an), UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
    return l;
}
static void halo_build_transport(lv_obj_t *root, int y){
    halo_g_prev_glyph = halo_tp_btn(root, LV_SYMBOL_PREV, TF(UI_22), -84, y + 5, 46, (void*)"0201000C0002");
    halo_g_pp_glyph = halo_tp_btn(root, LV_SYMBOL_PAUSE, TF(UI_28), 0, y, 56, (void*)"0201000C0000");
    halo_g_next_glyph = halo_tp_btn(root, LV_SYMBOL_NEXT, TF(UI_22), 84, y + 5, 46, (void*)"0201000C0001");
    if(halo_g_pp_glyph) ui_pp_glyph(halo_g_pp_glyph, ui_is_playing());
}

/* ---- brightness bar ---------------------------------------------------------------------------- */
static void halo_bright_change_cb(lv_event_t *e){ if(!theme_outdoor()) ui_backlight(lv_slider_get_value(lv_event_get_target(e))); }
static void halo_bright_release_cb(lv_event_t *e){ if(!theme_outdoor()) ui_set_brightness(lv_slider_get_value(lv_event_get_target(e))); }
static void halo_build_brightness(lv_obj_t *root, int y){
    halo_g_bright = lv_slider_create(root);
    lv_obj_set_size(halo_g_bright, 216, 34);
    lv_obj_set_ext_click_area(halo_g_bright, 8);
    lv_obj_align(halo_g_bright, LV_ALIGN_TOP_MID, 0, y);
    lv_slider_set_range(halo_g_bright, 4, 40);
    lv_slider_set_value(halo_g_bright, ui_effective_brightness(), LV_ANIM_OFF);
    if(theme_outdoor()) lv_obj_add_state(halo_g_bright, LV_STATE_DISABLED);   /* Outdoor holds full brightness */
    lv_obj_set_style_bg_color(halo_g_bright, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(halo_g_bright, 17, LV_PART_MAIN);
    lv_obj_set_style_bg_color(halo_g_bright, TC(CONTROL_FILL), LV_PART_INDICATOR);
    lv_obj_set_style_radius(halo_g_bright, 17, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(halo_g_bright, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(halo_g_bright, 0, LV_PART_KNOB);
    ui_on(halo_g_bright, halo_bright_change_cb, LV_EVENT_VALUE_CHANGED, NULL, "halo.quicksettings.bright_change.value", UI_CORE);
    lv_obj_add_event_cb(halo_g_bright, halo_bright_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(halo_g_bright, halo_bright_release_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *sun = lv_label_create(halo_g_bright);
    lv_obj_clear_flag(sun, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(sun, HWI_SUN);
    lv_obj_set_style_text_font(sun, TF(ICON_28), 0);
    lv_obj_set_style_text_color(sun, TC(ON_CONTROL), 0);
    lv_obj_align(sun, LV_ALIGN_LEFT_MID, 12, 0);
    if(theme_trait(THEME_TRAIT_QS_RING)){
        /* a thin bar with the sun beside it (the whole old height stays touchable) */
        lv_obj_set_size(halo_g_bright, 196, 6);
        lv_obj_align(halo_g_bright, LV_ALIGN_TOP_MID, 14, y + 14);
        lv_obj_set_ext_click_area(halo_g_bright, 16);
        lv_obj_set_style_radius(halo_g_bright, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(halo_g_bright, 3, LV_PART_INDICATOR);
        lv_obj_set_parent(sun, root);
        lv_obj_set_style_text_font(sun, TF(ICON_20), 0);
        lv_obj_set_style_text_color(sun, TC(TEXT_PRIMARY), 0);
        lv_obj_align_to(sun, halo_g_bright, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    }
}

/* ---- build / refresh --------------------------------------------------------------------------- */
void halo_qs_create(lv_obj_t *root){ halo_g_qs_root = root; }   /* defer to halo_qs_build on open */

/* Rebuild the whole panel from config. Called on every entry so customisation changes show at once. */
void halo_qs_build(void){
    if(!halo_g_qs_root) return;
    lv_obj_clean(halo_g_qs_root);
    halo_g_bright = NULL; halo_g_pp_glyph = NULL; halo_g_prev_glyph = NULL; halo_g_next_glyph = NULL;
    for(int i=0;i<HQT_COUNT;i++){ halo_g_tile_dot[i] = NULL; halo_g_tile_lbl[i] = NULL; }

    lv_obj_set_style_bg_color(halo_g_qs_root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(halo_g_qs_root, LV_OPA_COVER, 0);

    /* grabber */
    lv_obj_t *grab = lv_obj_create(halo_g_qs_root);
    lv_obj_remove_style_all(grab);
    lv_obj_clear_flag(grab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(grab, 36, 5);
    lv_obj_align(grab, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_radius(grab, 3, 0);
    lv_obj_set_style_bg_color(grab, TC(GRABBER), 0);
    lv_obj_set_style_bg_opa(grab, LV_OPA_COVER, 0);

    int have_tr = cfg_get_int("qs_transport", 0) ? 1 : 0;
    int ids[HQT_COUNT]; int nt = halo_qs_enabled(ids, halo_qs_cap());
    int rows = nt > 3 ? 2 : (nt > 0 ? 1 : 0);

    const int H_BR = 34, H_TR = 56, H_ROW = 80, GAP = 14;
    int nblocks = 1 + have_tr + rows;                 /* brightness is always present */
    int total = H_BR + (have_tr ? H_TR : 0) + rows*H_ROW + (nblocks > 1 ? (nblocks-1)*GAP : 0);
    int y = 185 - total/2;                            /* centre the stack a touch below the middle */
    if(y < 44) y = 44;                                /* never overlap the grab handle */

    halo_build_brightness(halo_g_qs_root, y);  y += H_BR + GAP;
    if(have_tr){ halo_build_transport(halo_g_qs_root, y); y += H_TR + GAP; }

    int placed = 0;
    for(int r=0; r<rows; r++){
        int k = (r == 0) ? (nt > 3 ? 3 : nt) : (nt - 3);   /* row 1 up to 3, row 2 the remainder */
        for(int i=0; i<k; i++){
            int x = (int)((i - (k-1)/2.0) * 84);           /* centre the row, 84px column pitch */
            halo_build_tile(halo_g_qs_root, ids[placed++], x, y);
        }
        y += H_ROW + GAP;
    }
    if(theme_kit()->quicksettings){                   /* the active theme lays the drawer out its own way */
        qs_parts_t q = { halo_g_qs_root, grab, halo_g_bright, halo_g_prev_glyph ? lv_obj_get_parent(halo_g_prev_glyph) : NULL,
                         halo_g_pp_glyph ? lv_obj_get_parent(halo_g_pp_glyph) : NULL,
                         halo_g_next_glyph ? lv_obj_get_parent(halo_g_next_glyph) : NULL, nt, {0}, {0} };
        for(int i = 0; i < nt; i++){ q.tile[i] = halo_g_tile_dot[ids[i]]; q.cap[i] = halo_g_tile_lbl[ids[i]]; }
        theme_kit()->quicksettings(&q);
    }
    halo_qs_refresh(ui_is_playing());
}

/* light update while the panel is open (playstate ticks, radio/EQ state changes) - no rebuild */
void halo_qs_refresh(int playing){
    if(halo_g_bright && !lv_obj_has_state(halo_g_bright, LV_STATE_PRESSED))
        lv_slider_set_value(halo_g_bright, ui_effective_brightness(), LV_ANIM_OFF);
    if(halo_g_pp_glyph) ui_pp_glyph(halo_g_pp_glyph, ui_pp_icon_playing(playing));
    int book = ui_book_active();
    if(halo_g_prev_glyph){
        lv_label_set_text(halo_g_prev_glyph, book ? "-15" : LV_SYMBOL_PREV);
        lv_obj_set_style_text_font(halo_g_prev_glyph, book ? TF(UI_16) : TF(UI_22), 0);
    }
    if(halo_g_next_glyph){
        lv_label_set_text(halo_g_next_glyph, book ? "+30" : LV_SYMBOL_NEXT);
        lv_obj_set_style_text_font(halo_g_next_glyph, book ? TF(UI_16) : TF(UI_22), 0);
    }
    for(int i=0;i<HQT_COUNT;i++)
        if(halo_g_tile_dot[i]) halo_tile_recolor(i, halo_qtile_is_on(i));
}

/* ---- customisation screen (SCR_QSCONFIG): pick which tiles appear -------------------------------- */
static lv_obj_t *halo_g_qscfg_root;

static void halo_qscfg_back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }
static void halo_qscfg_sw_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *sw = lv_event_get_target(e);
    int on = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    if(id < 0){
        int cnt = 0;
        for(int i=0;i<HQT_COUNT;i++) if(cfg_get_int(HALO_QTILES[i].cfg, HALO_QTILES[i].def_on)) cnt++;
        if(on && cnt > 5){
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
            ui_toast("Turn off a tile first");
            return;
        }
        cfg_set_int("qs_transport", on);
        return;
    }   /* id -1 = the transport row */
    if(on){                                                  /* refuse to enable past the drawer's tile cap */
        int cap = halo_qs_cap();
        int cnt = 0; for(int i=0;i<HQT_COUNT;i++) if(cfg_get_int(HALO_QTILES[i].cfg, HALO_QTILES[i].def_on)) cnt++;
        if(cnt >= cap){                                     /* this tile is still off in cfg, so cnt excludes it */
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
            char m[32]; snprintf(m, sizeof m, "Up to %d tiles shown", cap); ui_toast(m);
            return;
        }
    }
    cfg_set_int(HALO_QTILES[id].cfg, on);
}
/* a labelled row with an LVGL switch on the right */
static void halo_qscfg_row(lv_obj_t *list, const char *label, int id, int on){
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 268, 50);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_bg_color(row, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_70, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, tr(label));
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_style_text_font(lbl, TF(UI_16), 0);
    lv_obj_set_style_text_color(lbl, TC(TEXT_PRIMARY), 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 48, 26);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -12, 0);
    if(on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, ui_current_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    ui_on(sw, halo_qscfg_sw_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)id, "halo.quicksettings.qscfg_sw.value", UI_CORE);
}

void halo_qsconfig_create(lv_obj_t *root){ halo_g_qscfg_root = root; }

void halo_qsconfig_refresh(void){
    if(!halo_g_qscfg_root) return;
    lv_obj_clean(halo_g_qscfg_root);
    lv_obj_set_style_bg_color(halo_g_qscfg_root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(halo_g_qscfg_root, LV_OPA_COVER, 0);
    ui_header_cb(halo_g_qscfg_root, "Quick Settings", halo_qscfg_back_cb);

    lv_obj_t *list = lv_obj_create(halo_g_qscfg_root);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 30, 70);
    lv_obj_set_size(list, 300, 250);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_style_pad_bottom(list, 44, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    halo_qscfg_row(list, "Playback controls", -1, cfg_get_int("qs_transport", 0));
    for(int i=0;i<HQT_COUNT;i++)
        halo_qscfg_row(list, HALO_QTILES[i].cap, i, cfg_get_int(HALO_QTILES[i].cfg, HALO_QTILES[i].def_on));

    lv_obj_t *hint = lv_label_create(list);
    lv_label_set_text(hint, tr("The drawer shows up to 6 tiles (5 with playback controls)."));
    lv_obj_set_width(hint, 288);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(hint, TF(UI_14), 0);
    lv_obj_set_style_text_color(hint, TC(TEXT_MUTED), 0);
    lv_obj_set_style_pad_top(hint, 6, 0);
    lv_obj_set_style_pad_left(hint, 6, 0);
}

/* Battery fill: normal text colour, low red, charging green; no pulse or glow. */
void quicksettings_create(lv_obj_t *root)
{
    g_root=root;
    if(th_halo()){halo_qs_create(root);return;}
    if(th_simple()){simple_qs_create(root);return;}
    if(th_null()){null_qs_create(root);return;}
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* media surface: the album's blurred backdrop, dimmed well down (hidden when there is no art) */
    g_bg = lv_image_create(root);
    lv_obj_set_size(g_bg, 360, 360); lv_obj_center(g_bg);
    lv_obj_clear_flag(g_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
    g_dim = lv_obj_create(root);
    lv_obj_remove_style_all(g_dim);
    lv_obj_set_size(g_dim, 360, 360);
    lv_obj_set_style_bg_color(g_dim, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_dim,th_ringlike()?85:210,0);
    lv_obj_clear_flag(g_dim, LV_OBJ_FLAG_CLICKABLE);

    /* brightness: a control arc on the top rim, rounded ends; ADV_HITTEST keeps its touch area on the band */
    g_bright = lv_arc_create(root);
    lv_obj_remove_style(g_bright, NULL, LV_PART_KNOB);
    lv_obj_set_size(g_bright, QS_ARC_D, QS_ARC_D);
    lv_obj_center(g_bright);
    lv_obj_add_flag(g_bright, LV_OBJ_FLAG_ADV_HITTEST);
    lv_arc_set_rotation(g_bright, 0);
    lv_arc_set_bg_angles(g_bright, 228, 312);
    lv_arc_set_range(g_bright, 4, 40);
    lv_arc_set_value(g_bright, ui_get_brightness());
    lv_obj_set_style_arc_width(g_bright, QS_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_bright, th_braun() ? TC(SURFACE) : TC(CONTROL_TRACK_STRONG), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g_bright, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_bright, QS_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_bright, th_braun() ? TC(TEXT_PRIMARY) : TC(TEXT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_bright, true, LV_PART_INDICATOR);
    lv_obj_add_event_cb(g_bright, bright_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_bright, bright_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(g_bright, bright_release_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *sun = lv_label_create(root);
    lv_obj_clear_flag(sun, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(sun, WI_SUN);
    lv_obj_set_style_text_font(sun, TF(ICON_28), 0);
    lv_obj_set_style_transform_scale(sun, 128, 0);            /* half size: a small marker under the arc */
    lv_obj_set_style_text_color(sun, TC(TEXT_SECONDARY), 0);
    lv_obj_align(sun, LV_ALIGN_TOP_MID, 0, 18);
    g_sun = sun;

    /* battery: an indicator arc on the bottom rim, filling left to right; display only */
    battery_arc_create(&g_battery,root,QS_ARC_D,QS_ARC_W,48,132,1);g_batt=g_battery.arc;
    g_batt_icon = lv_label_create(root);                        /* battery glyph above the arc, the sun's mirror */
    lv_obj_clear_flag(g_batt_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(g_batt_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(g_batt_icon, TF(UI_14), 0);
    lv_obj_set_style_text_color(g_batt_icon, TC(TEXT_SECONDARY), 0);
    lv_obj_align(g_batt_icon, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* the six toggles, clockwise from the upper right, with captions */
    static const orbit_item_t it[T_N] = {
        { LV_SYMBOL_WIFI, "Wi-Fi" }, { LV_SYMBOL_BLUETOOTH, "Bluetooth" }, { LV_SYMBOL_DIRECTORY, "Library" },
        { LV_SYMBOL_REFRESH, "Rescan" }, { LV_SYMBOL_SD_CARD, "Mode" }, { LV_SYMBOL_SETTINGS, "Settings" } };
    orbit_create(&g_orb, root, it, T_N, -60, th_disco_family()?pick_cb:base_pick);
    if(th_disco_family()) for(int i = 0; i < T_N; i++){
        /* Also cover the optional icon-arcs layout. No timer or animation. */
        lv_obj_set_style_bg_opa(g_orb.btn[i], LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(g_orb.btn[i], 2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(g_orb.btn[i], ui_current_accent(), LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(g_orb.btn[i], LV_OPA_COVER, LV_STATE_PRESSED);
    }
    for(int i=0;i<T_N;i++)lv_obj_add_event_cb(g_orb.btn[i],base_hold,LV_EVENT_LONG_PRESSED,(void*)(intptr_t)i);
    if(!th_disco_family())base_build();
    if(th_disco_family())orbit_set_glyph(&g_orb,T_MODE,mode_glyph());
    paint_radios();

    /* hub: progress ring around the cover; the cover goes Home, the centre button plays/pauses */
    g_prog = lv_arc_create(root);
    lv_obj_remove_style(g_prog, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_prog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_prog, ORBIT_RING, ORBIT_RING);
    lv_obj_center(g_prog);
    lv_arc_set_rotation(g_prog, 270);
    lv_arc_set_bg_angles(g_prog, 0, 360);
    lv_arc_set_range(g_prog, 0, 1000);
    lv_obj_set_style_arc_width(g_prog, TH_ARC_IND, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(SURFACE) : TC(CONTROL_TRACK_STRONG), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_prog, TH_ARC_IND, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_prog, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(ACCENT_PRIMARY) : ui_media_accent(), LV_PART_INDICATOR);
    g_cover_clip = lv_button_create(root);
    lv_obj_remove_style_all(g_cover_clip);
    lv_obj_set_size(g_cover_clip, ORBIT_HUB, ORBIT_HUB);
    lv_obj_center(g_cover_clip);
    lv_obj_set_style_radius(g_cover_clip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(g_cover_clip, true, 0);
    lv_obj_set_style_bg_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_set_style_bg_opa(g_cover_clip, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(g_cover_clip, home_cb, LV_EVENT_CLICKED, NULL);
    g_cover_note = lv_label_create(g_cover_clip);                 /* shown when there's no cover */
    lv_label_set_text(g_cover_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(g_cover_note, TF(UI_20), 0);
    lv_obj_set_style_text_color(g_cover_note, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_cover_note, LV_ALIGN_CENTER, 0, -22);
    g_cover_img = lv_image_create(g_cover_clip);
    lv_obj_clear_flag(g_cover_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
    g_pp = lv_button_create(root);
    lv_obj_remove_style_all(g_pp);
    lv_obj_set_size(g_pp, 40, 40);
    lv_obj_center(g_pp);
    lv_obj_set_style_radius(g_pp, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_pp, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_pp, 170, 0);
    lv_obj_set_style_bg_opa(g_pp, 230, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(g_pp, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_pp, 1, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(g_pp, ui_current_accent(), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(g_pp, 4);
    lv_obj_add_event_cb(g_pp, pp_cb, LV_EVENT_CLICKED, NULL);
    g_pp_glyph = lv_label_create(g_pp);
    lv_label_set_text(g_pp_glyph, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(g_pp_glyph, TF(UI_16), 0);
    lv_obj_set_style_text_color(g_pp_glyph, TC(TEXT_PRIMARY), 0);
    lv_obj_center(g_pp_glyph);

    if(!g_prog_timer) g_prog_timer = lv_timer_create(prog_tick, 1000, NULL);
    if(th_disco_family()) disco_layout(root);
    if(th_braun()){ br_face(root); if(g_dim) lv_obj_add_flag(g_dim, LV_OBJ_FLAG_HIDDEN);   /* Braun: the grille face, no dimming veil */
                    lv_obj_set_style_text_color(g_batt_icon, TC(TEXT_SECONDARY), 0);
                    for(uint32_t k = 0; k < lv_obj_get_child_count(root); k++){ lv_obj_t *o = lv_obj_get_child(root, k);   /* the sun marker */
                        if(lv_obj_check_type(o, &lv_label_class) && lv_obj_get_style_text_font(o, 0) == TF(ICON_28)) lv_obj_set_style_text_color(o, TC(TEXT_SECONDARY), 0); } }
}

/* the cover (RAM decode, scaled into the hub) and the blurred backdrop; called on track / art changes */
void quicksettings_set_art(const void *cover_dsc, const void *backdrop_src)
{
    if(th_simple()){simple_qs_art(cover_dsc);return;}
    if(!g_cover_img) return;
    lv_image_set_src(g_cover_img, NULL);
    if(cover_dsc){
        const lv_image_dsc_t *d = cover_dsc;
        lv_image_set_src(g_cover_img, cover_dsc);
        if(d->header.w > 0) lv_image_set_scale(g_cover_img, (uint32_t)(g_hub_d * 256 / d->header.w) + 2);
        lv_obj_center(g_cover_img);
        lv_obj_remove_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN);
    }
    lv_image_set_src(g_bg, NULL);
    if(backdrop_src && !th_braun()){ lv_image_set_src(g_bg, backdrop_src); lv_obj_remove_flag(g_bg, LV_OBJ_FLAG_HIDDEN); }   /* Braun: the grille, no album wash */
    else lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(ACCENT_PRIMARY) : ui_media_accent(), LV_PART_INDICATOR);
}
void quicksettings_set_battery(int pct, int charging)
{
    if(th_simple()){simple_qs_battery(pct,charging);return;}
    if(th_null()){null_qs_battery(pct,charging);return;}
    if(!g_batt) return;
    if(pct < 0) return;
    if(pct > 100) pct = 100;
    battery_arc_set(&g_battery,pct,charging);
    if(g_batt_icon){
        const char *g = charging ? LV_SYMBOL_CHARGE : pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                      : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        if(strcmp(lv_label_get_text(g_batt_icon), g)) lv_label_set_text(g_batt_icon, g);
        lv_obj_set_style_text_color(g_batt_icon, charging?TC(STATUS_SUCCESS):pct<=15?TC(STATUS_DANGER):TC(TEXT_SECONDARY), 0);
    }
    quicksettings_refresh(ui_is_playing());
}
void quicksettings_set_now_playing(const char *title, const char *artist, int playing)
{
    if(th_simple()){simple_qs_track(title,artist,playing);return;}
    (void)title; (void)artist;
    if(g_pp_glyph) lv_label_set_text(g_pp_glyph, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if(g_prog) lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(ACCENT_PRIMARY) : ui_media_accent(), LV_PART_INDICATOR);
}
void quicksettings_set_volume(int vol){ (void)vol; }       /* the volume arc gave way to the battery */
void quicksettings_refresh(int playing)
{
    if(th_halo()){halo_qs_refresh(playing);return;}
    if(th_simple()){simple_qs_refresh(playing);return;}
    if(th_null()){null_qs_refresh();return;}
    if(!g_prog)return;
    if(g_pp_glyph) lv_label_set_text(g_pp_glyph, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if(g_bright && !lv_obj_has_state(g_bright, LV_STATE_PRESSED))   /* don't fight an active drag */
        lv_arc_set_value(g_bright, ui_get_brightness());
    if(th_disco_family())orbit_set_glyph(&g_orb,T_MODE,mode_glyph());
    else for(int i=0;i<g_nitems;i++)if(g_items[i]==T_MODE||g_items[i]==Q_PLAYBACK)orbit_set_glyph(&g_orb,i,quicksettings_item_glyph(g_items[i]));
    paint_radios();
}

/* ---- compatibility with 1.1's screen manager ----------------------------------------------------
 * Upstream's Quick Settings is user-configurable: a tile palette plus a config screen (SCR_QSCONFIG),
 * and the manager rebuilds the panel when that changes. Disco rebuilds only its configurable controls; other themes retain the fixed panel. */
void quicksettings_build(void){
    if(th_halo()){halo_qs_build();return;}
    if(th_simple()){simple_qs_build();return;}
    if(th_null()){null_qs_build();return;}
    if(!g_root)return;
    if(!th_disco_family()){base_build();return;}
    int style=simple_cfg_get_int("disco_qs_style",1)==1;
    if(style!=g_style_raw || strcmp(g_codes,simple_cfg_get_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items","wbmlefs")))controls_build(g_root);
    dq_paint();
}

static lv_obj_t *g_qscfg_root;
static void base_edit(lv_event_t*e){(void)e;disco_qs_menu_open();}
void qsconfig_create(lv_obj_t *root){
    if(th_halo()){halo_qsconfig_create(root);return;}
    g_qscfg_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header(root, "Quick Settings");
    if(!th_disco_family()&&!th_null()){
      lv_obj_t*b=lv_button_create(root);lv_obj_set_size(b,200,52);lv_obj_center(b);lv_obj_add_event_cb(b,base_edit,LV_EVENT_CLICKED,NULL);lv_obj_t*l=lv_label_create(b);lv_label_set_text(l,"Edit quick items");lv_obj_center(l);return;
    }
    lv_obj_t *m = lv_label_create(root);
    lv_label_set_text(m, th_disco_family()?"Choose Style and Items in\nDisplay > Disco Options >\nQuick Settings.":"This theme uses a fixed panel:\n"
                         "brightness and battery arcs,\nthe cover and six toggles.");
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(m, 50, 120); lv_obj_set_size(m, 260, 120);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m, TH_F_DETAIL, 0);
    lv_obj_set_style_text_color(m, TC(TEXT_SECONDARY), 0);
}
void qsconfig_refresh(void){ if(th_halo()) halo_qsconfig_refresh(); }

/* Host regression seams; no hardware commands are issued by these helpers. */
lv_obj_t *quicksettings_test_button(int slot){extern lv_obj_t*null_test_qs_field(int);if(th_simple())return simple_test_qs_button(slot);if(th_null())return null_test_qs_field(slot);return slot>=0&&slot<g_nitems?(th_disco()?g_dq_row[slot]:g_orb.btn[slot]):NULL;}
lv_obj_t *quicksettings_test_disc(void){return g_cover_clip;}

void quicksettings_null_action(int id,int hold){if(hold && (id==T_WIFI || id==T_BT))onboarding_discovered(HINT_RADIO_HOLD);if(hold&&id==T_WIFI)wifi_open();else if(hold&&id==T_BT)bt_open();else if(hold&&id==Q_EQ)screen_show(SCR_EQ);else if(hold && id>=0 && id<Q_COUNT)ui_toast(CATALOG[id].name);else if(!hold)pick_cb(id);}
