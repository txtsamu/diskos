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
extern const theme_kit_t theme_kit_ring;
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
  if(!strcmp(k,"disco_sheen")||!strcmp(k,"disco_side")||!strcmp(k,"disco_title_al")||!strcmp(k,"disco_prog_shape"))return 0;
 }
 return cfg_get_int(scope(k,b,sizeof b),d);
}
const char *simple_cfg_get_str(const char*k,const char*d){char b[64];return cfg_get_str(scope(k,b,sizeof b),d);}
int simple_cfg_set_int(const char*k,int v){char b[64];return cfg_set_int(scope(k,b,sizeof b),v);}
int simple_cfg_set_str(const char*k,const char*v){char b[64];return cfg_set_str(scope(k,b,sizeof b),v);}
int simple_cfg_set_int_deferred(const char*k,int v){char b[64];return cfg_set_int_deferred(scope(k,b,sizeof b),v);}
int simple_cfg_set_str_deferred(const char*k,const char*v){char b[64];return cfg_set_str_deferred(scope(k,b,sizeof b),v);}
static lv_obj_t *label(lv_obj_t*p,const char*s,int x,int y,int w,const lv_font_t*f,lv_color_t c){
 lv_obj_t*l=lv_label_create(p);lv_label_set_text(l,s);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);
 lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);lv_obj_set_style_text_font(l,f,0);lv_obj_set_style_text_color(l,c,0);
 lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_remove_flag(l,LV_OBJ_FLAG_CLICKABLE);kit_keep(l);return l;
}
static lv_obj_t *circle(lv_obj_t*p,int cx,int cy,int size){
 lv_obj_t*b=lv_button_create(p);lv_obj_remove_style_all(b);lv_obj_set_size(b,size,size);lv_obj_set_pos(b,cx-size/2,cy-size/2);
 lv_obj_set_style_radius(b,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(b,2,0);lv_obj_set_style_border_color(b,TC(ACCENT_PRIMARY),0);
 lv_obj_set_style_bg_color(b,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(b,24,0);lv_obj_set_style_bg_opa(b,90,LV_STATE_PRESSED);
 lv_obj_remove_flag(b,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);kit_keep(b);return b;
}
/* ---- One-item wheel; rotation is about the bottom hub, never the screen centre. ---- */
static lv_obj_t *nav_root,*nav_panel,*nav_hub,*nav_items[3],*nav_icons[3],*nav_names[3],*nav_pos;
static int nav_on,nav_count,nav_selected,nav_screen=SCR_HOME,dragged,nav_pressed;
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
  float delta=(base+off-nav_phase),a=delta*0.95f;
  int x=150+(int)lroundf(sinf(a)*137),y=270-(int)lroundf(cosf(a)*137);
  lv_obj_set_pos(nav_items[j],x-56,y-48);
  int opa=(int)(255*(1-fminf(1,fabsf(delta))));
  lv_obj_set_style_opa(nav_items[j],opa,0);
  const char *ic=disco_catalog_icon(index);lv_label_set_text(nav_icons[j],ic);disco_vec_icon(nav_icons[j],ic,40);lv_obj_center(nav_icons[j]);
  lv_label_set_text(nav_names[j],disco_catalog_name(index));
 }
 nav_selected=((int)lroundf(nav_phase)%nav_count+nav_count)%nav_count;
 char b[24];snprintf(b,sizeof b,"%d / %d",nav_selected+1,nav_count);lv_label_set_text(nav_pos,b);
}
static void nav_move(void*o,int32_t v){(void)o;nav_phase=v/1000.f;nav_paint();}
static void nav_snap(void){
 lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,nav_panel);lv_anim_set_exec_cb(&a,nav_move);
 lv_anim_set_values(&a,(int)(nav_phase*1000),(int)lroundf(nav_phase)*1000);lv_anim_set_duration(&a,150);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_start(&a);
}
static void nav_zoom(void*o,int32_t v){
 lv_obj_set_style_transform_scale_x(o,v,0);lv_obj_set_style_transform_scale_y(o,v,0);lv_obj_set_style_opa(o,v>255?255:v,0);
}
static void nav_closed(lv_anim_t*a){(void)a;if(!nav_on&&nav_root)lv_obj_add_flag(nav_root,LV_OBJ_FLAG_HIDDEN);}
void simple_nav_open(int open){
 if(!nav_root)return;nav_on=!!open;lv_obj_remove_flag(nav_root,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(nav_root);lv_obj_move_foreground(nav_hub);
 lv_anim_delete(nav_panel,nav_zoom);lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,nav_panel);lv_anim_set_exec_cb(&a,nav_zoom);
 lv_anim_set_values(&a,lv_obj_get_style_transform_scale_x(nav_panel,0),open?256:24);lv_anim_set_duration(&a,open?190:130);
 lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_set_completed_cb(&a,nav_closed);lv_anim_start(&a);nav_paint();
}
int simple_nav_is_open(void){return nav_on;}
static void outside(lv_event_t*e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)simple_nav_open(0);}
static void nav_event(lv_event_t*e){
 lv_event_code_t c=lv_event_get_code(e);lv_indev_t*in=lv_indev_active();lv_point_t p={0,0};if(in)lv_indev_get_point(in,&p);
 if(c==LV_EVENT_PRESSED){nav_down=p;nav_start=nav_phase;dragged=0;nav_pressed=1;lv_anim_delete(nav_panel,nav_move);}
 if(c==LV_EVENT_PRESSING&&nav_pressed){int dx=p.x-nav_down.x,dy=p.y-nav_down.y;if(abs(dx)+abs(dy)>10)dragged=1;
  if(dragged){nav_phase=nav_start+(abs(dx)>abs(dy)?-dx:dy)/86.f;nav_paint();}}
 if(c==LV_EVENT_RELEASED||c==LV_EVENT_PRESS_LOST){nav_pressed=0;if(dragged)nav_snap();}
 if(c==LV_EVENT_SHORT_CLICKED&&!dragged){int index=nav_selected;simple_nav_open(0);disco_catalog_open(index);}
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
void simple_nav_refresh(void){nav_count=disco_catalog_load();nav_paint();}
void simple_nav_init(lv_obj_t*parent){
 nav_root=lv_obj_create(parent);lv_obj_remove_style_all(nav_root);lv_obj_set_size(nav_root,360,360);lv_obj_remove_flag(nav_root,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_style_bg_color(nav_root,TC(SCRIM),0);lv_obj_set_style_bg_opa(nav_root,100,0);lv_obj_add_flag(nav_root,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(nav_root,outside,LV_EVENT_CLICKED,NULL);
 nav_panel=lv_obj_create(nav_root);lv_obj_remove_style_all(nav_panel);lv_obj_set_pos(nav_panel,30,30);lv_obj_set_size(nav_panel,300,300);
 lv_obj_set_style_radius(nav_panel,LV_RADIUS_CIRCLE,0);lv_obj_set_style_clip_corner(nav_panel,1,0);lv_obj_set_style_bg_color(nav_panel,TC(CANVAS),0);lv_obj_set_style_bg_opa(nav_panel,255,0);
 lv_obj_set_style_border_color(nav_panel,TC(ACCENT_PRIMARY),0);lv_obj_set_style_border_width(nav_panel,2,0);
 lv_obj_set_style_transform_pivot_x(nav_panel,150,0);lv_obj_set_style_transform_pivot_y(nav_panel,280,0);lv_obj_set_style_transform_scale_x(nav_panel,24,0);lv_obj_set_style_transform_scale_y(nav_panel,24,0);
 lv_obj_remove_flag(nav_panel,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_flag(nav_panel,LV_OBJ_FLAG_CLICKABLE);
 ui_on(nav_panel,nav_event,LV_EVENT_ALL,NULL,"simple.nav.rotate.open",UI_CORE);
 for(int j=0;j<3;j++){
  nav_items[j]=lv_obj_create(nav_panel);lv_obj_remove_style_all(nav_items[j]);lv_obj_set_size(nav_items[j],112,122);lv_obj_remove_flag(nav_items[j],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t*ring=circle(nav_items[j],56,44,88);lv_obj_remove_flag(ring,LV_OBJ_FLAG_CLICKABLE);
  nav_icons[j]=label(ring,"",0,0,56,TF(UI_40),TC(ACCENT_PRIMARY));
  nav_names[j]=label(nav_items[j],"",-25,94,162,TF(UI_24),TC(TEXT_PRIMARY));
 }
 nav_pos=label(nav_panel,"",100,222,100,TF(UI_14),TC(TEXT_SECONDARY));
 nav_hub=circle(parent,180,310,64);lv_obj_set_style_bg_color(nav_hub,TC(CANVAS),0);lv_obj_set_style_bg_opa(nav_hub,26,0);
 lv_obj_t*l=label(nav_hub,"D!",0,0,90,&diskos_splash_disco_72,TC(ACCENT_PRIMARY));lv_obj_set_style_transform_scale_x(l,112,0);lv_obj_set_style_transform_scale_y(l,112,0);lv_obj_set_style_transform_pivot_x(l,45,0);lv_obj_set_style_transform_pivot_y(l,36,0);lv_obj_center(l);
 ui_on(nav_hub,hub_event,LV_EVENT_ALL,NULL,"simple.nav.toggle.shortcuts",UI_CORE);
 simple_nav_refresh();lv_obj_add_flag(nav_root,LV_OBJ_FLAG_HIDDEN);
}
void simple_nav_screen(int scr){
 hub_timer_cancel();hub_last=0;nav_screen=scr;if(nav_root){nav_on=0;lv_obj_add_flag(nav_root,LV_OBJ_FLAG_HIDDEN);}
 if(!nav_hub)return;
 int show=scr==SCR_HOME;
 if(show){lv_obj_remove_flag(nav_hub,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(nav_hub);}else lv_obj_add_flag(nav_hub,LV_OBJ_FLAG_HIDDEN);
}
/* ---- Ring-inspired QS: eight icon-only settings, an independent transport cluster, rim arcs. ---- */
static lv_obj_t *qs_root,*qs_grid,*qs_bright,*qs_batt,*qs_batt_icon,*qs_pp_icon,*qs_clock;
static lv_obj_t *qs_tiles[8];static int qs_ids[8],qs_n,qs_percent,qs_charging;static lv_timer_t*qs_timer;
static void qs_tile(lv_event_t*e){
 int i=(int)(intptr_t)lv_event_get_user_data(e);lv_event_code_t c=lv_event_get_code(e);if(i<0||i>=qs_n)return;
 if(c==LV_EVENT_LONG_PRESSED){int id=qs_ids[i];if(id==0||id==1)quicksettings_null_action(id,1);else ui_toast(quicksettings_item(id)->name);}
 else if(c==LV_EVENT_SHORT_CLICKED)quicksettings_null_action(qs_ids[i],0);
}
static void qs_transport(lv_event_t*e){if(lv_event_get_code(e)==LV_EVENT_SHORT_CLICKED)ui_transport_command(lv_event_get_user_data(e));}
static void qs_brightness(lv_event_t*e){if(lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED)ui_set_brightness(lv_arc_get_value(qs_bright));}
static lv_obj_t *arc(lv_obj_t*p,int start,int end,int input){
 lv_obj_t*a=lv_arc_create(p);lv_obj_set_size(a,344,344);lv_obj_center(a);lv_arc_set_bg_angles(a,start,end);lv_arc_set_range(a,0,100);lv_arc_set_rotation(a,0);
 lv_obj_set_style_arc_width(a,7,LV_PART_MAIN);lv_obj_set_style_arc_width(a,7,LV_PART_INDICATOR);
 lv_obj_set_style_arc_color(a,TC(CONTROL_TRACK),LV_PART_MAIN);lv_obj_set_style_arc_color(a,TC(ACCENT_PRIMARY),LV_PART_INDICATOR);
 lv_obj_set_style_bg_color(a,TC(ACCENT_PRIMARY),LV_PART_KNOB);lv_obj_set_style_pad_all(a,3,LV_PART_KNOB);
 if(!input){lv_obj_remove_style(a,NULL,LV_PART_KNOB);lv_obj_remove_flag(a,LV_OBJ_FLAG_CLICKABLE);}else lv_obj_add_flag(a,LV_OBJ_FLAG_ADV_HITTEST);
 lv_obj_remove_flag(a,LV_OBJ_FLAG_SCROLLABLE);kit_keep(a);return a;
}
void simple_qs_battery(int pct,int charging){
 qs_percent=pct;qs_charging=charging;if(!qs_batt)return;
 lv_arc_set_value(qs_batt,pct);lv_color_t c=charging?TC(STATUS_SUCCESS):pct<15?TC(STATUS_DANGER):TC(ACCENT_PRIMARY);
 lv_obj_set_style_arc_color(qs_batt,c,LV_PART_INDICATOR);lv_obj_set_style_text_color(qs_batt_icon,c,0);
 lv_label_set_text(qs_batt_icon,charging?LV_SYMBOL_CHARGE:pct<15?LV_SYMBOL_BATTERY_EMPTY:LV_SYMBOL_BATTERY_FULL);
}
void simple_qs_refresh(int playing){
 if(!qs_root)return;if(qs_pp_icon)lv_label_set_text(qs_pp_icon,playing?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
 if(qs_bright&&!lv_obj_has_state(qs_bright,LV_STATE_PRESSED))lv_arc_set_value(qs_bright,ui_get_brightness());
 for(int i=0;i<qs_n;i++){int active=qs_ids[i]==0?wifi_radio_live()>0:qs_ids[i]==1?bt_state()>0:0;lv_obj_set_style_bg_opa(qs_tiles[i],active?95:20,0);}
}
void simple_qs_track(const char*t,const char*a,int p){(void)t;(void)a;simple_qs_refresh(p);}
static void qs_tick(lv_timer_t*t){(void)t;if(screen_current()!=SCR_QUICK)return;simple_qs_refresh(ui_is_playing());
 time_t now=time(NULL);struct tm tm;localtime_r(&now,&tm);char clock[12];strftime(clock,sizeof clock,"%H:%M",&tm);lv_label_set_text(qs_clock,clock);
 if(qs_charging)lv_obj_set_style_arc_opa(qs_batt,140+(lv_tick_get()/35)%115,LV_PART_INDICATOR);
}
void simple_qs_build(void){
 if(!qs_root)return;if(qs_grid)lv_obj_delete(qs_grid);
 qs_grid=lv_obj_create(qs_root);lv_obj_remove_style_all(qs_grid);lv_obj_set_size(qs_grid,360,360);lv_obj_remove_flag(qs_grid,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 int ids[QS_MAX_ITEMS];qs_n=quicksettings_items_load(ids);if(qs_n>8)qs_n=8;
 static const int X[8]={108,252,70,290,70,290,108,252},Y[8]={76,76,143,143,217,217,284,284};
 for(int i=0;i<qs_n;i++){
  qs_ids[i]=ids[i];qs_tiles[i]=circle(qs_grid,X[i],Y[i],56);
  label(qs_tiles[i],quicksettings_item_glyph(ids[i]),0,12,56,TF(QS_ICON_28),TC(ACCENT_PRIMARY));
  ui_on(qs_tiles[i],qs_tile,LV_EVENT_ALL,(void*)(intptr_t)i,"simple.qs.item",UI_CORE);
 }
 simple_qs_refresh(ui_is_playing());
}
void simple_qs_create(lv_obj_t*r){
 qs_root=r;lv_obj_set_style_bg_color(r,TC(CANVAS),0);lv_obj_set_style_bg_opa(r,255,0);
 qs_bright=arc(r,222,318,1);ui_on(qs_bright,qs_brightness,LV_EVENT_VALUE_CHANGED,NULL,"qs.brightness",UI_CORE);
 qs_batt=arc(r,42,138,0);
 label(r,"\xEF\x86\x85",162,35,36,TF(QS_ICON_28),TC(ACCENT_PRIMARY));
 qs_batt_icon=label(r,LV_SYMBOL_BATTERY_FULL,164,310,32,TF(UI_20),TC(ACCENT_PRIMARY));
 qs_clock=label(r,"--:--",140,12,80,TF(UI_18),TC(TEXT_PRIMARY));
 simple_qs_build();
 const void*art=ui_current_cover_dsc();lv_obj_t*cover=lv_obj_create(r);lv_obj_remove_style_all(cover);lv_obj_set_size(cover,102,102);lv_obj_set_pos(cover,129,113);
 lv_obj_set_style_radius(cover,LV_RADIUS_CIRCLE,0);lv_obj_set_style_clip_corner(cover,1,0);lv_obj_remove_flag(cover,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 if(art){lv_obj_t*im=lv_image_create(cover);lv_image_set_src(im,art);lv_image_set_scale(im,180);lv_obj_center(im);}
 lv_obj_t*pp=circle(r,180,164,76);lv_obj_set_style_bg_color(pp,TC(CANVAS),0);lv_obj_set_style_bg_opa(pp,225,0);
 qs_pp_icon=label(pp,LV_SYMBOL_PLAY,0,22,76,TF(UI_28),TC(ACCENT_PRIMARY));ui_on(pp,qs_transport,LV_EVENT_SHORT_CLICKED,"0201000C0000","qs.pp",UI_CORE);
 static const char*cmd[]={"0201000C0002","0201000C0001"};static const char*gl[]={LV_SYMBOL_PREV,LV_SYMBOL_NEXT};
 for(int i=0;i<2;i++){lv_obj_t*b=circle(r,i?211:149,252,48);label(b,gl[i],0,13,48,TF(UI_20),TC(ACCENT_PRIMARY));ui_on(b,qs_transport,LV_EVENT_SHORT_CLICKED,(void*)cmd[i],i?"qs.next":"qs.prev",UI_CORE);}
 simple_qs_battery(qs_percent?qs_percent:67,qs_charging);if(!qs_timer)qs_timer=lv_timer_create(qs_tick,100,NULL);
}
/* Scoped component styler: keep inherited Disco geometry; change only this theme's surfaces. */
static void simple_style(lv_obj_t*o,kit_role_t role){
 if(simple_music_protected(o)||o==nav_root||o==nav_panel||o==nav_hub)return;
 if(theme_kit_ring.style)theme_kit_ring.style(o,role);
 if(lv_obj_check_type(o,&lv_label_class)&&lv_label_get_long_mode(o)==LV_LABEL_LONG_SCROLL_CIRCULAR)lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
 switch(role){
 case KIT_TITLE:lv_obj_set_style_text_color(o,TC(ACCENT_PRIMARY),0);break;
 case KIT_BACK:case KIT_ICON_BUTTON:case KIT_BUTTON:{
  int w=lv_obj_get_width(o),h=lv_obj_get_height(o);if(w>0&&w<80&&h>0&&h<80){int d=w>h?w:h;if(d<44)d=44;lv_obj_set_size(o,d,d);}
  lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,TC(ACCENT_PRIMARY),0);
  lv_obj_set_style_bg_color(o,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(o,22,0);lv_obj_set_style_bg_opa(o,85,LV_STATE_PRESSED);break;}
 case KIT_ROW:lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,TC(ACCENT_PRIMARY),0);lv_obj_set_style_border_opa(o,55,0);break;
 case KIT_ROW_TEXT:lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);break;
 case KIT_SCRIM:lv_obj_set_style_bg_color(o,TC(CANVAS),0);lv_obj_set_style_bg_opa(o,255,0);break;
 case KIT_TEXT:break;
 default:break;
 }
}
static void simple_back(lv_event_t*e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)screen_back();}
static lv_obj_t*simple_header(lv_obj_t*r,const char*t,lv_event_cb_t cb){
 const char*shown=!strcmp(t,"Disco Options")?"Simple":!strcmp(t,"Disco Colours")?"Colours":!strcmp(t,"Disco Layout")?"Layout":t;
 lv_obj_t*b=circle(r,92,44,44);label(b,LV_SYMBOL_LEFT,0,10,44,TF(UI_20),TC(ACCENT_PRIMARY));
 ui_on(b,cb?cb:simple_back,LV_EVENT_CLICKED,NULL,"nav.back",UI_CORE);
 lv_obj_t*l=label(r,shown,120,26,168,TF(HEADER),TC(ACCENT_PRIMARY));lv_obj_set_height(l,40);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);return l;
}
const theme_kit_t theme_kit_simple={.id="simple",.style=simple_style,.header=simple_header};

int simple_test_selection(void){return nav_selected;}
int simple_test_qs_count(void){return qs_n;}

int simple_owns_point(int x,int y){if(!th_simple())return 0;if(nav_on)return 1;return nav_hub&&!lv_obj_has_flag(nav_hub,LV_OBJ_FLAG_HIDDEN)&&(x-180)*(x-180)+(y-310)*(y-310)<=36*36;}
