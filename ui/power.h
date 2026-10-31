/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_POWER_H
#define DISKOS_POWER_H
#include <stddef.h>
#include <stdint.h>

#define POWER_IDLE_N 7
int  power_idle_secs(int idx);
int  power_idle_idx_from_secs(int secs);
const char *power_idle_label(int idx);
int  power_frame_idle(char *buf, size_t n, int idx);
int  power_frame_charge(char *buf, size_t n, int on);
int  power_frame_sleep(char *buf, size_t n, int secs);
int  power_apply_charge_via(int (*send)(const char *), int on);
int  power_temp_state(int tenths_c);
const char *power_temp_msg(int state);

int  power_read_sysconfig(const char *db_path, const char *column);

int  power_idle_tick(uint32_t now_ms, uint32_t limit_secs, int playing, uint32_t last_input_ms, int (*request)(void));
void power_idle_reset(void);
typedef struct { int sent; } pw_take_t;
void power_takeover_reset(pw_take_t *t);
int  power_takeover_step(pw_take_t *t, int (*read_secs)(void), int (*send)(const char *), int *stock_secs);

#define POWER_SLEEP_PAUSE 0
#define POWER_SLEEP_SHUTDOWN 1

typedef enum { PW_OFF, PW_DRAIN, PW_SYNC, PW_HANDOFF, PW_PROTECTED } pw_phase_t;
typedef struct power_ops {
    void     (*quiesce_begin)(void *ctx);
    int      (*drained)(void *ctx);
    void     (*quiesce_end)(void *ctx);
    int      (*sync_start)(void *ctx);
    int      (*sync_poll)(void *ctx);
    void     (*sync_kill)(void *ctx);
    int      (*send)(void *ctx, const char *frame);
    uint32_t (*now_ms)(void *ctx);
    void     (*toast)(void *ctx, const char *msg);
    void     *ctx;
} power_ops_t;
#define POWER_DRAIN_MS   8000u
#define POWER_SYNC_MS   15000u
#define POWER_CANCEL_RETRY_MS 2000u
#define POWER_HANDOFF_MS 20000u
int  power_shutdown_begin(const power_ops_t *ops);
pw_phase_t power_shutdown_tick(void);
pw_phase_t power_shutdown_phase(void);
void power_shutdown_new_generation(void);

#ifndef POWER_CORE_ONLY
int  power_apply_charge(int on);
int  power_shutdown_request(void);
void power_tick(uint32_t last_input_ms, int playing);
void power_note_input(void);
int  power_supported(void);
void power_startup_mirror(void);
#endif
#endif
