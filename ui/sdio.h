/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_SDIO_H
#define DISKOS_SDIO_H

#include <stdint.h>

struct sd_env {
    uint32_t (*now)(void);
    int  (*media_mounted)(void);
    int  (*exported_to_host)(void);
    int  (*host_confirmed)(void);
    int  (*player_guarded)(void);
    int  (*playing)(void);
    int  (*scan_active)(void);
    void (*scan_if_library_empty)(void);
    void (*art_cancel)(void);
    void (*card_returned)(void);
    void (*restart)(void);
    void (*toast)(const char *msg);
    int  (*send_source)(int mode);
};

void sd_set_env(const struct sd_env *env);
void sd_init(void);
void sd_tick(void);

int  sd_io_begin(void);
void sd_io_end(void);
int  sd_write_begin(void);
void sd_write_end(void);
int  sd_io_allowed(void);
void sd_io_fault(void);
int  sd_io_healthy(void);
int  sd_drained(void);
int  sd_writable(void);

void sd_quiesce_begin(void);
void sd_quiesce_end(void);

typedef enum { SD_GATE_WAIT, SD_GATE_OPENED, SD_GATE_OPEN, SD_GATE_DENIED } sd_gate_t;
sd_gate_t sd_gate_poll(int launch_status);
void sd_hold(void);
int  sd_held(void);

int  sd_local_playback_allowed(void);
int  sd_switch_pending(void);
int  sd_switch_failed(void);
const char *sd_switch_refusal(void);
int  sd_host_owned(void);
int  sd_export_begin(void);
int  sd_return_begin(int mode);
void sd_restart_deadline(void);

int  sd_coldplug_begin(void);
void sd_coldplug_end(void);

#endif
