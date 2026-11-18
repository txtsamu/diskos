/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_TRANSPORT_H
#define DISKOS_TRANSPORT_H

#include <stdint.h>

typedef enum {
    UI_TRANSPORT_NONE = 0,
    UI_TRANSPORT_PLAYPAUSE,
    UI_TRANSPORT_NEXT,
    UI_TRANSPORT_PREV
} ui_transport_cmd_t;

int  ui_transport_command(ui_transport_cmd_t cmd);

static inline ui_transport_cmd_t ui_transport_click_command(int clicks){
    switch(clicks){
    case 1: return UI_TRANSPORT_PLAYPAUSE;
    case 2: return UI_TRANSPORT_NEXT;
    case 3: return UI_TRANSPORT_PREV;
    }
    return UI_TRANSPORT_NONE;
}

void ui_pp_tap_hint(void);
int  ui_pp_icon_playing(int real_playing);

void ui_rescan_library(void);

uint32_t lv_tick_get(void);
void ui_toast(const char *msg);
int  ui_is_playing(void);
void ui_defer_sleep(void);
void ui_note_transport_sent(void);
void ui_book_user_seeked(long target_ms);
void ui_cancel_book_resume(void);
void ui_disarm_book_eoc(void);
void ui_np_seek_hold(long target_ms);
void ui_np_pp_refresh(int playing);

#endif
