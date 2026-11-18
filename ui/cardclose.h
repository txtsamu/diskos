/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_CARDCLOSE_H
#define DISKOS_CARDCLOSE_H

#include <stdint.h>

#define CARDCLOSE_DRAIN_MS 10000u
#define CARDCLOSE_SYNC_MS  15000u

typedef enum {
    CARD_CLOSE_OFF = 0,
    CARD_CLOSE_DRAINING,
    CARD_CLOSE_SYNCING,
    CARD_CLOSE_DONE,
    CARD_CLOSE_DRAIN_TIMEOUT,
    CARD_CLOSE_SYNC_TIMEOUT,
    CARD_CLOSE_SYNC_FAILED,
    CARD_CLOSE_SPAWN_FAILED
} cardclose_phase_t;

struct cardclose_ops {
    int      (*drained)(void);
    int      (*sync_start)(void);
    int      (*sync_poll)(void);
    void     (*sync_kill)(void);
    int      (*unmount)(void);
    uint32_t (*now)(void);
};

int  cardclose_begin(const struct cardclose_ops *ops);
int  cardclose_tick(void);
void cardclose_abort(void);

#endif
