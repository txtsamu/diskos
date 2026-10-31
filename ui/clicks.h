/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_CLICKS_H
#define DISKOS_CLICKS_H
#include <stdint.h>

#define CLICKS_WINDOW_MS 350
#define CLICKS_MAX 3

typedef struct { int count; uint32_t last_ms; } clicks_t;

void clicks_reset(clicks_t *c);
int  clicks_press(clicks_t *c, uint32_t now);
int  clicks_poll(clicks_t *c, uint32_t now);

#endif
