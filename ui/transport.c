/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "transport.h"
#include "ipc.h"
#include "admit.h"
#include "musicdb.h"
#include "scanner.h"
#include "sdio.h"
#include <stdint.h>
#include <string.h>

/* Optimistic play/pause icon hint. Play state is INFERRED from position advance with a grace period,
 * so the transport glyph lags ~1s when PAUSING (the position stops but the inference waits before it
 * flips to paused); resuming is instant because the position advances immediately. On a play/pause tap
 * we predict the new state and show it at once; the real inference reconciles a beat later. Main-thread
 * only (taps + the glyph refresh both run on the LVGL loop), so no locking. */
static int      g_pp_hint = -1;          /* -1 none; 0 predict paused; 1 predict playing */
static uint32_t g_pp_hint_until = 0;
void ui_pp_tap_hint(void){               /* a toggle tap -> predict the opposite of the current state */
    g_pp_hint = ui_pp_icon_playing(ui_is_playing()) ? 0 : 1;
    g_pp_hint_until = lv_tick_get() + 2200; /* exceeds the 1600ms position-inference grace */
}
int ui_pp_icon_playing(int real_playing){
    if(g_pp_hint >= 0){
        if((int32_t)(lv_tick_get() - g_pp_hint_until) >= 0){ g_pp_hint = -1; }        /* window lapsed -> trust real */
        else return g_pp_hint;                                        /* still lagging -> show prediction */
    }
    return real_playing;
}

int ui_transport_command(const char *cmd)
{
    if(!cmd) return -1;
    int is_next = !strcmp(cmd, "0201000C0001");
    int is_prev = !strcmp(cmd, "0201000C0002");
    int is_toggle = !strcmp(cmd, PLAYER_CMD_PLAYPAUSE);
    if(!is_next && !is_prev && !is_toggle) return -1;
    { const char *why = admit_check(ADMIT_PLAYER_COMMAND); if(why){ ui_toast(why); return -1; } }
    ui_defer_sleep();
    if(is_next || is_prev){
        track_state_t st; ipc_get_state(&st);
        if(mdb_is_book_path(st.path)){
            long tgt = st.position_ms + (is_next ? 30000 : -15000);
            if(tgt < 0) tgt = 0;
            if(st.duration_ms > 0 && tgt > st.duration_ms) tgt = st.duration_ms;
            if(ui_seek_to(tgt) < 0){ ui_toast("Couldn't seek - try again"); return -1; }
            ui_np_seek_hold(tgt);
            ui_book_user_seeked(tgt);
            return 0;
        }
    }
    ui_note_transport_sent();
    if(ipc_send_cmd(cmd) < 0){ ui_toast("Player busy - try again"); return -1; }
    if(is_toggle){
        ui_pp_tap_hint();
        ui_np_pp_refresh(ui_pp_icon_playing(ui_is_playing()));
    } else {
        ui_cancel_book_resume();
        ui_disarm_book_eoc();
    }
    return 0;
}

void ui_rescan_library(void){
    if(scanner_active()){ if(scanner_cancel()) ui_toast("Cancelling scan..."); return; }   /* refused once it is committing: completion reports */   /* the row reads "Stop" while a scan runs */
    if(admit_check(ADMIT_LOCAL_PLAYBACK)){ ui_toast("Return to local playback to scan"); return; }
    if(!sd_io_allowed()){ ui_toast("SD card is not ready"); return; }
    if(scanner_start() != 0){ ui_toast("Couldn't start library scan"); return; }
    ui_toast("Scanning library...");
}
