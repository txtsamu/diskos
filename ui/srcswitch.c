/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Source-mode and Output-route switch (GLOSSARY.md): every change of where the Disc's sound comes from or goes to. One
 * module, because the two are one state machine: leaving USB Audio for USB DAC / Bluetooth Receiving / USB Storage is an
 * Output-route switch followed by a Source-mode switch, and leaving USB DAC with a computer attached is an ordered exit
 * followed by the same. It owns
 *   - the Output route: the pause -> quiet -> route -> work mode -> resume switch, the wait for a USB DAC to enumerate,
 *     the recovery that retries the internal DAC until it lands, the watcher that leaves a USB DAC that was unplugged;
 *   - the Source-mode switch itself (ui_set_source_mode), applying the transition table below, with the pass through
 *     Local ("via Local") and the ordered USB DAC exit as deferred steps INSIDE it, so nothing re-enters from outside;
 *   - the SD handoff to a computer (USB Storage): the phase machine, its crash-safe marker, the admission gate that
 *     closes before an export is queued and reopens only once the card is back and mounted;
 *   - the volume carry into USB DAC, and the boot restore of the saved Working Mode / USB Audio.
 * The persisted Working Mode and the Source mode / Output route values themselves stay in session.c, the frames in
 * outseq.c. No LVGL: time and everything that touches the system (gadget and mount state, the Player's pause state, the
 * library scan, toasts, the restart) come through struct srcswitch_env, the Player through ipc_*() and outseq, and the
 * picker is told about changes through one listener (srcswitch_set_listener). A host test drives it with fakes. All the
 * deferred steps advance from srcswitch_tick() (100 ms); each keeps the period its LVGL timer used to have. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <glob.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "srcswitch.h"
#include "sdio.h"
#include "outseq.h"
#include "modes.h"
#include "ipc.h"
#include "config.h"
#include "session.h"
#include "admit.h"

static const struct srcswitch_env *E;
static uint32_t now_ms(void){ return E->now(); }
static uint32_t elapsed(uint32_t t0){ return now_ms() - t0; }
void srcswitch_set_env(const struct srcswitch_env *env){ E = env; }
static void (*g_listener)(srcswitch_event_t);
void srcswitch_set_listener(void (*cb)(srcswitch_event_t)){ g_listener = cb; }
static void notify(srcswitch_event_t ev){ if(g_listener) g_listener(ev); }

/* A software timer that runs on srcswitch_tick(), standing in for an lv_timer: due once `period` ms have passed since it
 * was started or last ran. */
typedef struct { int on; uint32_t period, last; } tm_t;
static void tm_start(tm_t *t, uint32_t period){ t->on = 1; t->period = period; t->last = now_ms(); }
static void tm_stop(tm_t *t){ t->on = 0; }
static int  tm_due(tm_t *t){
    if(!t->on || elapsed(t->last) < t->period) return 0;
    t->last = now_ms(); return 1;
}

/* ================= Output route ================= */
/* Stock V2.57 (mq_ui_257.dis):
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
 * connection (recovery-pending: every play/route/source command is refused meanwhile).
 * Output route: Internal DAC / SPDIF / USB Audio (SPDIF row + USB Audio picker row: -DDISKOS_TEST_OUTPUTS builds only). */
#ifndef OUT_USB_CARD
#define OUT_USB_CARD "/proc/asound/card1"   /* the USB DAC card; its usbid node exists for USB audio cards only (UNVERIFIED on this kernel) */
#endif
#ifndef OUT_USB_ANY_CARD
#define OUT_USB_ANY_CARD "/proc/asound/card*/usbid"
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
/* The route frames themselves live in outseq.c; its route enum must stay the same numbers as OUT_* */
_Static_assert((int)OUTSEQ_INTERNAL == OUT_INTERNAL && (int)OUTSEQ_SPDIF == OUT_SPDIF && (int)OUTSEQ_USB == OUT_USB, "outseq route numbers differ from modes.h");
/* The route the UI believes the Player is on lives in session.c (session_route / session_set_route). */
static struct { int target, was_playing, full; unsigned gen; char path[256]; long pos; uint32_t t0, quiet0; tm_t tm; } g_osw;
static int g_rec = 0; static unsigned g_rec_gen = 0; static tm_t g_rec_tm;
static struct { tm_t tm; uint32_t t0, seen; } g_usbw;   /* waiting for the USB DAC to enumerate after 0642 5 */
static tm_t g_usbgone; static int g_usbgone_miss, g_usbgone_tries;
static tm_t g_bootusb;
/* passing through Local before a source switch (see leave_tick) */
static struct { tm_t tm; int mode; uint32_t t0; } g_leave;

static int source_switch(int mode, int exit_done);
static int out_switch(int target, int via_mode);

int modes_output_route(void){ return session_route(); }
void modes_output_reset(void){ session_set_route(OUT_INTERNAL); }
int modes_output_busy(void){ return g_osw.tm.on || g_rec || g_usbw.tm.on; }
/* A theme re-launch (same player, new UI process) carries the live route over: the player is still on it. session_adopt()
 * already took the route over; this is the part that needs the UI. */
void modes_output_adopt(void){
    notify(SRCSWITCH_EV_CHANGED);   /* the picker and Settings may already be on screen with the old (internal) mark */
    if(session_route() == OUT_USB){ g_usbgone_miss = 0; g_usbgone_tries = 0; if(!g_usbgone.on) tm_start(&g_usbgone, 1000); }
}
static int out_usb_card_ok(void){
    char p[96]; snprintf(p, sizeof p, "%s/usbid", OUT_USB_CARD);
    return access(p, F_OK) == 0;
}
/* any USB audio card at all (usbid exists only for USB cards, not the Disc's own UAC gadget): tells "no DAC" from "wrong card index" */
static int out_usb_any_card(void){
    glob_t g; int n = 0;
    if(glob(OUT_USB_ANY_CARD, 0, NULL, &g) == 0){ n = (int)g.gl_pathc; globfree(&g); }
    return n > 0;
}
/* recovery-pending: retry the full internal sequence until it lands on the same live player connection */
static void out_rec_clear(void){ tm_stop(&g_rec_tm); g_rec = 0; }
static void out_recover_tick(void){
    if(ipc_generation() != g_rec_gen){ out_rec_clear(); session_set_route(OUT_INTERNAL); notify(SRCSWITCH_EV_CHANGED); return; }   /* a new player initialises itself */
    if(!ipc_is_ready()) return;
    if(outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL) == OUTSEQ_OK){
        out_rec_clear(); session_set_route(OUT_INTERNAL);
        E->toast("Internal DAC restored"); notify(SRCSWITCH_EV_CHANGED);
    }
}
static void out_rec_enter(void){
    session_set_route(OUT_INTERNAL);
    if(g_rec) return;
    g_rec = 1; g_rec_gen = ipc_generation();
    tm_start(&g_rec_tm, 500);
}
/* resume only the same track, and only if the seek and the play toggle were accepted */
static void out_resume(int moved){
    if(!g_osw.was_playing) return;
    track_state_t st; ipc_get_state(&st);
    if(moved && (ipc_generation() != g_osw.gen || !st.have_track || strcmp(st.path, g_osw.path) != 0)){ E->toast("Output switched - press play"); return; }
    if(moved && g_osw.pos > 0 && E->seek_to(g_osw.pos) < 0){ E->toast("Couldn't resume - press play"); return; }
    if(ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0) E->toast("Couldn't resume - press play");   /* toggle: plays again */
}
/* USB DAC unplugged while routed to it: the player falls back to a silent USB_HOST_NULL sink and nothing in the
 * UI noticed. Poll card1 once a second; after two misses switch back to the internal DAC through the normal
 * pause -> quiet -> route path, but leave playback paused (like unplugging headphones) instead of resuming
 * into the jack. A refused switch (e.g. player busy) is retried on the next ticks. An ACCEPTED switch is only a timer
 * (osw_tick): it can still abort (3 s deadline, track change, admit_check) and leave the route on USB, so the
 * watcher stays until the route reads internal, and starts another attempt (no second toast) - at most USBGONE_TRIES
 * accepted ones, then it gives up and says so rather than retry forever. */
#define USBGONE_TRIES 3
static void usbgone_tick(void){
    if(modes_output_route() != OUT_USB){ tm_stop(&g_usbgone); return; }
    if(modes_output_busy()) return;
    if(out_usb_card_ok()){ g_usbgone_miss = 0; g_usbgone_tries = 0; return; }   /* the DAC is back: nothing to undo */
    if(++g_usbgone_miss < 2) return;
    g_usbgone_miss = 0;
    if(g_usbgone_tries >= USBGONE_TRIES){
        tm_stop(&g_usbgone);
        E->toast("Couldn't leave the USB DAC - pick the internal DAC in Settings");
        return;
    }
    fprintf(stderr, "usb audio: DAC gone - back to the internal DAC (paused)\n"); fflush(stderr);
    if(out_switch(OUT_INTERNAL, 1) == 0){
        g_osw.was_playing = 0;   /* out_switch already paused it; never resume into the jack */
        if(!g_usbgone_tries++) E->toast("USB DAC disconnected - internal DAC");
    }
}
/* The last picked Working Mode survives a restart. The saved "src_mode" (0 Local, 1 USB DAC, 2 BT Receiving; USB Storage
 * is never saved) and "usb_audio_on" (set once a USB DAC is confirmed connected, cleared when a switch away from USB
 * completes - a user choice or the unplug fallback) are replayed ~15 s after boot, past the player's settle. A USB Audio
 * restore that finds no DAC falls back via the usbw timeout and keeps the preference for the next boot. Whether a restore
 * is still wanted (a relaunch that kept the Player's state, the user already moved on) is decided in session.c; this
 * timer only polls it and runs the switch it names. */
static void bootusb_tick(void){
    int src = 0;
    session_restore_t r = session_restore_poll(now_ms(), modes_output_busy(), admit_check(ADMIT_OUTPUT_SWITCH) != NULL, &src);
    if(r == SESSION_RESTORE_WAIT) return;
    tm_stop(&g_bootusb);
    if(r == SESSION_RESTORE_SOURCE){
        if(ui_set_source_mode(src) == 0) E->toast(src == 1 ? "Restoring USB DAC mode..." : "Restoring Bluetooth Receiving...");
        notify(SRCSWITCH_EV_CHANGED);
    } else if(r == SESSION_RESTORE_USB_AUDIO){
        if(out_switch(OUT_USB, 1) == 0) E->toast("Restoring USB Audio...");
    }
}
void modes_output_boot_restore(void){
    session_restore_arm(now_ms());
    if(!g_bootusb.on) tm_start(&g_bootusb, 1000);
}
static void usbgone_start(void){
    g_usbgone_miss = 0; g_usbgone_tries = 0;
    if(!g_usbgone.on) tm_start(&g_usbgone, 1000);
}
static void usbw_tick(void){
    if(ipc_generation() != g_osw.gen){ tm_stop(&g_usbw.tm); session_set_route(OUT_INTERNAL); E->toast("Player restarted - try again"); notify(SRCSWITCH_EV_CHANGED); return; }
    if(out_usb_card_ok()){
        if(!g_usbw.seen) g_usbw.seen = now_ms() ? now_ms() : 1;
        if(elapsed(g_usbw.seen) < OUT_USB_SETTLE_MS) return;
        tm_stop(&g_usbw.tm); E->toast("USB DAC connected"); out_resume(1); usbgone_start(); notify(SRCSWITCH_EV_CHANGED);
        session_save_usb_audio(1);
        return;
    }
    if(elapsed(g_usbw.t0) < OUT_USB_WAIT_MS) return;
    tm_stop(&g_usbw.tm);   /* never leave the player on the silent USB_HOST_NULL sink */
    if(outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL) == OUTSEQ_OK){
        session_set_route(OUT_INTERNAL);
        E->toast(out_usb_any_card() ? "USB DAC not on card1 - internal DAC restored" : "No USB DAC found - internal DAC restored");
        out_resume(1);
    } else {
        out_rec_enter();
        E->toast("Output state uncertain - recovering");
    }
    notify(SRCSWITCH_EV_CHANGED);
}
static void usbw_start(void){
    g_usbw.t0 = now_ms(); g_usbw.seen = 0;
    tm_start(&g_usbw.tm, 250);
    E->toast("Powering USB DAC...");
}
static int out_run(void){
    int target = g_osw.target;
    outseq_rc rc = outseq_route((outseq_route_t)target, g_osw.full ? OUTSEQ_FULL : OUTSEQ_QUICK);
    if(rc == OUTSEQ_REFUSED){ session_set_route(modes_output_route()); E->toast("Couldn't switch output"); out_resume(0); notify(SRCSWITCH_EV_CHANGED); return -1; }
    if(rc == OUTSEQ_OK && target == OUT_USB){ session_set_route(target); usbw_start(); notify(SRCSWITCH_EV_CHANGED); return 0; }   /* resume once the DAC is up, not into USB_HOST_NULL */
    if(rc == OUTSEQ_OK){ session_set_route(target); out_resume(1); session_save_usb_audio(0); notify(SRCSWITCH_EV_CHANGED); return 0; }
    /* the route moved but the tail failed: back to the internal DAC; never end without 0657 8 landing */
    if(outseq_route(OUTSEQ_INTERNAL, OUTSEQ_FULL) == OUTSEQ_OK){
        session_set_route(OUT_INTERNAL);
        E->toast("Output switch failed - internal DAC restored");
        out_resume(1); notify(SRCSWITCH_EV_CHANGED);
        return -1;
    }
    out_rec_enter();
    E->toast("Output state uncertain - recovering");
    notify(SRCSWITCH_EV_CHANGED);
    return -1;
}
static void osw_stop(const char *why, int resume){
    tm_stop(&g_osw.tm);
    session_set_route(modes_output_route());
    E->toast(why);
    if(resume) out_resume(0);
    notify(SRCSWITCH_EV_CHANGED);
}
static void osw_tick(void){
    track_state_t st; ipc_get_state(&st);
    if(ipc_generation() != g_osw.gen){ osw_stop("Player restarted - try again", 0); return; }
    if(strcmp(st.path, g_osw.path) != 0){ osw_stop("Track changed - try again", 0); return; }
    if(elapsed(g_osw.t0) >= OUT_WAIT_MS){ osw_stop("Player is busy - try again", 0); return; }   /* deadline first: a late tick never routes, even with the hold complete */
    /* "Quiet" = the track position has stopped advancing. V2.57 keeps the I2S PCM RUNNING (writing silence) while
     * paused, so a closed-PCM requirement refused every switch after the first play since boot. ui_route_bt()
     * already sends 0666 straight after a pause on this firmware. */
    if(E->playing()) g_osw.quiet0 = 0;
    else if(!g_osw.quiet0) g_osw.quiet0 = now_ms() ? now_ms() : 1;
    if(!g_osw.quiet0 || elapsed(g_osw.quiet0) < OUT_QUIET_MS){
        return;
    }
    const char *why = admit_check(ADMIT_OUTPUT_SWITCH);
    if(why){ osw_stop(why, g_osw.was_playing); return; }
    tm_stop(&g_osw.tm);
    out_run();
}
static int out_switch(int target, int via_mode){
    if(target < OUT_INTERNAL || target > OUT_USB) return -1;
    if(modes_output_busy()){ E->toast("Switching..."); return -1; }
    const char *why = admit_check(ADMIT_OUTPUT_SWITCH);
    if(why){ session_set_route(modes_output_route()); E->toast(why); return -1; }
    int was_playing = E->playing();
    track_state_t st; ipc_get_state(&st);
    memset(&g_osw, 0, sizeof g_osw);
    g_osw.target = target; g_osw.was_playing = was_playing;
    g_osw.full = via_mode || target == OUT_USB || modes_output_route() == OUT_USB;   /* Settings toggle = stock's 2-frame form, except to/from USB */
    g_osw.gen = ipc_generation(); g_osw.pos = st.have_track ? st.position_ms : 0;
    snprintf(g_osw.path, sizeof g_osw.path, "%s", st.have_track ? st.path : "");
    if(was_playing && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0){ session_set_route(modes_output_route()); E->toast("Player is busy - try again"); return -1; }
    g_osw.t0 = now_ms();
    tm_start(&g_osw.tm, 100);
    return 0;
}
int modes_output_switch(int target){ return out_switch(target, 0); }
int modes_output_mode_switch(int target){ return out_switch(target, 1); }

/* ================= SD handoff ================= */
/* SD operations share an admission gate. It closes before an export is queued and
 * reopens only after that export was observed and its return mount is confirmed. */
static _Atomic int g_sd_writable = 1;
static _Atomic int g_sd_writers = 0;
static _Atomic int g_sd_hold = 0;
/* Player-launch verdict as the UI sees it: -1 = still pending, 0 = launched unprotected/unconfirmed,
 * 1 = this player's launch was confirmed guarded AND locally owned. Atomic because worker threads read
 * it through storage_media_local(). Admission stays CLOSED for anything but 1, which is why this is
 * separate from g_sd_hold: a PENDING verdict must not poison g_sd_phase the way a real hold does. */
static _Atomic int g_launch_verdict = -1;
static pthread_mutex_t g_sd_mode_mu = PTHREAD_MUTEX_INITIALIZER;
#ifndef SD_EXPORT_MARKER   /* a host test points it at a temp dir */
#define SD_EXPORT_MARKER "/tmp/.diskos_sd_export_intent"
#endif
typedef enum { SD_LOCAL, SD_DRAIN, SD_WAIT_HOST, SD_HOST, SD_WAIT_LOCAL, SD_UNKNOWN } sd_phase_t;
static sd_phase_t g_sd_phase = SD_LOCAL;
static uint32_t g_sd_deadline;
static int g_sd_return_mode;
static int g_sd_reissue;
static int storage_media_local(void){
    /* The launch verdict gates EVERY admission path (sd_io_init's callback, sd_io_resume, storage_tick,
     * the play path) - not just the three startup calls. Until the current player's launch is confirmed
     * we must not read the card at all, because we cannot yet know the delete guard is in effect. */
    if(atomic_load(&g_launch_verdict) != 1) return 0;
    return !atomic_load(&g_sd_hold) && !E->exported_to_host() && E->media_mounted();
}
int ui_local_playback_allowed(void){
    return atomic_load(&g_launch_verdict) == 1 && g_sd_phase == SD_LOCAL && !g_sd_hold && sd_io_healthy();
}

/* SD quiesce for a device Restart: closes BOTH admission paths (sd_io leases and sd_write_begin writers) and keeps
 * storage_tick from reopening them while set (it otherwise resumes a closed-but-local card within ~100 ms). The caller
 * polls ui_sd_quiesce_drained() and either reboots or ends the quiesce, which reopens only a still-local card. */
static _Atomic int g_sd_quiesce = 0;
void ui_sd_quiesce_begin(void){
    atomic_store(&g_sd_quiesce, 1);
    sd_io_hold();
    atomic_store(&g_sd_writable, 0);
}
int ui_sd_quiesce_drained(void){ return sd_io_active() == 0 && atomic_load(&g_sd_writers) == 0; }
void ui_sd_quiesce_end(void){
    atomic_store(&g_sd_quiesce, 0);
    if(g_sd_phase == SD_LOCAL && !g_sd_hold){ atomic_store(&g_sd_writable, 1); sd_io_resume(); }
}
int sd_write_begin(void){
    atomic_fetch_add(&g_sd_writers, 1);
    if(!atomic_load(&g_sd_writable) || E->exported_to_host()){
        atomic_fetch_sub(&g_sd_writers, 1); return 0;
    }
    return 1;
}
void sd_write_end(void){ atomic_fetch_sub(&g_sd_writers, 1); }
int ui_source_switch_failed(void){ return g_sd_phase == SD_UNKNOWN || g_sd_hold; }
int ui_source_switch_pending(void){
    return g_sd_phase == SD_DRAIN || g_sd_phase == SD_WAIT_HOST || g_sd_phase == SD_WAIT_LOCAL;
}
/* State is recorded BEFORE sending a request, so a UI restart cannot forget a
 * queued export. E/H/R distinguish an unobserved export from a confirmed one. */
static int storage_mark(char phase, int mode){
    char tmp[] = SD_EXPORT_MARKER ".XXXXXX";
    int fd = mkstemp(tmp); if(fd < 0) return 0;
    char b[8]; int n = snprintf(b, sizeof b, "%c %d\n", phase, mode);
    int ok = write(fd, b, (size_t)n) == n && fsync(fd) == 0;
    if(close(fd) != 0) ok = 0;
    if(!ok || rename(tmp, SD_EXPORT_MARKER) != 0){ unlink(tmp); return 0; }
    return 1;
}
static int source_send(int mode){
    outseq_rc rc = outseq_source((outseq_src_t)mode);
    if(rc != OUTSEQ_REFUSED) modes_output_reset();       /* every source change starts from the internal DAC route */
    return rc < 0 ? -1 : 0;
}
static void storage_unknown(const char *why){
    sd_io_hold(); atomic_store(&g_sd_writable, 0); g_sd_phase = SD_UNKNOWN;
    fprintf(stderr, "storage: %s; SD access remains held\n", why); fflush(stderr);
    E->toast("Storage state uncertain - reboot device");
}

/* ================= Source mode ================= */
/* Working Mode transitions (rows: from, cols: to; U = Local with the USB Audio output route, 0642 5 = port HOST +
 * OTG 5V). Player 0642 N (FUN_004e5af4): 0 closes UAC/storage, remounts the card, role device, OTG off; 2 closes
 * storage, role device, opens UAC; 5 closes UAC/storage, role host, OTG on; 1 (storage) is not fully decompiled:
 * it is not known to restore device role from host or to close the UAC gadget.
 *   0 -> 1/2/3, U : direct (port already device; 3 via the SD drain; U via modes_output_mode_switch)
 *   1 -> 0/2     : direct (0642 0/0818 path); refused while a computer has the gadget configured (capture deadlock)
 *   1 -> 3       : via Local (0642 0 closes UAC first), same refusal with a computer attached
 *   2 -> 0/1/3   : direct (port already device after 0642 0; 0657 leaves BT sink; BT radio as the player leaves it)
 *   3 -> 0/1/2   : storage return path (source_send(mode), local mount, then a device restart)
 *   1/2/3 -> U   : refused by admit_check(ADMIT_OUTPUT_SWITCH) ("Return to local playback first"); 3 -> Local restarts the device,
 *                  so no continuation could survive it, and 1 -> Local needs the cable unplugged first anyway
 *   U -> 0       : direct (0666 6, 0642 0, 0657 8 restore device role; usb_audio_on cleared below)
 *   U -> 1/2/3   : via Local (out_switch to the internal DAC, then source_switch again from leave_tick): a direct 0642 1
 *                  from host role never reached the computer and held SD access until a restart */
/* The player keeps a separate saved volume per work mode (sysconfig UAC_VOLUME = 27 of 120 for USB DAC, against ~53-60 for
 * Local/Bluetooth), so every switch into USB DAC started far quieter than the user listens, and the phone cannot help (the
 * gadget has no hardware volume control, Android just attenuates). Carry the Local volume over once the switch has landed:
 * only ever UP to it, never past the Max Volume setting, and never if cfg "dac_match_volume" is 0. */
/* Leaving USB DAC with a host attached used to be refused ("Unplug the USB cable first"): firmware analysis shows mq_player's
 * close_pcm_capture_thread sets the capture state to "stop" and pthread_joins with no timeout, while the USB uevents
 * (UAC_STATE=STOP, UAC_SRATE=0 -> reopen_capture_dev) overwrite that same state from the listener thread, so the stop is lost and
 * the join never returns - on the player's only command thread. The 0642 0 path tears the gadget down on the mode thread
 * and the same events settle the capture thread in its idle state (5), after which a close works. So: send 0642 0 FIRST, wait for the
 * gadget to be gone (+1.5 s margin), and only then do the normal switch (0666 / 0657). cfg dac_safe_exit=0 restores the refusal. */
static struct { int active; int mode; uint32_t t0, gone_at; } g_dacx;
static void leave_end(const char *why){
    tm_stop(&g_leave.tm);
    if(why) E->toast(why);
    notify(SRCSWITCH_EV_ABANDONED);   /* nothing is switching any more: the picker shows the real selection now */
}
static void dac_exit_tick(void){
    if(!g_dacx.active) return;
    if(elapsed(g_dacx.t0) > 30000){   /* measured on a Disc with a phone attached: the host flaps configured/suspended for ~17 s before the gadget is gone */
        g_dacx.active = 0;
        fprintf(stderr, "usb dac: exit timed out waiting for the gadget to go away\n"); fflush(stderr);
        E->toast("Couldn't leave USB DAC - unplug the cable first");
        leave_end(NULL);   /* an open Working Mode picker stops showing "switching" */
        return;
    }
    if(E->dac_gadget_present()){ g_dacx.gone_at = 0; return; }   /* gadget still there */
    if(!g_dacx.gone_at){ g_dacx.gone_at = now_ms() ? now_ms() : 1; return; }
    if(elapsed(g_dacx.gone_at) < 1500) return;                                               /* the mode thread sleeps ~1 s after teardown */
    int m = g_dacx.mode;
    g_dacx.active = 0;
    fprintf(stderr, "usb dac: gadget gone, continuing the switch to mode %d\n", m); fflush(stderr);
    /* the exit is done: the switch carries on without asking for it again (also through a pass via Local it may need) */
    if(source_switch(m, 1) == 0) notify(SRCSWITCH_EV_SETTLE_RESTART);   /* the real switch began: its settle window starts here */
    else leave_end(NULL);                                                /* it gave up or was refused (it toasted why) */
}
static int dac_exit_begin(int mode){
    if(g_dacx.active){ E->toast("Switching..."); return -1; }
    if(outseq_gadget_none() != OUTSEQ_OK){ E->toast("Player is busy - try again"); return -1; }
    g_dacx.mode = mode; g_dacx.t0 = now_ms(); g_dacx.gone_at = 0;
    g_dacx.active = 1;
    fprintf(stderr, "usb dac: leaving with a host attached: 0642 0 first, then mode %d\n", mode); fflush(stderr);
    E->toast("Leaving USB DAC... (up to 20 s)");
    return 0;
}
static int g_dac_carry_vol = -1;
static uint32_t g_dac_enter_t;   /* when the switch into USB DAC was sent (volume frames before it settles are not the user's) */
static uint32_t g_dac_vol_due;   /* when the carried volume is set (0 = nothing pending); one pending set, newest switch wins */
static void dac_volume_cb(void){
    if(session_source_mode() != 1 || cfg_get_int("dac_match_volume", 1) == 0) return;
    /* The player sends no a714 volume frame when the mode (and with it the active volume slot) changes, so the volume we
     * hold is still Local's: never compare against it - set the level outright. The level is what the user last chose
     * while in USB DAC (cfg dac_volume), else the Local volume carried over. 0715 is not clamped by the player's MAX_VOL. */
    int want = cfg_get_int("dac_volume", 0); if(want <= 0) want = g_dac_carry_vol;
    int mv = cfg_get_int("max_vol", -1);
    if(want <= 0) return;
    if(mv >= 10 && want > mv) want = mv;
    fprintf(stderr, "usb dac: volume -> %d (%s)\n", want, cfg_get_int("dac_volume", 0) > 0 ? "your last USB DAC level" : "carried over from Local"); fflush(stderr);
    E->set_volume(want);
}

/* honest wording: the frames are queued; the async switch completes a moment later. */
const char *srcswitch_switch_msg(int mode){
    static const char *const MSG[4] = {
        "Switching to local playback", "Switching to USB DAC",
        "Switching to Bluetooth receiving", "Switching to USB storage",
    };
    return mode >= 0 && mode < 4 ? MSG[mode] : "";
}
/* Via Local (see the transition table): USB Audio -> USB DAC / BT / Storage and USB DAC -> Storage first go to plain
 * Local - USB Audio through the internal-DAC output switch (pause -> quiet -> 0666 6, 0642 0, 0657 8), USB DAC through
 * source_switch(0) - and only once that landed and the player had the picker's 3 s to apply it, the picked mode is
 * switched to with all its guards (and the SD drain). Playback stays paused like the unplug fallback: never resume into
 * the jack on the way through. exit_done: the ordered USB DAC exit already ran for this switch, so the pass through
 * Local (source_switch(0)) must not start it again. */
static void leave_tick(void){
    if(elapsed(g_leave.t0) >= 10000){ leave_end("Couldn't return to Local - mode not changed"); return; }
    if(modes_output_busy() || elapsed(g_leave.t0) < 3000) return;   /* still pausing / routing / recovering, or settling */
    if(modes_output_route() != OUT_INTERNAL || session_source_mode() != 0){ leave_end("Still not on Local - mode not changed"); return; }   /* refused or stopped */
    int m = g_leave.mode;
    tm_stop(&g_leave.tm);
    session_save_usb_audio(0);   /* a deliberate leave, also when out_run only got here through its fallback */
    notify(SRCSWITCH_EV_SETTLE_RESTART);   /* the settle window runs from the real source switch */
    if(source_switch(m, 0) != 0){ leave_end(NULL); return; }   /* it toasted why; the picker shows Local */
    E->toast(srcswitch_switch_msg(m));
}
static int via_local(int mode, int exit_done){
    if(g_leave.tm.on){ E->toast("Switching..."); return -1; }
    const char *msg = "Leaving USB DAC...";
    if(modes_output_route() == OUT_USB){
        if(out_switch(OUT_INTERNAL, 1) != 0) return -1;   /* out_switch toasted why */
        g_osw.was_playing = 0;   /* out_switch already paused it; never resume into the jack */
        msg = "Leaving USB Audio...";
    } else if(source_switch(0, exit_done) != 0) return -1;     /* it toasted why (a connected computer, SD state) */
    g_leave.mode = mode; g_leave.t0 = now_ms();
    tm_start(&g_leave.tm, 100);
    E->toast(msg);
    return 0;
}
int srcswitch_via_local_pending(void){ return g_leave.tm.on; }
/* A source switch still has work left in THIS process: an output switch waiting for the pause, the pass through Local,
 * the ordered USB DAC exit (up to ~30 s) or the SD hand-over. The picker's settle waits on it, and so does a theme
 * re-launch, which would exec the continuation away. Not modes_output_busy(): that one is also the guard these
 * continuations themselves pass through. */
int modes_source_switch_busy(void){
    return g_osw.tm.on || g_leave.tm.on || g_dacx.active || ui_source_switch_pending();
}
/* exit_done: set only while the ordered USB DAC exit's continuation (dac_exit_tick) carries the switch on, so it is not
 * started a second time. It is a parameter, not a flag, so nothing outside this call chain can see it. */
static int source_switch(int mode, int exit_done){
    { const char *why = admit_check(ADMIT_OUTPUT_IDLE); if(why){ E->toast(why); return -1; } }
    if(mode < 0 || mode > 3) return -1;
    /* Leaving USB DAC while a computer is attached can wedge mq_player V2.57 for good: close_pcm_capture_thread
     * (0x4614bc) sets the capture state to 2 and pthread_joins, but the capture thread is blocked reading from an
     * idle host; an unplug then fires reopen_capture_dev (0x461644), which overwrites 2 with 3, the reopen at rate 0
     * fails into state 5 and the join never returns - on the player's only command thread. Unplugged first, the
     * thread already sits in state 5 polling every 100 ms, so the close completes. */
    if(session_source_mode() == 1 && mode != 1 && !exit_done && E->host_attached()){
        if(cfg_get_int("dac_safe_exit", 1) == 0){ E->toast("Unplug the USB cable first"); return -1; }
        return dac_exit_begin(mode);                      /* the ordered exit (see above); returns 0 = started */
    }
    if(ui_source_switch_pending()){ E->toast("Storage is switching"); return -1; }
    if(g_sd_phase == SD_UNKNOWN || g_sd_hold || !sd_io_healthy()){ E->toast("SD access is held this boot"); return -1; }
    /* Every source change drives the player (and USB/card ownership with it), so none may happen before this
     * boot's launch verdict has qualified the player - a PENDING or negative verdict refuses it too. */
    if(atomic_load(&g_launch_verdict) != 1){ E->toast("SD access is not ready yet"); return -1; }
    /* USB Audio holds the port in host role (0642 5). The player flips it back to device only on its 0642 0 path
     * (Local), so going straight to USB DAC / BT Receiving / USB Storage left the port in host: a USB Storage export
     * was never seen by the computer and SD access stayed held until a restart. Leave it through the internal-DAC
     * switch first; leave_tick switches to the same mode once that landed (0 = the leave started). USB DAC ->
     * Storage likewise passes through Local so 0642 0 closes the UAC gadget before 0642 1 opens storage. */
    if(mode != 0 && (modes_output_route() == OUT_USB || (session_source_mode() == 1 && mode == 3))) return via_local(mode, exit_done);
    if(mode == 3){
        if(g_sd_phase == SD_HOST) return 0;
        if(E->scan_active()){ E->toast("Let the library scan finish first"); return -1; }
        if(!E->player_guarded()){ E->toast("Storage protection is unavailable"); return -1; }
        if(!storage_media_local()){ E->toast("SD card is not ready"); return -1; }
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 200000000L; if(ts.tv_nsec >= 1000000000L){ ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        if(pthread_mutex_timedlock(&g_sd_mode_mu, &ts) != 0){ E->toast("SD card is busy - try again"); return -1; }
        if(!storage_mark('D', 3)){ pthread_mutex_unlock(&g_sd_mode_mu); E->toast("Couldn't prepare USB storage"); return -1; }
        sd_io_hold(); atomic_store(&g_sd_writable, 0); session_set_source_mode(3);
        g_sd_phase = SD_DRAIN; g_sd_deadline = now_ms() + 20000;
        E->art_cancel();
        pthread_mutex_unlock(&g_sd_mode_mu);
        return 0;
    }
    if(g_sd_phase == SD_HOST){
        if(!storage_mark('R', mode)){ E->toast("Couldn't leave USB storage"); return -1; }
        g_sd_return_mode = mode; g_sd_phase = SD_WAIT_LOCAL;
        g_sd_deadline = now_ms() + 20000;
        if(source_send(mode) < 0){ storage_unknown("return request failed"); return -1; }
        return 0;
    }
    int was_playing = E->playing();
    if(was_playing && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0){ E->toast("Player is busy - try again"); return -1; }
    if(session_source_mode() == 0 && mode != 0){ track_state_t vs; ipc_get_state(&vs); if(vs.volume > 0) g_dac_carry_vol = vs.volume; }   /* the Local volume, before the player swaps volumes */
    if(source_send(mode) < 0){ E->toast("Couldn't switch mode"); return -1; }
    session_set_source_mode(mode);
    if(mode == 1){ g_dac_enter_t = now_ms(); g_dac_vol_due = g_dac_enter_t + 4500; if(!g_dac_vol_due) g_dac_vol_due = 1; }   /* after the gadget and the player's own per-mode volume have settled */
    if(mode == 0 && was_playing) ipc_send_cmd(PLAYER_CMD_PLAYPAUSE);
    /* Remember the picked Working Mode for the next boot (the boot restore above). Only the picker calls this,
     * so it records user choices; picking any source mode also ends a remembered USB Audio output. USB Storage
     * (mode 3) returns above and is never remembered. */
    session_save_source(mode);
    return 0;
}
int ui_set_source_mode(int mode){ return source_switch(mode, 0); }

/* ================= SD handoff: the phase machine ================= */
static void storage_resume_local(void){
    if(unlink(SD_EXPORT_MARKER) != 0 && errno != ENOENT){ storage_unknown("cannot clear handoff state"); return; }
    if(!sd_io_resume()){ storage_unknown("local mount changed during return"); return; }
    atomic_store(&g_sd_writable, 1); g_sd_phase = SD_LOCAL; session_set_source_mode(g_sd_return_mode);
    session_save_source(g_sd_return_mode);   /* the pick that left Storage: the restart below would otherwise replay the stale pick from before it */
    g_sd_reissue = 0;
    E->card_returned();
    fprintf(stderr, "storage: local mount confirmed; SD readers and artwork cache resumed\n"); fflush(stderr);
    /* mq_player V2.57 keeps its music thread alive (only paused) across the export, holding a file on the card
     * that was unmounted under it; the next play's stop_music_theard (0x465230) then pthread_joins it forever on
     * the main thread and the /player queue fills ("Player didn't respond"). A fresh player is the reliable cure. */
    E->toast("Restarting to reload the card...");
    E->restart();
}
static void storage_step(void){
    if(g_sd_hold || atomic_load(&g_sd_quiesce)) return;   /* a Restart is draining the card: never reopen it here */
    if(!sd_io_healthy()){
        if(g_sd_phase != SD_UNKNOWN) storage_unknown("artwork decoder did not exit");
        return;
    }
    if(g_sd_phase == SD_LOCAL){
        if(!sd_io_allowed() && storage_media_local()){
            if(sd_io_resume()) E->scan_if_library_empty();
        }
        return;
    }
    if(g_sd_phase == SD_DRAIN){
        if(sd_io_active() == 0 && atomic_load(&g_sd_writers) == 0){
            if(!storage_mark('E', 3)){ storage_unknown("cannot record export request"); return; }
            g_sd_phase = SD_WAIT_HOST; g_sd_deadline = now_ms() + 20000;
            if(E->playing() && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0){ storage_unknown("pause request failed"); return; }
            if(source_send(3) < 0) storage_unknown("export request failed");
            return;
        }
        if((int32_t)(now_ms() - g_sd_deadline) >= 0){
            /* No export command was sent. Existing readers must finish before the
             * gate can reopen; never claim a transfer occurred. */
            storage_unknown("SD readers did not finish");
        }
    } else if(g_sd_phase == SD_WAIT_HOST){
        if(E->host_confirmed() && !E->media_mounted()){
            if(!storage_mark('H', 3)){ storage_unknown("cannot record completed export"); return; }
            g_sd_phase = SD_HOST;
            g_sd_reissue = 0;
            fprintf(stderr, "storage: export confirmed with no local mount\n"); fflush(stderr);
        } else if((int32_t)(now_ms() - g_sd_deadline) >= 0) storage_unknown("export not confirmed");
        /* A recovered E/R marker must not drive the player before this launch is qualified: the reissue
         * is a storage TRANSITION, so it belongs behind the same gate as every other card access. */
        else if(g_sd_reissue && atomic_load(&g_launch_verdict) == 1 && source_send(3) == 0) g_sd_reissue = 0;
    } else if(g_sd_phase == SD_WAIT_LOCAL){
        if(storage_media_local()){ storage_resume_local(); return; }
        if((int32_t)(now_ms() - g_sd_deadline) >= 0) storage_unknown("return mount not confirmed");
        /* same gate as the recovered export above: a recovered R marker must not drive the player before
         * this launch is qualified */
        else if(g_sd_reissue && atomic_load(&g_launch_verdict) == 1 && source_send(g_sd_return_mode) == 0)
            g_sd_reissue = 0;
    }
}
void srcswitch_init(void){
    sd_io_init(storage_media_local);
    FILE *f = fopen(SD_EXPORT_MARKER, "r");
    if(f){
        char phase = 0; int mode = 0;
        int n = fscanf(f, "%c %d", &phase, &mode); fclose(f);
        atomic_store(&g_sd_writable, 0);
        if(n == 2 && (phase == 'H' || phase == 'E') && E->host_confirmed() && !E->media_mounted()){
            g_sd_phase = storage_mark('H', 3) ? SD_HOST : SD_UNKNOWN; session_set_source_mode(3);
        } else if(n == 2 && phase == 'E' && mode == 3){
            g_sd_phase = SD_WAIT_HOST; session_set_source_mode(3); g_sd_reissue = 1;
            g_sd_deadline = now_ms() + 20000;
        } else if(n == 2 && phase == 'R' && mode >= 0 && mode < 3){
            g_sd_phase = SD_WAIT_LOCAL; g_sd_return_mode = mode; session_set_source_mode(3); g_sd_reissue = 1;
            g_sd_deadline = now_ms() + 20000;
        } else if(n == 2 && phase == 'D' && !E->exported_to_host()){
            /* D is always replaced by E before any export command is sent. */
            if(unlink(SD_EXPORT_MARKER) == 0){ g_sd_phase = SD_LOCAL; atomic_store(&g_sd_writable, 1); }
            else g_sd_phase = SD_UNKNOWN;
        } else g_sd_phase = SD_UNKNOWN;
    } else if(errno != ENOENT || E->exported_to_host()){
        g_sd_phase = SD_UNKNOWN; atomic_store(&g_sd_writable, 0);
    }
    if(g_sd_hold){ g_sd_phase = SD_UNKNOWN; atomic_store(&g_sd_writable, 0); }
    if(g_sd_phase == SD_LOCAL && !g_sd_hold) sd_io_resume();
}

/* ---- driven by main.c ---- */
/* One 100 ms tick for everything that advances over time: the Output-route switch and its USB waits and watcher, the pass
 * via Local, the boot restore (each at the period its LVGL timer had), the ordered DAC exit's poll, the one-shot volume
 * carry and the SD handoff. */
void srcswitch_tick(void){
    if(tm_due(&g_osw.tm)) osw_tick();
    if(tm_due(&g_leave.tm)) leave_tick();
    if(tm_due(&g_rec_tm)) out_recover_tick();
    if(tm_due(&g_usbw.tm)) usbw_tick();
    if(tm_due(&g_usbgone)) usbgone_tick();
    if(tm_due(&g_bootusb)) bootusb_tick();
    dac_exit_tick();
    if(g_dac_vol_due && (int32_t)(now_ms() - g_dac_vol_due) >= 0){ g_dac_vol_due = 0; dac_volume_cb(); }
    storage_step();
}
/* The USB DAC level the user picked is only trustworthy once the gadget and the Player's own per-mode volume have settled. */
int srcswitch_dac_level_settled(void){ return session_source_mode() == 1 && elapsed(g_dac_enter_t) > 8000; }

/* The launch verdict (see g_launch_verdict) and the boot-long SD hold. */
void srcswitch_set_launch_verdict(int v){ atomic_store(&g_launch_verdict, v); }
void srcswitch_hold(void){ g_sd_hold = 1; }
int  srcswitch_held(void){ return g_sd_hold; }
/* One-way revocation after a negative or lost verdict: admission closes and the phase reads UNKNOWN. The caller then stops the
 * work already in flight (scanner_abort, art_kill_all). */
void srcswitch_revoke(void){
    atomic_store(&g_launch_verdict, 0);
    g_sd_hold = 1;
    atomic_store(&g_sd_writable, 0);
    sd_io_hold();                                    /* no new leases */
    g_sd_phase = SD_UNKNOWN;
}
/* A UI restarted mid-switch gets a fresh window to see the switch through. */
void srcswitch_restart_deadline(void){ if(ui_source_switch_pending()) g_sd_deadline = now_ms() + 20000; }
/* The coldplug worker's view: 0 while a Storage export is intended or pending. */
int  srcswitch_sd_writable(void){ return atomic_load(&g_sd_writable); }
/* Serialises a direct card mount against a switch to USB Storage. */
pthread_mutex_t *srcswitch_mode_mutex(void){ return &g_sd_mode_mu; }
