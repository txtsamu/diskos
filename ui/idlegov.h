/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_IDLEGOV_H
#define DISKOS_IDLEGOV_H
#include <stdint.h>

#define IDLEGOV_TMAP_N 5
#define IDLEGOV_BLPOLL_MS 250u
#define IDLEGOV_INJECT_DELAY_MS 300u

enum { IDLEGOV_PK_NONE, IDLEGOV_PK_OFF, IDLEGOV_PK_WAKE, IDLEGOV_PK_RELEVEL };
enum { IDLEGOV_SF_NONE, IDLEGOV_SF_OFF, IDLEGOV_SF_WAKE, IDLEGOV_SF_INJECT };
enum { IDLEGOV_RL_NONE, IDLEGOV_RL_FULL, IDLEGOV_RL_DIM };

typedef struct { int seen; int player_off; unsigned off_seq; } idlegov_keys_t;

typedef struct {
    int bl_state;
    idlegov_keys_t pk;
    unsigned scr_seq_seen, aa03_seq_seen, gen_seen, inject_for, inject_gen;
    int poweroff_inhibit, inject_pending;
    uint32_t inject_due, last_blpoll, last_activity;
    int manual_sleep;
    uint32_t manual_sleep_at;
} idlegov_t;

typedef struct { int act, note_input; } idlegov_screen_out_t;
typedef struct { int show_saver, dim, off; } idlegov_saver_out_t;

int  idlegov_saver_secs(int idx);
int  idlegov_screenoff_secs(int idx);
void idlegov_boot(idlegov_t *g, int boot_br, int startup_deferred, int written, uint32_t now);
void idlegov_request_sleep(idlegov_t *g, uint32_t now);
void idlegov_screen(idlegov_t *g, int on, unsigned sq, unsigned pq, unsigned gen, uint32_t now, idlegov_screen_out_t *out);
int  idlegov_inject_pending(const idlegov_t *g, uint32_t now);
int  idlegov_inject_take(idlegov_t *g, int on, unsigned sq, unsigned pq, unsigned gen);
int  idlegov_blpoll_due(idlegov_t *g, uint32_t now);
int  idlegov_foreign_relevel(const idlegov_t *g, int cbr, int written);
int  idlegov_powerkey_step(idlegov_keys_t *k, int cbr, int ours, unsigned ours_seq, int dark);
int  idlegov_powerkey_pass(idlegov_t *g, int cbr, int ours, unsigned ours_seq, int dark, unsigned write_seq);
int  idlegov_screen_frame_step(int on, int bl_state, int *player_off, int poweroff_inhibit);
int  idlegov_touch_wake(idlegov_t *g, int any, uint32_t now);
void idlegov_saver(idlegov_t *g, uint32_t now, int in_saver, int saver_ok, int saver_secs, int off_secs, idlegov_saver_out_t *out);
#endif
