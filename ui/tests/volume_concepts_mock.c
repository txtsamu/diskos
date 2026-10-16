/* Native LVGL design previews only: no production volume behaviour. */
static lv_obj_t *vc_label(lv_obj_t*p,const char*s,int x,int y,int w,const lv_font_t*f,lv_color_t c){lv_obj_t*l=lv_label_create(p);lv_label_set_text(l,s);lv_obj_set_style_text_font(l,f,0);lv_obj_set_style_text_color(l,c,0);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);kit_keep(l);return l;}
static lv_obj_t *vc_circle(lv_obj_t*p,int x,int y,int size){lv_obj_t*o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_size(o,size,size);lv_obj_set_pos(o,x-size/2,y-size/2);lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(o,TC(CANVAS),0);lv_obj_set_style_bg_opa(o,255,0);lv_obj_set_style_border_width(o,2,0);lv_obj_set_style_border_color(o,TC(ACCENT_PRIMARY),0);kit_keep(o);return o;}
static void volume_concept(int option){
 LV_FONT_DECLARE(diskos_splash_disco_72)
 lv_obj_t*root=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(root);lv_obj_set_size(root,360,360);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_opa(root,0,0);kit_keep(root);
 lv_obj_t*cap=vc_circle(root,180,468,500);lv_obj_set_style_bg_color(cap,TC(SURFACE),0);lv_obj_set_style_border_opa(cap,125,0);
 lv_obj_t*a=lv_arc_create(root);lv_obj_remove_style_all(a);kit_keep(a);lv_obj_set_size(a,460,460);lv_obj_set_pos(a,-50,238);lv_arc_set_bg_angles(a,235,305);lv_arc_set_range(a,0,120);lv_arc_set_value(a,72);lv_obj_set_style_arc_width(a,5,LV_PART_MAIN);lv_obj_set_style_arc_width(a,5,LV_PART_INDICATOR);lv_obj_set_style_arc_rounded(a,1,LV_PART_MAIN);lv_obj_set_style_arc_rounded(a,1,LV_PART_INDICATOR);lv_obj_set_style_arc_color(a,TC(CONTROL_TRACK),LV_PART_MAIN);lv_obj_set_style_arc_color(a,TC(ACCENT_PRIMARY),LV_PART_INDICATOR);lv_obj_set_style_bg_color(a,TC(ACCENT_PRIMARY),LV_PART_KNOB);lv_obj_set_style_bg_opa(a,255,LV_PART_KNOB);lv_obj_set_style_radius(a,LV_RADIUS_CIRCLE,LV_PART_KNOB);lv_obj_set_style_pad_all(a,4,LV_PART_KNOB);
 if(option==3){lv_arc_set_bg_angles(a,248,292);lv_arc_set_value(a,72);}
 vc_label(root,"72",144,241,72,TF(MUSIC_ARTIST),TC(TEXT_PRIMARY));
 lv_obj_t*hub=vc_circle(root,180,316,72);lv_obj_set_style_bg_color(hub,TC(CANVAS),0);lv_obj_t*l=vc_label(hub,"D!",0,0,90,&diskos_splash_disco_72,TC(ACCENT_PRIMARY));lv_obj_set_style_transform_scale_x(l,124,0);lv_obj_set_style_transform_scale_y(l,124,0);lv_obj_set_style_transform_pivot_x(l,45,0);lv_obj_set_style_transform_pivot_y(l,36,0);lv_obj_center(l);
 if(option!=2){for(int k=0;k<2;k++){int x=option==3?(k?291:69):(k?266:94),y=option==3?268:307;lv_obj_t*b=vc_circle(root,x,y,44);lv_obj_set_style_bg_color(b,theme_color_mix(TC(ACCENT_PRIMARY),TC(CANVAS),25),0);l=vc_label(b,k?LV_SYMBOL_PLUS:LV_SYMBOL_MINUS,0,0,44,TF(UI_20),TC(ACCENT_PRIMARY));lv_obj_center(l);}}
 else{vc_label(root,"0",49,282,36,TF(UI_14),TC(TEXT_SECONDARY));vc_label(root,"120",275,282,36,TF(UI_14),TC(TEXT_SECONDARY));}
 lv_obj_update_layout(root);
}
