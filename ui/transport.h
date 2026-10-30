/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_TRANSPORT_H
#define DISKOS_TRANSPORT_H

#include <stdint.h>

int  ui_transport_command(const char *cmd);

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
