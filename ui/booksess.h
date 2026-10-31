/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_BOOKSESS_H
#define DISKOS_BOOKSESS_H

#include <stdint.h>
#include "ipc.h"

#define BOOKSESS_PATH_MAX 256

struct booksess_env {
    uint32_t    (*now)(void);
    const char *(*admit)(void);
    void        (*get_state)(track_state_t *out);
    int         (*playing)(void);
    int         (*seek_to)(long ms);
    int         (*playpause)(void);
    int         (*is_book_path)(const char *path);
    int         (*book_progress)(const char *key, long *position_ms);
    int         (*book_save)(const char *key, long position_ms);
    void        (*adopted)(void);
    int         (*sleep_action)(void);
    void        (*sleep_ended)(void);
    void        (*shutdown)(void);
};

void booksess_set_env(const struct booksess_env *env);
void booksess_begin(const char *path, long resume_ms);
void booksess_tick(void);
void booksess_sleep_tick(void);
void booksess_hold_off_adopt(void);
void booksess_player_restarted(void);

int  ui_book_active(void);
void ui_cancel_book_resume(void);
void ui_book_user_seeked(long target_ms);
void ui_defer_sleep(void);
long ui_smart_rewind_ms(long idle_seconds);
void ui_set_sleep_timer(int minutes);
void ui_set_sleep_eoc(long target_ms, const char *path, int at_book_end);
void ui_disarm_book_eoc(void);
int  ui_sleep_state(int *secs_left);

#endif
