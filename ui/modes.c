/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "modes.h"
#include "config.h"
#include "outseq.h"
#include "session.h"
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <glob.h>

/* Working Mode (audio source) picker - mirrors stock's "Working mode" list. Tapping a mode replays
 * the captured V2.28 switch sequence via ui_set_source_mode() and marks it selected.
 * 0=Local 1=USB-DAC 2=BT-Receiving 3=USB-Storage. The fifth row, USB Audio (stock's "USB AUDIO"), is an OUTPUT
 * route of the Local source (the Disc feeds an external USB DAC), not a source mode: see the output section. */

typedef struct { const char *name, *sub; } modeinfo_t;
static const modeinfo_t MODES[] = {
    { "Local Playback",      "Play from the microSD card" },
    { "USB DAC",             "Be a DAC for a phone or PC" },
    { "Bluetooth Receiving", "Play audio sent from a phone" },
    { "USB Storage",         "Open the card on a computer" },
#ifdef DISKOS_TEST_OUTPUTS
    { "USB Audio",           "Play to an external USB DAC" },   /* device-unverified: test builds only */
#endif
};
#define N_MODES ((int)(sizeof(MODES)/sizeof(MODES[0])))
#define ROW_USB_AUDIO 4   /* only present in DISKOS_TEST_OUTPUTS builds */

static lv_obj_t *g_check[N_MODES];   /* per-row checkmark label */
static lv_obj_t *g_row[N_MODES];     /* per-row button (for the selected highlight) */

/* OUTPUT-ROUTE-BEGIN */
/* Output route: Internal DAC / SPDIF / USB Audio (SPDIF row + USB Audio picker row: -DDISKOS_TEST_OUTPUTS builds only).
 * Stock V2.57 (mq_ui_257.dis):
 *   Settings SPDIF toggle (0x480510 -> 0x464f0c):  0666 <6|4>, 0657 8                       (no 0642, no sleep)
 *   Working Mode callback 0x46bbac (200 ms one-shot): internal/SPDIF (0x46bd28): 0666 <6|4>, usleep 65 ms, 0642 0, 0657 8
 *                                                    USB AUDIO (0x46bd90):       0666 3, 0642 5, 0657 8
 * 0666 payloads are the player's out modes (mq_player_257 set_out_device 0x47bf24): 6 local analog, 4 SPDIF_TX, 3
 * USB_HOST. The player's 0666 handler (0x4f1294) closes the player and re-inits the output; 0657 8 then sets
 * LOCALPLAYER again, so a 0666 not followed by 0657 8 leaves NO_WORK_MODE (dead player). Stock sends no pause;
 * diskOS does (0666 into a live stream killed mq_player): a switch is pause ->
 * PCM proven quiet for OUT_QUIET_MS -> route -> work mode -> resume. Leaving USB audio always uses the full sequence
 * (0642 0 drops the host request). USB_HOST needs the USB DAC card (else the player uses a silent USB_HOST_NULL
 * sink, 0x47c040), so diskOS re-checks it right before the frames and refuses instead.
 * If a send fails after the route moved, the internal sequence is retried until it fully lands on a live player
 * connection (recovery-pending: every play/route/source command is refused meanwhile). */
#ifndef OUT_USB_CARD
#define OUT_USB_CARD "/proc/asound/card1"   /* the USB DAC card; its usbid node exists for USB audio cards only (UNVERIFIED on this kernel) */
#endif
#define OUT_QUIET_MS 500
#define OUT_WAIT_MS  3000
/* USB AUDIO cannot be pre-checked: the DAC is unpowered until the player handles 0642 5 (mq_player_257 mode thread
 * 0x4e5af4: sleep 1 s, role -> host, set_out_device 3 = USB_HOST_NULL with no card yet, SGM41513 OTG_ON). The card
 * then enumerates and on_usb_audio_add (0x4f39c4) re-runs set_out_device 3 against /proc/asound/card1. So switch
 * first, then wait for the card; resume only once it is up, and fall back to the internal DAC if it never comes. */
#define OUT_USB_WAIT_MS   20000  /* was 8 s: on a cold boot the player's mode thread takes ~7 s before the DAC even enumerates
                                  * (seen 8.2 s, so the old limit fell back to the internal DAC 0.14 s after the DAC appeared) */
#define OUT_USB_SETTLE_MS 1000   /* card seen -> give on_usb_audio_add time to move USB_HOST_NULL to USB_HOST */
#define OUT_PAUSE    "0201000C0000"
/* The route frames themselves live in outseq.c; its route enum must stay the same numbers as OUT_* */
_Static_assert((int)OUTSEQ_INTERNAL == OUT_INTERNAL && (int)OUTSEQ_SPDIF == OUT_SPDIF && (int)OUTSEQ_USB == OUT_USB, "outseq route numbers differ from modes.h");
static void modes_ui_refresh(void);
/* The route the UI believes the Player is on lives in session.c (session_route / session_set_route). */
static struct { int target, was_playing, full; unsigned gen; char path[256]; long pos; uint32_t t0, quiet0; lv_timer_t *tm; } g_osw;
static int g_rec = 0; static unsigned g_rec_gen = 0; static lv_timer_t *g_rec_tm = NULL;
static struct { lv_timer_t *tm; uint32_t t0, seen; } g_usbw;   /* waiting for the USB DAC to enumerate after 0642 5 */

int modes_output_route(void){ return session_route(); }
void modes_output_reset(void){ session_set_route(OUT_INTERNAL); }
/* A theme re-launch (same player, new UI process) carries the live route over: the player is still on it. */
static lv_timer_t *g_usbgone_tm; static int g_usbgone_miss;
static void usbgone_tick(lv_timer_t *t);
void modes_output_adopt(int route){
    if(route < OUT_INTERNAL || route > OUT_USB) return;
    session_set_route(route);
    modes_ui_refresh();   /* the picker and Settings may already be on screen with the old (internal) mark */
    if(route == OUT_USB){ g_usbgone_miss = 0; if(!g_usbgone_tm) g_usbgone_tm = lv_timer_create(usbgone_tick, 1000, NULL); }
}
int modes_output_busy(void){ return g_osw.tm != NULL || g_rec || g_usbw.tm != NULL; }
static int out_usb_card_ok(void){
    char p[96]; snprintf(p, sizeof p, "%s/usbid", OUT_USB_CARD);
    return access(p, F_OK) == 0;
}
/* any USB audio card at all (usbid exists only for USB cards, not the Disc's own UAC gadget): tells "no DAC" from "wrong card index" */
static int out_usb_any_card(void){
    glob_t g; int n = 0;
    if(glob("/proc/asound/card*/usbid", 0, NULL, &g) == 0){ n = (int)g.gl_pathc; globfree(&g); }
    return n > 0;
}
/* recovery-pending: retry the full internal sequence until it lands on the same live player connection */
static void out_rec_clear(void){ if(g_rec_tm){ lv_timer_del(g_rec_tm); g_rec_tm = NULL; } g_rec = 0; }
static void out_recover_tick(lv_timer_t *t){
    (void)t;
    if(ipc_generation() != g_rec_gen){ out_rec_clear(); session_set_route(OUT_INTERNAL); modes_ui_refresh(); return; }   /* a new player initialises itself */
    if(!ipc_is_ready()) return;
    if(outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL) == OUTSEQ_OK){
        out_rec_clear(); session_set_route(OUT_INTERNAL);
        ui_toast("Internal DAC restored"); modes_ui_refresh();
    }
}
static void out_rec_enter(void){
    session_set_route(OUT_INTERNAL);
    if(g_rec) return;
    g_rec = 1; g_rec_gen = ipc_generation();
    g_rec_tm = lv_timer_create(out_recover_tick, 500, NULL);
}
/* resume only the same track, and only if the seek and the play toggle were accepted */
static void out_resume(int moved){
    if(!g_osw.was_playing) return;
    track_state_t st; ipc_get_state(&st);
    if(moved && (ipc_generation() != g_osw.gen || !st.have_track || strcmp(st.path, g_osw.path) != 0)){ ui_toast("Output switched - press play"); return; }
    if(moved && g_osw.pos > 0 && ui_seek_to(g_osw.pos) < 0){ ui_toast("Couldn't resume - press play"); return; }
    if(ipc_send_cmd(OUT_PAUSE) < 0) ui_toast("Couldn't resume - press play");   /* toggle: plays again */
}
static int out_switch(int target, int via_mode);
/* USB DAC unplugged while routed to it: the player falls back to a silent USB_HOST_NULL sink and nothing in the
 * UI noticed. Poll card1 once a second; after two misses switch back to the internal DAC through the normal
 * pause -> quiet -> route path, but leave playback paused (like unplugging headphones) instead of resuming
 * into the jack. A refused switch (e.g. player busy) is retried on the next ticks. */
static void usbgone_tick(lv_timer_t *t){
    (void)t;
    if(modes_output_route() != OUT_USB){ lv_timer_del(g_usbgone_tm); g_usbgone_tm = NULL; return; }
    if(modes_output_busy()) return;
    if(out_usb_card_ok()){ g_usbgone_miss = 0; return; }
    if(++g_usbgone_miss < 2) return;
    g_usbgone_miss = 0;
    fprintf(stderr, "usb audio: DAC gone - back to the internal DAC (paused)\n"); fflush(stderr);
    if(out_switch(OUT_INTERNAL, 1) == 0){
        g_osw.was_playing = 0;   /* out_switch already paused it; never resume into the jack */
        lv_timer_del(g_usbgone_tm); g_usbgone_tm = NULL;
        ui_toast("USB DAC disconnected - internal DAC");
    }
}
/* The last picked Working Mode survives a restart. "src_mode" (main.c ui_set_source_mode: 0 Local, 1 USB DAC,
 * 2 BT Receiving; USB Storage is never saved) and "usb_audio_on" (set once a USB DAC is confirmed connected,
 * cleared when a switch away from USB completes - a user choice or the unplug fallback) are replayed ~15 s after
 * boot, past the player's settle. A USB Audio restore that finds no DAC falls back via the usbw timeout and keeps
 * the preference for the next boot. */
#define OUT_USB_PREF "usb_audio_on"
static lv_timer_t *g_bootusb_tm; static uint32_t g_bootusb_t0;
static void bootusb_end(void){ if(g_bootusb_tm){ lv_timer_del(g_bootusb_tm); g_bootusb_tm = NULL; } }
static void bootusb_tick(lv_timer_t *t){
    (void)t;
    int src = cfg_get_int("src_mode", 0), usb = cfg_get_int(OUT_USB_PREF, 0) == 1;
    if((src != 1 && src != 2 && !usb) || lv_tick_elaps(g_bootusb_t0) > 120000){ bootusb_end(); return; }
    if(lv_tick_elaps(g_bootusb_t0) < 15000) return;   /* past the player's ~9 s settle and the boot local-init */
    if(ui_get_source_mode() != 0){ bootusb_end(); return; }   /* already left Local (the user, or a restore) */
    if(src == 1 || src == 2){
        if(modes_output_busy()) return;
        bootusb_end();
        if(ui_set_source_mode(src) == 0) ui_toast(src == 1 ? "Restoring USB DAC mode..." : "Restoring Bluetooth Receiving...");
        modes_ui_refresh();
        return;
    }
    if(modes_output_route() == OUT_USB){ bootusb_end(); return; }
    if(modes_output_busy() || ui_output_blocked()) return;
    bootusb_end();
    if(out_switch(OUT_USB, 1) == 0) ui_toast("Restoring USB Audio...");
}
void modes_output_boot_restore(void){
    g_bootusb_t0 = lv_tick_get();
    if(!g_bootusb_tm) g_bootusb_tm = lv_timer_create(bootusb_tick, 1000, NULL);
}
static void usbgone_start(void){
    g_usbgone_miss = 0;
    if(!g_usbgone_tm) g_usbgone_tm = lv_timer_create(usbgone_tick, 1000, NULL);
}
static void usbw_end(void){ if(g_usbw.tm){ lv_timer_del(g_usbw.tm); g_usbw.tm = NULL; } }
static void usbw_tick(lv_timer_t *t){
    (void)t;
    if(ipc_generation() != g_osw.gen){ usbw_end(); session_set_route(OUT_INTERNAL); ui_toast("Player restarted - try again"); modes_ui_refresh(); return; }
    if(out_usb_card_ok()){
        if(!g_usbw.seen) g_usbw.seen = lv_tick_get() ? lv_tick_get() : 1;
        if(lv_tick_elaps(g_usbw.seen) < OUT_USB_SETTLE_MS) return;
        usbw_end(); ui_toast("USB DAC connected"); out_resume(1); usbgone_start(); modes_ui_refresh();
        cfg_set_int(OUT_USB_PREF, 1);
        return;
    }
    if(lv_tick_elaps(g_usbw.t0) < OUT_USB_WAIT_MS) return;
    usbw_end();   /* never leave the player on the silent USB_HOST_NULL sink */
    if(outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL) == OUTSEQ_OK){
        session_set_route(OUT_INTERNAL);
        ui_toast(out_usb_any_card() ? "USB DAC not on card1 - internal DAC restored" : "No USB DAC found - internal DAC restored");
        out_resume(1);
    } else {
        out_rec_enter();
        ui_toast("Output state uncertain - recovering");
    }
    modes_ui_refresh();
}
static void usbw_start(void){
    g_usbw.t0 = lv_tick_get(); g_usbw.seen = 0;
    g_usbw.tm = lv_timer_create(usbw_tick, 250, NULL);
    ui_toast("Powering USB DAC...");
}
static int out_run(void){
    int target = g_osw.target;
    outseq_rc rc = outseq_route((outseq_route_t)target, g_osw.full ? OUTSEQ_FULL : OUTSEQ_QUICK);
    if(rc == OUTSEQ_REFUSED){ session_set_route(modes_output_route()); ui_toast("Couldn't switch output"); out_resume(0); modes_ui_refresh(); return -1; }
    if(rc == OUTSEQ_OK && target == OUT_USB){ session_set_route(target); usbw_start(); modes_ui_refresh(); return 0; }   /* resume once the DAC is up, not into USB_HOST_NULL */
    if(rc == OUTSEQ_OK){ session_set_route(target); out_resume(1); cfg_set_int(OUT_USB_PREF, 0); modes_ui_refresh(); return 0; }
    /* the route moved but the tail failed: back to the internal DAC; never end without 0657 8 landing */
    if(outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL) == OUTSEQ_OK){
        session_set_route(OUT_INTERNAL);
        ui_toast("Output switch failed - internal DAC restored");
        out_resume(1); modes_ui_refresh();
        return -1;
    }
    out_rec_enter();
    ui_toast("Output state uncertain - recovering");
    modes_ui_refresh();
    return -1;
}
static void osw_stop(const char *why, int resume){
    lv_timer_del(g_osw.tm); g_osw.tm = NULL;
    session_set_route(modes_output_route());
    ui_toast(why);
    if(resume) out_resume(0);
    modes_ui_refresh();
}
static void osw_tick(lv_timer_t *t){
    (void)t;
    track_state_t st; ipc_get_state(&st);
    if(ipc_generation() != g_osw.gen){ osw_stop("Player restarted - try again", 0); return; }
    if(strcmp(st.path, g_osw.path) != 0){ osw_stop("Track changed - try again", 0); return; }
    if(lv_tick_elaps(g_osw.t0) >= OUT_WAIT_MS){ osw_stop("Player is busy - try again", 0); return; }   /* deadline first: a late tick never routes, even with the hold complete */
    /* "Quiet" = the track position has stopped advancing. V2.57 keeps the I2S PCM RUNNING (writing silence) while
     * paused, so a closed-PCM requirement refused every switch after the first play since boot. ui_route_bt()
     * already sends 0666 straight after a pause on this firmware. */
    if(ui_is_playing()) g_osw.quiet0 = 0;
    else if(!g_osw.quiet0) g_osw.quiet0 = lv_tick_get() ? lv_tick_get() : 1;
    if(!g_osw.quiet0 || lv_tick_elaps(g_osw.quiet0) < OUT_QUIET_MS){
        return;
    }
    const char *why = ui_output_blocked();
    if(why){ osw_stop(why, g_osw.was_playing); return; }
    lv_timer_del(g_osw.tm); g_osw.tm = NULL;
    out_run();
}
static int out_switch(int target, int via_mode){
    if(target < OUT_INTERNAL || target > OUT_USB) return -1;
    if(modes_output_busy()){ ui_toast("Switching..."); return -1; }
    const char *why = ui_output_blocked();
    if(why){ session_set_route(modes_output_route()); ui_toast(why); return -1; }
    int was_playing = ui_is_playing();
    track_state_t st; ipc_get_state(&st);
    memset(&g_osw, 0, sizeof g_osw);
    g_osw.target = target; g_osw.was_playing = was_playing;
    g_osw.full = via_mode || target == OUT_USB || modes_output_route() == OUT_USB;   /* Settings toggle = stock's 2-frame form, except to/from USB */
    g_osw.gen = ipc_generation(); g_osw.pos = st.have_track ? st.position_ms : 0;
    snprintf(g_osw.path, sizeof g_osw.path, "%s", st.have_track ? st.path : "");
    if(was_playing && ipc_send_cmd(OUT_PAUSE) < 0){ session_set_route(modes_output_route()); ui_toast("Player is busy - try again"); return -1; }
    g_osw.t0 = lv_tick_get();
    g_osw.tm = lv_timer_create(osw_tick, 100, NULL);
    return 0;
}
int modes_output_switch(int target){ return out_switch(target, 0); }
int modes_output_mode_switch(int target){ return out_switch(target, 1); }
/* OUTPUT-ROUTE-END */

/* Row ids 0..3 are source modes; id 4 (USB Audio) shows selected while Local runs with the USB route. It is displayed
 * second, under Local Playback (modes_create), but keeps id 4. */
static int shown_mode(int src){ return (src == 0 && N_MODES > ROW_USB_AUDIO && modes_output_route() == OUT_USB) ? ROW_USB_AUDIO : src; }

static void mark_selected_mode(int cur){
    for(int i=0;i<N_MODES;i++){
        if(g_check[i]){ lv_label_set_text(g_check[i], i==cur ? LV_SYMBOL_OK : "");
                        lv_obj_set_style_text_color(g_check[i], ui_current_accent(), 0); }  /* track accent changes */
        if(g_row[i]){   /* selected row gets an accent ring + slightly lifted fill */
            lv_obj_set_style_border_width(g_row[i], i==cur ? 2 : 0, 0);
            lv_obj_set_style_border_color(g_row[i], ui_current_accent(), 0);
            lv_obj_set_style_bg_color(g_row[i], (i==cur ? TC(SURFACE_SELECTED) : TC(SURFACE)), 0);
        }
    }
}
static void mark_selected(void){ mark_selected_mode(ui_source_switch_failed() ? -1 : shown_mode(ui_get_source_mode())); }
/* an output switch finished, failed or recovered (or the player restarted): show the real route in the picker and Settings */
static void modes_ui_refresh(void){
    mark_selected();
    setting_detail_refresh();
    setlist_refresh();
}


static uint32_t g_last_switch = 0;   /* debounce: a switch takes a few seconds to apply in the player */

/* Pending state: the tapped row shows a "switching" glyph (not the confirmed checkmark) while the
 * gadget switch is in flight - there is no source-mode completion readback, so after the switch
 * window we settle to the selection best-effort (matches the honest "Switching..." toast). */
static void mark_pending(int m){
    for(int i=0;i<N_MODES;i++){
        if(g_check[i]){ lv_label_set_text(g_check[i], i==m ? LV_SYMBOL_REFRESH : "");
                        lv_obj_set_style_text_color(g_check[i], ui_current_accent(), 0); }
        if(g_row[i]){   /* highlight the row being switched to */
            lv_obj_set_style_border_width(g_row[i], i==m ? 2 : 0, 0);
            lv_obj_set_style_border_color(g_row[i], ui_current_accent(), 0);
            lv_obj_set_style_bg_color(g_row[i], (i==m ? TC(SURFACE_SELECTED) : TC(SURFACE)), 0);
        }
    }
}

static lv_timer_t *g_settle = NULL;
static int g_pending_mode = -1;   /* the mode a switch is settling to (so reopening the screen keeps showing "switching") */
static struct { lv_timer_t *tm; int mode; uint32_t t0; } g_leave;   /* passing through Local before a source switch (see leave_tick) */
/* M17: after the switch window, settle to the CONFIRMED mode read from the real USB gadget state,
 * not a blind assumption. If the gadget shows the switch didn't take, reflect reality + say so. */
static void settle_cb(lv_timer_t *t){
    (void)t;
    if(g_osw.tm || g_leave.tm) return;   /* an output switch is still waiting for the pause, or the source switch behind it */
    if(ui_source_switch_pending()) return;
    if(lv_tick_elaps(g_last_switch) < 3200) return;
    lv_timer_del(g_settle); g_settle = NULL;
    int intended = g_pending_mode; g_pending_mode = -1;
    if(ui_source_switch_failed()){ mark_selected_mode(-1); return; }
    /* M17: DISPLAY the ACTUAL gadget state (read-only) instead of a blind timer assumption. Do NOT
     * mutate the intent mirror (g_source_mode) - it also guards coldplug, and a transient mid-transition
     * sample must not flip that guard. */
    int show = (intended >= 0) ? intended : ui_get_source_mode();
    if(intended == 0 || intended == 1 || intended == 3){   /* USB gadget modes are readback-confirmable */
        int actual = ui_detect_source_mode();
        show = actual;
        if(actual != intended) ui_toast("Mode didn't switch");
    }
    /* intended == 2 (BT receiving) is gadget-invisible and needs a phone to connect - no reliable
     * readback here, so show the intent without asserting a false confirmation. */
    if(intended == ROW_USB_AUDIO) show = ui_get_source_mode();   /* USB Audio: not visible in the gadget state; the route we last set decides */
    mark_selected_mode(shown_mode(show));
}

/* honest wording: the frames are queued; the async switch completes a moment later. */
static const char *const SWITCH_MSG[N_MODES] = {
    "Switching to local playback", "Switching to USB DAC",
    "Switching to Bluetooth receiving", "Switching to USB storage",
#ifdef DISKOS_TEST_OUTPUTS
    "",
#endif
};
/* Via Local (main.c ui_set_source_mode, see its transition table): USB Audio -> USB DAC / BT / Storage and
 * USB DAC -> Storage first go to plain Local - USB Audio through the internal-DAC output switch (pause -> quiet ->
 * 0666 6, 0642 0, 0657 8), USB DAC through ui_set_source_mode(0) - and only once that landed and the player had
 * the picker's 3 s to apply it, re-enter ui_set_source_mode with the picked mode so all its guards (and the SD
 * drain) run. Playback stays paused like the unplug fallback: never resume into the jack on the way through. */
static void leave_end(const char *why){
    if(g_leave.tm){ lv_timer_del(g_leave.tm); g_leave.tm = NULL; }
    if(g_settle){ lv_timer_del(g_settle); g_settle = NULL; }   /* nothing is settling: show the real selection now */
    g_pending_mode = -1;
    if(why) ui_toast(why);
    modes_ui_refresh();
}
static void leave_tick(lv_timer_t *t){
    (void)t;
    if(lv_tick_elaps(g_leave.t0) >= 10000){ leave_end("Couldn't return to Local - mode not changed"); return; }
    if(modes_output_busy() || lv_tick_elaps(g_leave.t0) < 3000) return;   /* still pausing / routing / recovering, or settling */
    if(modes_output_route() != OUT_INTERNAL || ui_get_source_mode() != 0){ leave_end("Still not on Local - mode not changed"); return; }   /* refused or stopped */
    int m = g_leave.mode;
    lv_timer_del(g_leave.tm); g_leave.tm = NULL;
    cfg_set_int(OUT_USB_PREF, 0);   /* a deliberate leave, also when out_run only got here through its fallback */
    g_last_switch = lv_tick_get();  /* the settle window runs from the real source switch */
    if(ui_set_source_mode(m) != 0){ leave_end(NULL); return; }   /* it toasted why; the picker shows Local */
    ui_toast(SWITCH_MSG[m]);
}
int modes_via_local_then(int mode){
    if(g_leave.tm){ ui_toast("Switching..."); return -1; }
    const char *msg = "Leaving USB DAC...";
    if(modes_output_route() == OUT_USB){
        if(out_switch(OUT_INTERNAL, 1) != 0) return -1;   /* out_switch toasted why */
        g_osw.was_playing = 0;   /* out_switch already paused it; never resume into the jack */
        msg = "Leaving USB Audio...";
    } else if(ui_set_source_mode(0) != 0) return -1;     /* it toasted why (a connected computer, SD state) */
    g_leave.mode = mode; g_leave.t0 = lv_tick_get();
    g_leave.tm = lv_timer_create(leave_tick, 100, NULL);
    ui_toast(msg);
    return 0;
}

static void row_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int m = (int)(uintptr_t)lv_event_get_user_data(e);
    /* Serialise: ignore taps while the previous switch is still applying (the player's gadget
     * state-machine is asynchronous). NB we do NOT early-return on "same mode" - re-issuing must
     * always be allowed so Local works as a recover even if our cached mode is stale. */
    if(g_leave.tm || (g_last_switch && lv_tick_elaps(g_last_switch) < 3000)){ ui_toast("Switching..."); return; }
    g_last_switch = lv_tick_get();
    if(m == ROW_USB_AUDIO){   /* USB Audio: an output route of the Local source, not a source mode */
        if(modes_output_mode_switch(OUT_USB) == 0){
            g_pending_mode = m; mark_pending(m);
            if(g_settle) lv_timer_del(g_settle);
            g_settle = lv_timer_create(settle_cb, 500, NULL);
            ui_toast("Switching to USB audio output");
        }
        return;
    }
    if(ui_set_source_mode(m) == 0){
        g_pending_mode = m;
        mark_pending(m);      /* async switch in flight: show "switching", not a confirmed selection */
        if(g_settle) lv_timer_del(g_settle);
        g_settle = lv_timer_create(settle_cb, 500, NULL);   /* settle to the checkmark after the switch window */
        if(!g_leave.tm) ui_toast(SWITCH_MSG[m]);   /* passing through Local first: its toast stays, leave_tick says this later */
    }
}

void modes_open(void){
    if(g_settle && g_pending_mode >= 0) mark_pending(g_pending_mode);  /* a switch is still settling - keep showing it */
    else mark_selected();
    screen_show(SCR_WORKMODE);
}

void modes_create(lv_obj_t *root){
    if(cfg_get_int("spdif", 0)) cfg_set_int("spdif", 0);   /* the route is per player run: a fresh UI has not switched anything */
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* back button (kept out of the clipped top-left corner) */
    ui_header(root, "Working Mode");   /* shared standard header */

    /* vertical list of mode rows */
    lv_obj_t *col = lv_obj_create(root);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 300, 250); lv_obj_set_pos(col, 30, 76);
    lv_obj_set_style_pad_bottom(col, 44, 0);   /* last mode row scrolls clear of the round bezel */
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);

    for(int i=0;i<N_MODES;i++){
        lv_obj_t *row = lv_button_create(col);
        g_row[i] = row;
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 268, 54);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_bg_color(row, TC(SURFACE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
        ui_on(row, row_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i, "modes.row", UI_CORE);

        lv_obj_t *nm = lv_label_create(row);
        lv_label_set_text(nm, MODES[i].name);
        lv_obj_set_pos(nm, 16, 9);
        lv_obj_set_style_text_font(nm, TF(UI_16), 0);
        lv_obj_set_style_text_color(nm, TC(TEXT_PRIMARY), 0);

        lv_obj_t *sb = lv_label_create(row);
        lv_label_set_text(sb, MODES[i].sub);
        lv_obj_set_pos(sb, 16, 30);
        lv_obj_set_width(sb, 202);                       /* keep clear of the right-side checkmark */
        lv_label_set_long_mode(sb, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(sb, TF(UI_12), 0);
        lv_obj_set_style_text_color(sb, TC(TEXT_MUTED), 0);

        g_check[i] = lv_label_create(row);
        lv_label_set_text(g_check[i], "");
        lv_obj_align(g_check[i], LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_set_style_text_color(g_check[i], ui_current_accent(), 0);
        lv_obj_set_style_text_font(g_check[i], TF(UI_18), 0);
    }
    /* USB Audio is an output route of Local playback, so it sits right under Local Playback. Only the display order
     * changes: rows keep their ids (the row index is the mode id everywhere above), so nothing else moves. */
    if(N_MODES > ROW_USB_AUDIO) lv_obj_move_to_index(g_row[ROW_USB_AUDIO], 1);
    mark_selected();
}
