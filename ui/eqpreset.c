#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screens.h"
#include "braun.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "musicdb.h"
#include "fwcaps.h"
#include "eqrate.h"
#include "eqprofile.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static lv_obj_t *name_label,*edit_button,*rename_button,*switch_button,*switch_label,*note_label;
static int rename_preset;
static lv_obj_t *switch_pointer;
static const char *const names[]={"Off","Jazz","Rock","R&B","Hip-Hop","Pop","Dance","Classical","Retro","Sibilance 1","Sibilance 2"};
static int selected(void){int p=simple_cfg_get_int("eq_preset",0);return p<0?0:p>20?20:p;}
static void name_for(int p,char *b,size_t n){
 if(p<=10){snprintf(b,n,"%s",names[p]);return;}
 char key[24];snprintf(key,sizeof key,"eq_name%d",p);const char *s=simple_cfg_get_str(key,NULL);
 if(s && *s)snprintf(b,n,"%s",s);else snprintf(b,n,"USER%d",p-10);
}
static unsigned profile_ticket;
static lv_timer_t *profile_timer;
static void profile_paint(int p,int read,int graphic,const int hz[10]){
 int user=p>=11 && read>=0 && graphic;
 if(user){lv_obj_remove_state(edit_button,LV_STATE_DISABLED);lv_obj_remove_state(rename_button,LV_STATE_DISABLED);}
 else {lv_obj_add_state(edit_button,LV_STATE_DISABLED);lv_obj_add_state(rename_button,LV_STATE_DISABLED);}
 lv_label_set_text(note_label,read==-2?"Loading profile...":read<0?"Profile unavailable":p<11?"Built-in preset":!graphic?"Parametric profile":!fw_custom_peq_curve_writable(hz,10)?"Custom editing unavailable":"Editable profile");
}
static void profile_poll(lv_timer_t *timer){
 (void)timer;if(screen_current()!=SCR_EQ || !profile_ticket)return;
 eq_profile_t result;
 if(eq_profile_take(profile_ticket,&result)){
  profile_ticket=0;
  if(result.preset==selected())profile_paint(result.preset,result.read,result.graphic,result.hz);
 }
}
void eqpreset_refresh(void){
 if(!name_label)return;if(fw_os_ver()==257)eq_rate_request();int p=selected();char b[80];name_for(p,b,sizeof b);if(th_null()){char up[80];theme_upper(up,sizeof up,b);lv_label_set_text(name_label,up);}else lv_label_set_text(name_label,b);
 profile_ticket=0;
 if(p>=11){profile_ticket=eq_profile_request(p);profile_paint(p,profile_ticket?-2:-1,0,NULL);}
 else profile_paint(p,0,0,NULL);
 lv_label_set_text(switch_label,p?"On":"Off");
 if(switch_pointer&&th_braun())br_pointer_set(switch_pointer,58,p);
 if(switch_pointer)lv_obj_set_style_bg_color(switch_pointer,p?theme_on_color(ui_current_accent()):TC(TEXT_MUTED),0);
 lv_obj_set_style_bg_color(switch_button,p?ui_current_accent():TC(SURFACE),0);
 lv_obj_set_style_text_color(switch_label,p?theme_on_color(ui_current_accent()):TC(TEXT_PRIMARY),0);
 if(th_simple()){lv_obj_set_style_bg_color(switch_button,theme_color_mix(TC(ACCENT_PRIMARY),TC(CANVAS),p?60:24),0);lv_obj_set_style_text_color(switch_label,TC(TEXT_PRIMARY),0);lv_obj_add_flag(switch_pointer,LV_OBJ_FLAG_HIDDEN);}
 if(th_null()){
  lv_obj_set_style_bg_opa(switch_button,0,0);lv_obj_set_style_border_width(switch_button,1,0);
  lv_obj_set_style_border_color(switch_button,p?TC(TEXT_PRIMARY):TC(TEXT_SECONDARY),0);
  lv_obj_set_style_text_color(switch_label,p?TC(TEXT_PRIMARY):TC(TEXT_SECONDARY),0);
 }
}
static void cycle(lv_event_t *e){int dir=(int)(intptr_t)lv_event_get_user_data(e);int p=(selected()+dir+21)%21;
 if(ui_eq_select(p)<0){ui_toast("Couldn't switch EQ");return;}eqpreset_refresh();}
static void toggle(lv_event_t *e){(void)e;int p=0;if(!selected()){p=simple_cfg_get_int("eq_last",1);if(p<1||p>20)p=1;}
 if(ui_eq_select(p)<0){ui_toast("Couldn't switch EQ");return;}eqpreset_refresh();}
static void edit(lv_event_t *e){(void)e;screen_show(SCR_EQ_EDITOR);}
static void renamed(const char *text){if(!text||!*text)return;char key[24];snprintf(key,sizeof key,"eq_name%d",rename_preset);
 if(simple_cfg_set_str(key,text)){ui_toast("Couldn't save profile name");return;}eqpreset_refresh();}
static void rename_cb(lv_event_t *e){(void)e;rename_preset=selected();char b[80];name_for(rename_preset,b,sizeof b);kbinput_open("Rename EQ",b,renamed);}
static lv_obj_t *button(lv_obj_t *root,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,void *data,const char *action){
 lv_obj_t *b=lv_button_create(root);lv_obj_set_size(b,w,h);lv_obj_align(b,LV_ALIGN_TOP_MID,x,y);
 lv_obj_set_style_bg_color(b,TC(SURFACE),0);lv_obj_set_style_radius(b,w==h?LV_RADIUS_CIRCLE:14,0);lv_obj_set_style_border_width(b,0,0);
 lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_set_style_text_font(l,TF(UI_18),0);lv_obj_set_style_text_color(l,TC(TEXT_PRIMARY),0);lv_obj_center(l);
 ui_on(b,cb,LV_EVENT_CLICKED,data,action,UI_CORE);return b;
}
void eqpreset_create(lv_obj_t *root){
 lv_obj_set_style_bg_color(root,TC(CANVAS),0);ui_header(root,"Equalizer");
 lv_obj_t *prev_b=button(root,LV_SYMBOL_LEFT,-118,123,42,42,cycle,(void *)(intptr_t)-1,"eq.previous");
 lv_obj_t *next_b=button(root,LV_SYMBOL_RIGHT,118,123,42,42,cycle,(void *)(intptr_t)1,"eq.next");
 name_label=lv_label_create(root);lv_obj_set_width(name_label,178);lv_label_set_long_mode(name_label,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_align(name_label,LV_ALIGN_TOP_MID,0,128);
 lv_obj_set_style_text_align(name_label,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(name_label,TF(UI_24),0);lv_obj_set_style_text_color(name_label,TC(TEXT_PRIMARY),0);
 edit_button=button(root,"Edit",-60,183,104,44,edit,NULL,"eq.edit");rename_button=button(root,"Rename",60,183,104,44,rename_cb,NULL,"eq.rename");
 note_label=lv_label_create(root);lv_obj_set_width(note_label,280);lv_obj_align(note_label,LV_ALIGN_TOP_MID,0,244);lv_obj_set_style_text_align(note_label,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(note_label,TF(UI_12),0);lv_obj_set_style_text_color(note_label,TC(TEXT_SECONDARY),0);
 switch_button=button(root,"Off",0,274,58,58,toggle,NULL,"eq.toggle");switch_label=lv_obj_get_child(switch_button,0);
 switch_pointer=lv_obj_create(switch_button);lv_obj_remove_style_all(switch_pointer);
 lv_obj_set_size(switch_pointer,3,9);lv_obj_align(switch_pointer,LV_ALIGN_TOP_MID,10,4);
 lv_obj_set_style_bg_opa(switch_pointer,LV_OPA_COVER,0);lv_obj_set_style_radius(switch_pointer,1,0);
 lv_obj_set_style_transform_rotation(switch_pointer,300,0);lv_obj_clear_flag(switch_pointer,LV_OBJ_FLAG_CLICKABLE);
 if(th_disco()){   /* fork: Disco keeps the navigation circle's sliver on the right rim - the controls step left of it */
  for(uint32_t k=0;k<lv_obj_get_child_count(root);k++){
   lv_obj_t *o=lv_obj_get_child(root,(int32_t)k);
   if(o==name_label||o==edit_button||o==rename_button||o==note_label||o==switch_button) lv_obj_set_style_translate_x(o,disco_mirror()?20:-20,0);   /* Left: step right of it */
  }
  lv_obj_set_width(name_label,150);
  lv_obj_set_style_translate_x(next_b,disco_mirror()?4:-36,0); lv_obj_set_style_translate_x(prev_b,disco_mirror()?36:-4,0);
  lv_obj_add_flag(switch_pointer,LV_OBJ_FLAG_HIDDEN);
 }
 if(th_null()){
  lv_label_set_long_mode(name_label,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_width(name_label,192);
  lv_obj_align(name_label,LV_ALIGN_TOP_MID,0,125);lv_obj_set_style_text_font(name_label,TF(HEADER_SM),0);
  lv_obj_t *buttons[]={prev_b,next_b,edit_button,rename_button,switch_button};
  for(int i=0;i<5;i++){
   lv_obj_t *b=buttons[i];lv_obj_set_style_radius(b,0,0);lv_obj_set_style_bg_opa(b,0,0);
   lv_obj_set_style_text_color(b,TC(TEXT_PRIMARY),0);kit_keep(b);
   lv_obj_t *l=lv_obj_get_child(b,0);if(l){lv_obj_set_style_text_color(l,TC(TEXT_SECONDARY),0);kit_keep(l);}
  }
  lv_obj_set_style_text_font(lv_obj_get_child(edit_button,0),TF(HEADER_SM),0);
  lv_obj_set_style_text_font(lv_obj_get_child(rename_button,0),TF(HEADER_SM),0);
  lv_obj_set_style_text_font(switch_label,TF(HEADER_SM),0);
  lv_obj_set_size(switch_button,104,48);lv_obj_align(switch_button,LV_ALIGN_TOP_MID,0,280);
  lv_obj_add_flag(switch_pointer,LV_OBJ_FLAG_HIDDEN);kit_keep(name_label);
 }
 if(!profile_timer)profile_timer=lv_timer_create(profile_poll,100,NULL);
 /* Screen entry schedules a read; this page never waits on SQLite. */
}
