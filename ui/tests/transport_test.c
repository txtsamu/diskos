/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "transport.h"
#include "ipc.h"
#include "admit.h"
#include "musicdb.h"
#include "scanner.h"
#include "sdio.h"

static int g_playing, local = 1, book, send_fail, seeks, commands, cancelled, disarmed, hints;
static int scan_busy, scan_fail, sd_allowed = 1, scans;
static uint32_t now;
static long requested_seek, hold_target = -2;
static int glyph_playing = -1, glyph_sets;
static char toast[100];
static track_state_t state;
static int cancel_ok = 1, cancels;

uint32_t lv_tick_get(void){ return now; }
int ui_is_playing(void){ return g_playing; }
void ui_toast(const char *s){ snprintf(toast, sizeof toast, "%s", s); }
void ui_defer_sleep(void){}
void ui_note_transport_sent(void){}
void ui_book_user_seeked(long p){ (void)p; hints++; }
void ui_cancel_book_resume(void){ cancelled++; }
void ui_disarm_book_eoc(void){ disarmed++; }
void ui_np_seek_hold(long t){ hold_target = t; }
void ui_np_pp_refresh(int playing){ glyph_playing = playing; glyph_sets++; }
const char *admit_check(admit_what_t w){ (void)w; return local ? NULL : "Return to local playback first"; }
int ui_seek_to(long p){ seeks++; requested_seek = p; return send_fail ? -1 : 0; }
void ipc_get_state(track_state_t *s){ *s = state; }
int ipc_send_cmd(const char *s){ (void)s; commands++; return send_fail ? -1 : 0; }
int mdb_is_book_path(const char *s){ (void)s; return book; }
int scanner_active(void){ return scan_busy; }
int scanner_cancel(void){ cancels++; return cancel_ok; }
int scanner_start(void){ scans++; return scan_fail ? -1 : 0; }
int sd_io_allowed(void){ return sd_allowed; }

int main(void){
    g_playing = 1; now = 1000;
    assert(ui_transport_command(PLAYER_CMD_PLAYPAUSE) == 0);
    assert(glyph_playing == 0 && ui_pp_icon_playing(1) == 0);
    now = 1100; assert(ui_transport_command(PLAYER_CMD_PLAYPAUSE) == 0);
    assert(glyph_playing == 1 && ui_pp_icon_playing(0) == 1);
    now = 1200; assert(ui_transport_command(PLAYER_CMD_PLAYPAUSE) == 0);
    assert(ui_pp_icon_playing(1) == 0);
    now = 3000; assert(ui_pp_icon_playing(0) == 0);
    now = 3401; assert(ui_pp_icon_playing(1) == 1);
    now = UINT32_MAX - 100; ui_pp_tap_hint(); now = 50;
    assert(ui_pp_icon_playing(1) == 0); now = 2300; assert(ui_pp_icon_playing(1) == 1);
    puts("PASS instant feedback, rapid repeated taps, reconciliation and tick wrap");

    now = 10000; assert(ui_pp_icon_playing(0) == 0 && ui_pp_icon_playing(1) == 1);
    send_fail = 1; glyph_sets = 0; assert(ui_transport_command(PLAYER_CMD_PLAYPAUSE) < 0);
    assert(ui_pp_icon_playing(0) == 0 && ui_pp_icon_playing(1) == 1 && glyph_sets == 0 && !strcmp(toast, "Player busy - try again"));
    int sent = commands; local = 0;
    assert(ui_transport_command(PLAYER_CMD_PLAYPAUSE) < 0);
    assert(ui_transport_command("0201000C0001") < 0);
    assert(commands == sent && seeks == 0);
    puts("PASS no optimistic success on IPC failure and no commands during USB ownership");

    local = 1; send_fail = 0; book = 1; state.position_ms = 10000; state.duration_ms = 20000;
    assert(ui_transport_command("0201000C0002") == 0 && requested_seek == 0 && hold_target == 0);
    assert(ui_transport_command("0201000C0001") == 0 && requested_seek == 20000 && hold_target == 20000);
    assert(commands == sent && hints == 2 && !cancelled && !disarmed);
    hold_target = -2;
    send_fail = 1; assert(ui_transport_command("0201000C0001") < 0 && hints == 2 && hold_target == -2);
    book = 0; assert(ui_transport_command("0201000C0001") < 0 && !cancelled && !disarmed);
    send_fail = 0; assert(ui_transport_command("0201000C0001") == 0 && cancelled == 1 && disarmed == 1);
    puts("PASS bounded book seeks, failed seek preserves resume, successful music skip clears it");

    local = 0; ui_rescan_library(); assert(scans == 0);
    local = 1; scan_busy = 1; toast[0] = 0; ui_rescan_library(); assert(scans == 0 && cancels == 1 && !strcmp(toast, "Cancelling scan..."));
    cancel_ok = 0; toast[0] = 0; ui_rescan_library(); assert(scans == 0 && cancels == 2 && toast[0] == 0);
    cancel_ok = 1;
    scan_busy = 0; sd_allowed = 0; ui_rescan_library(); assert(scans == 0 && strstr(toast, "not ready"));
    sd_allowed = 1; scan_fail = 1; ui_rescan_library(); assert(scans == 1 && strstr(toast, "Couldn't start"));
    scan_fail = 0; ui_rescan_library(); assert(scans == 2 && !strcmp(toast, "Scanning library..."));
    puts("PASS honest scan admission, busy/error feedback and the started toast");
    return 0;
}
