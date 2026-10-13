/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SS_SPHERE_H
#define SS_SPHERE_H
#include "lvgl/lvgl.h"

/* The mood sphere: a lit ball drawn once (its picture never changes), with a glow, a marker and the two great circles
 * through the chosen point laid over it. Only those small pieces move while the marker is dragged. Coordinates are
 * the 2D circle: x = Dark (-1) .. Bright (+1), y = Calm (-1) .. Energetic (+1). */
typedef void (*sphere_cb_t)(int phase, float x, float y, void *ud);   /* phase 0 drag start, 1 moving, 2 released */

lv_obj_t *sphere_create(lv_obj_t *parent, int cx, int cy, lv_color_t canvas);
void sphere_set(float x, float y, int faded);     /* faded: an inherited / not-yet-placed point */
void sphere_get(float *x, float *y);
void sphere_set_cb(sphere_cb_t cb, void *ud);
const char *sphere_mood_name(float x, float y);
void sphere_mood_rgb(float x, float y, uint8_t rgb[3]);   /* the ball's colour at a point (list dots) */
void sphere_free(void);
const lv_image_dsc_t *sphere_picture(lv_color_t canvas, int small);   /* the ball as a picture (small: 118 px, for the first page) */
#endif
