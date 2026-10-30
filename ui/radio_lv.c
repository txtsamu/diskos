/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "radio.h"
#include "lvgl/lvgl.h"

static uint32_t lv_now(void){ return lv_tick_get(); }
static void lv_fired(lv_timer_t *t){ radio_watch_fire((struct radio_watch *)lv_timer_get_user_data(t)); }
static void *lv_start(unsigned interval_ms, struct radio_watch *w){ return lv_timer_create(lv_fired, interval_ms, w); }
static void lv_stop(void *timer){ lv_timer_del((lv_timer_t *)timer); }

const struct radio_env radio_lv_env = { lv_now, lv_start, lv_stop };
