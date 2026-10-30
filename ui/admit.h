/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_ADMIT_H
#define DISKOS_ADMIT_H

#include <stdint.h>
#include "ipc.h"
#include "outseq.h"

struct admit_env {
    uint32_t (*now)(void);
    int  (*local_playback_allowed)(void);
    int  (*output_busy)(void);
    int  (*play_pending)(void);
    int  (*book_active)(void);
    int  (*playing)(void);
    int  (*fw_needs_local_init)(void);
};
void admit_set_env(const struct admit_env *env);

typedef enum {
    ADMIT_OUTPUT_IDLE,
    ADMIT_LOCAL_PLAYBACK,
    ADMIT_PLAYER_COMMAND,
    ADMIT_OUTPUT_SWITCH,
    ADMIT_ROUTE_BT,
    ADMIT_ROUTE_BT_SEND,
    ADMIT_ROUTE_ANALOG,
    ADMIT_LOCAL_INIT
} admit_what_t;

const char *admit_check(admit_what_t what);

typedef struct { int was_playing; track_state_t st; } admit_held_t;
typedef outseq_rc (*admit_step_fn)(const admit_held_t *held, void *arg);
#define ADMIT_SWITCH_OK            0
#define ADMIT_SWITCH_NOT_PAUSED  (-1)
#define ADMIT_SWITCH_STEP_FAILED (-2)
int admit_switch(admit_step_fn step, void *arg, admit_held_t *held);

int ui_seek_to(long ms);

typedef enum { ADMIT_PLAY_LIST, ADMIT_PLAY_RESERVED_SLOT } admit_play_t;
typedef enum { ADMIT_SENT_NONE = 0, ADMIT_SENT_WORKMODE = 1, ADMIT_SENT_PREAMBLE = 2 } admit_sent_t;
admit_sent_t admit_play_start(admit_play_t kind);
void admit_note_play_sent(void);

void admit_note_player_restart(void);
int  admit_settling(void);
void admit_tick(void);

#endif
