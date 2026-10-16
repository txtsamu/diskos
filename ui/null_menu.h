#ifndef NULL_MENU_H
#define NULL_MENU_H
#include "lvgl/lvgl.h"
typedef struct null_menu null_menu_t;
typedef void (*null_menu_action_t)(int index, void *user);
null_menu_t *null_menu_create(lv_obj_t *parent, const char *const *labels, int count, lv_color_t idle,
                              lv_color_t on, null_menu_action_t action, void *user);
void null_menu_set_shortcut(null_menu_t *m, void (*callback)(int kind));
void null_menu_set_active(null_menu_t *m, int active);
void null_menu_destroy(null_menu_t *m);
float null_menu_position(const null_menu_t *m);
int null_menu_dragging(const null_menu_t *m);
#endif
