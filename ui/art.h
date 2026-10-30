/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef ART_H
#define ART_H
int art_make_all(const char *track, const char *cover_bmp,
                 const char *thumb_bmp, const char *backdrop_bmp);
int art_make_all_ex(const char *track, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable);
int art_make_all_ex_gen(const char *track, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0);
unsigned art_cancel_gen(void);
void art_cancel(void);
int  art_last_exit(void);
void art_kill_all(void);
int art_bmp_valid(const char *path, int n);
#include <stdint.h>
void art_sidecar_signature(const char *track, uint64_t *h);
int art_make_saver(const char *track, const char *out_raw, int64_t deadline_ms);
#endif
