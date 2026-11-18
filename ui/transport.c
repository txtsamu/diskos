/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "transport.h"
#include "ipc.h"
#include "admit.h"
#include "musicdb.h"
#include "scanner.h"
#include "sdio.h"
#include "bootstate.h"
#include <stdint.h>
#include <stddef.h>

static int      g_pp_hint = -1;
static uint32_t g_pp_hint_until = 0;
void ui_pp_tap_hint(void){
    g_pp_hint = ui_pp_icon_playing(ui_is_playing()) ? 0 : 1;
    g_pp_hint_until = lv_tick_get() + 2200;
}
int ui_pp_icon_playing(int real_playing){
    if(g_pp_hint >= 0){
        if((int32_t)(lv_tick_get() - g_pp_hint_until) >= 0){ g_pp_hint = -1; }
        else return g_pp_hint;
    }
    return real_playing;
}

static const char *transport_frame(ui_transport_cmd_t cmd){
    switch(cmd){
    case UI_TRANSPORT_PLAYPAUSE: return PLAYER_CMD_PLAYPAUSE;
    case UI_TRANSPORT_NEXT:     return "0201000C0001";
    case UI_TRANSPORT_PREV:     return "0201000C0002";
    case UI_TRANSPORT_NONE:     break;
    }
    return NULL;
}

int ui_transport_command(ui_transport_cmd_t cmd)
{
    const char *frame = transport_frame(cmd);
    if(!frame) return -1;
    int is_toggle = cmd == UI_TRANSPORT_PLAYPAUSE;
    { const char *why = admit_check(ADMIT_PLAYER_COMMAND); if(why){ ui_toast(why); return -1; } }
    ui_defer_sleep();
    if(!is_toggle){
        player_view_t st; ipc_get_view(&st);
        if(mdb_is_book_path(st.path)){
            long tgt = st.position_ms + (cmd == UI_TRANSPORT_NEXT ? 30000 : -15000);
            if(tgt < 0) tgt = 0;
            if(st.duration_ms > 0 && tgt > st.duration_ms) tgt = st.duration_ms;
            if(ui_seek_to(tgt) < 0){ ui_toast("Couldn't seek - try again"); return -1; }
            ui_np_seek_hold(tgt);
            ui_book_user_seeked(tgt);
            return 0;
        }
    }
    ui_note_transport_sent();
    if(ipc_send_cmd(frame) < 0){ ui_toast("Player busy - try again"); return -1; }
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
    if(scanner_active()){ if(scanner_cancel()) ui_toast("Cancelling scan..."); return; }   
    if(admit_check(ADMIT_LOCAL_PLAYBACK)){ ui_toast("Return to local playback to scan"); return; }
    if(!sd_io_allowed()){ ui_toast(bootstate_reason("SD card is not ready")); return; }
    if(scanner_start() != 0){ ui_toast("Couldn't start library scan"); return; }
    ui_toast("Scanning library...");
}
