/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Simple: presentation over Disco's action catalogue, configuration editor and Music behaviour. */
#include "simple_theme.h"
#include "theme.h"
#include "theme_kit.h"
#include "screens.h"
#include "config.h"
#include "quicksettings.h"
#include "ipc.h"
#include "onboarding.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
LV_FONT_DECLARE(diskos_splash_disco_72)
int disco_catalog_load(void);
const char *disco_catalog_name(int);
const char *disco_catalog_icon(int);
void disco_catalog_open(int);
void disco_vec_icon(lv_obj_t *,const char *,int);
void quicksettings_null_action(int,int);
int wifi_radio_live(void);int bt_state(void);
/* Never remap shared system preferences. Only inherited theme-owned keys are namespaced. */
static const char *scope(const char *key,char *buf,size_t n){
 if(!th_simple()||!key)return key;
 if(!strcmp(key,"accent_color")||!strcmp(key,"accent_mode")){snprintf(buf,n,"simple_%s",key);return buf;}
 if(!strncmp(key,"hint_",5)){snprintf(buf,n,"simple_%s",key);return buf;}
 if(!strncmp(key,"disco_",6)){snprintf(buf,n,"simple_%s",key+6);return buf;}
 if(!strncmp(key,"dm_app",6)){snprintf(buf,n,"sm_app%s",key+6);return buf;}
 return key;
}
int simple_cfg_get_int(const char*k,int d){char b[64];
 if(th_simple()){
  if(!strcmp(k,"accent_color"))d=0xF28A32;
  if(!strcmp(k,"disco_controls"))return 2;
  if(!strcmp(k,"disco_progress")||!strcmp(k,"disco_eq")||!strcmp(k,"disco_vol"))d=1;
  if(!strcmp(k,"disco_sheen")||!strcmp(k,"disco_side"))return 0;
 }
 return cfg_get_int(scope(k,b,sizeof b),d);
}
const char *simple_cfg_get_str(const char*k,const char*d){char b[64];return cfg_get_str(scope(k,b,sizeof b),d);}
int simple_cfg_set_int(const char*k,int v){char b[64];return cfg_set_int(scope(k,b,sizeof b),v);}
int simple_cfg_set_str(const char*k,const char*v){char b[64];return cfg_set_str(scope(k,b,sizeof b),v);}
int simple_cfg_set_int_deferred(const char*k,int v){char b[64];return cfg_set_int_deferred(scope(k,b,sizeof b),v);}
int simple_cfg_set_str_deferred(const char*k,const char*v){char b[64];return cfg_set_str_deferred(scope(k,b,sizeof b),v);}
static char simple_label_tag,simple_control_tag;
static lv_obj_t *label(lv_obj_t*p,const char*s,int x,int y,int w,const lv_font_t*f,lv_color_t c){
 lv_obj_t*l=lv_label_create(p);lv_label_set_text(l,s);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);
 lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);lv_obj_set_style_text_font(l,f,0);lv_obj_set_style_text_color(l,c,0);
 lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_remove_flag(l,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_user_data(l,&simple_label_tag);kit_keep(l);return l;
}
/* Quiet bevel shared by Simple's icon surfaces; no animation or bitmap cost. */
static void simple_shade(lv_obj_t *o){
 lv_obj_set_style_bg_color(o,theme_color_mix(TC(ACCENT_PRIMARY),TC(CANVAS),38),0);
 lv_obj_set_style_bg_grad_color(o,theme_color_mix(TC(ACCENT_PRIMARY),TC(CANVAS),12),0);
 lv_obj_set_style_bg_grad_dir(o,LV_GRAD_DIR_VER,0);lv_obj_set_style_bg_opa(o,255,0);
 lv_obj_set_style_shadow_color(o,TC(FIXED_MEDIA_BLACK),0);lv_obj_set_style_shadow_opa(o,24,0);
 lv_obj_set_style_shadow_width(o,5,0);lv_obj_set_style_shadow_offset_y(o,2,0);
}
static lv_obj_t *circle(lv_obj_t*p,int cx,int cy,int size){
 lv_obj_t*b=lv_button_create(p);lv_obj_remove_style_all(b);lv_obj_set_size(b,size,size);lv_obj_set_pos(b,cx-size/2,cy-size/2);
 lv_obj_set_style_radius(b,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(b,2,0);lv_obj_set_style_border_color(b,TC(ACCENT_PRIMARY),0);
 lv_obj_set_style_bg_color(b,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(b,24,0);lv_obj_set_style_bg_opa(b,90,LV_STATE_PRESSED);
 simple_shade(b);lv_obj_set_user_data(b,&simple_control_tag);lv_obj_remove_flag(b,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);kit_keep(b);return b;
}
/* ---- One-item wheel; rotation is about the bottom hub, never the screen centre. ---- */
static lv_obj_t *nav_root,*nav_panel,*nav_hub,*nav_items[3],*nav_icons[3],*nav_names[3],*nav_pos;
static int nav_on,nav_count,nav_selected,dragged,nav_pressed;
static int nav_content[3]={-1,-1,-1};
static unsigned nav_bindings;
static float nav_phase,nav_start;
static lv_point_t nav_down;
static uint32_t hub_last;
static lv_timer_t *hub_timer;
static void hub_timer_cancel(void){if(hub_timer){lv_timer_delete(hub_timer);hub_timer=NULL;}}
static void hub_close(lv_timer_t*t){lv_timer_delete(t);hub_timer=NULL;hub_last=0;simple_nav_open(0);}
static void nav_paint(void){
 if(!nav_panel||nav_count<1)return;
 int base=(int)lroundf(nav_phase);
 for(int j=0;j<3;j++){
  int off=j-1,index=(base+off)%nav_count;if(index<0)index+=nav_count;
  float delta=(base+off-nav_phase),a=delta*0.6105f;
  int x=360+(int)lroundf(sinf(a)*293),y=360-(int)lroundf(cosf(a)*293);
  lv_obj_set_pos(nav_items[j],x-90,y-56);
  float distance=fabsf(delta);
  int opa=distance<=1?(int)(255-55*distance):(int)(200*fmaxf(0,1-(distance-1)/0.6f));
  lv_obj_set_style_opa(nav_items[j],opa,0);
  if(nav_content[j]!=index){nav_content[j]=index;nav_bindings++;
  const char *ic=disco_catalog_icon(index);lv_label_set_text(nav_icons[j],ic);disco_vec_icon(nav_icons[j],ic,80);
  lv_obj_set_style_text_font(nav_icons[j],ic[0]!='@'&&(unsigned char)ic[0]<128?TF(UI_36):TF(LAUNCHER_ICON_80),0);
  lv_obj_set_style_transform_scale_x(nav_icons[j],256,0);lv_obj_set_style_transform_scale_y(nav_icons[j],256,0);
  lv_obj_center(nav_icons[j]);
  lv_label_set_text(nav_names[j],disco_catalog_name(index));
  }
  lv_obj_set_style_opa(nav_names[j],(int)(255*fmaxf(0,1-fabsf(delta))),0);
 }
 nav_selected=((int)lroundf(nav_phase)%nav_count+nav_count)%nav_count;
 char b[24];snprintf(b,sizeof b,"%d / %d",nav_selected+1,nav_count);lv_label_set_text(nav_pos,b);
}
static void nav_move(void*o,int32_t v){(void)o;nav_phase=v/1000.f;nav_paint();}
static void nav_snap(void){
 lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,nav_panel);lv_anim_set_exec_cb(&a,nav_move);
 lv_anim_set_values(&a,(int)(nav_phase*1000),(int)lroundf(nav_phase)*1000);lv_anim_set_duration(&a,110);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_start(&a);
}
static void nav_zoom(void*o,int32_t v){
 lv_obj_set_y(o,v);
}
static void nav_closed(lv_anim_t*a){(void)a;if(!nav_on&&nav_root)lv_obj_add_flag(nav_root,LV_OBJ_FLAG_HIDDEN);}
void simple_nav_open(int open){
 if(!nav_root)return;nav_on=!!open;lv_obj_remove_flag(nav_root,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(nav_root);lv_obj_move_foreground(nav_hub);
 lv_anim_delete(nav_panel,nav_zoom);lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,nav_panel);lv_anim_set_exec_cb(&a,nav_zoom);
 lv_anim_set_values(&a,lv_obj_get_y(nav_panel),open?108:360);lv_anim_set_duration(&a,open?180:130);
 lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_set_completed_cb(&a,nav_closed);lv_anim_start(&a);nav_paint();onboarding_screen(SCR_HOME,nav_on);
}
int simple_nav_is_open(void){return nav_on;}
static void outside(lv_event_t*e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)simple_nav_open(0);}
static void nav_event(lv_event_t*e){
 lv_event_code_t c=lv_event_get_code(e);lv_indev_t*in=lv_indev_active();lv_point_t p={0,0};if(in)lv_indev_get_point(in,&p);
 if(c==LV_EVENT_PRESSED){nav_down=p;nav_start=nav_phase;dragged=0;nav_pressed=1;lv_anim_delete(nav_panel,nav_move);}
 if(c==LV_EVENT_PRESSING&&nav_pressed){int dx=p.x-nav_down.x,dy=p.y-nav_down.y;if(abs(dx)+abs(dy)>10)dragged=1;
  if(dragged){nav_phase=nav_start+(abs(dx)>abs(dy)?-dx:dy)/86.f;nav_paint();}}
 if(c==LV_EVENT_RELEASED||c==LV_EVENT_PRESS_LOST){nav_pressed=0;if(dragged)nav_snap();}
 if(c==LV_EVENT_SHORT_CLICKED&&!dragged){int index=nav_selected;simple_nav_open(0);disco_catalog_open(index);simple_action_feedback();}
}
static void hub_event(lv_event_t*e){
 lv_event_code_t c=lv_event_get_code(e);
 if(c==LV_EVENT_LONG_PRESSED){hub_timer_cancel();hub_last=0;simple_nav_open(0);disco_nav_action_run(simple_cfg_get_int("disco_nav_hold",DISCO_NAV_MUSIC));return;}
 if(c!=LV_EVENT_SHORT_CLICKED)return;
 if(!nav_on){hub_last=0;simple_nav_open(1);onboarding_discovered(HINT_MENU);return;}
 uint32_t now=lv_tick_get();
 if(hub_last&&now-hub_last<330){hub_timer_cancel();hub_last=0;simple_nav_open(0);disco_nav_action_run(simple_cfg_get_int("disco_nav_double",DISCO_NAV_SEARCH));return;}
 hub_last=now;hub_timer_cancel();hub_timer=lv_timer_create(hub_close,330,NULL);
}
void simple_nav_refresh(void){nav_count=disco_catalog_load();for(int j=0;j<3;j++)nav_content[j]=-1;if(nav_on)nav_paint();}
void simple_nav_init(lv_obj_t*parent){
 lv_timer_set_period(lv_display_get_refr_timer(lv_display_get_default()),16);
 nav_root=lv_obj_create(parent);lv_obj_remove_style_all(nav_root);lv_obj_set_size(nav_root,360,360);lv_obj_remove_flag(nav_root,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_style_bg_color(nav_root,TC(SCRIM),0);lv_obj_set_style_bg_opa(nav_root,100,0);lv_obj_add_flag(nav_root,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(nav_root,outside,LV_EVENT_CLICKED,NULL);
 nav_panel=lv_obj_create(nav_root);lv_obj_remove_style_all(nav_panel);lv_obj_set_pos(nav_panel,-180,360);lv_obj_set_size(nav_panel,720,720);
 lv_obj_set_style_radius(nav_panel,LV_RADIUS_CIRCLE,0);lv_obj_set_style_clip_corner(nav_panel,1,0);lv_obj_set_style_bg_color(nav_panel,TC(CANVAS),0);lv_obj_set_style_bg_opa(nav_panel,255,0);
 lv_obj_set_style_border_color(nav_panel,TC(ACCENT_PRIMARY),0);lv_obj_set_style_border_width(nav_panel,2,0);

 lv_obj_remove_flag(nav_panel,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_flag(nav_panel,LV_OBJ_FLAG_CLICKABLE);
 ui_on(nav_panel,nav_event,LV_EVENT_ALL,NULL,"simple.nav.rotate.open",UI_CORE);
 for(int j=0;j<3;j++){
  nav_items[j]=lv_obj_create(nav_panel);lv_obj_remove_style_all(nav_items[j]);lv_obj_set_size(nav_items[j],180,160);lv_obj_remove_flag(nav_items[j],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t*ring=circle(nav_items[j],90,56,112);lv_obj_remove_flag(ring,LV_OBJ_FLAG_CLICKABLE);
  nav_icons[j]=label(ring,"",0,0,80,TF(UI_48),TC(ACCENT_PRIMARY));
  nav_names[j]=label(nav_items[j],"",-30,116,240,TF(UI_30),TC(TEXT_PRIMARY));
 }
 /* Fixed edge wash: ordinary gradient rectangles, clipped by the planet. */
 for(int side=0;side<2;side++){
  lv_obj_t*v=lv_obj_create(nav_panel);lv_obj_remove_style_all(v);lv_obj_set_pos(v,side?476:180,0);lv_obj_set_size(v,64,252);
  lv_obj_set_style_bg_color(v,TC(CANVAS),0);lv_obj_set_style_bg_grad_color(v,TC(CANVAS),0);lv_obj_set_style_bg_grad_dir(v,LV_GRAD_DIR_HOR,0);
  lv_obj_set_style_bg_opa(v,255,0);lv_obj_set_style_bg_main_opa(v,side?0:95,0);lv_obj_set_style_bg_grad_opa(v,side?95:0,0);lv_obj_remove_flag(v,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);kit_keep(v);
 }
 nav_pos=label(nav_panel,"",438,201,80,TF(UI_14),TC(TEXT_SECONDARY));
 lv_obj_add_flag(nav_pos,LV_OBJ_FLAG_HIDDEN);
 nav_hub=circle(parent,180,310,64);lv_obj_set_style_bg_color(nav_hub,TC(CANVAS),0);lv_obj_set_style_bg_opa(nav_hub,26,0);
 lv_obj_t*l=label(nav_hub,"D!",0,0,90,&diskos_splash_disco_72,TC(ACCENT_PRIMARY));lv_obj_set_style_transform_scale_x(l,112,0);lv_obj_set_style_transform_scale_y(l,112,0);lv_obj_set_style_transform_pivot_x(l,45,0);lv_obj_set_style_transform_pivot_y(l,36,0);lv_obj_center(l);
 ui_on(nav_hub,hub_event,LV_EVENT_ALL,NULL,"simple.nav.toggle.shortcuts",UI_CORE);
 simple_nav_refresh();lv_obj_add_flag(nav_root,LV_OBJ_FLAG_HIDDEN);
}
void simple_nav_screen(int scr){
 hub_timer_cancel();hub_last=0;nav_pressed=dragged=0;
 if(nav_panel){lv_anim_delete(nav_panel,nav_zoom);lv_anim_delete(nav_panel,nav_move);lv_obj_set_y(nav_panel,360);nav_phase=roundf(nav_phase);}
 if(nav_root){nav_on=0;lv_obj_add_flag(nav_root,LV_OBJ_FLAG_HIDDEN);}
 simple_qs_visible(scr==SCR_QUICK);
 if(!nav_hub)return;
 int show=scr==SCR_HOME;
 if(show){lv_obj_remove_flag(nav_hub,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(nav_hub);}else lv_obj_add_flag(nav_hub,LV_OBJ_FLAG_HIDDEN);
}
/* ---- Eight controls around circular artwork; rim arcs retain shared brightness/battery behaviour. ---- */
static lv_obj_t *qs_root,*qs_grid,*qs_bright,*qs_batt,*qs_batt_icon,*qs_pp_icon,*qs_pp_badge,*qs_cover,*qs_cover_img,*qs_progress;
static lv_obj_t *qs_tiles[8],*qs_glyphs[8];static int qs_ids[8],qs_n,qs_percent=-1,qs_charging;static lv_timer_t*qs_timer;
static int qs_last_playing=-1,qs_last_progress=-1,qs_last_mode=-1,qs_active[8];
static uint32_t qs_radio_tick;static int qs_radio_valid;
void simple_qs_visible(int visible){if(qs_timer){if(visible){lv_timer_resume(qs_timer);lv_timer_ready(qs_timer);}else lv_timer_pause(qs_timer);}if(visible)qs_radio_valid=0;}
static void qs_tile(lv_event_t*e){
 int i=(int)(intptr_t)lv_event_get_user_data(e);lv_event_code_t c=lv_event_get_code(e);if(i<0||i>=qs_n)return;
 if(c==LV_EVENT_LONG_PRESSED){int id=qs_ids[i];if(id==0||id==1)quicksettings_null_action(id,1);else ui_toast(quicksettings_item(id)->name);}
 else if(c==LV_EVENT_SHORT_CLICKED){qs_radio_valid=0;quicksettings_null_action(qs_ids[i],0);simple_action_feedback();}
}
static void qs_transport(lv_event_t*e){if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED)return;const char*g=ui_is_playing()?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY;if(ui_transport_command(lv_event_get_user_data(e))==0)simple_playback_feedback(g);}
static void qs_brightness(lv_event_t*e){int value=lv_arc_get_value(qs_bright);if(lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED)ui_backlight(value);else ui_set_brightness(value);}
static lv_obj_t *arc(lv_obj_t*p,int start,int end,int input){
 lv_obj_t*a=lv_arc_create(p);lv_obj_set_size(a,344,344);lv_obj_center(a);lv_arc_set_bg_angles(a,start,end);lv_arc_set_range(a,0,100);lv_arc_set_rotation(a,0);
 lv_obj_set_style_arc_width(a,7,LV_PART_MAIN);lv_obj_set_style_arc_width(a,7,LV_PART_INDICATOR);
 lv_obj_set_style_arc_color(a,TC(CONTROL_TRACK),LV_PART_MAIN);lv_obj_set_style_arc_color(a,TC(ACCENT_PRIMARY),LV_PART_INDICATOR);
 lv_obj_set_style_bg_color(a,TC(ACCENT_PRIMARY),LV_PART_KNOB);lv_obj_set_style_pad_all(a,3,LV_PART_KNOB);
 if(!input){lv_obj_remove_style(a,NULL,LV_PART_KNOB);lv_obj_remove_flag(a,LV_OBJ_FLAG_CLICKABLE);}else lv_obj_add_flag(a,LV_OBJ_FLAG_ADV_HITTEST);
 lv_obj_remove_flag(a,LV_OBJ_FLAG_SCROLLABLE);kit_keep(a);return a;
}
void simple_qs_battery(int pct,int charging){
 pct=pct<0?-1:pct>100?100:pct;charging=!!charging;
 if(qs_percent==pct&&qs_charging==charging&&qs_batt)return;
 qs_percent=pct;qs_charging=charging;if(!qs_batt)return;
 lv_arc_set_value(qs_batt,pct<0?100:pct);lv_obj_set_style_arc_opa(qs_batt,255,LV_PART_INDICATOR);lv_color_t c=charging?TC(STATUS_SUCCESS):pct>=0&&pct<15?TC(STATUS_DANGER):TC(ACCENT_PRIMARY);
 lv_obj_set_style_arc_color(qs_batt,c,LV_PART_INDICATOR);lv_obj_set_style_text_color(qs_batt_icon,c,0);
 lv_label_set_text(qs_batt_icon,charging?LV_SYMBOL_CHARGE:pct>=0&&pct<15?LV_SYMBOL_BATTERY_EMPTY:LV_SYMBOL_BATTERY_FULL);
}
void simple_qs_refresh(int playing){
 if(!qs_root||screen_current()!=SCR_QUICK)return;
 if(qs_pp_icon&&qs_last_playing!=!!playing){qs_last_playing=!!playing;lv_label_set_text(qs_pp_icon,playing?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);if(qs_pp_badge){lv_obj_set_style_bg_grad_dir(qs_pp_badge,LV_GRAD_DIR_NONE,0);lv_obj_set_style_bg_color(qs_pp_badge,playing?TC(ACCENT_PRIMARY):TC(CANVAS),0);lv_obj_set_style_bg_opa(qs_pp_badge,255,0);lv_obj_set_style_text_color(qs_pp_icon,playing?theme_on_color(TC(ACCENT_PRIMARY)):TC(ACCENT_PRIMARY),0);}}
 if(qs_bright&&!lv_obj_has_state(qs_bright,LV_STATE_PRESSED))lv_arc_set_value(qs_bright,ui_get_brightness());
 if(!qs_radio_valid||lv_tick_elaps(qs_radio_tick)>=1000){
  qs_radio_tick=lv_tick_get();qs_radio_valid=1;int wifi=wifi_radio_live()>0,bt=bt_state();
  for(int i=0;i<qs_n;i++){int active=qs_ids[i]==0?wifi:qs_ids[i]==1?bt:quicksettings_item_active(qs_ids[i]);if(active!=qs_active[i]){qs_active[i]=active;lv_obj_set_style_bg_grad_dir(qs_tiles[i],active==1?LV_GRAD_DIR_NONE:LV_GRAD_DIR_VER,0);lv_obj_set_style_bg_color(qs_tiles[i],active==1?TC(ACCENT_PRIMARY):theme_color_mix(TC(ACCENT_PRIMARY),TC(CANVAS),active==BT_TURNING_ON?60:38),0);lv_obj_set_style_text_color(qs_glyphs[i],active==1?theme_on_color(TC(ACCENT_PRIMARY)):TC(ACCENT_PRIMARY),0);lv_obj_set_style_border_opa(qs_tiles[i],active==BT_TURNING_ON?130:255,0);}}
 }
 for(int i=0;i<qs_n;i++)if(qs_ids[i]==quicksettings_playback_item()){const char*g=quicksettings_item_glyph(qs_ids[i]);if(strcmp(lv_label_get_text(qs_glyphs[i]),g)){lv_label_set_text(qs_glyphs[i],g);lv_obj_center(qs_glyphs[i]);}}
 int mode=ui_get_source_mode();if(mode!=qs_last_mode){qs_last_mode=mode;for(int i=0;i<qs_n;i++)if(qs_ids[i]==4){const char*g=mode==0?"\xEF\x83\xAC":quicksettings_item_glyph(4);lv_label_set_text(qs_glyphs[i],g);lv_obj_center(qs_glyphs[i]);}}
 if(qs_progress){track_state_t st;ipc_get_state(&st);int f=st.duration_ms>0?(int)(st.position_ms*1000LL/st.duration_ms):0;if(f<0)f=0;if(f>1000)f=1000;if(f!=qs_last_progress){qs_last_progress=f;lv_arc_set_value(qs_progress,f);}}
}
void simple_qs_art(const void*src){
 if(!qs_cover_img)return;
 extern unsigned ui_art_revision(void);static unsigned revision;static const void*previous;static lv_obj_t*target;
 unsigned current=ui_art_revision();if(target==qs_cover_img&&current==revision&&previous==src)return;
 target=qs_cover_img;revision=current;previous=src;
 lv_image_set_src(qs_cover_img,NULL);
 if(!src){lv_obj_add_flag(qs_cover_img,LV_OBJ_FLAG_HIDDEN);return;}
 lv_image_set_src(qs_cover_img,src);
 if(lv_image_src_get_type(src)==LV_IMAGE_SRC_VARIABLE){const lv_image_dsc_t*d=src;if(d->header.w)lv_image_set_scale(qs_cover_img,114*256/d->header.w+2);}
 lv_obj_center(qs_cover_img);lv_obj_remove_flag(qs_cover_img,LV_OBJ_FLAG_HIDDEN);
}
void simple_qs_track(const char*t,const char*a,int p){(void)t;(void)a;simple_qs_refresh(p);}
static void qs_tick(lv_timer_t*t){(void)t;if(screen_current()!=SCR_QUICK)return;simple_qs_refresh(ui_is_playing());
 if(qs_charging){int phase=lv_tick_get()%1600;lv_obj_set_style_arc_opa(qs_batt,140+115*(phase<800?phase:1600-phase)/800,LV_PART_INDICATOR);}
}
void simple_qs_build(void){
 if(!qs_root)return;
 if(qs_grid)lv_obj_delete(qs_grid);
 qs_grid=lv_obj_create(qs_root);lv_obj_remove_style_all(qs_grid);lv_obj_set_size(qs_grid,360,360);lv_obj_remove_flag(qs_grid,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 int ids[QS_MAX_ITEMS];qs_n=quicksettings_items_load(ids);if(qs_n>8)qs_n=8;
 static const int X[8]={134,226,69,291,69,291,134,226},Y[8]={69,69,134,134,226,226,291,291};
 for(int i=0;i<qs_n;i++){
  qs_ids[i]=ids[i];qs_active[i]=-1;qs_tiles[i]=circle(qs_grid,X[i],Y[i],68);
  const char*g=ids[i]==4&&ui_get_source_mode()==0?"\xEF\x83\xAC":quicksettings_item_glyph(ids[i]);lv_obj_t*l=qs_glyphs[i]=label(qs_tiles[i],g,0,0,68,(unsigned char)g[0]<128?TF(UI_26):TF(QS_ICON_36),TC(ACCENT_PRIMARY));lv_obj_center(l);
  ui_on(qs_tiles[i],qs_tile,LV_EVENT_ALL,(void*)(intptr_t)i,"simple.qs.item",UI_CORE);
 }
 qs_last_mode=-1;qs_radio_valid=0;simple_qs_refresh(ui_is_playing());
}
void simple_qs_create(lv_obj_t*r){
 qs_last_playing=qs_last_progress=-1;qs_root=r;lv_obj_set_style_bg_color(r,TC(CANVAS),0);lv_obj_set_style_bg_opa(r,255,0);
 qs_bright=arc(r,222,318,1);lv_arc_set_range(qs_bright,1,40);ui_on(qs_bright,qs_brightness,LV_EVENT_VALUE_CHANGED,NULL,"qs.brightness",UI_CORE);lv_obj_add_event_cb(qs_bright,qs_brightness,LV_EVENT_RELEASED,NULL);lv_obj_add_event_cb(qs_bright,qs_brightness,LV_EVENT_PRESS_LOST,NULL);
 qs_batt=arc(r,42,138,0);
 label(r,"\xEF\x86\x85",166,20,28,TF(QS_ICON_28),TC(ACCENT_PRIMARY));
 qs_batt_icon=label(r,LV_SYMBOL_BATTERY_FULL,166,314,28,TF(UI_20),TC(ACCENT_PRIMARY));
 simple_qs_build();
 qs_progress=arc(r,0,360,0);lv_obj_set_size(qs_progress,142,142);lv_obj_center(qs_progress);lv_arc_set_range(qs_progress,0,1000);lv_arc_set_rotation(qs_progress,270);lv_obj_set_style_arc_width(qs_progress,5,LV_PART_MAIN);lv_obj_set_style_arc_width(qs_progress,5,LV_PART_INDICATOR);
 qs_cover=circle(r,180,180,116);lv_obj_set_style_clip_corner(qs_cover,1,0);lv_obj_set_style_bg_color(qs_cover,TC(CANVAS),0);lv_obj_set_style_bg_opa(qs_cover,255,0);
 qs_cover_img=lv_image_create(qs_cover);lv_obj_remove_flag(qs_cover_img,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(qs_cover_img,LV_OBJ_FLAG_HIDDEN);kit_keep(qs_cover_img);
 lv_obj_t*wash=lv_obj_create(qs_cover);lv_obj_remove_style_all(wash);lv_obj_set_size(wash,116,116);lv_obj_set_style_bg_color(wash,TC(SCRIM),0);lv_obj_set_style_bg_opa(wash,55,0);lv_obj_remove_flag(wash,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);kit_keep(wash);
 lv_obj_t*badge=qs_pp_badge=circle(qs_cover,58,58,58);lv_obj_set_style_bg_color(badge,TC(CANVAS),0);lv_obj_set_style_bg_opa(badge,210,0);lv_obj_remove_flag(badge,LV_OBJ_FLAG_CLICKABLE);
 qs_pp_icon=label(badge,LV_SYMBOL_PLAY,0,0,58,TF(QS_ICON_36),TC(ACCENT_PRIMARY));lv_obj_center(qs_pp_icon);
 ui_on(qs_cover,qs_transport,LV_EVENT_SHORT_CLICKED,"0201000C0000","qs.pp",UI_CORE);
 simple_qs_art(ui_current_cover_dsc()?ui_current_cover_dsc():ui_current_sharp_img());{int pct=qs_percent,charging=qs_charging;qs_percent=-2;simple_qs_battery(pct,charging);}simple_qs_refresh(ui_is_playing());if(!qs_timer)qs_timer=lv_timer_create(qs_tick,100,NULL);simple_qs_visible(screen_current()==SCR_QUICK);
}
/* Scoped component styler: keep inherited Disco geometry; change only this theme's surfaces. */
static void simple_style(lv_obj_t*o,kit_role_t role){
 if(lv_obj_get_user_data(o)==&simple_label_tag||lv_obj_get_user_data(o)==&simple_control_tag)return;
 if(simple_music_protected(o)||o==nav_root||o==nav_panel||o==nav_hub)return;
 /* Simple owns control ink; Ring's filled-button ink assumes an opaque accent. */
 if(lv_obj_check_type(o,&lv_label_class)&&lv_label_get_long_mode(o)==LV_LABEL_LONG_SCROLL_CIRCULAR)lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
 switch(role){
 case KIT_TITLE:lv_obj_set_style_text_color(o,TC(ACCENT_PRIMARY),0);break;
 case KIT_BACK:case KIT_ICON_BUTTON:case KIT_BUTTON:{
  if(lv_obj_get_width(o)>=200&&lv_obj_get_height(o)>100){if(lv_obj_get_width(o)>=340&&lv_obj_get_height(o)>=340){lv_obj_set_style_bg_color(o,TC(CANVAS),0);lv_obj_set_style_bg_grad_dir(o,LV_GRAD_DIR_NONE,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_bg_opa(o,255,0);}break;}
  lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,TC(ACCENT_PRIMARY),0);
  simple_shade(o);lv_obj_set_style_bg_color(o,TC(ACCENT_PRIMARY),LV_STATE_PRESSED);lv_obj_set_style_bg_opa(o,85,LV_STATE_PRESSED);lv_obj_set_style_bg_color(o,TC(ACCENT_PRIMARY),LV_STATE_CHECKED);lv_obj_set_style_bg_grad_dir(o,LV_GRAD_DIR_NONE,LV_STATE_CHECKED);lv_obj_set_style_bg_opa(o,255,LV_STATE_CHECKED);lv_obj_set_style_text_color(o,theme_on_color(TC(ACCENT_PRIMARY)),LV_STATE_CHECKED);
  lv_obj_set_style_text_color(o,TC(TEXT_PRIMARY),0);lv_obj_set_style_text_color(o,TC(TEXT_DISABLED),LV_STATE_DISABLED);
  for(uint32_t i=0;i<lv_obj_get_child_count(o);i++){lv_obj_t*l=lv_obj_get_child(o,i);if(lv_obj_check_type(l,&lv_label_class)&&lv_obj_get_user_data(l)!=&simple_label_tag)lv_obj_set_style_text_color(l,lv_obj_has_state(o,LV_STATE_CHECKED)?theme_on_color(TC(ACCENT_PRIMARY)):TC(TEXT_PRIMARY),0);}
  break;}
 case KIT_ROW:lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,TC(ACCENT_PRIMARY),0);lv_obj_set_style_border_opa(o,55,0);break;
 case KIT_ROW_TEXT:lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);break;
 case KIT_SCRIM:lv_obj_set_style_bg_color(o,TC(CANVAS),0);lv_obj_set_style_bg_opa(o,255,0);break;
 case KIT_TEXT:{
  lv_obj_t*p=lv_obj_get_parent(o);
  if(p==screen_get_root(screen_current())&&lv_obj_get_y(o)>=20&&lv_obj_get_y(o)<=50)lv_obj_set_style_text_color(o,TC(ACCENT_PRIMARY),0);
  if(p&&lv_obj_has_flag(p,LV_OBJ_FLAG_CLICKABLE)&&lv_obj_get_event_count(p)>0&&lv_obj_get_width(p)<=200&&lv_obj_get_height(p)<=90)
   lv_obj_set_style_text_color(o,lv_obj_has_state(p,LV_STATE_DISABLED)?TC(TEXT_DISABLED):lv_obj_has_state(p,LV_STATE_CHECKED)?theme_on_color(TC(ACCENT_PRIMARY)):TC(TEXT_PRIMARY),0);
  break;}
 case KIT_TEXTAREA:lv_obj_set_style_radius(o,22,0);lv_obj_set_style_border_color(o,TC(ACCENT_PRIMARY),0);lv_obj_set_style_border_width(o,1,0);break;
 case KIT_SLIDER:case KIT_BAR:
  lv_obj_set_style_bg_color(o,TC(BORDER_STRONG),LV_PART_MAIN);lv_obj_set_style_bg_color(o,TC(ACCENT_PRIMARY),LV_PART_INDICATOR);lv_obj_set_style_bg_color(o,TC(ACCENT_PRIMARY),LV_PART_KNOB);break;
 case KIT_POPUP:lv_obj_set_style_bg_color(o,TC(CANVAS),0);lv_obj_set_style_border_color(o,TC(ACCENT_PRIMARY),0);lv_obj_set_style_radius(o,24,0);break;
 default:break;
 }
}
static void simple_back(lv_event_t*e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)screen_back();}
static lv_obj_t*simple_header(lv_obj_t*r,const char*t,lv_event_cb_t cb){
 const char*shown=!strcmp(t,"Disco Options")?"Simple":!strcmp(t,"Disco Colours")?"Colours":!strcmp(t,"Disco Layout")?"Layout":t;
 lv_obj_t*b=circle(r,92,44,36);lv_obj_set_ext_click_area(b,6);label(b,LV_SYMBOL_LEFT,0,6,36,TF(UI_20),TC(ACCENT_PRIMARY));
 ui_on(b,cb?cb:simple_back,LV_EVENT_CLICKED,NULL,"nav.back",UI_CORE);
 lv_point_t size;const lv_font_t*f=TF(HEADER);lv_text_get_size(&size,shown,f,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
 if(size.x>152){f=TF(UI_22);lv_text_get_size(&size,shown,f,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);if(size.x>152)f=TF(UI_20);}
 lv_obj_t*l=label(r,shown,116,30,160,f,TC(ACCENT_PRIMARY));lv_obj_set_height(l,40);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);return l;
}
const theme_kit_t theme_kit_simple={.id="simple",.style=simple_style,.header=simple_header};

void simple_test_nav_phase(int v){nav_phase=v/1000.0f;nav_paint();}
int simple_test_nav_bindings(void){return nav_bindings;}
int simple_test_nav_y(void){return nav_panel?lv_obj_get_y(nav_panel):360;}
int simple_test_qs_paused(void){return !qs_timer||lv_timer_get_paused(qs_timer);}
int simple_test_selection(void){return nav_selected;}
int simple_test_qs_count(void){return qs_n;}
lv_obj_t*simple_test_qs_button(int slot){return slot>=0&&slot<qs_n?qs_tiles[slot]:NULL;}

int simple_owns_point(int x,int y){if(!th_simple())return 0;if(nav_on)return 1;return nav_hub&&!lv_obj_has_flag(nav_hub,LV_OBJ_FLAG_HIDDEN)&&(x-180)*(x-180)+(y-310)*(y-310)<=36*36;}

/* Acknowledgement animation: a single upward arrow leaves the logo's position. */
static lv_obj_t *action_arrow;
static void arrow_frame(void*o,int32_t v){
 lv_obj_set_y(o,288-112*v/1000);
 int opacity=v<300?255:255*(1000-v)/700;if(opacity<0)opacity=0;
 lv_obj_set_style_opa(o,opacity,0);
}
static void arrow_done(lv_anim_t*a){lv_obj_add_flag(a->var,LV_OBJ_FLAG_HIDDEN);}
void simple_playback_feedback(const char *glyph){
 if(!th_simple())return;
 if(!action_arrow){action_arrow=label(lv_layer_top(),LV_SYMBOL_UP,156,288,48,TF(QS_ICON_36),TC(ACCENT_PRIMARY));lv_obj_remove_flag(action_arrow,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);}
 lv_label_set_text(action_arrow,glyph?glyph:LV_SYMBOL_UP);lv_obj_set_style_text_color(action_arrow,TC(ACCENT_PRIMARY),0);
 lv_anim_delete(action_arrow,arrow_frame);lv_obj_remove_flag(action_arrow,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(action_arrow);
 lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,action_arrow);lv_anim_set_exec_cb(&a,arrow_frame);lv_anim_set_values(&a,0,1000);lv_anim_set_duration(&a,300);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_set_completed_cb(&a,arrow_done);lv_anim_start(&a);
}
void simple_action_feedback(void){simple_playback_feedback(LV_SYMBOL_UP);}
void simple_test_feedback_frame(int v){simple_action_feedback();lv_anim_delete(action_arrow,arrow_frame);arrow_frame(action_arrow,v);}
