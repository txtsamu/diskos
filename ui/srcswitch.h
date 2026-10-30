/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_SRCSWITCH_H
#define DISKOS_SRCSWITCH_H

#include <pthread.h>
#include <stdint.h>

/* Source-mode and Output-route switch module (GLOSSARY.md): the Working Mode change, the Output route change, the pass via
 * Local between them, the SD handoff to a computer and the ordered USB DAC exit. No LVGL; everything that touches the system
 * comes in through this table, set once before srcswitch_init(); the Player is reached through ipc_send_cmd() (via outseq),
 * ipc_get_state() and ipc_generation(), so a host test links fakes of those. */
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
    const char *(*output_blocked)(void); /* NULL when an Output-route switch may run now, else a short reason for the toast */
    int  (*seek_to)(long ms);            /* 0 = accepted */
};

void srcswitch_set_env(const struct srcswitch_env *env);
void srcswitch_init(void);               /* once at startup: read the handoff marker a previous UI left, open the SD gate */
void srcswitch_tick(void);               /* every 100 ms: advances every deferred step of every switch */

/* ---- Source mode ---- */
/* Replay the stock V2.28 switch sequence; 0 = started/ok, -1 = refused (it toasted why). Passing through Local first (from
 * USB Audio, or USB DAC to Storage) and the ordered USB DAC exit with a computer attached are steps of this call that
 * finish on later ticks; the caller does not re-enter. */
int  ui_set_source_mode(int mode);
int  ui_source_switch_pending(void);     /* an SD handoff step is in flight */
int  ui_source_switch_failed(void);      /* the SD state is uncertain, or SD activity is held for the boot */
int  ui_local_playback_allowed(void);
int  modes_source_switch_busy(void);     /* 1 while a source switch has work left in this process (pause, pass through Local, USB DAC exit, SD hand-over): a re-launch would drop it */
int  srcswitch_via_local_pending(void);  /* 1 while a switch is passing through Local first (the picker ignores taps; its toast comes later) */
const char *srcswitch_switch_msg(int mode); /* "Switching to ..." for mode 0..3, else "" */

/* ---- Output route (stock: Settings > SPDIF, Working Mode > USB AUDIO; OUT_* are in modes.h) ---- */
int  modes_output_route(void);           /* the route diskOS last set THIS player generation (else OUT_INTERNAL) */
int  modes_output_switch(int target);    /* Settings SPDIF toggle: pause -> silent -> 0666 -> 0657 8 -> resume; 0 = started, -1 = refused */
int  modes_output_mode_switch(int target); /* Working Mode entry (adds 0642 + stock's 65 ms) */
void modes_output_reset(void);           /* another path (source change, BT route) put the player back on the internal DAC */
void modes_output_adopt(void);           /* theme re-launch, after session_adopt(): refresh the route display (and watch a USB DAC) */
int  modes_output_busy(void);            /* 1 while a switch waits for silence or recovers: play/next/source/BT must refuse */
void modes_output_boot_restore(void);    /* at startup: re-select the saved Working Mode / USB Audio ~15 s in */

/* ---- the picker is told, never asked ---- */
typedef enum {
    SRCSWITCH_EV_CHANGED,         /* a route or mode switch finished, failed or recovered: redraw what shows it */
    SRCSWITCH_EV_SETTLE_RESTART,  /* a deferred switch (via Local / USB DAC exit) is now really switching: settle from here */
    SRCSWITCH_EV_ABANDONED,       /* a deferred switch ended without switching: drop the "switching" mark, redraw */
} srcswitch_event_t;
void srcswitch_set_listener(void (*cb)(srcswitch_event_t));

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
