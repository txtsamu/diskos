/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_SHUFFLE_H
#define DISKOS_SHUFFLE_H

static inline int shuffle_start(int mode, int list_type, int shown, int player_rows, unsigned r){
    if(mode != 1 || list_type == 2 || shown < 2) return 1;
    int hi = player_rows > 0 ? player_rows : 1;
    if(hi > shown) hi = shown;
    if(hi < 2) return 1;
    return 1 + (int)(r % (unsigned)hi);
}

#endif
