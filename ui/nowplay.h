/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_NOWPLAY_H
#define DISKOS_NOWPLAY_H

#include "ipc.h"

struct nowplay_env {
    void (*view)(const player_view_t *view, const player_evidence_t *ev, int playing);
    void (*home)(const player_view_t *view, int playing);
    void (*progress)(const player_view_t *view, int playing);
    void (*saver_state)(int playing);
    void (*art)(const player_view_t *view);
    void (*track)(const player_view_t *view, int playing);
    void (*quick)(int playing);
    void (*info)(const player_view_t *view);
    void (*menu)(const player_view_t *view, int playing);
};

void nowplay_invalidate(void);
void nowplay_publish(const struct nowplay_env *env, const player_view_t *view,
                     const player_evidence_t *ev, int playing, int visible, int force);

#endif
