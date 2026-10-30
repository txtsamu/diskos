/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef BOOKS_H
#define BOOKS_H
#include "lvgl/lvgl.h"
void books_create(lv_obj_t *root);
void books_open(void);
void chapters_create(lv_obj_t *root);
void chapters_open(void);
#endif
