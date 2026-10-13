/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef STATS_H
#define STATS_H
#include "ipc.h"
/* Listening Stats recorder: called from the main loop with the player's state; a song that really played is logged
 * (PLAY_LOG) when the next one starts, the music stops, or stats_flush() runs (before a shutdown). */
void stats_tick(const track_state_t *st, int playing);
void stats_flush(void);
#endif
