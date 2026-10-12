/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Contextual first-use hints. Only short-lived timers; no idle polling. */
#include "onboarding.h"
#include "screens.h"
#include "theme.h"
#include "config.h"

static const char *keys[HINT_COUNT] = {"hint_menu", "hint_nav", "hint_library", "hint_music", "hint_shortcuts"};
static const int limits[HINT_COUNT] = {2, 2, 1, 1, 1};
static const char *text[HINT_COUNT] = {NULL, "Arrows: sections\nTap icon: open", "Hold a song or album\nfor actions", "Tap title for more", NULL};
static lv_obj_t *pill;
static lv_timer_t *offer_timer, *hide_timer, *startup_timer;
static unsigned shown;
static int started, current = -1, menu_open, library_actions;

static void cancel(lv_timer_t **timer){if(*timer){lv_timer_delete(*timer);*timer=NULL;}}
void onboarding_dismiss(void){
    cancel(&offer_timer);cancel(&hide_timer);
    if(pill){lv_obj_delete(pill);pill=NULL;}
}
static int eligible(int id){
    int mode=cfg_get_int("disco_hints",0);
    if(!started || !th_disco() || mode==2 || (shown & (1u<<id)))return 0;
    if(mode==1)return 1; /* explicitly On: once per context per session, even after learning */
    if(cfg_get_int("hint_starts",0)>=5)return 0;
    int fallback=id==HINT_MENU && cfg_get_int("disco_nav_hint",0)?2:0;
    return cfg_get_int(keys[id],fallback)<limits[id];
}
static void timeout(lv_timer_t *t){hide_timer=NULL;lv_timer_delete(t);if(pill){lv_obj_delete(pill);pill=NULL;}}
static void menu_hint_cb(lv_event_t *e){
    (void)e;onboarding_discovered(HINT_MENU);disco_nav_set_open(1);
}
static void offer(lv_timer_t *t){
    offer_timer=NULL;lv_timer_delete(t);
    if(current!=screen_current() || pill)return;
    int id=-1;
    if(menu_open){if(eligible(HINT_NAV))id=HINT_NAV;}
    else if(current==SCR_HOME || current==SCR_LIBRARY){
        if(eligible(HINT_MENU))id=HINT_MENU;
        else if(current==SCR_LIBRARY && library_actions && eligible(HINT_LIBRARY))id=HINT_LIBRARY;
        else if(current==SCR_HOME && eligible(HINT_MUSIC)){
            track_state_t st;ipc_get_state(&st);if(st.have_track)id=HINT_MUSIC;
        }
        else if(current==SCR_HOME && eligible(HINT_SHORTCUTS))id=HINT_SHORTCUTS;   /* after the title tip has been used */
    }
    if(id<0)return;
    pill=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(pill);
    lv_obj_clear_flag(pill,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(pill,18,0);
    lv_obj_set_style_bg_color(pill,TC(SCRIM),0);lv_obj_set_style_bg_opa(pill,245,0);
    lv_obj_set_style_border_width(pill,1,0);lv_obj_set_style_border_color(pill,TC(FIXED_MEDIA_WHITE),0);
    lv_obj_set_style_border_opa(pill,80,0);
    int x=84,y=35,w=192,h=48;
    const char *message=text[id];
    if(id==HINT_MENU){w=158;h=42;y=159;x=disco_mirror()?54:148;message=disco_mirror()?LV_SYMBOL_LEFT " Tap for menu":"Tap for menu " LV_SYMBOL_RIGHT;}
    if(id==HINT_MUSIC){y=110;x=92;w=176;h=24;}
    if(id==HINT_SHORTCUTS){y=110;x=82;w=196;h=24;message=disco_mirror()?"Swipe right for shortcuts":"Swipe left for shortcuts";}
    if(id==HINT_MENU){lv_obj_add_flag(pill,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(pill,menu_hint_cb,LV_EVENT_CLICKED,NULL);}
    lv_obj_set_pos(pill,x,y);lv_obj_set_size(pill,w,h);
    lv_obj_t *label=lv_label_create(pill);lv_label_set_text(label,message);
    lv_obj_set_style_text_font(label,TF(UI_16),0);lv_obj_set_style_text_color(label,TC(FIXED_MEDIA_WHITE),0);
    lv_obj_set_width(label,w-12);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label);
    hide_timer=lv_timer_create(timeout,4000,NULL);
    if(!hide_timer){lv_obj_delete(pill);pill=NULL;return;}
    shown|=1u<<id;
}
static void schedule(void){
    if(!started || !th_disco() || cfg_get_int("disco_hints",0)==2 || (cfg_get_int("disco_hints",0)==0 && cfg_get_int("hint_starts",0)>=5))return;
    if(!menu_open && current!=SCR_HOME && current!=SCR_LIBRARY)return;
    cancel(&offer_timer);offer_timer=lv_timer_create(offer,800,NULL);
}
void onboarding_screen(int screen,int nav_open){
    onboarding_dismiss();current=screen;menu_open=nav_open;
    schedule();
}
void onboarding_library(int actionable){library_actions=actionable;if(current==SCR_LIBRARY && !menu_open){onboarding_dismiss();schedule();}}
void onboarding_discovered(int id){
    if(!started || !th_disco() || id<0 || id>=HINT_COUNT)return;
    onboarding_dismiss();
    int fallback=id==HINT_MENU && cfg_get_int("disco_nav_hint",0)?2:0;
    int count=cfg_get_int(keys[id],fallback);if(count>=limits[id])return;
    cfg_begin();cfg_set_int_deferred(keys[id],count+1);
    if(cfg_commit()<0){cfg_set_int_deferred(keys[id],count);cfg_flush();}
}
static void input_cb(lv_event_t *e){if(!pill || lv_event_get_param(e)!=pill)onboarding_dismiss();}
static void healthy_start(lv_timer_t *t){
    startup_timer=NULL;lv_timer_delete(t);
    int n=cfg_get_int("hint_starts",0);
    if(n<5){cfg_begin();cfg_set_int_deferred("hint_starts",n+1);if(cfg_commit()<0){cfg_set_int_deferred("hint_starts",n);cfg_flush();}}
    if(cfg_get_int("disco_hints",0)==0 && cfg_get_int("hint_starts",0)>=5)onboarding_dismiss();
}
void onboarding_start(void){
    if(started || !th_disco())return;
    started=1;current=screen_current();
    for(lv_indev_t *in=lv_indev_get_next(NULL);in;in=lv_indev_get_next(in))lv_indev_add_event_cb(in,input_cb,LV_EVENT_PRESSED,NULL);
    /* Count only after 30 seconds of event-loop uptime; an immediate crash/restart consumes nothing. */
    startup_timer=lv_timer_create(healthy_start,30000,NULL);schedule();
}
void onboarding_mode_changed(void){onboarding_dismiss();shown=0;schedule();}
int onboarding_reset(void){
    int old[HINT_COUNT+3];
    old[0]=cfg_get_int("hint_starts",0);old[1]=cfg_get_int("disco_nav_hint",0);old[2]=cfg_get_int("disco_hints",0);
    for(int i=0;i<HINT_COUNT;i++)old[i+3]=cfg_get_int(keys[i],i==HINT_MENU && old[1]?2:0);
    cfg_begin();int error=0;
    error|=cfg_set_int_deferred("hint_starts",0);error|=cfg_set_int_deferred("disco_nav_hint",0);
    error|=cfg_set_int_deferred("disco_hints",0);
    for(int i=0;i<HINT_COUNT;i++)error|=cfg_set_int_deferred(keys[i],0);
    int result=cfg_commit();
    if(error<0 || result<0){
        cfg_set_int_deferred("hint_starts",old[0]);cfg_set_int_deferred("disco_nav_hint",old[1]);cfg_set_int_deferred("disco_hints",old[2]);
        for(int i=0;i<HINT_COUNT;i++)cfg_set_int_deferred(keys[i],old[i+3]);
        cfg_flush();return -1;
    }
    onboarding_mode_changed();return 0;
}
