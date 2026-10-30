/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef PLAYSTATE_H
#define PLAYSTATE_H

typedef struct {
    long     last_pos_ms;
    unsigned change_tick;
} playstate_t;

void playstate_reset(playstate_t *ps);

int playstate_playing(playstate_t *ps, int have_track, long position_ms,
                      unsigned now_tick, unsigned grace_ms, unsigned max_adv_ms);

#endif
