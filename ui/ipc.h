/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef IPC_H
#define IPC_H
#include <stdint.h>
#define IPC_TITLE_CAPACITY 256
unsigned ipc_transport_seq(void);
typedef struct {
    char title[IPC_TITLE_CAPACITY];
    char artist[160];
    char album[160];
    char playing_num[24];
    char work_mode[24];
    char path[256];
    long duration_ms;
    long position_ms;
    int  state;
    int  have_track;
    int  pos_id;
    int  sample_rate;
    int  is_dsd;
    int  is_favorite;
    int  volume;
    unsigned volume_seq;
    uint64_t song_id_raw, pos_id_raw, playback_token;
    unsigned song_id_valid, pos_id_valid, is_cue_valid, is_sacd_valid;
    int is_cue, is_sacd, player_confirmed, provisional, title_ready;
    unsigned player_generation;
    unsigned seq;
    unsigned path_seq;
    unsigned pos_seq;
} track_state_t;

typedef struct {
    track_state_t state;
    char player_title[IPC_TITLE_CAPACITY];
    uint64_t evidence_revision;
} ipc_title_event;
void ipc_get_title_event(ipc_title_event *out);
int ipc_publish_title(ipc_title_event *event, const char *title, int freeze);

void ipc_note_library_commit(void);
uint64_t ipc_library_epoch(void);
static inline const char *ipc_display_title(const track_state_t *st){
    return st->title;
}

#define VOL_MAX 120

int  ipc_start(void);
void ipc_get_state(track_state_t *out);
void ipc_seed_state(const track_state_t *s);
int  ipc_frame_is_dangerous(const char *frame);
int  ipc_send_cmd_unsafe(const char *frame);
#define PLAYER_CMD_PLAYPAUSE "0201000C0000"
int  ipc_send_cmd(const char *frame);
int  ipc_send_probe(const char *frame);
int  ipc_send_quiet(const char *frame);
void ipc_tx_pump(void);
int  ipc_tx_waiting(void);
void ipc_quiet_begin(void);
int  ipc_quiet_end(void);
int  ipc_is_ready(void);
int  ipc_take_send_error(void);
void ipc_health_check(void);
int  ipc_take_reconnected(void);
unsigned ipc_rx_frames(void);
int  ipc_player_mode(void);
unsigned ipc_generation(void);
void ipc_screen_snapshot(int *on, unsigned *scr_seq, unsigned *poweroff_seq, unsigned *generation);
#endif
