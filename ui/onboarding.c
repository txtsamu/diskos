/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Contextual first-use hints. Only short-lived timers; no idle polling. */
#include "onboarding.h"
#include "screens.h"
#include "theme.h"
#include "config.h"
#include <math.h>

#include <string.h>
#include <stdio.h>
#include "null_theme.h"
/* Learning is per theme, not shared with an already-used Disco setup. */
static const char *hint_key(const char *key){
    if(!th_null())return key;
    static const char *const from[]={"hint_menu","hint_nav","hint_library","hint_music","hint_shortcuts","hint_lock","hint_starts","disco_nav_hint","disco_hints","hint_back","hint_home","hint_quick","hint_radio_hold","hint_seek","hint_precision","hint_music_gestures","hint_clock","hint_volume","hint_az","hint_flow","hint_immersive","hint_popup","hint_handle","hint_eq","hint_rim","hint_brightness"};
    static const char *const to[]={"null_hint_menu","null_hint_nav","null_hint_library","null_hint_music","null_hint_shortcuts","null_hint_lock","null_hint_starts","null_nav_hint","null_hints","null_hint_back","null_hint_home","null_hint_quick","null_hint_radio_hold","null_hint_seek","null_hint_precision","null_hint_music_gestures","null_hint_clock","null_hint_volume","null_hint_az","null_hint_flow","null_hint_immersive","null_hint_popup","null_hint_handle","null_hint_eq","null_hint_rim","null_hint_brightness"};
    for(unsigned i=0;i<sizeof from/sizeof *from;i++)if(!strcmp(key,from[i]))return to[i];
    return key;
}
static const char *keys[] = {"hint_menu", "hint_nav", "hint_library", "hint_music", "hint_shortcuts", "hint_lock","hint_back","hint_home","hint_quick","hint_radio_hold","hint_seek","hint_precision","hint_music_gestures","hint_clock","hint_volume","hint_az","hint_flow","hint_immersive","hint_popup","hint_handle","hint_eq","hint_rim","hint_brightness"};
static const int limits[] = {2, 2, 1, 1, 1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
static const char *text[] = {NULL, "Arrows: sections\nTap icon: open", "Hold a song or album\nfor actions", "Tap title for more", NULL,
                                       "Tip: the power button locks\nthe screen in a pocket","Swipe inward from the back edge\nto return one screen.","On other screens, swipe up\nfrom the bottom to reach Music.","Pull down from the top\nfor Quick Settings.","Tap Wi-Fi / BT to switch.\nHold to open connections.","Tap or drag progress\nto seek through the track.","Hold the progress line\nor arc for precise seeking.","Double-tap: play / pause.\nSwipe right: next; left: previous.","Hold the clock/status area\nto hide it. Hold again to show.","Drag the volume control.\nTap outside it to dismiss.","Tap A-Z, then a letter\nto jump through the list.","Drag covers to browse. Tap to\nopen tracks; hold to play album.","Tap the full-screen artwork\nto return to Music.","Swipe back from the edge\nto close track actions.","Drag menu text to browse. Hold\nhandle ends for continuous scroll.","Tap a band, then drag or use +/-.\nTap frequency / gain to edit either.","Drag along the outer rim\nto scroll long lists quickly.","Drag the brightness bar or arc\nto adjust the screen."};
_Static_assert(sizeof keys/sizeof *keys==HINT_COUNT,"hint keys");
_Static_assert(sizeof limits/sizeof *limits==HINT_COUNT,"hint limits");
_Static_assert(sizeof text/sizeof *text==HINT_COUNT,"hint text");
const char *onboarding_gesture_text(int id){
    if(id<0 || id>=HINT_COUNT)return "";
    if(id==HINT_BACK)return disco_mirror()?"Swipe left from the right edge to go back.":"Swipe right from the left edge to go back.";
    if(id==HINT_QUICK)return th_null()?"Pull down from the top for QS. Swipe up beside the panel, tap outside, or tap its bottom arrow to close.":"Pull down from the top for QS; swipe up to close.";
    if(id==HINT_NAV)return th_null()?"Tap the side arrow for the menu. Drag text or the handle to browse; tap an item to open.":"Tap the side tab for the menu. Arrows browse; centre icon opens. Double-tap / hold shortcuts are configurable.";
    if(id==HINT_HANDLE){
        static const char *names[]={"Music","Search","Queue","Up Next","Shortcuts","Quick Settings","Volume","Screen off","Play/Pause","Next track","Nothing"};
        static char tip[192];
        int tap=cfg_get_int("null_nav_double",DISCO_NAV_SEARCH),hold=cfg_get_int("null_nav_hold",DISCO_NAV_MUSIC);
        if(tap<0 || tap>=DISCO_NAV_COUNT)tap=DISCO_NAV_SEARCH;
        if(hold<0 || hold>=DISCO_NAV_COUNT)hold=DISCO_NAV_MUSIC;
        snprintf(tip,sizeof tip,"Handle centre: double-tap for %s; hold for %s. Drag to browse; hold either end to scroll.",names[tap],names[hold]);
        return tip;
    }
    if(id==HINT_MUSIC){
        int action=cfg_get_int(th_null()?"null_title_hold":"disco_title_hold",0);
        return action==0?"Tap the title for track actions and Mood. Hold for Immersive.":action==1?"Tap the title for track actions and Mood. Hold to favourite / unfavourite.":"Tap the title for track actions and Mood. Title hold is disabled.";
    }
    if(id==HINT_SHORTCUTS)return disco_mirror()?"Swipe right across Music to open Shortcuts.":"Swipe left across Music to open Shortcuts.";
    if(id==HINT_MUSIC_GESTURES)return th_null()?"In Gestures mode: double-tap track text to play/pause; swipe right for next, left for previous.":"In Gestures mode: double-tap the lower field to play/pause; swipe right for next, left for previous.";
    if(id==HINT_CLOCK)return "Tap the clock for Weather. Hold the top strip to hide clock/status; hold it again to show.";
    return text[id]?text[id]:"";
}
static int requested=-1;
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
    int mode=cfg_get_int(hint_key("disco_hints"),0);
    if(!started || (!th_disco() && !th_null()) || mode==2 || (shown & (1u<<id)))return 0;
    if(mode==1)return 1; /* explicitly On: once per context per session, even after learning */
    if(id<HINT_BACK && id!=HINT_LOCK && cfg_get_int(hint_key("hint_starts"),0)>=5)return 0;   /* the lock tip is new in 2.1.0: also for settled users */
    int fallback=id==HINT_MENU && cfg_get_int(hint_key("disco_nav_hint"),0)?2:0;
    return cfg_get_int(hint_key(keys[id]),fallback)<limits[id];
}
static void show_pill(int id);
static void onboarding_discovered_quiet(int id);
static void onboarding_dismiss_pill_only(void){ cancel(&hide_timer); if(pill){ lv_obj_delete(pill); pill=NULL; } }
static void timeout(lv_timer_t *t){hide_timer=NULL;lv_timer_delete(t);if(pill){lv_obj_delete(pill);pill=NULL;}}
static void menu_hint_cb(lv_event_t *e){
    (void)e;onboarding_discovered(HINT_MENU);if(th_null())null_nav_set_open(1);else disco_nav_set_open(1);
}
static void offer(lv_timer_t *t){
    offer_timer=NULL;lv_timer_delete(t);
    if(current!=screen_current() || pill)return;
    int id=-1;
    extern int ui_volume_overlay_visible(void);
    if(requested==HINT_VOLUME && !ui_volume_overlay_visible())requested=-1;
    if(requested==HINT_POPUP && !disco_track_overlay_visible())requested=-1;
    if(requested>=0){int want=requested;requested=-1;if(eligible(want))id=want;}
    if(id<0 && menu_open){if(eligible(HINT_NAV))id=HINT_NAV;else if(th_null() && eligible(HINT_HANDLE))id=HINT_HANDLE;}
    else if(id<0 && (current==SCR_HOME || current==SCR_LIBRARY)){
        if(eligible(HINT_MENU))id=HINT_MENU;
        else if(current==SCR_LIBRARY && library_actions && eligible(HINT_LIBRARY))id=HINT_LIBRARY;
        else if(current==SCR_LIBRARY && eligible(HINT_AZ))id=HINT_AZ;
        else if(current==SCR_LIBRARY && eligible(HINT_RIM))id=HINT_RIM;
        else if(current==SCR_HOME){
            track_state_t st;ipc_get_state(&st);
            int choices[]={HINT_MUSIC,HINT_SHORTCUTS,HINT_SEEK,HINT_CLOCK,HINT_QUICK,HINT_MUSIC_GESTURES,HINT_PRECISION};
            for(unsigned i=0;i<sizeof choices/sizeof *choices;i++){
                int h=choices[i];
                if((h==HINT_MUSIC || h==HINT_SEEK || h==HINT_PRECISION) && !st.have_track)continue;
                if(h==HINT_PRECISION && th_null())continue;
                if(h==HINT_MUSIC_GESTURES && cfg_get_int(th_null()?"null_controls":"disco_controls",0)!=2)continue;
                if(eligible(h)){id=h;break;}
            }
        }
    }else if(id<0){
        int context=current==SCR_QUICK?HINT_RADIO_HOLD:current==SCR_ALBUMWALL?HINT_FLOW:current==SCR_EQ_EDITOR?HINT_EQ:current==SCR_NOWPLAYING?HINT_IMMERSIVE:HINT_BACK;
        if(eligible(context))id=context;else if(current==SCR_QUICK && eligible(HINT_BRIGHTNESS))id=HINT_BRIGHTNESS;else if(current!=SCR_QUICK && current!=SCR_NOWPLAYING && eligible(HINT_HOME))id=HINT_HOME;
    }
    if(id<0)return;
    show_pill(id);
}
/* The pill: sized to its text (one or two lines, at the user's font size), centred where its hint belongs and kept
 * inside the round screen. The menu hint keeps its fixed size: it sits on, and is tapped like, the navigation tab. */
static void show_pill(int id){
    if(pill)return;
    pill=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(pill);
    lv_obj_clear_flag(pill,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(pill,18,0);
    lv_obj_set_style_bg_color(pill,TC(SCRIM),0);lv_obj_set_style_bg_opa(pill,245,0);
    lv_obj_set_style_border_width(pill,1,0);lv_obj_set_style_border_color(pill,TC(FIXED_MEDIA_WHITE),0);
    lv_obj_set_style_border_opa(pill,80,0);
    int x=84,y=35,w=192,h=48;
    const char *message=onboarding_gesture_text(id);
    if(id==HINT_MENU){w=158;h=42;y=159;x=disco_mirror()?54:148;message=disco_mirror()?LV_SYMBOL_LEFT " Tap for menu":"Tap for menu " LV_SYMBOL_RIGHT;}
    if(id==HINT_MUSIC){y=104;x=82;w=196;
        if(cfg_get_int(th_null()?"null_title_hold":"disco_title_hold",0)==0)message="Tap title for more\nHold it for full screen";}   /* Title Hold: Immersive */
    if(id==HINT_SHORTCUTS){y=104;x=82;w=196;message=disco_mirror()?"Swipe right for shortcuts":"Swipe left for shortcuts";}
    if(th_null() && id==HINT_NAV)message="Drag text to browse\nHandle: double tap / hold";
    if(th_null() && id==HINT_MENU){x=disco_mirror()?54:148;message=disco_mirror()?LV_SYMBOL_LEFT " Open drawer":"Open drawer " LV_SYMBOL_RIGHT;}
    if(id==HINT_LOCK){y=262;x=60;w=240;}
    if(id==HINT_MENU){lv_obj_add_flag(pill,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(pill,menu_hint_cb,LV_EVENT_CLICKED,NULL);}
    lv_obj_t *label=lv_label_create(pill);lv_label_set_text(label,message);
    lv_obj_set_style_text_font(label,TF(UI_18),0);lv_obj_set_style_text_color(label,th_null()?TC(TEXT_PRIMARY):TC(FIXED_MEDIA_WHITE),0);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    if(id==HINT_MENU){ lv_obj_set_pos(pill,x,y);lv_obj_set_size(pill,w,h);lv_obj_set_width(label,w-12);lv_obj_center(label); }
    else {
        lv_obj_set_style_pad_hor(pill,14,0);lv_obj_set_style_pad_ver(pill,5,0);
        lv_obj_set_size(pill,LV_SIZE_CONTENT,LV_SIZE_CONTENT);
        lv_obj_set_width(label,250);lv_obj_set_style_max_width(label,250,0);   /* longer lines wrap */
        lv_obj_update_layout(pill);
        int pw=lv_obj_get_width(pill),ph=lv_obj_get_height(pill),cx=x+w/2;
        if(ph>108){lv_obj_set_height(label,94);lv_label_set_long_mode(label,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_update_layout(pill);ph=lv_obj_get_height(pill);}
        if(ph>120){lv_obj_set_height(pill,120);ph=120;}
        int px=cx-pw/2,py=y;
        if(id==HINT_MUSIC || id==HINT_SHORTCUTS)py=y+28-ph;            /* above the title, whatever its height */
        if(id!=HINT_LOCK){py=180-ph/2;px=180-pw/2;}
        int mid=py+ph/2-180,half=(int)sqrtf((float)(178*178-mid*mid))-6;     /* inside the circle at that height */
        if(px<180-half)px=180-half;
        if(px+pw>180+half)px=180+half-pw;
        lv_obj_set_pos(pill,px,py);
    }
    hide_timer=lv_timer_create(timeout,id==HINT_LOCK?5000:6500,NULL);
    if(!hide_timer){lv_obj_delete(pill);pill=NULL;return;}
    shown|=1u<<id;
    if(id>=HINT_BACK)onboarding_discovered_quiet(id);
}
static void schedule(void){
    if(!started || (!th_disco() && !th_null()) || cfg_get_int(hint_key("disco_hints"),0)==2)return;
    if(current==SCR_SAVER)return;
    cancel(&offer_timer);offer_timer=lv_timer_create(offer,800,NULL);
}
void onboarding_screen(int screen,int nav_open){
    onboarding_dismiss();requested=-1;current=screen;menu_open=nav_open;
    schedule();
}
void onboarding_context(int id){if(id<0 || id>=HINT_COUNT || !eligible(id))return;onboarding_dismiss();requested=id;schedule();}
void onboarding_library(int actionable){library_actions=actionable;if(current==SCR_LIBRARY && !menu_open){onboarding_dismiss();schedule();}}
static void onboarding_discovered_quiet(int id){     /* count it as seen without taking the pill away */
    int count=cfg_get_int(hint_key(keys[id]),0);if(count>=limits[id])return;
    cfg_begin();cfg_set_int_deferred(hint_key(keys[id]),count+1);
    if(cfg_commit()<0){cfg_set_int_deferred(hint_key(keys[id]),count);cfg_flush();}
}
void onboarding_discovered(int id){
    if(!started || (!th_disco() && !th_null()) || id<0 || id>=HINT_COUNT)return;
    onboarding_dismiss();
    int fallback=id==HINT_MENU && cfg_get_int(hint_key("disco_nav_hint"),0)?2:0;
    int count=cfg_get_int(hint_key(keys[id]),fallback);if(count>=limits[id])return;
    cfg_begin();cfg_set_int_deferred(hint_key(keys[id]),count+1);
    if(cfg_commit()<0){cfg_set_int_deferred(hint_key(keys[id]),count);cfg_flush();}
}
static void lock_offer(lv_timer_t *t){ offer_timer=NULL; lv_timer_delete(t); if(!pill && eligible(HINT_LOCK)){ show_pill(HINT_LOCK); onboarding_discovered_quiet(HINT_LOCK); } }
void onboarding_woke_by_touch(void){
    if(!eligible(HINT_LOCK))return;
    cancel(&offer_timer);onboarding_dismiss_pill_only();
    offer_timer=lv_timer_create(lock_offer,700,NULL);              /* once the finger is off and the screen settled */
}
static void input_cb(lv_event_t *e){if(!pill || lv_event_get_param(e)!=pill)onboarding_dismiss();}
static void healthy_start(lv_timer_t *t){
    startup_timer=NULL;lv_timer_delete(t);
    int n=cfg_get_int(hint_key("hint_starts"),0);
    if(n<5){cfg_begin();cfg_set_int_deferred(hint_key("hint_starts"),n+1);if(cfg_commit()<0){cfg_set_int_deferred(hint_key("hint_starts"),n);cfg_flush();}}
    if(cfg_get_int(hint_key("disco_hints"),0)==0 && cfg_get_int(hint_key("hint_starts"),0)>=5)onboarding_dismiss();
}
void onboarding_start(void){
    if(started || (!th_disco() && !th_null()))return;
    started=1;current=screen_current();
    for(lv_indev_t *in=lv_indev_get_next(NULL);in;in=lv_indev_get_next(in))lv_indev_add_event_cb(in,input_cb,LV_EVENT_PRESSED,NULL);
    /* Count only after 30 seconds of event-loop uptime; an immediate crash/restart consumes nothing. */
    startup_timer=lv_timer_create(healthy_start,30000,NULL);schedule();
}
void onboarding_mode_changed(void){onboarding_dismiss();shown=0;requested=-1;schedule();}
int onboarding_reset(void){
    int old[HINT_COUNT+3];
    old[0]=cfg_get_int(hint_key("hint_starts"),0);old[1]=cfg_get_int(hint_key("disco_nav_hint"),0);old[2]=cfg_get_int(hint_key("disco_hints"),0);
    for(int i=0;i<HINT_COUNT;i++)old[i+3]=cfg_get_int(hint_key(keys[i]),i==HINT_MENU && old[1]?2:0);
    cfg_begin();int error=0;
    error|=cfg_set_int_deferred(hint_key("hint_starts"),0);error|=cfg_set_int_deferred(hint_key("disco_nav_hint"),0);
    error|=cfg_set_int_deferred(hint_key("disco_hints"),0);
    for(int i=0;i<HINT_COUNT;i++)error|=cfg_set_int_deferred(hint_key(keys[i]),0);
    int result=cfg_commit();
    if(error<0 || result<0){
        cfg_set_int_deferred(hint_key("hint_starts"),old[0]);cfg_set_int_deferred(hint_key("disco_nav_hint"),old[1]);cfg_set_int_deferred(hint_key("disco_hints"),old[2]);
        for(int i=0;i<HINT_COUNT;i++)cfg_set_int_deferred(hint_key(keys[i]),old[i+3]);
        cfg_flush();return -1;
    }
    onboarding_mode_changed();return 0;
}
