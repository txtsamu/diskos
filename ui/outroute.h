/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_OUTROUTE_H
#define DISKOS_OUTROUTE_H

#include <stdint.h>
#include "ipc.h"
#include "outseq.h"

enum { OUT_INTERNAL = OUTSEQ_INTERNAL, OUT_SPDIF = OUTSEQ_SPDIF, OUT_USB = OUTSEQ_USB };

#define OUTROUTE_MAC_LEN 20

struct outroute_env {
    uint32_t    (*now)(void);
    int         (*playing)(void);
    const char *(*switch_blocked)(void);
    void        (*toast)(const char *msg);
    int         (*seek_to)(long ms);
    void        (*charge_limit_reapply)(void);
};
void outroute_init(const struct outroute_env *env);
void outroute_set_listener(void (*cb)(void));

int  outroute_route(void);
void outroute_set(int route);
void outroute_reset(void);
int  outroute_spdif_chosen(void);
void outroute_clear_spdif_choice(void);

const char *outroute_bt_mac(void);
int  outroute_bt_routed(void);
void outroute_set_bt(const char *mac);
void outroute_bt_forget(void);
int  outroute_mac_normalize(const char *in, char out[OUTROUTE_MAC_LEN]);

void outroute_restore(void);
double outroute_player_age_s(void);

int  outroute_saved_usb_audio(void);
void outroute_save_usb_audio(int on);

typedef enum { OUTROUTE_SETTING, OUTROUTE_MODE, OUTROUTE_MODE_STAY_PAUSED } outroute_how_t;
int  outroute_pick(int target, outroute_how_t how);
int  outroute_busy(void);
int  outroute_pick_pending(void);
void outroute_adopt(void);
void outroute_tick(void);

typedef struct { int was_playing; track_state_t st; } outroute_held_t;
#define OUTROUTE_OK            0
#define OUTROUTE_NOT_PAUSED  (-1)
#define OUTROUTE_STEP_FAILED (-2)
int  outroute_to_bt(const char *mac, const char *codec_frame, outroute_held_t *held);
int  outroute_to_analog(int force, outroute_held_t *held);

#endif
