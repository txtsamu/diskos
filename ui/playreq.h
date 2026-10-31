/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_PLAYREQ_H
#define DISKOS_PLAYREQ_H

#include <stdint.h>
#include "admit.h"
#include "ipc.h"

struct playreq_env {
    uint32_t     (*now)(void);
    unsigned     (*generation)(void);
    unsigned     (*transport_seq)(void);
    void         (*get_state)(track_state_t *out);
    int          (*tx_waiting)(void);
    int          (*send)(const char *frame);
    admit_sent_t (*admit_start)(admit_play_t kind);
    void         (*note_playback)(void);
};
void playreq_set_env(const struct playreq_env *env);

int  playreq_play_list(int list_type, const char *name, int pos1, int build_idx);
int  playreq_queue_jump(int ord1);
void playreq_reserve_slot(void);

int  playreq_tick(const track_state_t *st);
int  playreq_playing(void);
int  playreq_pending(void);
void playreq_clear(void);

int  playreq_recent(void);

void playreq_note_transport(unsigned gen, uint32_t now_ms);
int  playreq_transport_in(unsigned gen);
int  playreq_startup_busy(unsigned gen, uint32_t now_ms, int playing, int starting, int have_track);

#endif
