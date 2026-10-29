/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_SRCSWITCH_H
#define DISKOS_SRCSWITCH_H

#include <pthread.h>
#include <stdint.h>

/* Source-mode switch module (GLOSSARY.md): the Working Mode change, the SD handoff to a computer and the ordered USB DAC
 * exit. No LVGL; everything that touches the system comes in through this table, set once before srcswitch_init(). */
struct srcswitch_env {
    uint32_t (*now)(void);               /* milliseconds, wrapping (lv_tick_get) */
    /* the system, read */
    int  (*media_mounted)(void);         /* the card is mounted on the Disc */
    int  (*exported_to_host)(void);      /* a computer may own the card (fail-closed: unreadable gadget state reads 1) */
    int  (*host_confirmed)(void);        /* the gadget is bound AND backed by the card */
    int  (*host_attached)(void);         /* any computer on the cable */
    int  (*dac_gadget_present)(void);    /* the USB DAC (uac_demo) gadget still exists */
    int  (*player_guarded)(void);        /* the running Player has the delete guard in its PATH */
    /* the UI and the Player */
    int  (*playing)(void);
    int  (*scan_active)(void);
    void (*scan_if_library_empty)(void); /* the first-run scan, once the card is back */
    void (*art_cancel)(void);
    void (*card_returned)(void);         /* the card is local again: reseed the cover cache */
    void (*restart)(void);               /* the device restart after a USB Storage return */
    void (*toast)(const char *msg);
    void (*set_volume)(int volume);
};

void srcswitch_set_env(const struct srcswitch_env *env);
void srcswitch_init(void);               /* once at startup: read the handoff marker a previous UI left, open the SD gate */
void srcswitch_tick(void);               /* every 100 ms */

int  ui_set_source_mode(int mode);       /* replay the stock V2.28 switch sequence; 0 = started/ok, -1 = refused (it toasted why) */
int  ui_source_switch_pending(void);     /* an SD handoff step is in flight */
int  ui_source_switch_failed(void);      /* the SD state is uncertain, or SD activity is held for the boot */
int  ui_dac_exit_pending(void);          /* the ordered USB DAC exit is waiting for the gadget to go */
int  ui_local_playback_allowed(void);

/* SD admission for writers (sd_io_* covers readers) and the Restart card drain. */
int  sd_write_begin(void);
void sd_write_end(void);
void ui_sd_quiesce_begin(void);
int  ui_sd_quiesce_drained(void);
void ui_sd_quiesce_end(void);

int  srcswitch_dac_level_settled(void);

/* The launch verdict (-1 pending, 0 negative, 1 guarded and locally owned) and the boot-long hold. */
void srcswitch_set_launch_verdict(int v);
void srcswitch_hold(void);
int  srcswitch_held(void);
void srcswitch_revoke(void);
void srcswitch_restart_deadline(void);
int  srcswitch_sd_writable(void);
pthread_mutex_t *srcswitch_mode_mutex(void);

#endif
