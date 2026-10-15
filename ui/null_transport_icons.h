#ifndef NULL_TRANSPORT_ICONS_H
#define NULL_TRANSPORT_ICONS_H
#include "lvgl/lvgl.h"
typedef enum { NULL_ICON_PLAY, NULL_ICON_PAUSE, NULL_ICON_PREVIOUS, NULL_ICON_NEXT } null_transport_icon_t;
/*22x22px bitmap silhouette on a2px grid. Parent button owns touch and actions.*/
lv_obj_t *null_transport_icon_create(lv_obj_t *parent, int x, int y, null_transport_icon_t icon,
                                     lv_color_t colour);
void null_transport_icon_set_colour(lv_obj_t *icon, lv_color_t colour);
#endif
