/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Render actual LVGL screens without starting diskOS's hardware main loop. */
#include "md5.h"
#include "lvgl/src/misc/lv_timer_private.h"
#include "simple_theme.h"
#include "lvgl/src/misc/lv_timer_private.h"
#include "null_theme.h"
#include "screens.h"
#include "onboarding.h"
#include "quicksettings.h"
#include <sys/stat.h>
#include "ui.h"
#include "theme.h"
#include "theme_kit.h"
#include "fork_theme.h"
#include "config.h"
#include "ota.h"
#include "system.h"
#include "sdio.h"
#include "musicdb.h"
#include "battery.h"
#include "sqlite3.h"
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

/* Host-only controlled failure: hold album B's load while the UI returns to A. */
static void volume_concept(int);
static atomic_int race_waiting, race_release,art_decode_calls;
static const char *race_path="/tmp/diskos-art-race-unreadable.mp3";
int __real_art_make_all_ex_gen(const char *,const char *,const char *,const char *,int,unsigned);
int __wrap_art_make_all_ex_gen(const char *path,const char *c,const char *t,const char *b,int cancel,unsigned gen){
    atomic_fetch_add(&art_decode_calls,1);
    if(!strcmp(path,race_path)){
        atomic_store(&race_waiting,1);
        while(!atomic_load(&race_release)) usleep(1000);
        return -1;
    }
    return __real_art_make_all_ex_gen(path,c,t,b,cancel,gen);
}

int __real_ui_is_playing(void);
int __wrap_ui_is_playing(void){const char*p=getenv("NP_PLAYING");return p?atoi(p):__real_ui_is_playing();}

/* Radio fixtures are host-only: tests never toggle the computer's real radios. */
static int qs_wifi=-1,qs_bt=-1,qs_wifi_taps,qs_bt_taps;
int __real_wifi_radio_live(void);int __real_bt_state(void);
int __real_wifi_toggle(void);int __real_bt_toggle(void);
int __wrap_wifi_radio_live(void){return qs_wifi>=0?qs_wifi:__real_wifi_radio_live();}
int __wrap_bt_state(void){return qs_bt>=0?qs_bt:__real_bt_state();}
int __wrap_wifi_toggle(void){if(qs_wifi<0)return __real_wifi_toggle();qs_wifi_taps++;return qs_wifi=!qs_wifi;}
int __wrap_bt_toggle(void){if(qs_bt<0)return __real_bt_toggle();qs_bt_taps++;qs_bt=qs_bt==BT_OFF?BT_ON:BT_OFF;return qs_bt==BT_ON;}

/* A fixed clock makes repeated frames comparable; this symbol exists only in the host executable. */
time_t time(time_t *out){ time_t t=1791022170; if(out) *out=t; return t; }

static unsigned char pixels[360 * 360 * 3];
static void flush(lv_display_t *d, const lv_area_t *a, unsigned char *p){
    int width = a->x2 - a->x1 + 1;
    for(int y=a->y1; y<=a->y2; y++)
        memcpy(pixels + ((size_t)y*360 + a->x1)*3, p + (size_t)(y-a->y1)*width*3, (size_t)width*3);
    lv_display_flush_ready(d);
}
static void assert_round_target(lv_obj_t *o){
    lv_area_t a; lv_obj_get_coords(o, &a);
    int x[2]={a.x1,a.x2}, y[2]={a.y1,a.y2};
    for(int i=0;i<2;i++) for(int j=0;j<2;j++){
        int dx=x[i]-180, dy=y[j]-180;
        assert(dx*dx + dy*dy < 180*180);
    }
    /* Clear the complete orbit of A's 38px cover on the 108px progress ring. */
    double dx=(a.x1+a.x2)/2.0-180, dy=(a.y1+a.y2)/2.0-180;
    assert(hypot(dx,dy) - lv_obj_get_width(o)/2.0 > 108+19);
}
int ui_np_cover_shown(void);
static int media_local(void){ return 1; }
static int find_sharp_image(lv_obj_t *root){
    if(lv_obj_check_type(root,&lv_image_class) && !lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN)){
        const void *src=lv_image_get_src(root);
        if(src && lv_image_src_get_type(src)==LV_IMAGE_SRC_VARIABLE){
            const lv_image_dsc_t *d=src;
            if(d->header.w==364 && d->header.h==364) return 1;
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(root);i++)
        if(find_sharp_image(lv_obj_get_child(root,i))) return 1;
    return 0;
}
/* Fixture seams exist only in the host executable; device firmware gates remain intact. */
static int fixture, send_result, send_count;
static int null_transport_calls;static char null_last_command[24];
int __real_ui_transport_command(const char*);
int __wrap_ui_transport_command(const char*cmd){if(getenv("REVIEW_NULL")||getenv("REVIEW_NULL_GESTURES")||getenv("REVIEW_SQUASH_HOLD")||getenv("REVIEW_SIMPLE_PERF")||getenv("REVIEW_SIMPLE")){null_transport_calls++;snprintf(null_last_command,sizeof null_last_command,"%s",cmd);return 0;}return __real_ui_transport_command(cmd);}
int __real_ipc_send_cmd(const char *frame);
int __wrap_ipc_send_cmd(const char *frame){send_count++;return fixture?send_result:__real_ipc_send_cmd(frame);}
int __real_fw_custom_peq_writable(void);
int __wrap_fw_custom_peq_writable(void){return fixture?1:__real_fw_custom_peq_writable();}
int __real_fw_custom_peq_curve_writable(const int *hz,int count);
int __wrap_fw_custom_peq_curve_writable(const int *hz,int count){return fixture?1:__real_fw_custom_peq_curve_writable(hz,count);}
int __real_mdb_get_peq_ex(int p,double *m,int *g,int *hz,int *editable,double *q);
int __wrap_mdb_get_peq_ex(int p,double *m,int *g,int *hz,int *editable,double *q){
 if(!fixture)return __real_mdb_get_peq_ex(p,m,g,hz,editable,q);
 static const int freq[]={32,64,125,250,500,1000,2000,4000,8000,16000};
 *m=0;*editable=1;for(int i=0;i<10;i++){g[i]=(i%3-1)*20;hz[i]=freq[i];if(q)q[i]=0.7;}return 1;
}
int __real_mdb_get_peq_readonly(int p,double *m,int *g,int *hz,int *ed,double *q);
int __wrap_mdb_get_peq_readonly(int p,double *m,int *g,int *hz,int *ed,double *q){
 return fixture?__wrap_mdb_get_peq_ex(p,m,g,hz,ed,q):__real_mdb_get_peq_readonly(p,m,g,hz,ed,q);
}
int __real_mdb_song_accent(const char *p);
int __wrap_mdb_song_accent(const char *p){return fixture?0xCE713E:__real_mdb_song_accent(p);}
static void assert_no_tokens(lv_obj_t *o){
 if(lv_obj_check_type(o,&lv_label_class))assert(lv_label_get_text(o)[0]!='@');
 for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)assert_no_tokens(lv_obj_get_child(o,i));
}
static int count_text(lv_obj_t *o,const char *text){
 int n=lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text);
 for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)n+=count_text(lv_obj_get_child(o,i),text);return n;
}
static lv_obj_t *find_text(lv_obj_t *o,const char *text){
    if(lv_obj_check_type(o,&lv_label_class) && !strcmp(lv_label_get_text(o),text)) return o;
    for(uint32_t i=0;i<lv_obj_get_child_count(o);i++){
        lv_obj_t *found=find_text(lv_obj_get_child(o,i),text); if(found) return found;
    }
    return NULL;
}
static lv_obj_t *find_visible_text(lv_obj_t *o,const char *text){
    if(!lv_obj_is_visible(o)) return NULL;
    if(lv_obj_check_type(o,&lv_label_class) && !strcmp(lv_label_get_text(o),text)) return o;
    for(uint32_t i=0;i<lv_obj_get_child_count(o);i++){
        lv_obj_t *found=find_visible_text(lv_obj_get_child(o,i),text); if(found) return found;
    }
    return NULL;
}
static void assert_visible_icon_glyphs(lv_obj_t *root){
    if(lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN))return;
    if(lv_obj_check_type(root,&lv_label_class)){
        const unsigned char *t=(const unsigned char *)lv_label_get_text(root);
        if(t[0]==0xef && t[1] && t[2]){
            unsigned cp=((t[0]&15)<<12)|((t[1]&63)<<6)|(t[2]&63);
            lv_font_glyph_dsc_t d={0};assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(root,0),&d,cp,0));
            assert(!d.is_placeholder);
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(root);i++)assert_visible_icon_glyphs(lv_obj_get_child(root,i));
}

/* Real pointer events exercise LVGL's hold/release handling, not direct callbacks. */
static lv_indev_data_t nav_pointer;
static void nav_read(lv_indev_t *in,lv_indev_data_t *data){(void)in;*data=nav_pointer;}
static void nav_wait(int ms){for(int i=0;i<ms;i+=10){lv_tick_inc(10);simple_refresh_budget(lv_anim_count_running()>0||nav_pointer.state==LV_INDEV_STATE_PRESSED);lv_timer_handler();}}
static void nav_touch(lv_point_t point,int ms){
    nav_pointer.point=point;nav_pointer.state=LV_INDEV_STATE_PRESSED;nav_wait(ms);
    nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(40);
}
static void review_null(void){
 assert(th_null()&&theme_variant()==0);
 lv_indev_t*in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,nav_read);lv_indev_set_long_press_time(in,400);lv_timer_set_period(lv_indev_get_read_timer(in),10);
 screen_home();null_nav_set_open(1);nav_wait(250);
 int before=screen_current();int commands=null_transport_calls;
 nav_pointer=(lv_indev_data_t){.point={260,165},.state=LV_INDEV_STATE_PRESSED};nav_wait(40);
 for(int x=260;x>=85;x-=7){nav_pointer.point.x=x;nav_wait(10);}nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(500);
 assert(screen_current()==before&&null_transport_calls==commands);
 null_menu_refresh();nav_wait(30);nav_touch((lv_point_t){240,165},90);nav_wait(240);assert(screen_current()==SCR_LIBRARY);
 screen_home();null_nav_set_open(0);nav_wait(250);
 lv_obj_t*t=find_text(screen_get_root(SCR_HOME),"NORTHERN LIGHTS");assert(t);lv_area_t start,after;lv_obj_get_coords(t,&start);
 screen_show(SCR_QUICK);nav_wait(250);lv_obj_get_coords(t,&after);assert(!memcmp(&start,&after,sizeof start));assert(screen_current()==SCR_QUICK);
 int wifi=qs_wifi_taps;nav_touch((lv_point_t){120,42},80);assert(qs_wifi_taps==wifi+1);
 commands=null_transport_calls;nav_touch((lv_point_t){96,180},80);assert(null_transport_calls==commands+1&&!strcmp(null_last_command,"0201000C0002"));
 commands=null_transport_calls;nav_touch((lv_point_t){264,180},80);assert(null_transport_calls==commands+1);
 nav_touch((lv_point_t){180,336},90);nav_wait(250);assert(screen_current()==SCR_HOME);
 // Navigation and seek gestures must not send playback commands when they merely move.
 commands=null_transport_calls;nav_pointer=(lv_indev_data_t){.point={120,308},.state=LV_INDEV_STATE_PRESSED};nav_wait(30);nav_pointer.point.x=190;nav_wait(30);nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(50);assert(null_transport_calls==commands);
 for(int bg=0;bg<3;bg++){cfg_set_int_deferred("null_music_background",bg);null_options_changed();nav_wait(30);}
 cfg_set_int_deferred("null_controls",2);null_options_changed();nav_wait(30);commands=null_transport_calls;nav_touch((lv_point_t){180,155},60);nav_wait(90);nav_touch((lv_point_t){180,155},60);assert(null_transport_calls==commands+1);
 cfg_set_int_deferred("null_controls",1);null_options_changed();nav_wait(5300);commands=null_transport_calls;nav_touch((lv_point_t){120,308},80);assert(null_transport_calls==commands); // waking touch consumes the transport action
 for(int i=0;i<8;i++){screen_show(SCR_QUICK);nav_wait(210);screen_back();nav_wait(210);assert(screen_current()==SCR_HOME);}
 lv_indev_delete(in);puts("PASS: Null real-pointer drag suppression, menu destination, stationary NP under QS, radios, prev/next, close arrow, button swipe suppression, backgrounds, gesture double-tap, autofade wake and repeated QS lifecycle");
}
static void review_nav(void){
    void disco_test_nav_points(lv_point_t *,lv_point_t *);int disco_test_nav_open(void);
    lv_point_t icon,tab;disco_test_nav_points(&icon,&tab);
    lv_indev_t *in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in,nav_read);lv_indev_set_long_press_time(in,400);
    int destinations[]={SCR_HOME,SCR_SEARCH,SCR_QUEUE,SCR_UPNEXT,SCR_APPS,SCR_QUICK,SCR_LIBRARY,SCR_LIBRARY,SCR_LIBRARY,SCR_LIBRARY,SCR_LIBRARY};   /* Volume, Screen off, Play/Pause, Next track and Nothing stay put */
    _Static_assert(sizeof destinations/sizeof destinations[0]==DISCO_NAV_COUNT,"one destination per action");
    for(int action=0;action<DISCO_NAV_COUNT;action++){
        cfg_set_int_deferred("disco_nav_double",action);cfg_set_int_deferred("disco_nav_hold",action);
        screen_show(SCR_LIBRARY);disco_nav_set_open(0);nav_wait(300);
        /* A hold on the CLOSED shard only opens it on release. */
        nav_touch(tab,650);nav_wait(300);assert(screen_current()==SCR_LIBRARY);
        assert(disco_test_nav_open());
        nav_touch(icon,650);nav_wait(350);assert(screen_current()==destinations[action]);
        assert(disco_test_nav_open()==(action==DISCO_NAV_NONE));
        if(action==DISCO_NAV_VOLUME)nav_wait(2100);   /* the volume arc it opened hides by itself (a tap would only close it) */
        screen_show(SCR_LIBRARY);disco_nav_set_open(1);nav_wait(300);
        nav_touch(icon,70);nav_wait(50);assert(screen_current()==SCR_LIBRARY);
        nav_touch(icon,70);nav_wait(350);
        assert(screen_current()==destinations[action]);
        assert(disco_test_nav_open()==(action==DISCO_NAV_NONE));
        if(action==DISCO_NAV_VOLUME)nav_wait(2100);
    }
    cfg_set_int_deferred("disco_nav_double",DISCO_NAV_SEARCH);
    screen_show(SCR_LIBRARY);disco_nav_set_open(1);nav_wait(300);
    nav_touch(icon,70);disco_nav_set_open(0);nav_wait(500);assert(screen_current()==SCR_LIBRARY);
    disco_nav_set_open(1);nav_wait(300);nav_touch(icon,70);
    screen_show(SCR_SETTINGS);nav_wait(500);assert(screen_current()==SCR_SETTINGS);
    screen_show(SCR_LIBRARY);disco_nav_set_open(0);nav_wait(300);
    nav_touch(tab,60);nav_wait(50);nav_touch(tab,60);nav_wait(500);
    assert(screen_current()==SCR_LIBRARY); /* rapid taps on closed shard do not run Search */
    disco_nav_set_open(1);nav_wait(300);
    lv_point_t top=icon;top.y=115;nav_touch(top,70);
    assert(screen_current()==SCR_HOME); /* arrow tap has no double-tap delay */
    screen_show(SCR_LIBRARY);disco_nav_set_open(1);nav_wait(300);
    nav_pointer.point=icon;nav_pointer.state=LV_INDEV_STATE_PRESSED;nav_wait(70);
    nav_pointer.point.x=180;nav_pointer.point.y=40;nav_wait(50);
    nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(500);
    assert(screen_current()==SCR_LIBRARY); /* leaving the menu cancels the press */
    cfg_set_int_deferred("disco_nav_hold",DISCO_NAV_MUSIC);
    nav_pointer.point=icon;nav_pointer.state=LV_INDEV_STATE_PRESSED;nav_wait(70);
    nav_pointer.point.x=180;nav_pointer.point.y=40;nav_wait(50);
    nav_pointer.point=icon;nav_wait(600);
    nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(500);
    assert(screen_current()==SCR_LIBRARY); /* returning inside cannot revive a cancelled hold */
    /* Apply is the only persistence point; Back discards an unconfirmed choice. */
    for(int attempt=0;attempt<2;attempt++){
        setlist_open("Display");nav_wait(100);   /* 1.4.1: Display > Disco Options > Layout */
        lv_obj_t *opts=find_text(screen_get_root(SCR_SETLIST),"Disco Options");assert(opts);
        lv_obj_send_event(lv_obj_get_parent(opts),LV_EVENT_CLICKED,NULL);nav_wait(100);
        lv_obj_t *controls=find_text(screen_get_root(SCR_SETLIST),"Layout");assert(controls);
        lv_obj_send_event(lv_obj_get_parent(controls),LV_EVENT_CLICKED,NULL);nav_wait(100);
        lv_obj_t *label=find_visible_text(screen_get_root(SCR_SETLIST),"Double tap");assert(label);
        lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);nav_wait(100);
        lv_obj_t *root=screen_get_root(SCR_SETTING_DETAIL);
        label=find_text(root,LV_SYMBOL_RIGHT);assert(label);
        lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);nav_wait(100);
        assert(cfg_get_int("disco_nav_double",-1)==DISCO_NAV_SEARCH);
        if(attempt){label=find_text(root,"Apply");assert(label);lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);nav_wait(100);assert(cfg_get_int("disco_nav_double",-1)==DISCO_NAV_QUEUE);}
        else screen_back();
    }
    lv_indev_delete(in);
    puts("PASS: open-only nav gestures, every action, hold-release suppression, cancellation, Apply/Back");
}


static void review_hints(void){
    void disco_test_nav_points(lv_point_t *,lv_point_t *);
    lv_point_t icon,tab;disco_test_nav_points(&icon,&tab);
    lv_indev_t *in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,nav_read);
    int side=cfg_get_int("disco_side",0);
    assert(onboarding_reset()==0);onboarding_start();nav_wait(900);
    const char *menu=side?LV_SYMBOL_LEFT " Tap for menu":"Tap for menu " LV_SYMBOL_RIGHT;
    assert(find_visible_text(lv_layer_top(),menu));
    nav_wait(6700);assert(!find_visible_text(lv_layer_top(),menu));
    assert(cfg_get_int("hint_starts",0)==0); /* quick restarts do not consume a start */
    onboarding_mode_changed();nav_wait(900);
    lv_point_t hint_point={side?133:227,180};nav_touch(hint_point,70);nav_wait(900);
    assert(cfg_get_int("hint_menu",0)==1);
    assert(find_visible_text(lv_layer_top(),onboarding_gesture_text(HINT_NAV)));
    lv_point_t top=icon;top.y=115;nav_touch(top,70);assert(cfg_get_int("hint_nav",0)==1);
    nav_touch(top,70);assert(cfg_get_int("hint_nav",0)==2);
    disco_nav_set_open(0);nav_wait(300);nav_touch(tab,70);assert(cfg_get_int("hint_menu",0)==2);
    disco_nav_set_open(0);screen_show(SCR_LIBRARY);onboarding_library(1);nav_wait(900);
    assert(find_visible_text(lv_layer_top(),"Hold a song or album\nfor actions"));
    songmenu_open_song("/fixture/hint-song.flac");assert(cfg_get_int("hint_library",0)==1);
    assert(!find_visible_text(lv_layer_top(),"Hold a song or album\nfor actions"));
    lv_point_t hub={180,180};nav_touch(hub,70);
    screen_show(SCR_HOME);nav_wait(900);
    assert(find_visible_text(lv_layer_top(),"Tap title for more\nHold it for full screen"));   /* Title Hold: Immersive */
    lv_point_t title={180,155};nav_touch(title,70);assert(cfg_get_int("hint_music",0)==1);
    dhome_overlay_close();nav_wait(100);
    /* All four contexts learned: no automatic hints. */
    onboarding_screen(SCR_HOME,0);nav_wait(900);assert(!find_visible_text(lv_layer_top(),menu));
    assert(!find_visible_text(lv_layer_top(),"Tap title for more\nHold it for full screen"));
    /* the power-button tip: after a touch woke a timed-out screen, once - even for settled users - never when Off */
    { const char *lock="Tip: the power button locks\nthe screen in a pocket";
      cfg_set_int("hint_starts",5); onboarding_woke_by_touch(); nav_wait(900); assert(find_visible_text(lv_layer_top(),lock));
      nav_wait(5200); assert(!find_visible_text(lv_layer_top(),lock)); assert(cfg_get_int("hint_lock",0)==1);
      onboarding_mode_changed(); onboarding_woke_by_touch(); nav_wait(900); assert(!find_visible_text(lv_layer_top(),lock));
      cfg_set_int("hint_lock",0); cfg_set_int("disco_hints",2); onboarding_woke_by_touch(); nav_wait(900);
      assert(!find_visible_text(lv_layer_top(),lock)); cfg_set_int("disco_hints",0); cfg_set_int("hint_starts",0); }
    cfg_set_int("disco_hints",1);onboarding_mode_changed();nav_wait(900);
    assert(find_visible_text(lv_layer_top(),menu)); /* On ignores discovery */
    cfg_set_int("disco_hints",2);onboarding_mode_changed();nav_wait(900);assert(!find_visible_text(lv_layer_top(),menu));
    assert(onboarding_reset()==0);assert(cfg_get_int("disco_side",-1)==side);
    cfg_set_int("hint_starts",4);nav_wait(30000);assert(cfg_get_int("hint_starts",0)==5);
    onboarding_screen(SCR_HOME,0);nav_wait(900);assert(!find_visible_text(lv_layer_top(),menu));
    cfg_set_int("disco_hints",1);onboarding_mode_changed();nav_wait(900);assert(find_visible_text(lv_layer_top(),menu));
    /* A failed reset reports failure and restores prior in-memory counters/mode. */
    assert(mkdir(CFG_PATH ".tmp",0700)==0);assert(onboarding_reset()<0);
    assert(cfg_get_int("hint_starts",0)==5);assert(cfg_get_int("disco_hints",0)==1);
    assert(rmdir(CFG_PATH ".tmp")==0);assert(onboarding_reset()==0);
    assert(cfg_get_int("hint_starts",-1)==0);assert(cfg_get_int("disco_hints",-1)==0);
    FILE *saved=fopen(CFG_PATH,"r");assert(saved);char line[128];int persisted=0;
    while(fgets(line,sizeof line,saved))if(!strcmp(line,"hint_starts=0\n") || !strcmp(line,"hint_music=0\n") || !strcmp(line,"disco_hints=0\n"))persisted++;
    fclose(saved);assert(persisted==3);
    setlist_open("System");nav_wait(100);
    lv_obj_t *label=find_text(screen_get_root(SCR_SETLIST),"Help");assert(label);
    lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);nav_wait(100);
    label=find_text(screen_get_root(SCR_SETLIST),"First-use hints");assert(label);
    lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);nav_wait(100);
    for(int value=1;value<=3;value++){
        label=find_text(screen_get_root(SCR_SETTING_DETAIL),LV_SYMBOL_RIGHT);assert(label);
        lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);nav_wait(100);
        assert(cfg_get_int("disco_hints",-1)==value%3);
    }
    screen_back();nav_wait(100);
    label=find_text(screen_get_root(SCR_SETLIST),"Show hints again");assert(label);
    lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);nav_wait(100);
    assert(cfg_get_int("hint_menu",-1)==0);
    onboarding_dismiss();lv_indev_delete(in);
    puts("PASS: hint lifetime/discovery, click-through, Auto/On/Off, healthy-start limit, reset/persistence/save failure");
}


static void qs_editor_click(const char *name,const char *glyph){
    lv_obj_t *label=find_text(lv_layer_top(),name);assert(label);
    lv_obj_t *button=find_text(lv_obj_get_parent(label),glyph);assert(button);
    lv_obj_send_event(lv_obj_get_parent(button),LV_EVENT_CLICKED,NULL);nav_wait(60);
}
static void review_qs(void){
    lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,nav_read);
    lv_obj_t *quicksettings_test_button(int);lv_obj_t *quicksettings_test_disc(void);
    void disco_test_editor_close(void);
    int items[QS_MAX_ITEMS];
    cfg_set_str("disco_qs_items","wbmlefrs");assert(quicksettings_items_load(items)==7);
    qs_wifi=0;qs_bt=BT_OFF;
    cfg_set_str("disco_qs_items","wbmlefrs");
    for(int style=0;style<2;style++){
        qs_wifi=0;qs_bt=BT_OFF;
        cfg_set_int("disco_qs_style",style);screen_show(SCR_QUICK);quicksettings_build();nav_wait(100);
        lv_obj_t *root=screen_get_root(SCR_QUICK);lv_area_t disc;lv_obj_get_coords(quicksettings_test_disc(),&disc);
        assert(lv_obj_get_width(quicksettings_test_disc())==120);
        assert((disc.x1+disc.x2+1)/2==(disco_mirror()?88:272));
        for(int i=0;i<8;i++){
            lv_obj_t *button=quicksettings_test_button(i);assert(button);lv_area_t a;lv_obj_get_coords(button,&a);
            int x[2]={a.x1,a.x2},y[2]={a.y1,a.y2};
            for(int k=0;k<2;k++)for(int j=0;j<2;j++){int dx=x[k]-180,dy=y[j]-180;assert(dx*dx+dy*dy<180*180);}
            for(int j=0;j<i;j++){lv_area_t b;lv_obj_get_coords(quicksettings_test_button(j),&b);if(style){int dx=a.x1-b.x1,dy=a.y1-b.y1;assert(dx*dx+dy*dy>=66*66);}else assert(a.x2<b.x1 || b.x2<a.x1 || a.y2<b.y1 || b.y2<a.y1);}
        }
        if(style){
            for(int i=2;i<8;i++){
                lv_area_t a;lv_obj_get_coords(quicksettings_test_button(i),&a);
                int cx=(a.x1+a.x2+1)/2,cy=(a.y1+a.y2+1)/2;
                int dx=cx-180,dy=cy-180;assert(abs(dx*dx+dy*dy-122*122)<180);
            }
        }else{
            /* List: rows sit like the Library's (left edge on the curve, at most 268 px wide) and stay clear of the disc */
            assert(lv_obj_get_style_text_align(lv_obj_get_child(quicksettings_test_button(2),1),0)==LV_TEXT_ALIGN_LEFT);
            for(int i=2;i<8;i++){
                lv_area_t area;lv_obj_get_coords(quicksettings_test_button(i),&area);
                if(area.y2>=disc.y1+8 && area.y1<=disc.y2-8) assert(disco_mirror()?area.x1>disc.x2:area.x2<disc.x1);
                assert(disco_mirror()?area.x2<=314:area.x1>=46);
            }
        }
        assert(find_text(quicksettings_test_button(0),"Off"));assert(find_text(quicksettings_test_button(1),"Off"));
        lv_obj_t *wifi=quicksettings_test_button(0);lv_area_t hit;lv_obj_get_coords(wifi,&hit);
        nav_pointer.point.x=(hit.x1+hit.x2)/2;nav_pointer.point.y=(hit.y1+hit.y2)/2;
        nav_pointer.state=LV_INDEV_STATE_PRESSED;nav_wait(40);assert(lv_obj_has_state(wifi,LV_STATE_PRESSED));
        nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(40);assert(qs_wifi_taps==style+1);
        assert(find_text(quicksettings_test_button(0),qs_wifi?"On":"Off"));
        /* A moving press must not toggle a radio when the finger is released. */
        nav_pointer.point.x=(hit.x1+hit.x2)/2;nav_pointer.point.y=(hit.y1+hit.y2)/2;
        nav_pointer.state=LV_INDEV_STATE_PRESSED;nav_wait(40);
        nav_pointer.point.x+=disco_mirror()?-24:24;nav_wait(40);
        nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(40);
        assert(qs_wifi_taps==style+1);

        qs_bt=BT_TURNING_ON;quicksettings_refresh(1);assert(find_text(quicksettings_test_button(1),"..."));
        qs_bt=BT_ON;quicksettings_refresh(1);assert(find_text(quicksettings_test_button(1),"On"));
        assert_visible_icon_glyphs(root);
    }
    /* Reorder Wi-Fi to a different slot: state and action must follow the item. */
    int reordered[]={6,7,8,9,1,0};assert(quicksettings_items_save(reordered,6)==0);quicksettings_build();
    lv_obj_send_event(quicksettings_test_button(5),LV_EVENT_SHORT_CLICKED,NULL);assert(qs_wifi_taps==3);
    assert(find_text(quicksettings_test_button(5),qs_wifi?"On":"Off"));
    lv_obj_send_event(quicksettings_test_button(4),LV_EVENT_SHORT_CLICKED,NULL);assert(qs_bt_taps==1);
    assert_visible_icon_glyphs(screen_get_root(SCR_QUICK));
    /* List rows share height, keep a gap, and captions never wrap/clip.
     * Exercise every catalog entry at the narrow bottom of a full selection. */
    cfg_set_int("disco_qs_style",0);
    for(int last=2;last<quicksettings_item_count();last++){
        int full[8]={0,1},n=2;
        for(int id=2;n<7;id++)if(id!=last)full[n++]=id;
        full[7]=last;assert(quicksettings_items_save(full,8)==0);quicksettings_build();nav_wait(60);lv_obj_update_layout(screen_get_root(SCR_QUICK));
        int height=-1,bottom=79,gap=-1,width=-1;
        for(int i=2;i<8;i++){
            lv_obj_t *b=quicksettings_test_button(i);lv_area_t area;lv_obj_get_coords(b,&area);
            if(height<0)height=lv_obj_get_height(b);assert(lv_obj_get_height(b)==height);
            if(gap<0)gap=area.y1-bottom-1;
            assert(area.y1-bottom-1==gap);assert(gap>=4);bottom=area.y2;
            if(width<0)width=lv_obj_get_width(b);   /* List widths follow the curve; no longer uniform */
            lv_obj_t *label=lv_obj_get_child(b,1);assert(label);
            const lv_font_t *font=lv_obj_get_style_text_font(label,0);lv_point_t size;
            lv_text_get_size(&size,lv_label_get_text(label),font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
            if(size.x>lv_obj_get_width(label))fprintf(stderr,"QS caption %s: %d > %d\n",lv_label_get_text(label),size.x,(int)lv_obj_get_width(label));
            assert(size.x<=lv_obj_get_width(label));assert(size.y<=height-4);
            assert(lv_obj_get_height(label)<=font->line_height);
        }
        lv_area_t wifi,bt;lv_obj_get_coords(quicksettings_test_button(0),&wifi);lv_obj_get_coords(quicksettings_test_button(1),&bt);
        assert(wifi.y1==42 && bt.y1==42);
    }
    int no_radios[8]={2,3,4,5,6,7,10,11};
    assert(quicksettings_items_save(no_radios,8)==0);quicksettings_build();nav_wait(60);
    lv_obj_update_layout(screen_get_root(SCR_QUICK));
    for(int i=0;i<8;i++){
        lv_obj_t *b=quicksettings_test_button(i),*label=lv_obj_get_child(b,1);
        const lv_font_t *font=lv_obj_get_style_text_font(label,0);lv_point_t size;
        lv_text_get_size(&size,lv_label_get_text(label),font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        assert(size.x<=lv_obj_get_width(label));assert(size.y<=lv_obj_get_height(b)-4);
    }
    cfg_set_int("disco_qs_style",1);
    /* Every selectable glyph and sparse/empty layouts. */
    for(int id=0;id<quicksettings_item_count();id++){
        assert(quicksettings_items_save(&id,1)==0);quicksettings_build();nav_wait(20);
        assert_visible_icon_glyphs(screen_get_root(SCR_QUICK));assert(quicksettings_test_button(0));assert(!quicksettings_test_button(1));
    }
    assert(quicksettings_items_save(items,0)==0);quicksettings_build();assert(!quicksettings_test_button(0));
    cfg_set_str("disco_qs_items","????");assert(quicksettings_items_load(items)==8);
    cfg_set_str("disco_qs_items","wwbbm");assert(quicksettings_items_load(items)==3);
    cfg_set_str("disco_qs_items","wbmlefrs");
    /* The shared editor enforces eight, reorders, removes/adds and persists. */
    screen_show(SCR_SETTINGS);disco_qs_menu_open();qs_editor_click("Wi-Fi",LV_SYMBOL_DOWN);
    assert(!strcmp(cfg_get_str("disco_qs_items",""),"bwmlefrs"));
    qs_editor_click("Queue",LV_SYMBOL_PLUS);assert(!strcmp(cfg_get_str("disco_qs_items",""),"bwmlefrs"));
    qs_editor_click("Modes",LV_SYMBOL_CLOSE);qs_editor_click("Queue",LV_SYMBOL_PLUS);
    assert(!strcmp(cfg_get_str("disco_qs_items",""),"bwlefrsq"));
    char original[48];snprintf(original,sizeof original,"%s",cfg_get_str("disco_qs_items",""));
    assert(mkdir(CFG_PATH ".tmp",0700)==0);qs_editor_click("Wi-Fi",LV_SYMBOL_DOWN);
    assert(!strcmp(cfg_get_str("disco_qs_items",""),original));assert(rmdir(CFG_PATH ".tmp")==0);
    disco_test_editor_close();
    /* Existing Disco Menu uses the same editor and retains protected entries. */
    disco_menu_open();lv_obj_t *music=find_text(lv_layer_top(),"Music");assert(music);
    assert(!find_text(lv_obj_get_parent(music),LV_SYMBOL_CLOSE));disco_test_editor_close();
    setlist_open("Display");nav_wait(60);lv_obj_t *l=find_text(screen_get_root(SCR_SETLIST),"Disco Options");assert(l);
    lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);nav_wait(60);
    l=find_text(screen_get_root(SCR_SETLIST),"Quick Settings");assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);nav_wait(60);
    l=find_text(screen_get_root(SCR_SETLIST),"Style");assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);nav_wait(60);
    l=find_text(screen_get_root(SCR_SETTING_DETAIL),LV_SYMBOL_RIGHT);assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);nav_wait(60);
    assert(cfg_get_int("disco_qs_style",-1)==0);screen_back();nav_wait(60);
    assert(find_text(screen_get_root(SCR_SETLIST),"Items"));
    lv_indev_delete(pointer);
    puts("PASS: QS both styles, round bounds/non-overlap, disc preserved, radio state/action mapping, all glyphs, editor/persistence/failure, submenu");
}


/* ---- Listening Stats demo: a seeded PLAY_LOG and placeholder covers (STATS_ART_DIR/a<n>_t.bmp, a<n>_c.bmp) ---- */
int __real_artcache_get_thumb(const char*,const char*); int __real_artcache_get_cover(const char*,const char*);
static int demo_art(const char *track,const char *out,int big){
    const char *dir=getenv("STATS_ART_DIR"); if(!dir||strncmp(track,"/demo/a",7)) return -1;
    char src[256]; snprintf(src,sizeof src,"%s/a%d_%c.bmp",dir,atoi(track+7),big?'c':'t');
    FILE *i=fopen(src,"rb"),*o=i?fopen(out,"wb"):NULL; if(!i||!o){ if(i)fclose(i); return -1; }
    char b[8192]; size_t n; while((n=fread(b,1,sizeof b,i))>0) fwrite(b,1,n,o); fclose(i); fclose(o); return 0;
}
int __wrap_artcache_get_thumb(const char *t,const char *o){ return getenv("STATS_ART_DIR")?demo_art(t,o,0):__real_artcache_get_thumb(t,o); }
int __wrap_artcache_get_cover(const char *t,const char *o){ return getenv("STATS_ART_DIR")?demo_art(t,o,1):__real_artcache_get_cover(t,o); }
static void stats_seed_demo(void){
    static const char *AL[8][3]={{"Endless Summer","The Midnight","Sunset|Vampires|Jason|Gloria|Endless Summer"},
        {"OutRun","Kavinsky","Nightcall|Odd Look|Roadgame|Protovision"},{"Night Drive","Timecop1983","Neon Lights|On the Run|Back to You"},
        {"Atlas","FM-84","Running in the Night|Wild Ones|Never Stop"},{"Monsters","The Midnight","Fire in Bloom|Brooklyn|Deep Blue"},
        {"Turbo Kid","Le Matos","No Tomorrow|Chasing Yesterday"},{"Dreamwave","Gunship, Carpenter Brut","Tech Noir|Dark All Day"},
        {"Kids","The Midnight","Lost Boy|America 2|Kids"}};
    static const int W[8]={30,18,12,9,11,4,5,8};
    static const int HW[24]={2,1,0,0,0,0,1,6,14,8,3,4,6,7,5,4,7,13,15,11,17,20,13,6};
    system("mkdir -p /usr/data/fiio/db && rm -f /usr/data/fiio/db/song.db");
    sqlite3 *d=NULL; assert(sqlite3_open("/usr/data/fiio/db/song.db",&d)==SQLITE_OK);
    sqlite3_exec(d,"CREATE TABLE PLAY_LOG(ID INTEGER PRIMARY KEY, AT INTEGER NOT NULL, MS INTEGER NOT NULL, PATH TEXT, TITLE TEXT, ARTIST TEXT, ALBUM TEXT, AARTIST TEXT);"
                   "CREATE INDEX PLAY_LOG_AT ON PLAY_LOG(AT);BEGIN;",0,0,0);
    if(getenv("STATS_EMPTY")){ sqlite3_close(d); return; }
    sqlite3_stmt *st; sqlite3_prepare_v2(d,"INSERT INTO PLAY_LOG(AT,MS,PATH,TITLE,ARTIST,ALBUM,AARTIST) VALUES(?,?,?,?,?,?,?);",-1,&st,NULL);
    unsigned rnd=12345; long long now=(long long)time(NULL); now-=now%86400;
    for(int day=75;day>=0;day--){
        if(day%9==4) continue;                                         /* a few days off */
        int n=8+(int)((rnd=rnd*1103515245u+12345u)>>16)%16; if(day<14) n+=3;
        for(int k=0;k<n;k++){
            rnd=rnd*1103515245u+12345u; int r=(int)(rnd>>16)%102, a=0; while(a<7 && r>=W[a]){ r-=W[a]; a++; }
            char songs[128]; snprintf(songs,sizeof songs,"%s",AL[a][2]); char *tok[6]; int ns=0;
            for(char *p=strtok(songs,"|");p&&ns<6;p=strtok(NULL,"|")) tok[ns++]=p;
            rnd=rnd*1103515245u+12345u; int s2=(int)(rnd>>16)%(ns*2); s2=s2>=ns?0:s2;        /* the first song of an album twice as often */
            rnd=rnd*1103515245u+12345u; int hr=(int)(rnd>>16)%160, h=0; while(h<23 && hr>=HW[h]){ hr-=HW[h]; h++; }
            char path[64]; snprintf(path,sizeof path,"/demo/a%d/t%d.flac",a,s2);
            sqlite3_bind_int64(st,1,now-day*86400LL+h*3600+(k*7)%60*60); sqlite3_bind_int(st,2,180000+(int)(rnd>>20)%90000);
            sqlite3_bind_text(st,3,path,-1,SQLITE_TRANSIENT); sqlite3_bind_text(st,4,tok[s2],-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(st,5,AL[a][1],-1,SQLITE_STATIC); sqlite3_bind_text(st,6,AL[a][0],-1,SQLITE_STATIC); sqlite3_bind_null(st,7);
            sqlite3_step(st); sqlite3_reset(st);
        }
    }
    sqlite3_finalize(st); sqlite3_exec(d,"COMMIT;",0,0,0); sqlite3_close(d);
}
int main(int argc, char **argv){
    if(argc != 6){ fprintf(stderr,"usage: host-render PRESET VARIANT home|np|actions UP_NEXT OUT.ppm\n"); return 2; }
    setenv("TZ","UTC",1); tzset();
    lv_init();
    lv_display_t *d=lv_display_create(360,360); assert(d);
    static unsigned char buf[360*360*3];
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(d,buf,NULL,sizeof buf,LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d,flush);
    cfg_load();
    if(getenv("QS_WIFI"))qs_wifi=atoi(getenv("QS_WIFI"));
    if(getenv("QS_BT"))qs_bt=atoi(getenv("QS_BT"));
    if(!strcmp(argv[3],"migration") || !strcmp(argv[3],"reset")){
        int prior=atoi(argv[1]), variant=atoi(argv[2]);
        assert(!cfg_set_int_deferred("volume",37));
        if(prior==-2) assert(!cfg_set_int_deferred("ui_theme",variant));
        if(prior>=0){
            assert(!cfg_set_int_deferred("theme_preset",prior));
            if(variant>=0) assert(!cfg_set_int_deferred("theme_variant",variant));
        }
        int want=prior>=0 ? prior : (prior==-2 && variant==0 ? 8 : 9);
        int light=prior>=0 ? (variant>=0 ? variant : 0) :
                  (prior==-2 && (variant==0 || variant==2) ? 0 : 1);
        if(!strcmp(argv[3],"reset")){
            cfg_set_int_deferred("fork_theme_migrated",1);
            cfg_set_int_deferred("theme_auto",1);
            cfg_set_int_deferred("up_next",1);
            cfg_set_int_deferred("np_poster_v1",1);
            cfg_set_int_deferred("saver_ring_v1",1);
            cfg_set_int_deferred("accent_red_v1",1);
            assert(!sys_reset_settings());
            assert(cfg_get_int("theme_auto",0)==0 && cfg_get_int("up_next",0)==0);
            assert(cfg_get_int("ui_theme",-1)==-1);
            assert(cfg_get_int("np_poster_v1",0)==0);
            assert(cfg_get_int("saver_ring_v1",0)==0);
            assert(cfg_get_int("accent_red_v1",0)==0);
            want=9; light=1;
        }
        theme_init();
        assert(theme_preset()==want && theme_variant()==light);
        assert(cfg_get_int("volume",0)==37);
        assert(cfg_get_int("fork_theme_migrated",0)==1);
        assert(!cfg_flush());
        FILE *f=fopen(CFG_PATH,"r"); assert(f);
        char body[8192]; size_t n=fread(body,1,sizeof body-1,f); body[n]=0; fclose(f);
        char key[64];
        /* Reset preserves the completed migration marker and uses fresh defaults. */
        if(strcmp(argv[3],"reset")){
            snprintf(key,sizeof key,"theme_preset=%d",want); assert(strstr(body,key));
            snprintf(key,sizeof key,"theme_variant=%d",light); assert(strstr(body,key));
        }
        assert(strstr(body,"volume=37") && strstr(body,"fork_theme_migrated=1"));
        puts("PASS: persisted theme migration/reset preserves unrelated preferences");
        return 0;
    }
    assert(!cfg_set_int_deferred("fork_theme_migrated",1));
    assert(!cfg_set_int_deferred("theme_preset",atoi(argv[1])));
    assert(!cfg_set_int_deferred("theme_variant",atoi(argv[2])));
    assert(!cfg_set_int_deferred("theme_auto",0));
    assert(!cfg_set_int_deferred("up_next",atoi(argv[4])));
    if(atoi(argv[1])==THEME_PRESET_SIMPLE){cfg_set_int_deferred("simple_clock",1);cfg_set_str_deferred("simple_qs_items","wbmerfhs");}
    if(getenv("SIMPLE_SHAPE"))cfg_set_int_deferred("simple_prog_shape",atoi(getenv("SIMPLE_SHAPE")));
    if(getenv("SIMPLE_ALIGN"))cfg_set_int_deferred("simple_title_al",atoi(getenv("SIMPLE_ALIGN")));
    if(getenv("SIMPLE_BG"))cfg_set_int_deferred("simple_background",atoi(getenv("SIMPLE_BG")));
    if(getenv("NP_FONT"))cfg_set_int_deferred(atoi(argv[1])==THEME_PRESET_SIMPLE?"simple_track_font":"np_font",atoi(getenv("NP_FONT")));
    if(getenv("NULL_PALETTE"))cfg_set_int_deferred("null_palette",atoi(getenv("NULL_PALETTE")));
    if(getenv("NULL_BG"))cfg_set_int_deferred("null_music_background",atoi(getenv("NULL_BG")));
    if(getenv("NULL_SIDE"))cfg_set_int_deferred("null_side",atoi(getenv("NULL_SIDE")));
    if(getenv("NULL_TEXT"))cfg_set_int_deferred("null_text_align",atoi(getenv("NULL_TEXT")));
    if(getenv("NULL_QS_LAYOUT"))cfg_set_int_deferred("null_qs_layout",atoi(getenv("NULL_QS_LAYOUT")));
    if(getenv("NULL_FONTS"))cfg_set_int_deferred("null_music_fonts",atoi(getenv("NULL_FONTS")));
    if(getenv("NULL_CTRL"))cfg_set_int_deferred("null_controls",atoi(getenv("NULL_CTRL")));
    theme_init();
    if(!strcmp(argv[3],"theme-retirement")){
        int requested=atoi(argv[1]),expected=theme_preset_available(requested)?requested:THEME_PRESET_DISCO;
        assert(theme_preset()==expected&&cfg_get_int("theme_preset",-1)==expected);
        assert(THEME_PRESET_RING==8&&THEME_PRESET_BRAUN==9&&THEME_PRESET_STONE==6);
        assert(!theme_def_find("something")&&!theme_def_find("hi-fi")&&!theme_def_find("terminal")&&!theme_def_find("bauhaus")&&!theme_def_find("blueprint")&&!theme_def_find("zine"));
        puts("PASS: retired theme IDs migrate safely to Disco; visible IDs and definitions retained");return 0;
    }
    if(!strcmp(argv[3],"shutdown")){
        screen_set_anim(0); screens_init(); ui_shutdown_screen();
        goto render;
    }
    if(!strncmp(argv[3],"shortcuts-",10)){
        screen_set_anim(0); screens_init();
        setlist_open("Display");
        lv_obj_t *root=screen_get_root(SCR_SETLIST);
        lv_obj_t *label=find_text(root,"Shortcuts"); assert(label);
        assert(!find_text(root,"Quick Settings"));
        lv_obj_t *row=lv_obj_get_parent(label), *list=lv_obj_get_parent(row);
        lv_obj_update_layout(root);
        lv_obj_scroll_to_y(list,lv_obj_get_y(row)-72,LV_ANIM_OFF);
        if(strcmp(argv[3],"shortcuts-display")){
            lv_obj_send_event(row,LV_EVENT_CLICKED,NULL);
            assert(screen_current()==SCR_SCCONFIG);
            root=screen_get_root(SCR_SCCONFIG);
            if(!strcmp(argv[3],"shortcuts-options")){
                lv_obj_t *slot_list=NULL;
                for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){
                    lv_obj_t *child=lv_obj_get_child(root,i);
                    if(lv_obj_get_child_count(child)==5 && lv_obj_check_type(lv_obj_get_child(child,0),&lv_button_class)) slot_list=child;
                }
                assert(slot_list);
                lv_obj_send_event(lv_obj_get_child(slot_list,0),LV_EVENT_CLICKED,NULL);
            } else if(!strcmp(argv[3],"shortcuts-orbit")) screen_show(SCR_APPS);
        }
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"detail-",7)){            /* open Settings > Display > <row> detail, e.g. detail-Theme */
        screen_set_anim(0); screens_init(); setlist_open("Display"); screen_show(SCR_SETLIST);
        lv_obj_t *root=screen_get_root(SCR_SETLIST); lv_obj_t *label=find_text(root,argv[3]+7); assert(label);
        lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"stress")){              /* quick skips + album switches, then check art still loads */
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        cfg_set_int_deferred("online_lyrics",0);cfg_set_int_deferred("online_art",0);
        screen_set_anim(0);screens_init();ui_set_np_style(3);screen_show(SCR_NOWPLAYING);
        const char *dir=getenv("STRESS_DIR"); assert(dir);
        int rounds=atoi(getenv("STRESS_ROUNDS")?getenv("STRESS_ROUNDS"):"40");
        unsigned seed=1; int fails=0;
        for(int r=0;r<rounds;r++){
            int alb=(int)((seed=seed*1103515245u+12345u)>>16)%6, trk=(int)(seed>>8)%3;
            int wait=(int)((seed>>4)%5)*60;      /* 0..240 ms between skips */
            track_state_t st={0};
            snprintf(st.path,sizeof st.path,"%s/a%d/t%d.png",dir,alb,trk);
            snprintf(st.album,sizeof st.album,"Album %d",alb);snprintf(st.title,sizeof st.title,"T%d",trk);snprintf(st.artist,sizeof st.artist,"X");
            st.have_track=1;st.state=2;st.duration_ms=200000;ipc_seed_state(&st);ui_update(&st);
            for(int i=0;i<wait/10;i++){ui_art_poll(NULL);lv_tick_inc(10);lv_timer_handler();usleep(10000);}
            if(r%8==7){                          /* settle: the current album must load */
                int ok=0;for(int i=0;i<1500;i++){ui_art_poll(NULL);lv_tick_inc(10);lv_timer_handler();if(ui_current_cover_dsc()&&ui_np_cover_shown()){ok=1;break;}usleep(10000);}
                { int nfd=0; DIR *dd=opendir("/proc/self/fd"); if(dd){ while(readdir(dd)) nfd++; closedir(dd); }
                  long rss=0; FILE *sf=fopen("/proc/self/statm","r"); if(sf){ long a; if(fscanf(sf,"%ld %ld",&a,&rss)!=2) rss=0; fclose(sf); }
                  printf("settle %d (album %d): %s fds=%d rss_kb=%ld\n",r,alb,ok?"cover":"MISSING",nfd,rss*4); } if(!ok) fails++;
            }
        }
        printf("%s: %d settles missing\n",fails?"FAIL":"PASS",fails); return fails?1:0;
    }
    if(!strcmp(argv[3],"fb-long")){              /* folder browser: hold a row -> its action menu */
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        screen_set_anim(0);screens_init();
        void folderbrowser_open(void); folderbrowser_open();
        for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();usleep(2000);}
        lv_obj_t *root=screen_get_root(SCR_FOLDER); lv_obj_t *lbl=find_text(root,"Album"); assert(lbl);
        lv_obj_t *row=lv_obj_get_parent(lbl);
        lv_obj_send_event(row,LV_EVENT_PRESSED,NULL);
        lv_obj_send_event(row,LV_EVENT_LONG_PRESSED,NULL);
        lv_obj_send_event(row,LV_EVENT_RELEASED,NULL);
        lv_obj_send_event(row,LV_EVENT_CLICKED,NULL);
        for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"group-menu")){           /* Library long-press on an album: its action menu */
        screen_set_anim(0);screens_init();screen_show(SCR_LIBRARY);
        songmenu_open_group("ALBUM","Mother's Milk","Mother's Milk",NULL);
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"lib-",4)){                 /* Library: open a category (lib-Albums, lib-Songs...), scroll a bit */
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        if(getenv("DISCO_SCROLL"))cfg_set_int_deferred("disco_scroll",atoi(getenv("DISCO_SCROLL")));if(getenv("DISCO_SIDE"))cfg_set_int_deferred("disco_side",atoi(getenv("DISCO_SIDE")));
        screen_set_anim(0);screens_init();screen_show(SCR_LIBRARY);
        for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
        lv_obj_t *root=screen_get_root(SCR_LIBRARY);
        char cat[40]; snprintf(cat,sizeof cat,"%s",argv[3]+4); char *sub=strchr(cat,'/'); if(sub) *sub++=0;
        lv_obj_t *l=find_text(root,cat); assert(l); lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
        if(sub){ for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();} l=find_text(root,sub); assert(l); lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_SHORT_CLICKED,NULL); }
        for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();}
        const char *sc=getenv("LIB_SCROLL"); if(sc){ lv_obj_t *lst=NULL;
            for(uint32_t k=0;k<lv_obj_get_child_count(root);k++){ lv_obj_t *c=lv_obj_get_child(root,k); if(lv_obj_get_child_count(c)>5 && lv_obj_get_scroll_bottom(c)>0) lst=c; }
            if(lst){ lv_obj_scroll_by(lst,0,-atoi(sc),LV_ANIM_OFF); lv_obj_send_event(lst,LV_EVENT_SCROLL,NULL); } }
        if(getenv("REVIEW_LIBRARY_POSITION")){
            lv_obj_t *list=library_scroller();assert(list);
            lv_obj_scroll_to_y(list,500,LV_ANIM_OFF);lv_obj_update_layout(list);
            int before=lv_obj_get_scroll_y(list);assert(before>0);
            library_refresh();lv_obj_update_layout(list);
            assert(lv_obj_get_scroll_y(list)==before);
            screen_show(SCR_HOME);screen_show(SCR_LIBRARY);lv_obj_update_layout(list);
            assert(lv_obj_get_scroll_y(list)==before);
            assert(library_back());for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
            lv_obj_t *catlabel=find_text(root,cat);assert(catlabel);
            lv_obj_send_event(lv_obj_get_parent(catlabel),LV_EVENT_CLICKED,NULL);
            lv_obj_update_layout(list);assert(lv_obj_get_scroll_y(list)==before);
            puts("PASS: library position survives refresh, screen return and category return");
        }
        if(getenv("LIB_GHOLD")){                     /* fork: hold an album / artist / genre row -> its action menu */
            for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
            lv_obj_t *t=ui_action_find("library.group_play.long"); assert(t); lv_obj_send_event(t,LV_EVENT_LONG_PRESSED,NULL);
        }
        if(getenv("LIB_HOLD")){                      /* fork: hold a track row -> its action menu, never playback */
            for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
            lv_obj_t *t=ui_action_find("library.song.long"); if(!t) t=ui_action_find("library.scope_song"); assert(t);
            int before=send_count; lv_obj_send_event(t,LV_EVENT_LONG_PRESSED,NULL); assert(send_count==before);
        }
        for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();}
        if(getenv("LIB_AZ")){lv_obj_t*l=find_text(root,"A - Z");assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);kit_pass(root);
            if(getenv("REVIEW_SIMPLE_AZ")){lv_obj_t*m=find_visible_text(root,"M");assert(m);lv_obj_send_event(lv_obj_get_parent(m),LV_EVENT_CLICKED,NULL);assert(!lv_obj_is_visible(m));assert(lv_obj_get_scroll_y(library_scroller())>0);puts("PASS: Squash A-Z opens, jumps to M and closes");}}
        goto render;
    }
    if(!strncmp(argv[3],"search",6)){              /* fork: search-<query>[-go]: type, optionally press Search */
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        screen_set_anim(0);screens_init();screen_show(SCR_SEARCH);
        for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
        lv_obj_t *root=screen_get_root(SCR_SEARCH);
        const char *q=argv[3][6]=='-'?argv[3]+7:""; int go=strstr(q,"-go")!=NULL;
        if(q[0]){ void search_landing_leave(void); search_landing_leave(); }   /* Disco: past the landing page */
        for(const char *c=q;*c && *c!='-';c++){                 /* tap the keys by their label */
            char lab[2]={(char)(*c>='a'&&*c<='z'?*c-32:*c),0}; lv_obj_t *l=find_text(root,*c==' '?"space":lab); assert(l);
            lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_SHORT_CLICKED,NULL);
        }
        assert(search_scroller()==NULL);                       /* typing never searches */
        if(getenv("REVIEW_239_SEARCH")){
            assert(th_null());lv_obj_t *go=ui_action_find("search.go");assert(go);
            lv_obj_t *label=lv_obj_get_child(go,0);assert(label);
            assert(lv_color_eq(lv_obj_get_style_text_color(label,0),TC(TEXT_PRIMARY)));
            puts("PASS: typed Null search button uses visible active ink");
        }

        if(getenv("REVIEW_SIMPLE_SEARCH")){
            assert(th_simple());lv_obj_update_layout(root);lv_obj_t*button=ui_action_find("search.go");assert(button);assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(button,0),0),TC(TEXT_PRIMARY)));
            lv_obj_t*a=find_text(root,"H"),*b=find_text(root,"I");assert(a&&b);lv_area_t aa,bb;lv_obj_get_coords(lv_obj_get_parent(a),&aa);lv_obj_get_coords(lv_obj_get_parent(b),&bb);assert(aa.x2<bb.x1);
            puts("PASS: Squash typed search has readable ink and separated keyboard targets");
        }
        if(go){ int before=send_count; lv_obj_send_event(ui_action_find("search.go"),LV_EVENT_CLICKED,NULL);
                assert(search_scroller()!=NULL); assert(send_count==before);
                if(getenv("SEARCH_BACK")){ assert(search_back_consumed()); assert(search_scroller()==NULL); } }
        for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"scan-orbit")){             /* fork: the rescan runner on the rim (over Home) */
        screen_set_anim(0);screens_init();screen_show(SCR_HOME);
        ui_scan_orbit(1);
        for(int i=0;i<50;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"volume")){                 /* the volume popup over Home */
        screen_set_anim(0);screens_init();screen_show(SCR_HOME);
        void ui_show_volume(int); ui_show_volume(74);
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"ssphere-",8)){            /* SongSphere built in: ssphere-0 first page, -1 list, -3 play; ssphere-tag = an album's tag page */
        screen_set_anim(0);screens_init();
        if(!strcmp(argv[3],"ssphere-tag")) sphere_ui_tag(1,getenv("SS_KEY")?getenv("SS_KEY"):"Atlas",getenv("SS_KEY")?getenv("SS_KEY"):"Atlas");
        else sphere_ui_test(atoi(argv[3]+8),getenv("SS_X")?(float)atof(getenv("SS_X")):0.4f,getenv("SS_Y")?(float)atof(getenv("SS_Y")):0.5f);
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"appshelf",8)){            /* Apps (appshelf.c): the list; -pick = Choose apps; -land = the 3rd app's landing page */
        if(getenv("APPS_PINS")){ char b[256]; snprintf(b,sizeof b,"%s",getenv("APPS_PINS")); int s=0;   /* folder names / built-in keys */
            for(char *p=strtok(b,",");p&&s<5;p=strtok(NULL,",")){ char id[33],k[12]; md5_hex(p,strlen(p),id); snprintf(k,sizeof k,"apps_p%d",++s); cfg_set_str(k,id); }
            while(s<5){ char k[12]; snprintf(k,sizeof k,"apps_p%d",++s); cfg_set_str(k,""); } }
        screen_set_anim(0);screens_init();
        if(!strcmp(argv[3],"appshelf-pick")) appshelf_pick_open();
        else { appshelf_open();
            if(!strcmp(argv[3],"appshelf-land")){ for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();}
                lv_obj_t *l=appshelf_scroller(); lv_obj_t *r=lv_obj_get_child(l,3); assert(r); lv_obj_send_event(r,LV_EVENT_CLICKED,NULL); } }
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"roulette-",9)){           /* Album Roulette built in: roulette-<ms> after I feel lucky! (roulette-land: its page) */
        screen_set_anim(0);screens_init();
        if(!strcmp(argv[3],"roulette-land")) screen_show(SCR_ROULETTE);
        else { roulette_spin_open(); int ms=atoi(argv[3]+9);
            for(int t=0;t<ms;t+=10){ lv_tick_inc(10); lv_timer_handler(); if(t%200==0) usleep(2000); } }
        for(int i=0;i<35;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"splashbench")){           /* the splash's cost per frame: ms of work per 40 ms step */
        screen_set_anim(0);screens_init(); lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL);
        for(int ms=0;ms<=2600;ms+=40){ struct timespec a,b,c; clock_gettime(CLOCK_MONOTONIC,&a);
            splash_test_frame(ms); clock_gettime(CLOCK_MONOTONIC,&b); lv_refr_now(NULL); clock_gettime(CLOCK_MONOTONIC,&c);
            printf("%5d frame %6.2f render %6.2f\n",ms,(b.tv_sec-a.tv_sec)*1e3+(b.tv_nsec-a.tv_nsec)/1e6,(c.tv_sec-b.tv_sec)*1e3+(c.tv_nsec-b.tv_nsec)/1e6); }
        return 0;
    }
    if(!strncmp(argv[3],"splash-",7)){             /* the boot splash standing at splash-<ms> */
        screen_set_anim(0);screens_init();
        splash_test_frame(atoi(argv[3]+7));
        for(int i=0;i<3;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"statsapp",8)){           /* Listening Stats (stats.c) on a seeded PLAY_LOG; statsapp-land = its landing page */
        stats_seed_demo();
        screen_set_anim(0);screens_init();
        if(!strcmp(argv[3],"statsapp-land")) stats_landing();
        else{
            if(getenv("STATS_PERIOD")){ /* tap Week / Year */
                stats_open(); lv_obj_t *l=find_text(screen_get_root(SCR_STATS),getenv("STATS_PERIOD")); assert(l);
                lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
            } else stats_open();
            if(getenv("STATS_BACK")){ for(int k=0;k<atoi(getenv("STATS_BACK"));k++){ lv_obj_t *l=find_text(screen_get_root(SCR_STATS),LV_SYMBOL_LEFT); assert(l);
                lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);} }
            for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();}
            if(getenv("STATS_SUB")) stats_test_sub(atoi(getenv("STATS_SUB")));
            if(getenv("STATS_WRAP")) stats_test_wrap(atoi(getenv("STATS_WRAP")),getenv("STATS_PAGE")?atoi(getenv("STATS_PAGE")):0);
            if(getenv("STATS_SCROLL")){ lv_obj_t *list=stats_scroller();
                lv_obj_scroll_to_y(list,atoi(getenv("STATS_SCROLL")),LV_ANIM_OFF); lv_obj_send_event(list,LV_EVENT_SCROLL,NULL); }
        }
        for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"sphere-",7)){             /* SongSphere concept mockups (tests/sphere_mock.c) */
        void sphere_mock(lv_obj_t *root,int which);
        lv_obj_t *r=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(r); lv_obj_set_size(r,360,360);
        lv_obj_set_style_radius(r,LV_RADIUS_CIRCLE,0); lv_obj_set_style_clip_corner(r,true,0);
        lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);
        sphere_mock(r,atoi(argv[3]+7));
        for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"hint-",5)){              /* one first-use hint, shown (disco_hints On); HINT_SIDE=1 mirrors */
        int id=atoi(argv[3]+5);
        if(getenv("HINT_SIDE"))cfg_set_int("disco_side",atoi(getenv("HINT_SIDE")));
        if(getenv("HINT_FONT"))cfg_set_int("font_size",atoi(getenv("HINT_FONT")));
        screen_set_anim(0);screens_init();
        onboarding_reset();cfg_set_int("disco_hints",0);
        onboarding_start();
        for(int k=0;k<HINT_COUNT;k++) if(k!=id){ onboarding_discovered(k); onboarding_discovered(k); }
        { track_state_t st; memset(&st,0,sizeof st); snprintf(st.title,sizeof st.title,"Northern Lights"); snprintf(st.artist,sizeof st.artist,"The Midnight");
          snprintf(st.path,sizeof st.path,"/fixture/hint.flac"); st.have_track=1; st.state=2; st.duration_ms=240000; st.position_ms=92000; ipc_seed_state(&st); ui_update(&st); }
        if(id==HINT_NAV){ void disco_nav_set_open(int); disco_nav_set_open(1); onboarding_screen(SCR_HOME,1); }
        else if(id==HINT_LIBRARY){ screen_show(SCR_LIBRARY); onboarding_screen(SCR_LIBRARY,0); onboarding_library(1); }
        else if(id==HINT_LOCK){ onboarding_screen(SCR_HOME,0); onboarding_woke_by_touch(); }
        else onboarding_screen(SCR_HOME,0);
        for(int i=0;i<120;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"stats-",6)){              /* Listening Stats concept mockups (tests/stats_mock.c) */
        void stats_mock(lv_obj_t *root,int which);
        lv_obj_t *r=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(r); lv_obj_set_size(r,360,360);
        lv_obj_set_style_radius(r,LV_RADIUS_CIRCLE,0); lv_obj_set_style_clip_corner(r,true,0);
        lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);
        stats_mock(r,atoi(argv[3]+6));
        for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"appland-boom")){           /* the Boomshine landing page, for comparison */
        screen_set_anim(0);screens_init();boomshine_landing();
        for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
        if(th_null()){
            lv_obj_t *l=find_visible_text(screen_get_root(SCR_APPLAND),"Play");assert(l);
            lv_color_t ink=lv_obj_get_style_text_color(l,0);assert(ink.red+ink.green+ink.blue>96);
            assert(lv_obj_get_style_border_width(lv_obj_get_parent(l),0)==1);
            if(getenv("REVIEW_NULL_LANDING")){
                const char *labels[]={"Show me","Open"};
                for(int j=0;j<2;j++){applanding_show(j?"Installed App":"Listening Stats","Tagline","App description",labels[j],j?"/not-launched":APP_BUILTIN_STATS);nav_wait(40);
                    l=find_visible_text(screen_get_root(SCR_APPLAND),labels[j]);assert(l);ink=lv_obj_get_style_text_color(l,0);assert(ink.red+ink.green+ink.blue>96);}
                puts("PASS: Boomshine, Stats and generic app landing buttons remain readable");
            }
        }
        goto render;
    }
    if(!strncmp(argv[3],"disco-",6)){              /* "Disco" theme concept mockups (tests/disco_mock.c) */
        void disco_mock(lv_obj_t *root,int which);
        lv_obj_t *r=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(r); lv_obj_set_size(r,360,360);
        lv_obj_set_style_radius(r,LV_RADIUS_CIRCLE,0); lv_obj_set_style_clip_corner(r,true,0);
        disco_mock(r,atoi(argv[3]+6));
        for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"checks")){
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());screen_set_anim(0);screens_init();
        battery_arc_t battery; battery_arc_create(&battery,lv_screen_active(),328,3,236,304,0);
        int values[]={0,10,25,50,67,100,120};
        for(int i=0;i<7;i++){
            battery_arc_set(&battery,values[i],0);assert(lv_arc_get_value(battery.arc)==(values[i]>100?100:values[i]));
            lv_color_t want=values[i]<=15?TC(STATUS_DANGER):TC(TEXT_PRIMARY);
            assert(lv_color_eq(lv_obj_get_style_arc_color(battery.arc,LV_PART_INDICATOR),want));
            battery_arc_set(&battery,values[i],1);assert(lv_color_eq(lv_obj_get_style_arc_color(battery.arc,LV_PART_INDICATOR),TC(STATUS_SUCCESS)));
            assert(lv_obj_get_style_shadow_width(battery.arc,0)==0);
        }
        battery_arc_set(&battery,-1,0);assert(lv_arc_get_value(battery.arc)==100);
        screen_show(SCR_SETTINGS);setlist_open("Display");screen_show(SCR_SETLIST);
        lv_obj_t *r=screen_get_root(SCR_SETLIST);assert_no_tokens(r);assert(count_text(r,"100%")<=1);
        const char *groups[]={"Playback","Audio","Display","Network","System"};
        for(int i=0;i<5;i++){setlist_open(groups[i]);assert_no_tokens(r);}
        cfg_set_int_deferred("eq_preset",11);cfg_set_int_deferred("eq_last",11);
        assert(cfg_get_int("eq_preset",-1)==11);
        screen_show(SCR_EQ);for(int n=0;n<40;n++){lv_tick_inc(10);lv_timer_handler();usleep(1000);}assert(screen_current()==SCR_EQ);assert(ui_action_find("eq.edit"));
        lv_obj_send_event(ui_action_find("eq.edit"),LV_EVENT_CLICKED,NULL);assert(screen_current()==SCR_EQ_EDITOR);
        screen_back();assert(screen_current()==SCR_EQ);
        assert(cfg_get_int("eq_preset",-1)==11);
        lv_obj_send_event(ui_action_find("eq.toggle"),LV_EVENT_CLICKED,NULL);assert(cfg_get_int("eq_preset",-1)==0);
        lv_obj_send_event(ui_action_find("eq.toggle"),LV_EVENT_CLICKED,NULL);assert(cfg_get_int("eq_preset",-1)==11);
        send_result=-1;lv_obj_send_event(ui_action_find("eq.next"),LV_EVENT_CLICKED,NULL);assert(cfg_get_int("eq_preset",-1)==11);
        if(theme_preset()==THEME_PRESET_RING){        /* fork: Ring buttons - accent ring, album-colour glyph */
            lv_obj_t *edit=ui_action_find("eq.edit"), *lbl=lv_obj_get_child(edit,0);
            lv_obj_set_style_text_color(lbl,TC(STATUS_SUCCESS),0);
            screen_home();kit_accent_changed();lv_refr_now(d);
            assert(lv_color_eq(lv_obj_get_style_text_color(lbl,0),TC(STATUS_SUCCESS)));
            screen_show(SCR_EQ);
            assert(lv_color_eq(lv_obj_get_style_text_color(lbl,0),ui_media_accent()));
            assert(lv_color_eq(lv_obj_get_style_border_color(edit,0),TC(ACCENT_PRIMARY)) && lv_obj_get_style_border_width(edit,0)==2);
            puts("PASS: hidden screen styling deferred; current album colour applied on entry");
        }
        puts("PASS: battery 0..100/clamping/colour/no-glow; settings tokens; selector -> editor/back and EQ restore/failure");return 0;
    }
    if(!strncmp(argv[3],"preview-",8)){
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        if(strstr(argv[3],"queue")){
            char qp[512]; snprintf(qp,sizeof qp,"%s",CFG_PATH);
            char *slash=strrchr(qp,'/'); assert(slash); strcpy(slash+1,"queue.tsv");
            FILE *qf=fopen(qp,"w"); assert(qf);
            if(!getenv("QUEUE_EMPTY")) fputs("/preview/01.flac\tSunset Drive\tThe Midnight\t240000\t1\t1\t0\t0\t1\n/preview/02.flac\tNightfall\tKavinsky\t220000\t2\t2\t0\t0\t1\n/preview/03.flac\tA Long Way Home\tTimecop1983\t260000\t3\t3\t0\t0\t1\n",qf);
            if(getenv("QUEUE_LONG"))for(int i=3;i<30;i++)fprintf(qf,"/preview/%02d.flac\tSong %02d\tThe Midnight\t240000\t%d\t%d\t0\t0\t1\n",i,i,i,i);
            assert(!fclose(qf));
        }
        cfg_set_int_deferred("online_lyrics",0);cfg_set_int_deferred("online_art",0);
        cfg_set_int_deferred("eq_preset",11);cfg_set_int_deferred("eq_last",11);cfg_set_str("eq_name11","Warm analogue");
        if(getenv("QS_ITEMS"))simple_cfg_set_str_deferred(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items",getenv("QS_ITEMS"));
        if(getenv("DISCO_QS"))cfg_set_int_deferred("disco_qs_style",atoi(getenv("DISCO_QS")));
        if(getenv("DISCO_PROG"))cfg_set_int_deferred("disco_progress",atoi(getenv("DISCO_PROG")));
        if(getenv("DISCO_IMMSTYLE"))cfg_set_int_deferred("disco_imm",atoi(getenv("DISCO_IMMSTYLE")));
        if(getenv("IMM_STYLE"))cfg_set_int_deferred("imm_style",atoi(getenv("IMM_STYLE")));
        if(getenv("IMM_LYRICS"))cfg_set_int_deferred("imm_lyrics",atoi(getenv("IMM_LYRICS")));
        if(getenv("DISCO_SCROLL"))cfg_set_int_deferred("disco_scroll",atoi(getenv("DISCO_SCROLL")));if(getenv("DISCO_SIDE"))cfg_set_int_deferred("disco_side",atoi(getenv("DISCO_SIDE")));
        if(getenv("DISCO_SHAPE"))cfg_set_int_deferred("disco_prog_shape",atoi(getenv("DISCO_SHAPE")));
        if(getenv("DISCO_CTRL"))cfg_set_int_deferred("disco_controls",atoi(getenv("DISCO_CTRL")));
        if(getenv("DISCO_TAL"))cfg_set_int_deferred("disco_title_al",atoi(getenv("DISCO_TAL")));
        if(getenv("DISCO_SHEEN"))cfg_set_int_deferred("disco_sheen",atoi(getenv("DISCO_SHEEN")));
        if(getenv("NP_FONT"))cfg_set_int_deferred(th_simple()?"simple_track_font":"np_font",atoi(getenv("NP_FONT")));
        if(getenv("FONTSZ"))cfg_set_int_deferred("font_size",atoi(getenv("FONTSZ")));
        if(getenv("DISCO_MNU"))cfg_set_str_deferred("disco_mnu",getenv("DISCO_MNU"));
        cfg_set_int_deferred("disco_nav_hint",getenv("NAV_HINT")?0:1);   /* the first-time hint only when asked for */
        if(getenv("TRACKNO"))cfg_set_int_deferred("disco_trackno",1);
        screen_set_anim(0);screens_init();ui_set_accent_config(getenv("ACCENT")?1:0,getenv("ACCENT")?(int)strtol(getenv("ACCENT"),NULL,16):0);ui_set_np_style(theme_preset()==THEME_PRESET_RING?3:1);
        track_state_t st={0};snprintf(st.path,sizeof st.path,"%s",argv[5]);
        char *dot=strrchr(st.path,'.');assert(dot);strcpy(dot,".png");
        snprintf(st.title,sizeof st.title,"Northern Lights");snprintf(st.artist,sizeof st.artist,"The Midnight");snprintf(st.album,sizeof st.album,"Afterglow");
        if(getenv("NP_TITLE"))snprintf(st.title,sizeof st.title,"%s",getenv("NP_TITLE"));
        if(getenv("NP_ARTIST"))snprintf(st.artist,sizeof st.artist,"%s",getenv("NP_ARTIST"));if(getenv("NP_ALBUM"))snprintf(st.album,sizeof st.album,"%s",getenv("NP_ALBUM"));
        if(getenv("NULL_DEMO_FLAC")){char *ext=strrchr(st.path,'.');assert(ext);strcpy(ext,".flac");}
        int long_names=!strncmp(argv[3],"preview-long-",13);
        if(long_names){
            snprintf(st.title,sizeof st.title,"The Last Train Home Through Northern Lights");
            snprintf(st.artist,sizeof st.artist,"The Midnight & The Northern Lights Orchestra");
        }
        st.have_track=1;st.state=2;st.duration_ms=240000;st.position_ms=92000;st.sample_rate=44100;ipc_seed_state(&st);
        if(getenv("MA_TRACK")){                      /* MA Sendspin: Music shows Music Assistant's track + its sidecar cover */
            struct timespec tn; clock_gettime(CLOCK_MONOTONIC,&tn);
            ipc_set_external("Midnight City","M83","Hurry Up, We're Dreaming",getenv("MA_TRACK"),243000,61000,tn.tv_sec*1000LL+tn.tv_nsec/1000000,1000);
            ipc_get_state(&st); st.state=2;
        }
        ui_update(&st);
        int loaded=0;for(int i=0;i<500;i++){ui_art_poll(NULL);if(ui_current_cover_dsc()){loaded=1;break;}usleep(10000);}assert(loaded);
        if(getenv("REVIEW_SIMPLE_ALBUM")){
            unsigned ui_art_revision(void);
            assert(th_simple());unsigned revision=ui_art_revision();int calls=atomic_load(&art_decode_calls);
            track_state_t same=st;snprintf(same.path,sizeof same.path,"/tmp/squash-album-reuse/Disc2/track.png");ipc_seed_state(&same);ui_update(&same);
            for(int i=0;i<50;i++){ui_art_poll(NULL);usleep(1000);}assert(ui_art_revision()==revision&&atomic_load(&art_decode_calls)==calls&&ui_current_cover_dsc());
            snprintf(same.path,sizeof same.path,"/tmp/squash-album-reuse/Other/track.png");snprintf(same.artist,sizeof same.artist,"A different artist");ipc_seed_state(&same);ui_update(&same);
            for(int i=0;i<500&&ui_art_revision()==revision;i++){ui_art_poll(NULL);usleep(1000);}assert(atomic_load(&art_decode_calls)>calls&&ui_art_revision()>revision&&ui_current_cover_dsc());
            revision=ui_art_revision();calls=atomic_load(&art_decode_calls);void ui_art_redo(void);ui_art_redo();ui_update(&same);
            for(int i=0;i<500&&ui_art_revision()==revision;i++){ui_art_poll(NULL);usleep(1000);}assert(atomic_load(&art_decode_calls)>calls&&ui_art_revision()>revision);
            revision=ui_art_revision();calls=atomic_load(&art_decode_calls);snprintf(same.path,sizeof same.path,"/tmp/squash-album-reuse/Other/book.m4b");ipc_seed_state(&same);ui_update(&same);
            for(int i=0;i<500&&ui_art_revision()==revision;i++){ui_art_poll(NULL);usleep(1000);}assert(atomic_load(&art_decode_calls)>calls&&ui_art_revision()>revision);
            puts("PASS: Squash same-album/artist cross-folder skip preserves pixels and avoids decoder work; different artist, explicit refresh and audiobook load independently");return 0;
        }
        if(getenv("REVIEW_TRACK_SWITCHES")){
            extern unsigned ui_art_revision(void);
            unsigned revision = ui_art_revision();
            track_state_t same = st;
            snprintf(same.path, sizeof same.path, "%s-same.png", st.path);
            snprintf(same.title, sizeof same.title, "Another song in the same album");
            ipc_seed_state(&same); ui_update(&same);
            for(int i=0;i<20;i++){ui_art_poll(NULL);usleep(1000);}
            assert(ui_art_revision() == revision && ui_current_cover_dsc());
            ui_publish_art_surfaces(&same,1); ui_publish_art_surfaces(&same,1);
            /* Rapid new albums must ultimately bind the LAST sharp pixels. */
            track_state_t next = st;
            for(int i=0;i<12;i++){
                snprintf(next.path,sizeof next.path,"%s-%s.png",st.path,i%2?"blue":"red");
                snprintf(next.album,sizeof next.album,"Album %d",i);
                ipc_seed_state(&next);ui_update(&next);
            }
            int fresh=0;
            for(int i=0;i<500;i++){
                ui_art_poll(NULL);
                const lv_image_dsc_t *dsc=ui_current_sharp_img();
                if(ui_art_revision()!=revision && dsc && dsc->header.cf==LV_COLOR_FORMAT_RGB888 &&
                   dsc->data[0]==209 && dsc->data[1]==71 && dsc->data[2]==11){fresh=1;break;}
                usleep(10000);
            }
            assert(fresh);
            st=next;
            puts("PASS: same-album pixels reused; 12 rapid skips retain the latest sharp cover");
        }
        int art_race=!strncmp(argv[3],"preview-art-race-",17);
        if(art_race){
            track_state_t other=st;
            snprintf(other.path,sizeof other.path,"%s",race_path);
            snprintf(other.album,sizeof other.album,"Other album");
            ipc_seed_state(&other);ui_update(&other);
            for(int i=0;i<500 && !atomic_load(&race_waiting);i++)usleep(1000);
            assert(atomic_load(&race_waiting));
            ipc_seed_state(&st);ui_update(&st);
            atomic_store(&race_release,1);
            /* Wait past the obsolete failure so the final current-track state is tested. */
            for(int i=0;i<200;i++){ui_art_poll(NULL);usleep(10000);}
            loaded=ui_current_cover_dsc()!=NULL;
            assert(loaded == (getenv("DISKOS_ART_RACE_EXPECT_MISSING")==NULL));
            printf("PASS: controlled A -> B (blocked load) -> A; final artwork=%s\n",loaded?"present":"missing (baseline reproduction)");
        }
        ui_publish_art_surfaces(&st,1);
        home_set_now_playing(st.title,st.artist,ui_current_accent(),true);home_set_clock("10:09","Saturday, 3 October");
        home_set_weather("\xEF\x86\x85  18\xC2\xB0" "C  Clear");
        home_set_status(67,0,1,1);quicksettings_set_battery(getenv("QS_BATT")?atoi(getenv("QS_BATT")):67,getenv("QS_CHARGE")?atoi(getenv("QS_CHARGE")):0);
        quicksettings_set_now_playing(st.title,st.artist,1);
        const char *page=argv[3]+(art_race?17:(long_names?13:8));int scr=SCR_HOME;
        if(!strcmp(page,"bt")){scr=SCR_BT;}
        else if(!strcmp(page,"wifi")){scr=SCR_WIFI;}
        else if(!strcmp(page,"settings")){scr=SCR_SETTINGS;}
        else if(!strcmp(page,"howto")){extern void settings_open_howto(void);settings_open_howto();scr=SCR_SETLIST;}
        else if(!strcmp(page,"quick"))scr=SCR_QUICK;else if(!strcmp(page,"np") || !strncmp(page,"immersive",9))scr=SCR_NOWPLAYING;else if(!strcmp(page,"eq"))scr=SCR_EQ;
        else if(!strcmp(page,"bands"))scr=SCR_EQ_EDITOR;else if(!strcmp(page,"display")){setlist_open("Display");scr=SCR_SETLIST;}else if(!strcmp(page,"network")){setlist_open("Network");scr=SCR_SETLIST;}else if(!strcmp(page,"system")){setlist_open("System");scr=SCR_SETLIST;}else if(!strcmp(page,"info")){songinfo_unpin();songinfo_set(&st);scr=SCR_SONGINFO;}else if(!strcmp(page,"group")){void settings_open_group(const char*);setlist_open("Display");scr=SCR_SETLIST;screen_show(scr);settings_open_group(getenv("SETGROUP"));}
        else if(!strcmp(page,"library") || !strcmp(page,"albums"))scr=SCR_LIBRARY;else if(!strcmp(page,"settings"))scr=SCR_SETTINGS;
        else if(!strcmp(page,"upnext"))scr=SCR_UPNEXT;else if(!strcmp(page,"modes"))scr=SCR_WORKMODE;
        else if(!strcmp(page,"queue"))scr=SCR_QUEUE;
        else if(!strcmp(page,"shortcuts")){apps_reload();scr=SCR_APPS;}
        else if(!strcmp(page,"search")){if(getenv("SEARCH_RECENT")){cfg_set_str("search_r1","boris");cfg_set_str("search_r2","alpha band");cfg_set_str("search_r3","northern");}scr=SCR_SEARCH;}
        else if(!strcmp(page,"battery")){void usage_demo(int);scr=SCR_USAGE;screen_show(scr);usage_demo(getenv("CHG")!=NULL);}
        else if(!strcmp(page,"discoopts")){void settings_open_disco(void);setlist_open("Display");scr=SCR_SETLIST;screen_show(scr);settings_open_disco();}
        else if(!strncmp(page,"scr",3)){scr=atoi(page+3);}
        else if(!strcmp(page,"weather")){void weather_demo(void);scr=SCR_WEATHER;screen_show(scr);weather_demo();}
        else if(!strcmp(page,"low"))home_set_status(10,0,1,1);else if(!strcmp(page,"charging"))home_set_status(67,1,1,1);
        else if(!strncmp(page,"saver",5)){
            cfg_set_int_deferred("saver_style",5);
            saver_set_track(st.title,st.artist,NULL,st.path);
            saver_set_clock("10:09","Saturday, 3 October");
            saver_set_weather("\xEF\x86\x85  18°C Clear");
            scr=SCR_SAVER;
        }
        if(th_null()&&!strcmp(page,"scr43"))appshelf_open();
        screen_show(scr);
        if(!strcmp(page,"bt")){extern void bt_preview_null(void);bt_preview_null();}
        if(!strcmp(page,"wifi")){extern void wifi_preview_null(void);wifi_preview_null();}
        if(!strncmp(page,"immersive",9)){
            if(th_disco() || th_null()) disco_open_np_immersive(); else ui_np_fsart_open();assert(ui_np_fsart_active());
            for(int i=0;i<100;i++){lyrics_poll(NULL);usleep(1000);}
        }
        for(int i=0;i<120;i++){lv_tick_inc(10);lv_timer_handler();}kit_pass(screen_get_root(scr));
        if(getenv("PICK_DOWN")){   /* open the menu and step the picker down N times (NAV_CLOSE=1: then close it) */
            void disco_test_tap(int);disco_nav_set_open(1);nav_wait(300);
            for(int i=atoi(getenv("PICK_DOWN"));i>0;i--){disco_test_tap(2);nav_wait(300);}
            if(getenv("NAV_CLOSE")){disco_nav_set_open(0);nav_wait(400);}
        }
        if(getenv("REVIEW_NULL_BATTERY")){
            assert(th_null());lv_color_t green=TC(STATUS_SUCCESS);assert(green.green>green.red && green.green>green.blue);screen_show(SCR_QUICK);nav_wait(250);
            lv_obj_t *root=screen_get_root(SCR_QUICK),*bar=NULL;
            for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){lv_obj_t *o=lv_obj_get_child(root,i);if(lv_obj_get_width(o)==6 && lv_obj_get_height(o)==160 && !lv_obj_check_type(o,&lv_slider_class))bar=o;}
            assert(bar);lv_obj_t *fill=lv_obj_get_child(bar,0),*flow=lv_obj_get_child(bar,2);
            null_qs_battery(14,0);assert(lv_color_eq(lv_obj_get_style_bg_color(fill,0),TC(STATUS_DANGER)));
            null_qs_battery(15,0);assert(lv_color_eq(lv_obj_get_style_bg_color(fill,0),TC(TEXT_PRIMARY)));
            null_qs_battery(67,1);assert(lv_color_eq(lv_obj_get_style_bg_color(fill,0),TC(STATUS_SUCCESS)));
            nav_wait(100);int y=lv_obj_get_y(flow),height=lv_obj_get_height(flow);nav_wait(150);assert(lv_obj_get_y(flow)!=y || lv_obj_get_height(flow)!=height);
            lv_obj_t *bolt=find_visible_text(root,LV_SYMBOL_CHARGE);assert(bolt);int opacity=lv_obj_get_style_text_opa(bolt,0);nav_wait(500);assert(lv_obj_get_style_text_opa(bolt,0)!=opacity);
            screen_back();nav_wait(250);y=lv_obj_get_y(flow);nav_wait(200);assert(lv_obj_get_y(flow)==y);
            puts("PASS: battery threshold, charging colour, flow, flashing icon and closed-panel pause");
            return 0;
        }
        if(getenv("REVIEW_HELP_MENU")){
            screen_show(SCR_SETTINGS);nav_wait(100);
            lv_obj_t *help=find_text(screen_get_root(SCR_SETTINGS),"Help");assert(help);
            assert(!find_text(screen_get_root(SCR_SETTINGS),"How to"));
            lv_obj_send_event(lv_obj_get_parent(help),LV_EVENT_CLICKED,NULL);nav_wait(100);
            lv_obj_t *root=screen_get_root(SCR_SETLIST),*how=find_text(root,"How to");assert(how);
            assert(find_text(root,"First-use hints") && find_text(root,"Show hints again"));
            lv_obj_send_event(lv_obj_get_parent(how),LV_EVENT_CLICKED,NULL);nav_wait(100);
            assert(settings_back_consumed());nav_wait(100);assert(find_text(root,"First-use hints"));
            cfg_set_int("disco_hints",th_null()?2:0);cfg_set_int("null_hints",0);
            lv_obj_t *mode=find_text(root,"First-use hints");lv_obj_send_event(lv_obj_get_parent(mode),LV_EVENT_CLICKED,NULL);nav_wait(100);
            lv_obj_t *right=find_text(screen_get_root(SCR_SETTING_DETAIL),LV_SYMBOL_RIGHT);assert(right);
            lv_obj_send_event(lv_obj_get_parent(right),LV_EVENT_CLICKED,NULL);nav_wait(100);
            assert(cfg_get_int(th_null()?"null_hints":"disco_hints",0)==1);
            if(th_null())assert(cfg_get_int("disco_hints",0)==2);
            puts("PASS: Help below Battery, How to and back path, independent hint preferences");return 0;
        }
        if(getenv("HELP_KEY")){extern void settings_open_key(const char *);settings_open_key(getenv("HELP_KEY"));scr=SCR_SETTING_DETAIL;nav_wait(300);assert(screen_current()==SCR_SETTING_DETAIL);}
        if(getenv("HANDLE_TIP")){
            onboarding_reset();onboarding_start();onboarding_context(HINT_HANDLE);nav_wait(900);
            lv_obj_t *tip=find_text(lv_layer_top(),onboarding_gesture_text(HINT_HANDLE));assert(tip);
            lv_obj_update_layout(lv_obj_get_parent(tip));lv_area_t a;lv_obj_get_coords(lv_obj_get_parent(tip),&a);assert(abs((a.y1+a.y2)/2-180)<3);
        }
        if(getenv("HOWTO_PAGE")){
            lv_obj_t *root=screen_get_root(SCR_SETLIST),*list=NULL;
            for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){
                lv_obj_t *o=lv_obj_get_child(root,i);if(lv_obj_get_height(o)==240 && lv_obj_get_width(o)==276)list=o;
            }
            assert(list);lv_obj_scroll_to_y(list,240*atoi(getenv("HOWTO_PAGE")),LV_ANIM_OFF);nav_wait(100);
        }
        if(getenv("REVIEW_240")){
            lv_obj_t *r=lv_obj_create(screen_get_root(scr));lv_obj_set_size(r,280,260);lv_obj_set_pos(r,40,60);
            lv_obj_t *labels[3];
            for(int i=0;i<3;i++){
                lv_obj_t *row=lv_obj_create(r);lv_obj_set_pos(row,0,i*70);lv_obj_set_size(row,260,60);
                labels[i]=lv_label_create(row);lv_obj_set_width(labels[i],180);
                lv_label_set_text(labels[i],"A very long library title that needs a quiet abbreviated row");
                ui_library_row(row);
            }
            ui_library_list_watch(r);nav_wait(250);
            int scrolling=0;for(int i=0;i<3;i++)scrolling+=lv_label_get_long_mode(labels[i])==LV_LABEL_LONG_SCROLL_CIRCULAR;
            assert(scrolling==1);
            lv_obj_delete(r);
            lv_obj_t *l=lv_label_create(screen_get_root(scr));lv_obj_set_width(l,100);
            lv_label_set_text(l,"A long music title for a real three-cycle scroll check");ui_music_scroll(l);
            const lv_anim_t *motion=lv_obj_get_style_anim(l,0);assert(motion && lv_anim_get_repeat_count(motion)==3);
            nav_wait(120000);assert(lv_label_get_long_mode(l)==LV_LABEL_LONG_CLIP);lv_obj_delete(l);
            if(th_null()){
                assert(!disco_mirror());extern void settings_open_key(const char *);settings_open_key("null_side");nav_wait(100);
                lv_obj_t *detail=screen_get_root(SCR_SETTING_DETAIL);int disabled=0;
                for(uint32_t i=0;i<lv_obj_get_child_count(detail);i++){
                    lv_obj_t *o=lv_obj_get_child(detail,i);if(lv_obj_check_type(o,&lv_button_class) && lv_obj_has_state(o,LV_STATE_DISABLED))disabled++;
                }
                assert(disabled==2);assert(find_text(detail,"Right"));
                assert(strstr(onboarding_gesture_text(HINT_HANDLE),"Search"));
            }
            puts("PASS: one focused library row, three-cycle Music scroll termination, Null right-only UI and handle hint");return 0;
        }
        if(getenv("HELP_MENU_BOTTOM")){lv_obj_t *how=find_text(screen_get_root(SCR_SETTINGS),"How to");assert(how);lv_obj_scroll_to_view(lv_obj_get_parent(how),LV_ANIM_OFF);nav_wait(100);}
        if(getenv("HELP_SCROLL")){extern void settings_test_scroll(int);settings_test_scroll(atoi(getenv("HELP_SCROLL")));}
        if(getenv("REVIEW_239_POLISH")){
            extern void settings_open_group(const char *);
            assert(th_null());
            null_home_weather("18\xC2\xB0" "C");
            assert(find_text(screen_get_root(SCR_HOME),"18C"));
            if(scr==SCR_SAVER){
                lv_obj_t *sb=lv_obj_get_child(screen_get_root(SCR_SAVER),0);
                assert(lv_obj_check_type(sb,&lv_image_class));
                assert(lv_obj_has_flag(sb,LV_OBJ_FLAG_HIDDEN)==(cfg_get_int("null_music_background",0)==0));
                if(cfg_get_int("null_music_background",0))assert(lv_image_get_src(sb));
            }
            setlist_open("Display");settings_open_group("Theme Colours");nav_wait(80);
            lv_obj_t *a=find_text(screen_get_root(SCR_SETLIST),"Appearance");assert(a);
            lv_obj_send_event(lv_obj_get_parent(a),LV_EVENT_CLICKED,NULL);nav_wait(80);
            lv_obj_t *detail=screen_get_root(SCR_SETTING_DETAIL);int disabled=0,before=cfg_get_int("theme_variant",-1);
            for(uint32_t i=0;i<lv_obj_get_child_count(detail);i++){
                lv_obj_t *o=lv_obj_get_child(detail,i);
                if(lv_obj_check_type(o,&lv_button_class) && lv_obj_has_state(o,LV_STATE_DISABLED)){
                    disabled++;lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);
                }
            }
            assert(disabled==2 && cfg_get_int("theme_variant",-1)==before);
            assert(find_text(detail,"Null uses a fixed dark appearance.\nLight/Dark is unavailable in this theme."));
            puts("PASS: Null degree removal, saver background visibility and disabled appearance with explanation");return 0;
        }
        if(getenv("REVIEW_239")){
            extern void disco_track_overlay_open(void);
            assert(th_disco_family() || th_null());screen_home();screen_set_anim(0);
            lv_indev_t *in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,nav_read);
            nav_wait(60);disco_track_overlay_open();nav_wait(60);assert(disco_track_overlay_visible());
            int side=disco_mirror(),x=side?330:30;
            nav_pointer=(lv_indev_data_t){.point={x,180},.state=LV_INDEV_STATE_PRESSED};nav_wait(50);
            nav_pointer.point.x=side?210:150;nav_wait(80);
            nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(100);
            assert(!disco_track_overlay_visible());assert(screen_current()==SCR_HOME);
            /* Start on an action button too: its events must bubble and it must not fire. */
            disco_track_overlay_open();nav_wait(40);
            nav_pointer=(lv_indev_data_t){.point={side?300:60,120},.state=LV_INDEV_STATE_PRESSED};nav_wait(50);
            nav_pointer.point.x=180;nav_wait(70);
            nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(60);
            assert(!disco_track_overlay_visible());assert(screen_current()==SCR_HOME);
            disco_track_overlay_open();nav_wait(40);screen_back();assert(!disco_track_overlay_visible());assert(screen_current()==SCR_HOME);
            if(th_null()){
                null_home_update(NULL,NULL,0);nav_wait(50);nav_touch((lv_point_t){180,125},60);nav_wait(80);assert(screen_current()==SCR_LIBRARY);
                screen_home();cfg_set_int_deferred("null_nav_double",DISCO_NAV_SEARCH);cfg_set_int_deferred("null_nav_hold",DISCO_NAV_QUICK);
                null_nav_set_open(1);nav_wait(250);
                nav_touch((lv_point_t){180,200},50);nav_wait(80);nav_touch((lv_point_t){180,200},50);nav_wait(100);assert(screen_current()==SCR_SEARCH);
                screen_home();null_nav_set_open(1);nav_wait(250);nav_touch((lv_point_t){180,200},750);nav_wait(100);assert(screen_current()==SCR_QUICK);
            }
            int fs=theme_font_size(),px=fs==0?14:fs==2?18:16;
            assert(lv_font_get_line_height(theme_font_default(THEME_FONT_UI_16))==lv_font_get_line_height(theme_font_original(px)));
            lv_indev_delete(in);puts("PASS: shared track popup swipe/back and Stats font policy; Null-specific shortcuts where applicable");return 0;
        }
        if(getenv("REVIEW_NULL_GESTURES")){
            extern int disco_track_overlay_visible(void);extern void dhome_overlay_close(void);
            assert(th_null());screen_home();screen_set_anim(0);cfg_set_int_deferred("null_controls",2);null_options_changed();
            lv_indev_t *in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,nav_read);
            nav_wait(60);int n=null_transport_calls;
            nav_touch((lv_point_t){180,154},60);nav_wait(80);nav_touch((lv_point_t){180,154},60);
            assert(null_transport_calls==n+1 && !strcmp(null_last_command,"0201000C0000"));
            nav_wait(450);assert(!disco_track_overlay_visible());
            nav_touch((lv_point_t){180,120},60);nav_wait(450);assert(disco_track_overlay_visible());dhome_overlay_close();nav_wait(60);
            nav_pointer=(lv_indev_data_t){.point={100,188},.state=LV_INDEV_STATE_PRESSED};nav_wait(50);
            nav_pointer.point.x=240;nav_wait(50);nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(40);
            assert(null_transport_calls==n+2 && !strcmp(null_last_command,"0201000C0001"));
            nav_wait(450);assert(!disco_track_overlay_visible());
            nav_pointer=(lv_indev_data_t){.point={240,130},.state=LV_INDEV_STATE_PRESSED};nav_wait(50);
            nav_pointer.point.x=100;nav_wait(50);nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(40);
            assert(null_transport_calls==n+3 && !strcmp(null_last_command,"0201000C0002"));
            puts("PASS: Null metadata double-tap, delayed popup tap, album next swipe, title previous swipe");
            goto render;
        }
        if(getenv("REVIEW_NULL_VOLUME")){
            screen_set_anim(1);screen_home();nav_wait(300);
            ui_show_volume(74);nav_wait(150);
            lv_obj_t *slider=ui_action_find("null.volume.adjust"),*strip=ui_action_find("null.volume.dismiss");
            assert(slider && strip && lv_obj_get_height(strip)==90 && lv_obj_get_y(strip)==135);
            assert(lv_obj_get_x(strip)==0 && lv_obj_get_style_bg_opa(strip,0)==LV_OPA_COVER);
            lv_slider_set_value(slider,92,LV_ANIM_OFF);lv_obj_send_event(slider,LV_EVENT_VALUE_CHANGED,NULL);
            lv_obj_send_event(slider,LV_EVENT_RELEASED,NULL);assert(find_text(strip,"92"));
            lv_obj_send_event(strip,LV_EVENT_CLICKED,NULL);nav_wait(150);
            assert(lv_obj_has_flag(strip,LV_OBJ_FLAG_HIDDEN));
            ui_show_volume(32);nav_wait(150);assert(find_text(strip,"32"));
            nav_wait(1900);assert(lv_obj_has_flag(strip,LV_OBJ_FLAG_HIDDEN));
            puts("PASS: Null volume strip adjusts, dismisses, reopens and times out");return 0;
        }
        if(getenv("REVIEW_NULL_ROUTES")||getenv("REVIEW_SIMPLE_ROUTES")){
            assert(th_null()||th_simple());
            for(int animated=0;animated<=1;animated++){
                screen_set_anim(animated);screen_home();nav_wait(300);
                /* Every library/queue/search playback picker uses this same public destination. */
                screen_show(SCR_LIBRARY);nav_wait(300);screen_show(SCR_NOWPLAYING);nav_wait(300);
                assert(screen_current()==SCR_HOME);
                assert(!lv_obj_has_flag(screen_get_root(SCR_HOME),LV_OBJ_FLAG_HIDDEN));
                assert(lv_obj_has_flag(screen_get_root(SCR_NOWPLAYING),LV_OBJ_FLAG_HIDDEN));
                disco_open_np_immersive();nav_wait(800);
                assert(screen_current()==SCR_NOWPLAYING && ui_np_fsart_active());
                ui_np_fsart_close();nav_wait(600);
                assert(screen_current()==SCR_HOME && !ui_np_fsart_active());
                assert(lv_obj_has_flag(screen_get_root(SCR_NOWPLAYING),LV_OBJ_FLAG_HIDDEN));
            }
            puts("PASS: playback redirects to Music and Immersive exits to Music, animations on/off");
            return 0;
        }
        if(getenv("REVIEW_NULL")){review_null();return 0;}
        if(getenv("REVIEW_NAV")){review_nav();return 0;}
        if(getenv("REVIEW_HINTS")){review_hints();return 0;}
        if(getenv("REVIEW_QS_PARITY")){
            void disco_test_editor_close(void);lv_obj_t *null_test_qs_field(int);int null_test_qs_item(int);
            const char *key=th_simple()?"simple_qs_items":th_null()?"null_qs_items":"disco_qs_items",*other=(th_null()||th_simple())?"disco_qs_items":"null_qs_items";
            int initial[8]={0,1,4,2,6,7,3,5},items[QS_MAX_ITEMS],n=(th_null()||th_simple())?8:7;
            if(!th_null()&&!th_simple())initial[6]=5;
            cfg_set_str(other,"rf");assert(quicksettings_items_save(initial,n)==0);
            screen_show(SCR_SETTINGS);disco_qs_menu_open();qs_editor_click("Wi-Fi",LV_SYMBOL_DOWN);
            assert(quicksettings_items_load(items)==n && items[0]==1 && items[1]==0);
            qs_editor_click("Wi-Fi",LV_SYMBOL_CLOSE);qs_editor_click("Settings",LV_SYMBOL_CLOSE);
            assert(quicksettings_items_load(items)==n-2);
            for(int i=0;i<n-2;i++)assert(items[i]!=0 && items[i]!=5);
            qs_editor_click("Queue",LV_SYMBOL_PLUS);qs_editor_click("Queue",LV_SYMBOL_UP);
            int count=quicksettings_items_load(items);assert(count==n-1 && items[count-2]==8);
            char saved[48];snprintf(saved,sizeof saved,"%s",cfg_get_str(key,""));
            disco_test_editor_close();screen_show(SCR_QUICK);nav_wait(250);
            if(th_simple())assert(simple_test_qs_count()==count);
            if(th_null())for(int i=0;i<8;i++){
                assert(null_test_qs_item(i)==(i<count?items[i]:-1));
                assert(lv_obj_has_flag(null_test_qs_field(i),LV_OBJ_FLAG_HIDDEN)==(i>=count));
            }
            screen_show(SCR_SETTINGS);disco_qs_menu_open();assert(!strcmp(saved,cfg_get_str(key,"")));
            while((count=quicksettings_items_load(items))>0)qs_editor_click(quicksettings_item(items[0])->name,LV_SYMBOL_CLOSE);
            disco_test_editor_close();screen_show(SCR_QUICK);nav_wait(150);
            assert(!strcmp(cfg_get_str(key,"?"),""));assert(!strcmp(cfg_get_str(other,""),"rf"));
            if(th_simple())assert(simple_test_qs_count()==0);
            if(th_null())for(int i=0;i<8;i++)assert(lv_obj_has_flag(null_test_qs_field(i),LV_OBJ_FLAG_HIDDEN));
            assert(quicksettings_items_save(initial,n)==0);quicksettings_build();nav_wait(150);
            if(th_null())for(int i=0;i<8;i++)assert(null_test_qs_item(i)==initial[i] && !lv_obj_has_flag(null_test_qs_field(i),LV_OBJ_FLAG_HIDDEN));
            puts("PASS: real shared QS editor reorder, remove Wi-Fi/Settings, add, reopen, empty selection, panel refresh and theme isolation");return 0;
        }
        if(getenv("REVIEW_QS")){review_qs();return 0;}
        if(getenv("REVIEW_DISCO_SETTINGS")){
            void settings_open_group(const char *);
            setlist_open("Display");settings_open_group("Theme Colours");nav_wait(80);
            lv_obj_t *label=find_text(screen_get_root(SCR_SETLIST),"Theme");assert(label);
            lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);nav_wait(80);
            lv_obj_t *root=screen_get_root(SCR_SETTING_DETAIL);
            lv_obj_t *disco=find_text(root,"Disco"),*ring=find_text(root,"Ring"),*braun=find_text(root,"Braun");
            assert(disco && ring && braun);lv_obj_update_layout(root);
            lv_area_t a,b,c;lv_obj_get_coords(disco,&a);lv_obj_get_coords(ring,&b);lv_obj_get_coords(braun,&c);
            assert(a.y1<b.y1 && b.y1<c.y1);
            screen_show(SCR_SETLIST);settings_open_group("Disco Colours");nav_wait(80);
            assert(!find_text(screen_get_root(SCR_SETLIST),"Progress Shape"));
            settings_open_group("Disco Layout");nav_wait(80);
            assert(find_text(screen_get_root(SCR_SETLIST),"Progress Shape"));
            puts("PASS: Disco first in theme picker; Progress Shape in Layout, absent from Colours");return 0;
        }
        if(getenv("QS_EDITOR"))disco_qs_menu_open();
        if(getenv("QS_SETTINGS")){
            setlist_open("Display");nav_wait(60);lv_obj_t *l=find_text(screen_get_root(SCR_SETLIST),"Disco Options");assert(l);
            lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);nav_wait(60);
            l=find_text(screen_get_root(SCR_SETLIST),"Quick Settings");assert(l);lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);nav_wait(60);
        }
        if(getenv("HINT_SHOT")){
            cfg_set_int("disco_hints",1);onboarding_start();
            int id=atoi(getenv("HINT_SHOT"));
            if(id==4){
                setlist_open("System");nav_wait(100);
                lv_obj_t *help=find_text(screen_get_root(SCR_SETLIST),"Help");assert(help);
                lv_obj_send_event(lv_obj_get_parent(help),LV_EVENT_CLICKED,NULL);
            }
            else if(id==1)disco_nav_set_open(1);
            else if(id==2){screen_show(SCR_LIBRARY);onboarding_library(1);cfg_set_int("disco_hints",0);cfg_set_int("hint_menu",2);}
            else if(id==3){cfg_set_int("disco_hints",0);cfg_set_int("hint_menu",2);cfg_set_int("hint_music",0);}
            onboarding_mode_changed();nav_wait(1000);
        }
        if(!strcmp(page,"albums")){                  /* Library > Albums */
            lv_obj_t *l=find_text(screen_get_root(SCR_LIBRARY),"Albums"); assert(l); lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
            for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        if(getenv("POPUP")){
            const char *which=getenv("POPUP");
            if(!strcmp(which,"track")){
                songmenu_open_song(st.path);const char *base=strrchr(st.path,'/');
                lv_obj_t *title=find_text(lv_layer_top(),base?base+1:st.path);if(title)lv_label_set_text(title,st.title);
            }
            else if(!strcmp(which,"artist"))songmenu_open_group("ARTIST","The Midnight","The Midnight",NULL);
            else if(!strcmp(which,"album"))songmenu_open_group("ALBUM","Afterglow","Afterglow",NULL);
            else if(!strcmp(which,"file"))songmenu_open_file("/preview","Northern Lights.flac",NULL);
            else if(!strcmp(which,"confirm"))fileops_confirm("Remove this track?","The file will stay on your SD card","Remove",NULL);
            for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        if(getenv("DISCO_OV")){                      /* fork: Disco - tap the title on Music: the track overlay */
            lv_obj_t *t=find_text(screen_get_root(SCR_HOME),st.title); assert(t); lv_obj_send_event(lv_obj_get_parent(t),LV_EVENT_SHORT_CLICKED,NULL);
            for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        if(getenv("DISCO_HOLD")){                    /* fork: Disco - hold the title (Title Hold: DISCO_HOLD=0 Immersive, 1 Favourite) */
            cfg_set_int_deferred("disco_title_hold",atoi(getenv("DISCO_HOLD")));
            lv_obj_t *t=find_text(screen_get_root(SCR_HOME),st.title); assert(t); lv_obj_send_event(lv_obj_get_parent(t),LV_EVENT_LONG_PRESSED,NULL);
            for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        if(getenv("DISCO_IMM")){ void ui_imm_style_apply(void); ui_imm_style_apply(); disco_open_np_immersive(); for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();} assert(ui_np_fsart_active()); }
        if(getenv("REVIEW_MA")){
            assert(!cfg_set_int("ma_on", 0));
            assert(!cfg_set_str("ma_server", "192.168.1.25"));
            void ma_edit_server(void); ma_edit_server();
            lv_obj_t *cancel = find_text(lv_layer_top(), LV_SYMBOL_CLOSE); assert(cancel);
            lv_obj_send_event(lv_obj_get_parent(cancel), LV_EVENT_CLICKED, NULL);
            assert(!strcmp(cfg_get_str("ma_server", ""), "192.168.1.25"));
            for(int i=0;i<5;i++){lv_tick_inc(10);lv_timer_handler();}
            ma_edit_server();
            lv_obj_t *modal = lv_obj_get_child(lv_layer_top(), -1);
            lv_obj_t *ta = lv_obj_get_child(modal, 0); assert(lv_obj_check_type(ta, &lv_textarea_class));
            lv_textarea_set_text(ta, "");
            lv_obj_t *save = find_text(modal, LV_SYMBOL_OK); assert(save);
            lv_obj_send_event(lv_obj_get_parent(save), LV_EVENT_CLICKED, NULL);
            assert(!strcmp(cfg_get_str("ma_server", "missing"), ""));
            puts("PASS: MA keyboard cancel preserves server; saved empty field selects Auto");
        }
        if(getenv("REVIEW_NO_TRACK")){
            track_state_t empty={0}; ipc_seed_state(&empty); ui_update(&empty);
            int before = screen_current();
            disco_open_np_immersive();
            assert(screen_current() == before && !ui_np_fsart_active());
            void shortcut_run(const char *); shortcut_run("immersive");
            assert(screen_current() == before && !ui_np_fsart_active());
            puts("PASS: no-track Immersive stays on the current Disco screen");
        }
        if(getenv("REVIEW_IMM_EXIT")){
            int ms = atoi(getenv("REVIEW_IMM_EXIT"));
            void ui_imm_style_apply(void); ui_imm_style_apply();
            screen_set_anim(getenv("REVIEW_ANIM_OFF") ? 0 : 1); disco_open_np_immersive();
            for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
            lv_obj_t *np=screen_get_root(SCR_NOWPLAYING);
            lv_obj_t *hint=find_text(np, "No synced lyrics for this track"); assert(hint);
            lv_obj_t *cover=lv_obj_get_parent(hint);
            ui_np_fsart_close();
            assert(screen_current()==SCR_HOME && !ui_np_fsart_active());
            for(int i=0;i<ms/5;i++){
                lv_tick_inc(5);lv_timer_handler();
                if(!lv_obj_has_flag(np,LV_OBJ_FLAG_HIDDEN)){
                    assert(!lv_obj_has_flag(cover,LV_OBJ_FLAG_HIDDEN));
                    assert(lv_obj_get_style_opa(cover,0)==LV_OPA_COVER);
                }
            }
            if(ms>=260) assert(lv_obj_has_flag(np,LV_OBJ_FLAG_HIDDEN));
            printf("PASS: Disco exit %dms keeps Ring covered until NP is hidden\n",ms);
        }
        if(getenv("REVIEW_BATTERY_LAYOUT")){
            lv_obj_t *root=screen_get_root(SCR_USAGE),*arc=NULL;
            lv_obj_update_layout(root);
            for(uint32_t j=0;j<lv_obj_get_child_count(root);j++){
                lv_obj_t *o=lv_obj_get_child(root,j);if(lv_obj_check_type(o,&lv_arc_class))arc=o;
            }
            assert(arc && lv_obj_get_width(arc)==344 && lv_obj_get_height(arc)==344);
            assert(lv_arc_get_rotation(arc)==(disco_mirror()?220:40));
            assert(lv_arc_get_bg_angle_start(arc)==0 && lv_arc_get_bg_angle_end(arc)==280);
            assert(lv_arc_get_mode(arc)==(disco_mirror()?LV_ARC_MODE_REVERSE:LV_ARC_MODE_NORMAL));
            lv_obj_t *title=find_text(root,"Battery");assert(title);
            lv_area_t a;lv_obj_get_coords(title,&a);assert(abs((a.x1+a.x2+1)/2-180)<=1);
            const char *caps[]={"Playback","Screen on"};
            for(int j=0;j<2;j++){
                lv_obj_t *l=find_text(root,caps[j]);assert(l);lv_obj_t *pill=lv_obj_get_parent(l);
                assert(lv_obj_get_index(pill)<lv_obj_get_index(arc));
                lv_point_t size;lv_text_get_size(&size,caps[j],lv_obj_get_style_text_font(l,0),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
                assert(size.x<=lv_obj_get_width(pill));
            }
            const int pct[]={0,15,67,100};
            for(int charging=0;charging<2;charging++)for(int j=0;j<4;j++){
                usage_note_battery(pct[j],charging);usage_refresh();assert(lv_arc_get_value(arc)==pct[j]);
                lv_color_t want=charging?TC(STATUS_SUCCESS):pct[j]<=15?TC(STATUS_DANGER):th_braun()?TC(ACCENT_PRIMARY):ui_current_accent();
                assert(lv_color_eq(lv_obj_get_style_arc_color(arc,LV_PART_INDICATOR),want));
            }
            puts("PASS: Battery geometry, mirrored sweep, centred title, pill layering and charge states");
        }
        if(getenv("REVIEW_MODES")){
            static const char *names[]={"Playback","USB storage","Bluetooth DAC","USB DAC","AirPlay"};
            lv_obj_t *root=screen_get_root(SCR_WORKMODE); lv_obj_update_layout(root);
            for(int i=0;i<5;i++){
                lv_obj_t *label=find_text(root,names[i]); assert(label);
                assert(lv_obj_get_style_text_font(label,0)==TF(UI_22));
                lv_obj_t *row=lv_obj_get_parent(label); assert(lv_obj_get_height(row)==56);
                lv_point_t size; lv_text_get_size(&size,names[i],lv_obj_get_style_text_font(label,0),lv_obj_get_style_text_letter_space(label,0),0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
                assert(size.x<=lv_obj_get_width(label));
                assert(lv_obj_get_width(row)>130);
                lv_area_t area;lv_obj_get_coords(row,&area);
                assert(disco_mirror()?area.x2<292:area.x1>=68);
                assert(lv_obj_get_width(row)<=260);
            }
            lv_obj_t *last=lv_obj_get_parent(find_text(root,"AirPlay"));
            assert(lv_obj_get_y(last)+lv_obj_get_height(last)==342);
            puts("PASS: Modes 56px rows, labels fit, protected edge gutter, shortened caps, bottom y=342");
        }
        if(getenv("REVIEW_IMM_REOPEN")){
            screen_set_anim(1);
            for(int j=0;j<8;j++){
                disco_open_np_immersive(); assert(ui_np_fsart_active());
                for(int i=0;i<5;i++){lv_tick_inc(10);lv_timer_handler();}
                ui_np_fsart_close();
                for(int i=0;i<3;i++){lv_tick_inc(10);lv_timer_handler();}
            }
            disco_open_np_immersive();
            for(int i=0;i<50;i++){lv_tick_inc(10);lv_timer_handler();}
            lv_obj_t *hint=find_text(screen_get_root(SCR_NOWPLAYING),"No synced lyrics for this track"); assert(hint);
            assert(ui_np_fsart_active() && !lv_obj_has_flag(lv_obj_get_parent(hint),LV_OBJ_FLAG_HIDDEN));
            puts("PASS: rapid Immersive close/reopen cancels stale hide callbacks");
        }
        if(getenv("REVIEW_STANDARD_EXIT")){
            screen_show(SCR_NOWPLAYING); ui_np_fsart_open(); assert(ui_np_fsart_active());
            for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
            ui_np_fsart_close();
            for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
            assert(screen_current()==SCR_NOWPLAYING && !ui_np_fsart_active());
            puts("PASS: Ring/Braun Immersive still returns to its own Now Playing screen");
        }
        if(getenv("DISCO_NAV")){                 /* DISCO_NAV=1 open; =o<ms> opening after ms; =c<ms> closing after ms */
            const char *nv=getenv("DISCO_NAV"); int ms=atoi(nv+1);
            disco_nav_set_open(1);
            if(nv[0]=='o'){ for(int i=0;i<ms/5;i++){lv_tick_inc(5);lv_timer_handler();} }
            else { for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();}
                   if(nv[0]=='c'){ disco_nav_set_open(0); for(int i=0;i<ms/5;i++){lv_tick_inc(5);lv_timer_handler();} } }
        }
        if(getenv("OPTS_SCROLL")){ void settings_test_scroll(int); settings_test_scroll(atoi(getenv("OPTS_SCROLL"))); for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("PERF")){                          /* fork: full-screen redraw cost of this page (ms per frame, host) */
            struct timespec t0,t1; int n=40; clock_gettime(CLOCK_MONOTONIC,&t0);
            for(int i=0;i<n;i++){ lv_obj_invalidate(lv_screen_active()); lv_obj_invalidate(lv_layer_top()); lv_refr_now(NULL); }
            clock_gettime(CLOCK_MONOTONIC,&t1);
            fprintf(stderr,"PERF full %.2f ms/frame\n",((t1.tv_sec-t0.tv_sec)*1e3+(t1.tv_nsec-t0.tv_nsec)/1e6)/n);
            clock_gettime(CLOCK_MONOTONIC,&t0);
            for(int i=0;i<200;i++){ lv_tick_inc(1000); lv_timer_handler(); }   /* 200 idle seconds: what the timers redraw */
            clock_gettime(CLOCK_MONOTONIC,&t1);
            fprintf(stderr,"PERF idle %.2f ms/s\n",((t1.tv_sec-t0.tv_sec)*1e3+(t1.tv_nsec-t0.tv_nsec)/1e6)/200);
        }
        if(getenv("MODES_PENDING")){ void modes_test_pending(int); modes_test_pending(atoi(getenv("MODES_PENDING"))); for(int i=0;i<25;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("KB")){ int pg=atoi(getenv("KB")); kbinput_open("Password for Home", pg==9?"Sn0wsky!":"", NULL); for(int i=0;i<pg && pg<9;i++){ extern void kbinput_test_page(void); kbinput_test_page(); } for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("FB_AZ")){ sd_io_init(media_local); sd_io_resume(); void folderbrowser_open(void); folderbrowser_open(); scr=SCR_FOLDER;
                             for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();usleep(2000);}
                             if(getenv("FB_AZ")[0]>='A'){ void folderbrowser_test_jump(char); folderbrowser_test_jump(getenv("FB_AZ")[0]); }
                             for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("MA_STATE")){ void ma_test_state(int); scr=SCR_MA; screen_show(scr); ma_test_state(atoi(getenv("MA_STATE"))); for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("MA_DELAY")){ void settings_open_ma_delay(void); cfg_set_int("ma_delay_ms",atoi(getenv("MA_DELAY"))); setlist_open("Network"); settings_open_ma_delay(); scr=SCR_SETTING_DETAIL; for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("VK_SET")){ void settings_open_volkeys(void); setlist_open("System"); scr=SCR_SETLIST; screen_show(scr); settings_open_volkeys(); for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("MA_SET")){ void settings_open_ma(void); setlist_open("Network"); scr=SCR_SETLIST; screen_show(scr); settings_open_ma(); for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("MODES_INFO")){ void modes_test_info(int); scr=SCR_MODEINFO; screen_show(scr); modes_test_info(atoi(getenv("MODES_INFO"))); for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("BACK_HINT")){ ui_back_hint(180, atoi(getenv("BACK_HINT"))); for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("HOME_HINT")){ ui_home_hint(180, atoi(getenv("HOME_HINT"))); for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("DISCO_MENU")){ disco_menu_open(); for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("NP_GO")){ if(!strcmp(getenv("NP_GO"),"artist")) ui_np_open_artist(); else ui_np_open_album(); scr=SCR_LIBRARY; for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("DISCO_FUZZ")){ void disco_test_tap(int); srand(atoi(getenv("DISCO_FUZZ"))); for(int it=0;it<600;it++){ int r=rand()%10; if(r<2) disco_nav_set_open(1); else if(r<3) disco_nav_set_open(0); else disco_test_tap(rand()%4); int t=rand()%30; for(int i=0;i<t;i++){lv_tick_inc(10);lv_timer_handler();} } }
        if(getenv("DISCO_EQ"))cfg_set_int_deferred("disco_eq",atoi(getenv("DISCO_EQ")));
        if(getenv("DISCO_VOL"))cfg_set_int_deferred("disco_vol",atoi(getenv("DISCO_VOL")));
        if(getenv("EQSEL")){ void eqcustom_test_select(int); eqcustom_test_select(atoi(getenv("EQSEL"))); for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("REVIEW_PRESS")){
            lv_obj_t *label=find_visible_text(screen_get_root(scr),getenv("REVIEW_PRESS")); assert(label);
            lv_obj_t *button=lv_obj_get_parent(label);
            assert(button!=screen_get_root(scr));
            lv_opa_t resting=lv_obj_get_style_bg_opa(button,0);
            lv_obj_add_state(button,LV_STATE_PRESSED);
            assert(lv_obj_get_style_bg_opa(button,0)==LV_OPA_COVER);
            assert(lv_obj_get_style_border_width(button,0)>0);
            lv_obj_remove_state(button,LV_STATE_PRESSED);
            assert(lv_obj_get_style_bg_opa(button,0)==resting);
            lv_obj_add_state(button,LV_STATE_PRESSED); /* leave held for the render */
            puts("PASS: pressed feedback restores on release");
        }
        if(getenv("REVIEW_QUEUE_POSITION")){
            assert(queue_count()==30);lv_obj_t *root=screen_get_root(SCR_QUEUE),*list=NULL,*summary=NULL;
            for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){
                lv_obj_t *c=lv_obj_get_child(root,i);
                if(lv_obj_get_child_count(c)>20)list=c;
                if(lv_obj_check_type(c,&lv_label_class) && strstr(lv_label_get_text(c),"up next"))summary=c;
            }
            assert(list && summary);char original[64];snprintf(original,sizeof original,"%s",lv_label_get_text(summary));
            lv_obj_scroll_to_y(list,700,LV_ANIM_OFF);lv_obj_send_event(list,LV_EVENT_SCROLL,NULL);
            assert(strstr(lv_label_get_text(summary)," / 30 up next"));
            lv_obj_send_event(list,LV_EVENT_SCROLL_END,NULL);
            for(int i=0;i<110;i++){lv_tick_inc(10);lv_timer_handler();}
            assert(!strcmp(original,lv_label_get_text(summary)));
            puts("PASS: long queue position restores the summary after scrolling");
        }
        if(getenv("REVIEW_QUEUE_DISABLED")){
            assert(queue_count()==0);
            lv_obj_t *root=screen_get_root(SCR_QUEUE);int disabled=0;
            for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){
                lv_obj_t *child=lv_obj_get_child(root,i);
                if(lv_obj_has_state(child,LV_STATE_DISABLED)){assert(lv_obj_get_style_opa(child,0)<LV_OPA_COVER);disabled++;}
            }
            assert(disabled==3);
            puts("PASS: empty queue disables and dims all three controls");
        }
        if(getenv("REVIEW_ICON_GLYPHS")){
            assert_visible_icon_glyphs(lv_layer_top());
            puts("PASS: enlarged popup icons resolve to real glyphs");
        }
        if(getenv("REVIEW_NP_FONT")){
            const lv_font_t *titlefont=ui_np_font(28),*artistfont=ui_np_font(20);
            for(int i=0;i<2;i++){
                const lv_font_t *font=i?artistfont:titlefont;
                unsigned points[]={0x4e16,0xe9,0x410,0x3a9,0x2019};
                for(unsigned j=0;j<sizeof points/sizeof points[0];j++){
                    lv_font_glyph_dsc_t d={0};assert(lv_font_get_glyph_dsc(font,&d,points[j],0));assert(!d.is_placeholder);
                }
            }
            puts("PASS: alternate Now Playing fonts preserve international/CJK fallback");
        }
        if(getenv("REVIEW_QUEUE_CLEAR")){
            int n=queue_count(); assert(n>0);
            lv_obj_t *clear=find_visible_text(screen_get_root(SCR_QUEUE),"Clear"); assert(clear);
            lv_obj_t *button=lv_obj_get_parent(clear);
            lv_obj_send_event(button,LV_EVENT_CLICKED,NULL);
            assert(queue_count()==n && !strcmp(lv_label_get_text(clear),"Sure?"));
            lv_obj_send_event(button,LV_EVENT_CLICKED,NULL);
            assert(!queue_count());
            assert(find_visible_text(screen_get_root(SCR_QUEUE),st.title));
            puts("PASS: Queue Clear requires confirmation and keeps the current track visible");
        }
        if(getenv("REVIEW_TRACK_INFO")){
            lv_obj_t *info=ui_action_find("disco.track.info"); assert(info);
            lv_obj_send_event(info,LV_EVENT_CLICKED,NULL);
            for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
            assert(screen_current()==SCR_SONGINFO);
            assert(find_visible_text(screen_get_root(SCR_SONGINFO),st.title));
            assert(!ui_action_find("disco.track.info"));
            lv_obj_send_event(lv_obj_get_parent(find_visible_text(screen_get_root(SCR_SONGINFO),"Song Info")),LV_EVENT_CLICKED,NULL);
            for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
            assert(screen_current()==SCR_HOME);
            puts("PASS: Disco popup Info opens current-track details and Back returns to Music");
        }
        if(getenv("VOLSHOW")){ if(getenv("VOL_FRAME_MS"))screen_set_anim(1);void ui_show_volume(int);ui_show_volume(atoi(getenv("VOLSHOW")));nav_wait(getenv("VOL_FRAME_MS")?atoi(getenv("VOL_FRAME_MS")):200);
            if(getenv("VOL_CLOSE_MS")){lv_obj_t *close=ui_action_find("null.volume.dismiss");assert(close);lv_obj_send_event(close,LV_EVENT_CLICKED,NULL);nav_wait(atoi(getenv("VOL_CLOSE_MS")));}
        }
        if(getenv("TICKS")){ int ms=atoi(getenv("TICKS")); for(int i=0;i<ms/20;i++){lv_tick_inc(20);lv_timer_handler();} }
        if(!strncmp(page,"immersive",9)){
            int extra=atoi(page+9);
            for(int i=0;i<extra/10;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        if(scr==SCR_SAVER){
            if(th_null()){assert_visible_icon_glyphs(screen_get_root(scr));assert(lv_font_get_line_height(lv_obj_get_style_text_font(find_text(screen_get_root(scr),"10:09"),0))>=70);}
            char saver_title[192];theme_upper(saver_title,sizeof saver_title,st.title);
            lv_obj_t *title=find_text(screen_get_root(scr),th_null()?saver_title:st.title), *artist=find_text(screen_get_root(scr),st.artist);
            assert(title && artist);
            assert(lv_label_get_long_mode(title)==(th_simple()?LV_LABEL_LONG_DOT:LV_LABEL_LONG_SCROLL_CIRCULAR));
            assert(lv_label_get_long_mode(artist)==(th_simple()?LV_LABEL_LONG_DOT:LV_LABEL_LONG_SCROLL_CIRCULAR));
            int extra=atoi(page+5);
            for(int i=0;i<extra/10;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        goto render;
    }
    if(!strcmp(argv[3],"actions")){
        cfg_set_int_deferred("ota_allow",1);
        assert(!ota_allowed());
        ota_prog_t pg={0};
        assert(ota_stage(NULL,"1.2.0","v9.9.9",&pg)==OTA_E_OFF);
        screen_set_anim(0); screens_init();
        assert(screen_current()==SCR_HOME);
        lv_obj_t *fav=ui_action_find("home.favourites"), *up=ui_action_find("home.upnext");
        lv_obj_t *queue=ui_action_find("np.queue");
        assert(!fav && up && queue);
        for(int enabled=0; enabled<=1; enabled++){
            cfg_set_int_deferred("up_next",enabled); home_shortcuts_refresh();
            lv_obj_send_event(up,LV_EVENT_CLICKED,NULL);
            assert(screen_current()==(enabled ? SCR_UPNEXT : SCR_HOME));
            if(enabled) screen_back();
            screen_show(SCR_NOWPLAYING);
            lv_obj_send_event(queue,LV_EVENT_SHORT_CLICKED,NULL); assert(screen_current()==SCR_QUEUE);
            screen_back(); assert(screen_current()==SCR_NOWPLAYING);
            lv_obj_send_event(queue,LV_EVENT_LONG_PRESSED,NULL);
            assert(screen_current()==(enabled ? SCR_UPNEXT : SCR_QUEUE));
            /* LVGL emits CLICKED after a hold; it must not undo the chosen long-press route. */
            lv_obj_send_event(queue,LV_EVENT_CLICKED,NULL);
            assert(screen_current()==(enabled ? SCR_UPNEXT : SCR_QUEUE));
            screen_back(); screen_back(); assert(screen_current()==SCR_HOME);
        }
        puts("PASS: Home toggle/navigation and Now Playing queue/Up Next event routing");
        return 0;
    }
    lv_obj_t *root=lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root); lv_obj_set_size(root,360,360);
    lv_obj_set_style_radius(root,LV_RADIUS_CIRCLE,0); lv_obj_set_style_clip_corner(root,true,0);
    if(!strcmp(argv[3],"home")){
        home_create(root); home_set_clock("10:09","Saturday, 3 October");
        home_set_now_playing("Long title to check the round panel", "Artist", TC(ACCENT_PRIMARY), false);
        home_set_status(67,0,1,1);
        lv_obj_update_layout(root);
        lv_obj_t *fav=ui_action_find("home.favourites"), *up=ui_action_find("home.upnext");
        assert(!fav && up); assert_round_target(up);
        int enabled=cfg_get_int("up_next",0);
        assert(lv_obj_has_flag(up,LV_OBJ_FLAG_HIDDEN)==!enabled);
        cfg_set_int_deferred("up_next",!enabled); home_shortcuts_refresh();
        assert(lv_obj_has_flag(up,LV_OBJ_FLAG_HIDDEN)==enabled);
        cfg_set_int_deferred("up_next",enabled); home_shortcuts_refresh();
    } else if(!strcmp(argv[3],"np") || !strcmp(argv[3],"immersive")){
        ui_create(root);
        assert(ui_action_find("np.queue")); assert(ui_action_find("np.queue.long"));
    } else return 2;
    if(!strcmp(argv[3],"immersive")){
        sd_io_init(media_local); assert(sd_io_resume());
        cfg_set_int_deferred("online_lyrics",0);
        cfg_set_int_deferred("online_art",0);
        cfg_set_int_deferred("accent_mode",1);
        ui_set_np_style(2);
        track_state_t st={0};
        snprintf(st.path,sizeof st.path,"%s",argv[5]);
        char *dot=strrchr(st.path,'.'); assert(dot); strcpy(dot,".png");
        snprintf(st.title,sizeof st.title,"Immersive cover test");
        snprintf(st.artist,sizeof st.artist,"Artist");
        st.have_track=1; st.state=1; st.duration_ms=180000; st.position_ms=1500;
        ipc_seed_state(&st);
        ui_take_art_applied(); ui_update(&st);
        int loaded=0;
        for(int i=0;i<500;i++){
            ui_art_poll(NULL);
            if(ui_current_cover_dsc()){ loaded=1; break; }
            usleep(10000);
        }
        assert(loaded);
        ui_np_fsart_open(); assert(ui_np_fsart_active());
        for(int i=0;i<80;i++){
            lv_tick_inc(10); lv_timer_handler(); lyrics_poll(NULL); usleep(1000);
        }
        assert(find_sharp_image(root));
        lyr_line_t lines[8]; unsigned rev;
        assert(lyrics_immersive_load(lines,8,&rev)==2);
        assert(!strcmp(lines[0].text,"First lyric"));
        ui_np_fsart_close(); assert(!ui_np_fsart_active());
        ui_np_fsart_open(); assert(ui_np_fsart_active());
        for(int i=0;i<30;i++){ lv_tick_inc(10); lv_timer_handler(); }
        puts("PASS: real bounded decoder feeds 364px immersive image and async timed lyrics; close/reopen works");
    }
render:
    if(getenv("IMM_IN")){ void ui_test_imm_in(int); ui_test_imm_in(atoi(getenv("IMM_IN"))); }
    if(getenv("IMM_OUT")){ void ui_test_imm_out(int); ui_test_imm_out(atoi(getenv("IMM_OUT"))); }
    if(getenv("IMM_PARTS")){ void dhome_test_imm_parts(int); dhome_test_imm_parts(atoi(getenv("IMM_PARTS"))); }
    if(getenv("NP_ZOOM")){ void dhome_test_zoom(int); dhome_test_zoom(atoi(getenv("NP_ZOOM"))); for(int i=0;i<5;i++){lv_tick_inc(10);lv_timer_handler();} }
    if(getenv("TOPHOLD")){ void dhome_test_top_hold(void); for(int k=0;k<atoi(getenv("TOPHOLD"));k++) dhome_test_top_hold(); for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();} }
    if(getenv("SETKEY")){ void settings_open_key(const char*); settings_open_key(getenv("SETKEY")); for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();} }
    if(getenv("IDLE")){ void dhome_set_now_playing(const char*,const char*,lv_color_t,bool); void disco_art_changed(void);   /* Disco Music with nothing playing */
        dhome_set_now_playing(NULL,NULL,lv_color_hex(0x8080FF),false); for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
    if(getenv("REVIEW_TRACK_POPUP")){
        extern void disco_track_overlay_open(void);
        disco_track_overlay_open(); assert(disco_track_overlay_visible());
        lv_obj_t *ov=lv_obj_get_child(lv_layer_top(),-1);
        lv_obj_t *tb=lv_obj_get_child(ov,0);
        lv_obj_t *title=lv_obj_get_child(tb,0),*artist=lv_obj_get_child(tb,1);
        lv_label_set_text(title,"An exceptionally long track title that must continue scrolling in the track popup forever");
        lv_label_set_text(artist,"An exceptionally long artist name and collaborators that must continue scrolling forever");
        kit_pass(ov); nav_wait(200);
        for(int i=0;i<2;i++){
            lv_obj_t *label=i?artist:title;
            assert(lv_label_get_long_mode(label)==LV_LABEL_LONG_SCROLL_CIRCULAR);
            assert(lv_obj_get_style_anim(label,0)==NULL); /* LVGL default repeats forever. */
        }
        if(th_simple()){assert(lv_obj_get_width(title)==220);assert(lv_obj_get_width(artist)==210);}
        nav_wait(120000); kit_restyle(ov);
        assert(lv_label_get_long_mode(title)==LV_LABEL_LONG_SCROLL_CIRCULAR);
        assert(lv_label_get_long_mode(artist)==LV_LABEL_LONG_SCROLL_CIRCULAR);
        puts("PASS: popup title and artist retain infinite scrolling after two minutes and theme passes");
    }
    if(th_null()&&getenv("NULL_POPUP")){extern void disco_track_overlay_open(void);disco_track_overlay_open();}
    if(th_null()&&getenv("NULL_NAV")){null_nav_set_open(atoi(getenv("NULL_NAV")));for(int i=0;i<24;i++){lv_tick_inc(10);lv_timer_handler();}}
    lv_refr_now(d);
    if(getenv("REVIEW_LIBRARY_FOCUS_AUDIT")){
        assert(mdb_load()==40);screen_set_anim(0);screen_show(SCR_LIBRARY);
        library_open_album_focus("Focus fixture","Focus Artist","/fixture/focus/01.flac");nav_wait(400);
        lv_obj_t*list=library_scroller();lv_obj_t*text=find_text(list,"01 Focus audit track with a long title");assert(text);
        lv_obj_t*row=lv_obj_get_parent(text);assert(lv_obj_get_style_bg_opa(row,0)==LV_OPA_COVER);
        printf("FOCUS before manual scroll: state=%u background=%u\n",(unsigned)lv_obj_get_state(row),(unsigned)lv_obj_get_style_bg_opa(row,0));
        lv_indev_t*in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,nav_read);lv_timer_set_period(lv_indev_get_read_timer(in),10);
        nav_pointer=(lv_indev_data_t){.point={180,260},.state=LV_INDEV_STATE_PRESSED};nav_wait(50);
        for(int y=260;y>=220;y-=5){nav_pointer.point.y=y;nav_wait(20);}
        nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(300);assert(lv_obj_get_scroll_y(list)>0);
        printf("FOCUS after manual scroll: state=%u background=%u\n",(unsigned)lv_obj_get_state(row),(unsigned)lv_obj_get_style_bg_opa(row,0));
        assert(lv_obj_get_style_bg_opa(row,0)==LV_OPA_COVER);
        puts("CONFIRMED: jump-to-track filled highlight persists after manual scrolling, independently of LV_STATE_FOCUSED");return 0;
    }
    if(th_simple()&&getenv("REVIEW_SIMPLE_MENU_REFLOW")){
        char id[33];const char*name="A Very Long Album Discovery Application";md5_hex("__squash_reflow_fixture",strlen("__squash_reflow_fixture"),id);
        cfg_set_str_deferred("simple_mnu","0ms");cfg_set_str_deferred("sm_app0",id);simple_nav_refresh();simple_nav_open(1);nav_wait(300);simple_test_nav_phase(1000);lv_obj_update_layout(screen_get_root(SCR_HOME));
        lv_obj_t*l=find_visible_text(lv_screen_active(),name);assert(l&&lv_obj_get_height(l)==54&&lv_label_get_long_mode(l)==LV_LABEL_LONG_WRAP);lv_area_t area;lv_obj_get_coords(l,&area);assert(area.y2<280&&area.x1>=0&&area.x2<360);
        puts("PASS: long Squash launcher name wraps into bounded text above D! without per-frame rebinding");
    }
    if(getenv("REVIEW_QS_EQ")){
        lv_obj_t *quicksettings_test_button(int);
        fixture=1;send_result=0;qs_wifi=0;qs_bt=BT_OFF;
        int eq=quicksettings_equalizer_item(),ids[]={eq,0,1,5};assert(quicksettings_items_save(ids,4)==0);
        cfg_set_int_deferred("eq_preset",0);cfg_set_int_deferred("eq_last",11);cfg_set_str_deferred("eq_name11","Warm analogue");
        screen_show(SCR_QUICK);nav_wait(500);lv_obj_t*button=quicksettings_test_button(0);assert(button);
        lv_event_code_t tap=th_null()?LV_EVENT_CLICKED:LV_EVENT_SHORT_CLICKED;
        int before=send_count;lv_obj_send_event(button,tap,NULL);nav_wait(150);
        assert(screen_current()==SCR_QUICK&&send_count==before+1&&cfg_get_int("eq_preset",0)==11&&quicksettings_item_active(eq));
        assert(find_visible_text(lv_layer_top(),"EQ On\nPreset: Warm analogue"));
        lv_obj_t*glyph=find_text(button,"EQ");assert(glyph);
        if(th_simple())assert(lv_color_eq(lv_obj_get_style_bg_color(button,0),TC(ACCENT_PRIMARY)));
        if(th_null())assert(lv_color_eq(lv_obj_get_style_text_color(glyph,0),TC(TEXT_PRIMARY)));
        if(th_disco())assert(lv_color_eq(lv_obj_get_style_text_color(glyph,0),ui_current_accent()));
        lv_obj_send_event(button,tap,NULL);nav_wait(150);assert(cfg_get_int("eq_preset",-1)==0&&!quicksettings_item_active(eq)&&cfg_get_int("eq_last",0)==11);assert(find_visible_text(lv_layer_top(),"EQ Off\nPreset: Warm analogue"));
        if(th_simple())assert(!lv_color_eq(lv_obj_get_style_bg_color(button,0),TC(ACCENT_PRIMARY)));
        if(th_null())assert(lv_color_eq(lv_obj_get_style_text_color(glyph,0),TC(TEXT_SECONDARY)));
        if(th_disco())assert(lv_color_eq(lv_obj_get_style_text_color(glyph,0),TC(TEXT_PRIMARY)));
        send_result=-1;lv_obj_send_event(button,tap,NULL);nav_wait(150);assert(cfg_get_int("eq_preset",-1)==0&&!quicksettings_item_active(eq));assert(find_visible_text(lv_layer_top(),"Couldn't switch EQ"));send_result=0;
        cfg_set_int_deferred("eq_last",999);lv_obj_send_event(button,tap,NULL);nav_wait(150);assert(cfg_get_int("eq_preset",0)==1&&quicksettings_item_active(eq));assert(find_visible_text(lv_layer_top(),"EQ On\nPreset: Jazz"));
        before=send_count;lv_obj_send_event(button,LV_EVENT_LONG_PRESSED,NULL);nav_wait(350);assert(screen_current()==SCR_EQ&&send_count==before&&cfg_get_int("eq_preset",0)==1);
        screen_back();nav_wait(350);assert(screen_current()==SCR_QUICK);assert(ui_eq_select(2)==0);quicksettings_refresh(1);nav_wait(150);assert(quicksettings_item_active(eq));
        puts("PASS: QS EQ tap toggles last preset, custom-name On/Off popup, active styling, failure preservation, invalid-preset fallback and Hold opens EQ without toggling");return 0;
    }
    if(th_simple()&&getenv("REVIEW_SQUASH_VOLUME")){
        fixture=1;send_result=0;screen_set_anim(1);
        lv_timer_t*old_timers[256];int old_n=0;
        for(lv_timer_t*t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t)){assert(old_n<256);old_timers[old_n++]=t;}
        ui_show_volume(72);nav_wait(110);assert(ui_volume_overlay_visible());
        lv_timer_t*dismiss=NULL,*retry=NULL;
        for(lv_timer_t*t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t)){
            int old=0;for(int i=0;i<old_n;i++)if(old_timers[i]==t)old=1;
            if(!old&&t->period==1800)dismiss=t;if(!old&&t->period==120)retry=t;
        }
        assert(dismiss&&retry&&!lv_timer_get_paused(dismiss)&&lv_timer_get_paused(retry));
        lv_obj_t*number=find_visible_text(lv_layer_top(),"72");assert(number);lv_obj_t*sheet=lv_obj_get_parent(number),*arc=NULL;for(uint32_t i=0;i<lv_obj_get_child_count(sheet);i++){lv_obj_t*o=lv_obj_get_child(sheet,i);if(lv_obj_check_type(o,&lv_arc_class))arc=o;}assert(arc&&lv_arc_get_max_value(arc)==120);assert(lv_obj_get_y(sheet)==0);nav_wait(100);
        int before=send_count;lv_arc_set_value(arc,80);lv_obj_send_event(arc,LV_EVENT_VALUE_CHANGED,NULL);assert(send_count==before+1);lv_arc_set_value(arc,81);lv_obj_send_event(arc,LV_EVENT_VALUE_CHANGED,NULL);assert(send_count==before+1);ui_show_volume(72);assert(lv_arc_get_value(arc)==81);lv_obj_send_event(arc,LV_EVENT_PRESS_LOST,NULL);assert(send_count==before+2);assert(find_visible_text(lv_layer_top(),"81"));
        before=send_count;send_result=-1;lv_arc_set_value(arc,82);lv_obj_send_event(arc,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_send_event(arc,LV_EVENT_RELEASED,NULL);nav_wait(130);assert(send_count>=before+2);send_result=0;nav_wait(150);int sent=send_count;assert(sent>=before+3);nav_wait(150);assert(send_count==sent);
        ui_show_volume(83);assert(lv_arc_get_value(arc)==83&&find_visible_text(lv_layer_top(),"83"));
        before=send_count;send_result=-1;lv_arc_set_value(arc,84);lv_obj_send_event(arc,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_send_event(arc,LV_EVENT_RELEASED,NULL);nav_wait(700);assert(send_count==before+4);nav_wait(250);assert(send_count==before+4);send_result=0;
        assert(lv_timer_get_paused(retry));
        /* A failed final send survives dismissal and stops retrying after success. */
        before=send_count;send_result=-1;lv_arc_set_value(arc,85);lv_obj_send_event(arc,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_send_event(arc,LV_EVENT_RELEASED,NULL);
        assert(!lv_timer_get_paused(retry));lv_obj_send_event(lv_obj_get_parent(sheet),LV_EVENT_CLICKED,NULL);nav_wait(120);assert(!ui_volume_overlay_visible());
        send_result=0;nav_wait(160);assert(send_count>before&&lv_timer_get_paused(retry)&&lv_timer_get_paused(dismiss));
        ui_show_volume(86);nav_wait(110);assert(lv_arc_get_value(arc)==86);
        /* Failed sends do not fight an ongoing drag. Release sends its latest target. */
        send_result=-1;lv_obj_add_state(arc,LV_STATE_PRESSED);lv_arc_set_value(arc,87);lv_obj_send_event(arc,LV_EVENT_VALUE_CHANGED,NULL);
        before=send_count;nav_wait(500);assert(send_count==before);ui_show_volume(70);assert(lv_arc_get_value(arc)==87);
        send_result=0;lv_obj_remove_state(arc,LV_STATE_PRESSED);lv_obj_send_event(arc,LV_EVENT_RELEASED,NULL);assert(send_count==before+1&&lv_timer_get_paused(retry));
        ui_show_volume(88);assert(lv_arc_get_value(arc)==88);nav_wait(100);
        lv_obj_add_state(arc,LV_STATE_PRESSED);nav_wait(2200);assert(ui_volume_overlay_visible());lv_obj_remove_state(arc,LV_STATE_PRESSED);lv_obj_send_event(arc,LV_EVENT_RELEASED,NULL);nav_wait(2000);assert(!ui_volume_overlay_visible());
        ui_show_volume(999);nav_wait(180);assert(lv_arc_get_value(arc)==120);ui_show_volume(-5);assert(lv_arc_get_value(arc)==0);
        lv_indev_t*in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,nav_read);lv_timer_set_period(lv_indev_get_read_timer(in),10);
        nav_pointer=(lv_indev_data_t){.point={90,256},.state=LV_INDEV_STATE_PRESSED};nav_wait(60);for(int x=90;x<=270;x+=10){nav_pointer.point.x=x;nav_pointer.point.y=238+(x-180)*(x-180)/460;nav_wait(20);}nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(50);assert(lv_arc_get_value(arc)>80&&ui_volume_overlay_visible());
        nav_touch((lv_point_t){180,316},50);nav_wait(180);assert(!ui_volume_overlay_visible());assert(screen_current()==SCR_HOME&&!simple_nav_is_open());
        ui_show_volume(60);nav_wait(180);nav_touch((lv_point_t){180,120},50);nav_wait(180);assert(!ui_volume_overlay_visible());
        ui_show_volume(60);nav_wait(180);number=find_visible_text(lv_layer_top(),"60");assert(number);kit_pass(lv_obj_get_parent(lv_obj_get_parent(number)));assert(lv_obj_get_style_bg_opa(lv_obj_get_parent(lv_obj_get_parent(number)),0)==0);
        /* Fast close/reopen cannot let an old completion hide the reopened overlay. */
        for(int anim=0;anim<=1;anim++){
            screen_set_anim(anim);
            for(int k=0;k<12;k++){
                ui_show_volume(40+k);nav_wait(anim?160:20);lv_obj_update_layout(sheet);assert(lv_obj_get_y(sheet)==0&&ui_volume_overlay_visible());
                lv_obj_send_event(lv_obj_get_parent(sheet),LV_EVENT_CLICKED,NULL);nav_wait(anim?20:2);
                ui_show_volume(60+k);nav_wait(anim?160:20);lv_obj_update_layout(sheet);assert(ui_volume_overlay_visible()&&lv_obj_get_y(sheet)==0);
            }
            lv_obj_send_event(lv_obj_get_parent(sheet),LV_EVENT_CLICKED,NULL);nav_wait(anim?120:20);assert(!ui_volume_overlay_visible());
        }
        assert(lv_timer_get_paused(retry)&&lv_timer_get_paused(dismiss));
        puts("PASS: Squash volume B native arc drag, 0..120 clamp, throttled final commit, immediate player reports, hold timeout, logo/outside dismissal and transparent scrim");return 0;
    }
    if(th_simple()&&getenv("REVIEW_SQUASH_HOLD")){
        lv_indev_t*in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,nav_read);lv_timer_set_period(lv_indev_get_read_timer(in),10);simple_cfg_set_int_deferred("disco_nav_v",2);
        const lv_point_t targets[]={{180,160},{180,310}};
        for(int target=0;target<2;target++){
            screen_home();simple_cfg_set_int_deferred("disco_nav_hold",DISCO_NAV_SEARCH);simple_nav_open(1);nav_wait(250);nav_touch(targets[target],850);nav_wait(400);assert(screen_current()==SCR_SEARCH&&!simple_nav_is_open());
            screen_home();simple_cfg_set_int_deferred("disco_nav_hold",DISCO_NAV_QUICK);simple_nav_open(1);nav_wait(250);nav_touch(targets[target],850);nav_wait(400);assert(screen_current()==SCR_QUICK&&!simple_nav_is_open());
            screen_home();simple_cfg_set_int_deferred("disco_nav_hold",DISCO_NAV_PLAYPAUSE);simple_nav_open(1);nav_wait(250);int before=null_transport_calls;nav_touch(targets[target],850);nav_wait(400);assert(null_transport_calls==before+1&&!strcmp(null_last_command,"0201000C0000"));assert(!simple_nav_is_open());
            simple_cfg_set_int_deferred("disco_nav_hold",DISCO_NAV_NONE);simple_nav_open(1);nav_wait(250);before=null_transport_calls;nav_touch(targets[target],850);nav_wait(400);assert(screen_current()==SCR_HOME&&simple_nav_is_open()&&null_transport_calls==before);simple_nav_open(0);nav_wait(250);
        }
        simple_cfg_set_int_deferred("disco_nav_hold",DISCO_NAV_SEARCH);simple_nav_open(1);nav_wait(250);nav_pointer=(lv_indev_data_t){.point={240,162},.state=LV_INDEV_STATE_PRESSED};nav_wait(40);for(int x=240;x>=145;x-=5){nav_pointer.point.x=x;nav_wait(10);}nav_wait(850);nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(250);assert(screen_current()==SCR_HOME&&simple_nav_is_open());simple_nav_open(0);nav_wait(250);
        simple_cfg_set_int_deferred("disco_nav_double",DISCO_NAV_QUICK);simple_nav_open(1);nav_wait(250);nav_touch((lv_point_t){180,310},40);nav_wait(80);nav_touch((lv_point_t){180,310},40);nav_wait(250);assert(screen_current()==SCR_QUICK);
        puts("PASS: Squash real hold on launcher icon and logo runs saved Search/QS/Play-Pause/Nothing, fires once, ignores drag; configured double tap retained");return 0;
    }
    if(getenv("REVIEW_SQUASH_240")){
        void dhome_times_apply(void);
        if(th_simple()){
            track_state_t st;ipc_get_state(&st);lv_obj_t*root=screen_get_root(SCR_HOME);
            lv_obj_t*title=find_visible_text(root,st.title),*artist=find_visible_text(root,st.artist),*album=find_visible_text(root,st.album);assert(title&&artist&&album);
            assert(lv_obj_get_style_text_font(title,0)==TF(MUSIC_TITLE));assert(lv_obj_get_style_text_font(artist,0)==TF(MUSIC_ARTIST));assert(lv_obj_get_style_text_font(album,0)==TF(MUSIC_ALBUM));
            assert(lv_font_get_line_height(TF(MUSIC_TITLE))>lv_font_get_line_height(TF(MUSIC_ARTIST)));assert(lv_font_get_line_height(TF(MUSIC_ARTIST))>lv_font_get_line_height(TF(MUSIC_ALBUM)));
            lv_obj_t*status=find_visible_text(root,ui_is_playing()?LV_SYMBOL_PLAY:LV_SYMBOL_PAUSE);assert(status&&!lv_obj_has_flag(status,LV_OBJ_FLAG_CLICKABLE));
            assert(lv_color_eq(lv_obj_get_style_text_color(status,0),ui_is_playing()?TC(STATUS_SUCCESS):TC(TEXT_SECONDARY)));
            const char*mode[]={LV_SYMBOL_RIGHT,LV_SYMBOL_SHUFFLE,LV_SYMBOL_LOOP "1",LV_SYMBOL_LOOP,LV_SYMBOL_RIGHT "1"};
            for(int i=0;i<5;i++){cfg_set_int_deferred("work_mode",i);nav_wait(1100);lv_obj_t*m=find_visible_text(root,mode[i]);assert(m&&!lv_obj_has_flag(m,LV_OBJ_FLAG_CLICKABLE));}
            assert(!find_visible_text(root,LV_SYMBOL_LEFT LV_SYMBOL_LEFT));assert(!find_visible_text(root,LV_SYMBOL_RIGHT LV_SYMBOL_RIGHT));
            for(int i=0;i<2;i++){cfg_set_int_deferred("simple_play_state",i);cfg_set_int_deferred("simple_play_mode",i);dhome_times_apply();assert(!!find_visible_text(root,ui_is_playing()?LV_SYMBOL_PLAY:LV_SYMBOL_PAUSE)==i);}
            simple_cfg_set_int_deferred("disco_times",0);dhome_times_apply();assert(find_visible_text(root,ui_is_playing()?LV_SYMBOL_PLAY:LV_SYMBOL_PAUSE));simple_cfg_set_int_deferred("disco_times",1);dhome_times_apply();
            lv_obj_t*elapsed=find_visible_text(root,"1:32");if(elapsed){lv_obj_t*shadow=find_text(root,"1:32");if(shadow!=elapsed)assert(!lv_obj_is_visible(shadow));}

            screen_show(SCR_LIBRARY);nav_wait(250);lv_obj_t*back=find_visible_text(screen_get_root(SCR_LIBRARY),LV_SYMBOL_LEFT);assert(back);lv_area_t a,b;lv_obj_get_coords(back,&a);lv_obj_get_coords(lv_obj_get_parent(back),&b);assert(abs(a.y1+a.y2-b.y1-b.y2)<=2&&abs(a.x1+a.x2-b.x1-b.x2)<=2);
        }
        setlist_open("Display");nav_wait(250);lv_obj_t*entry=find_visible_text(screen_get_root(SCR_SETLIST),th_null()?"Null Options":th_simple()?"Squash Options":"Disco Options");assert(entry);lv_obj_send_event(lv_obj_get_parent(entry),LV_EVENT_CLICKED,NULL);nav_wait(250);lv_obj_t*root=screen_get_root(SCR_SETLIST);lv_obj_t*menu=find_visible_text(root,"Menu items"),*qs=find_visible_text(root,"Quick settings");assert(menu&&qs);assert(lv_obj_get_y(lv_obj_get_parent(menu))<lv_obj_get_y(lv_obj_get_parent(qs)));
        const char*key=th_null()?"null_menu":th_simple()?"simple_menu":"disco_menu";cfg_set_str(key,"library");int disco_catalog_load(void);const char*disco_catalog_id(int);int count=disco_catalog_load(),found=0;for(int i=0;i<count;i++)if(!strcmp(disco_catalog_id(i),"settings"))found++;assert(found==1);
        screen_show(SCR_SETTINGS);disco_menu_open();lv_obj_t*settings=find_visible_text(lv_layer_top(),"Settings");assert(settings);assert(!find_text(lv_obj_get_parent(settings),LV_SYMBOL_CLOSE));screen_back();
        printf("PASS: preset %d fixed fonts, read-only state/mode, no gesture hints, independent indicators, centred Back, options order and protected Settings (applicable checks)\n",theme_preset());return 0;
    }
    if(getenv("REVIEW_V6_QS")){
        lv_obj_t*quicksettings_test_button(int);void disco_test_editor_close(void);
        int item=quicksettings_playback_item(),selection[]={item,0,1};assert(quicksettings_items_save(selection,3)==0);quicksettings_build();screen_show(SCR_QUICK);nav_wait(250);
        lv_obj_t*button=quicksettings_test_button(0);assert(button&&lv_obj_is_visible(button));int before=screen_current();
        simple_cfg_set_int_deferred("work_mode",0);
        for(int i=1;i<=5;i++){lv_obj_send_event(button,th_null()?LV_EVENT_CLICKED:LV_EVENT_SHORT_CLICKED,NULL);nav_wait(40);assert(simple_cfg_get_int("work_mode",-1)==i%5);assert(screen_current()==before);}
        screen_show(SCR_SETTINGS);disco_qs_menu_open();assert(find_visible_text(lv_layer_top(),"Play mode"));screen_back();assert(screen_current()==SCR_SETTINGS);assert(!find_visible_text(lv_layer_top(),"Play mode"));
        printf("PASS: preset %d QS playback-mode selection, real button cycles all five modes, editor availability\n",theme_preset());return 0;
    }
    if(th_simple()&&getenv("REVIEW_V6_MUSIC")){
        track_state_t st;ipc_get_state(&st);snprintf(st.title,sizeof st.title,"A long track title that extends beyond this field");snprintf(st.artist,sizeof st.artist,"An artist with a deliberately long readable name");snprintf(st.album,sizeof st.album,"An album with a deliberately long readable name");snprintf(st.path,sizeof st.path,"/fixture/scroll-test.flac");ipc_set_external(st.title,st.artist,st.album,st.path,st.duration_ms,st.position_ms,0,0);home_set_now_playing(st.title,st.artist,ui_current_accent(),1);
        lv_obj_t*root=screen_get_root(SCR_HOME),*title=find_text(root,st.title),*artist=find_text(root,st.artist),*album=find_text(root,st.album);assert(title&&artist&&album);kit_pass(root);nav_wait(50);
        lv_obj_t*labels[]={title,artist,album};for(int i=0;i<3;i++){assert(lv_label_get_long_mode(labels[i])==LV_LABEL_LONG_SCROLL_CIRCULAR);lv_anim_t*a=lv_anim_get(labels[i],NULL);assert(a&&lv_anim_get_repeat_count(a)==3);}
        home_set_now_playing(st.title,st.artist,ui_current_accent(),1);nav_wait(120000);for(int i=0;i<3;i++){assert(lv_label_get_long_mode(labels[i])==LV_LABEL_LONG_CLIP);assert(lv_obj_get_style_text_align(labels[i],0)==LV_TEXT_ALIGN_LEFT);lv_point_t first;lv_label_get_letter_pos(labels[i],0,&first);assert(first.x==0);}
        snprintf(st.path,sizeof st.path,"/fixture/next-track.flac");ipc_set_external(st.title,st.artist,st.album,st.path,st.duration_ms,st.position_ms,0,0);home_set_now_playing(st.title,st.artist,ui_current_accent(),1);for(int i=0;i<3;i++)assert(lv_label_get_long_mode(labels[i])==LV_LABEL_LONG_SCROLL_CIRCULAR);
        const char*g[]={LV_SYMBOL_PLAY,LV_SYMBOL_PAUSE,LV_SYMBOL_NEXT,LV_SYMBOL_PREV};for(int i=0;i<4;i++){simple_playback_feedback(g[i]);nav_wait(30);assert(find_visible_text(lv_layer_top(),g[i]));nav_wait(350);}
        snprintf(st.title,sizeof st.title,"Short title");snprintf(st.artist,sizeof st.artist,"Artist");snprintf(st.album,sizeof st.album,"Album");snprintf(st.path,sizeof st.path,"/fixture/short.flac");ipc_set_external(st.title,st.artist,st.album,st.path,st.duration_ms,st.position_ms,0,0);home_set_now_playing(st.title,st.artist,ui_current_accent(),1);nav_wait(50);
        for(int i=0;i<3;i++)assert(lv_obj_get_style_text_align(labels[i],0)==(cfg_get_int("simple_title_al",0)==1?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER));
        puts("PASS: Squash long metadata scrolls three cycles and settles at the first character; new short text restores saved alignment; identity restart and action feedback preserved");return 0;
    }
    if(th_simple()&&getenv("REVIEW_SIMPLE_PERF")){
        screen_home();nav_wait(250);assert(simple_test_qs_paused());simple_nav_open(1);nav_wait(250);int bindings=simple_test_nav_bindings();
        for(int i=0;i<40;i++)simple_test_nav_phase(i*10);
        assert(simple_test_nav_bindings()==bindings);screen_show(SCR_LIBRARY);nav_wait(250);screen_home();nav_wait(250);assert(simple_test_nav_y()==360);simple_nav_open(1);nav_wait(320);assert(simple_test_nav_y()==108);simple_nav_open(0);nav_wait(250);
        screen_show(SCR_QUICK);nav_wait(250);assert(!simple_test_qs_paused());lv_obj_t*root=screen_get_root(SCR_QUICK),*bright=NULL,*batt=NULL;
        for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){lv_obj_t*c=lv_obj_get_child(root,i);if(lv_obj_check_type(c,&lv_arc_class)&&lv_obj_get_width(c)==344){if(lv_arc_get_bg_angle_start(c)==222)bright=c;if(lv_arc_get_bg_angle_start(c)==42)batt=c;}}
        assert(bright&&batt&&lv_arc_get_min_value(bright)==1&&lv_arc_get_max_value(bright)==40);
        int old=cfg_get_int("brightness",16);lv_arc_set_value(bright,30);lv_obj_send_event(bright,LV_EVENT_VALUE_CHANGED,NULL);assert(cfg_get_int("brightness",16)==old);lv_obj_send_event(bright,LV_EVENT_RELEASED,NULL);assert(cfg_get_int("brightness",0)==30);
        quicksettings_set_battery(67,1);nav_wait(250);assert(find_visible_text(root,LV_SYMBOL_CHARGE));quicksettings_set_battery(67,0);assert(lv_obj_get_style_arc_opa(batt,LV_PART_INDICATOR)==255);quicksettings_set_battery(14,0);assert(lv_color_eq(lv_obj_get_style_arc_color(batt,LV_PART_INDICATOR),TC(STATUS_DANGER)));quicksettings_set_battery(-1,0);assert(lv_color_eq(lv_obj_get_style_arc_color(batt,LV_PART_INDICATOR),TC(TEXT_DISABLED)));assert(lv_arc_get_value(batt)==0&&find_visible_text(root,"?"));
        assert(lv_arc_get_mode(batt)==LV_ARC_MODE_REVERSE);quicksettings_set_battery(25,0);assert(lv_arc_get_angle_start(batt)==114&&lv_arc_get_angle_end(batt)==138);quicksettings_set_battery(75,0);assert(lv_arc_get_angle_start(batt)==66);quicksettings_set_battery(100,0);assert(lv_arc_get_angle_start(batt)==42);
        simple_refresh_budget(0);assert(lv_display_get_refr_timer(lv_display_get_default())->period==33);simple_refresh_budget(1);assert(lv_display_get_refr_timer(lv_display_get_default())->period==16);
        lv_indev_t*in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,nav_read);lv_timer_set_period(lv_indev_get_read_timer(in),10);
        lv_obj_t*pp=ui_action_find("qs.pp");assert(pp&&lv_obj_get_width(pp)==40&&lv_obj_get_height(pp)==40);int before=null_transport_calls;nav_touch((lv_point_t){180,180},50);assert(screen_current()==SCR_QUICK&&null_transport_calls==before+1);nav_touch((lv_point_t){218,180},50);nav_wait(250);assert(screen_current()==SCR_HOME&&null_transport_calls==before+1);
        assert_visible_icon_glyphs(root);screen_home();assert(simple_test_qs_paused());
        printf("PASS: Squash cached launcher content (%d unchanged bindings over 40 frames), clean return geometry, QS timer pause, brightness 1..40/save-on-release, charge/unplug/low/unknown battery and icon glyphs\n",bindings);
        return 0;
    }
    if(th_simple()&&getenv("REVIEW_SIMPLE")){
        assert(simple_test_qs_count()==8);int ids[8];assert(quicksettings_items_load(ids)==8);
        int old=cfg_get_int("disco_clock",1);assert(simple_cfg_set_int_deferred("disco_clock",0)==0);assert(cfg_get_int("disco_clock",1)==old);assert(cfg_get_int("simple_clock",-1)==0);
        int chosen[]={7,0,5,1,6,4,3,2};assert(quicksettings_items_save(chosen,8)==0);simple_qs_build();assert(quicksettings_items_load(ids)==8);for(int i=0;i<8;i++)assert(ids[i]==chosen[i]);
        lv_indev_t*in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,nav_read);lv_timer_set_period(lv_indev_get_read_timer(in),10);
        simple_nav_open(1);nav_wait(250);assert(simple_nav_is_open());
        nav_pointer=(lv_indev_data_t){.point={240,162},.state=LV_INDEV_STATE_PRESSED};nav_wait(40);
        for(int x=240;x>=145;x-=5){nav_pointer.point.x=x;nav_wait(10);}nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(250);assert(simple_test_selection()==1);assert(screen_current()==SCR_HOME);
        nav_touch((lv_point_t){180,160},50);nav_wait(250);assert(screen_current()==SCR_LIBRARY);
        screen_home();simple_nav_open(1);nav_wait(250);screen_back();nav_wait(180);assert(!simple_nav_is_open()&&screen_current()==SCR_HOME);
        nav_wait(250);int commands=null_transport_calls;
        nav_touch((lv_point_t){180,205},40);nav_wait(100);nav_touch((lv_point_t){180,205},40);nav_wait(100);assert(null_transport_calls==commands+1&&!strcmp(null_last_command,"0201000C0000"));
        nav_pointer=(lv_indev_data_t){.point={95,205},.state=LV_INDEV_STATE_PRESSED};nav_wait(30);for(int x=95;x<=255;x+=8){nav_pointer.point.x=x;nav_wait(10);}nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(100);assert(null_transport_calls==commands+2&&!strcmp(null_last_command,"0201000C0001"));
        nav_pointer=(lv_indev_data_t){.point={255,205},.state=LV_INDEV_STATE_PRESSED};nav_wait(30);for(int x=255;x>=95;x-=8){nav_pointer.point.x=x;nav_wait(10);}nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(100);assert(null_transport_calls==commands+3&&!strcmp(null_last_command,"0201000C0002"));
        simple_cfg_set_int_deferred("disco_title_hold",0);
        nav_pointer=(lv_indev_data_t){.point={95,205},.state=LV_INDEV_STATE_PRESSED};nav_wait(30);nav_pointer.point.x=255;nav_wait(500);nav_pointer.state=LV_INDEV_STATE_RELEASED;nav_wait(80);assert(screen_current()==SCR_HOME&&null_transport_calls==commands+4&&!strcmp(null_last_command,"0201000C0001"));
        simple_cfg_set_int_deferred("disco_title_hold",2);int calls=send_count;
        nav_touch((lv_point_t){180,210},800);nav_wait(450);assert(send_count==calls&&screen_current()==SCR_HOME&&!disco_track_overlay_visible());
        simple_cfg_set_int_deferred("disco_title_hold",0);
        nav_touch((lv_point_t){180,210},800);nav_wait(600);assert(screen_current()==SCR_NOWPLAYING&&ui_np_fsart_active());ui_np_fsart_close();nav_wait(500);assert(screen_current()==SCR_HOME);
        nav_touch((lv_point_t){180,210},40);nav_wait(450);assert(disco_track_overlay_visible());screen_back();assert(!disco_track_overlay_visible());
        simple_cfg_set_int_deferred("disco_nav_double",DISCO_NAV_SEARCH);simple_nav_open(1);nav_wait(250);nav_touch((lv_point_t){180,310},40);nav_wait(80);nav_touch((lv_point_t){180,310},40);nav_wait(200);assert(screen_current()==SCR_SEARCH&&search_landing_active());
        void search_landing_leave(void);search_landing_leave();screen_home();nav_wait(250);screen_show(SCR_SEARCH);nav_wait(100);assert(search_landing_active());
        screen_home();nav_wait(250);simple_cfg_set_int_deferred("memory_play",0);void dhome_set_now_playing(const char*,const char*,lv_color_t,bool);dhome_set_now_playing(NULL,NULL,TC(ACCENT_PRIMARY),false);nav_wait(100);nav_touch((lv_point_t){180,205},50);nav_wait(250);assert(screen_current()==SCR_LIBRARY);
        puts("PASS: Squash QS isolation, launcher drag/open/back, playback swipes/double-tap, slow swipe versus Hold, Title hold None/Immersive, idle Library tap, popup Back, logo shortcut and fresh Search landing");return 0;
    }
    if(th_simple()&&getenv("SIMPLE_NAV")){simple_nav_open(1);int ms=getenv("SIMPLE_NAV_MS")?atoi(getenv("SIMPLE_NAV_MS")):350;for(int i=0;i<ms/10;i++){lv_tick_inc(10);lv_timer_handler();}}
    if(th_simple()&&getenv("SIMPLE_NAV_PHASE"))simple_test_nav_phase(atoi(getenv("SIMPLE_NAV_PHASE")));
    if(th_simple()&&getenv("SIMPLE_POPUP")){extern void disco_track_overlay_open(void);disco_track_overlay_open();}
    if(th_simple()&&getenv("SIMPLE_ARROW_MS")){simple_action_feedback();for(int i=0;i<atoi(getenv("SIMPLE_ARROW_MS"))/10;i++){lv_tick_inc(10);lv_timer_handler();}}
    if(th_simple()&&getenv("SIMPLE_ARROW_FRAME"))simple_test_feedback_frame(atoi(getenv("SIMPLE_ARROW_FRAME")));
    lv_refr_now(d);
    if(th_simple()&&getenv("SQUASH_VOLUME")){ui_show_volume(atoi(getenv("SQUASH_VOLUME")));nav_wait(180);lv_refr_now(d);}
    if(getenv("VOLUME_CONCEPT")){volume_concept(atoi(getenv("VOLUME_CONCEPT")));lv_refr_now(d);}
    FILE *f=fopen(argv[5],"wb"); if(!f) return 1;
    fprintf(f,"P6\n360 360\n255\n");
    for(size_t i=0;i<sizeof pixels;i+=3){
        unsigned char rgb[3]={pixels[i+2],pixels[i+1],pixels[i]}; fwrite(rgb,1,3,f);
    }
    if(fclose(f)) return 1;
    printf("PASS: %s preset=%s variant=%s up_next=%s\n",argv[3],argv[1],argv[2],argv[4]);
    return 0;
}
#include "disco_mock.c"
#include "stats_mock.c"
#include "sphere_mock.c"

#include "volume_concepts_mock.c"
