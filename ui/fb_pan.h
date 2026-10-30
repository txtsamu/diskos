/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef FB_PAN_H
#define FB_PAN_H
#include "lvgl/lvgl.h"
lv_display_t *fbpan_create(const char *dev);
void fbpan_set_rotation(int v);
void fbpan_bind_touch(lv_indev_t *indev);
#endif
