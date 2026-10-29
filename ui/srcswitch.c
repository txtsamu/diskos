/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Source-mode switch (GLOSSARY.md): the Working Mode change itself. Before this module it lived in main.c, so modes.c
 * and bt.c reached back into it and the host tests could only see it by slicing main.c as text. It owns
 *   - the SD handoff to a computer (USB Storage): the phase machine, its crash-safe marker, the admission gate that
 *     closes before an export is queued and reopens only once the card is back and mounted;
 *   - the ordered USB DAC exit (0642 0 first, wait for the gadget to go, then the normal switch);
 *   - the volume carry into USB DAC;
 *   - ui_set_source_mode(), which applies the transition table below.
 * The persisted Working Mode and the Source mode value itself stay in session.c. No LVGL: time and everything that
 * touches the system (gadget and mount state, the Player's pause state, the library scan, toasts, the restart) come
 * through struct srcswitch_env, so a host test drives it with fakes. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
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

static const struct srcswitch_env *E;
static uint32_t now_ms(void){ return E->now(); }
static uint32_t elapsed(uint32_t t0){ return now_ms() - t0; }
void srcswitch_set_env(const struct srcswitch_env *env){ E = env; }

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
/* Working Mode transitions (rows: from, cols: to; U = Local with the USB Audio output route, 0642 5 = port HOST +
 * OTG 5V). Player 0642 N (FUN_004e5af4): 0 closes UAC/storage, remounts the card, role device, OTG off; 2 closes
 * storage, role device, opens UAC; 5 closes UAC/storage, role host, OTG on; 1 (storage) is not fully decompiled:
 * it is not known to restore device role from host or to close the UAC gadget.
 *   0 -> 1/2/3, U : direct (port already device; 3 via the SD drain; U via modes_output_mode_switch)
 *   1 -> 0/2     : direct (0642 0/0818 path); refused while a computer has the gadget configured (capture deadlock)
 *   1 -> 3       : via Local (0642 0 closes UAC first), same refusal with a computer attached
 *   2 -> 0/1/3   : direct (port already device after 0642 0; 0657 leaves BT sink; BT radio as the player leaves it)
 *   3 -> 0/1/2   : storage return path (source_send(mode), local mount, then a device restart)
 *   1/2/3 -> U   : refused by ui_output_blocked ("Return to local playback first"); 3 -> Local restarts the device,
 *                  so no continuation could survive it, and 1 -> Local needs the cable unplugged first anyway
 *   U -> 0       : direct (0666 6, 0642 0, 0657 8 restore device role; usb_audio_on cleared below)
 *   U -> 1/2/3   : via Local (modes.c out_switch to the internal DAC, then this call again): a direct 0642 1 from
 *                  host role never reached the computer and held SD access until a restart */
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
static int g_dac_exit_ok;   /* set only while the continuation re-enters ui_set_source_mode */
static void dac_exit_tick(void){
    if(!g_dacx.active) return;
    if(elapsed(g_dacx.t0) > 30000){   /* measured on a Disc with a phone attached: the host flaps configured/suspended for ~17 s before the gadget is gone */
        g_dacx.active = 0;
        fprintf(stderr, "usb dac: exit timed out waiting for the gadget to go away\n"); fflush(stderr);
        E->toast("Couldn't leave USB DAC - unplug the cable first");
        modes_dac_exit_done(0);   /* an open Working Mode picker stops showing "switching" */
        return;
    }
    if(E->dac_gadget_present()){ g_dacx.gone_at = 0; return; }   /* gadget still there */
    if(!g_dacx.gone_at){ g_dacx.gone_at = now_ms() ? now_ms() : 1; return; }
    if(elapsed(g_dacx.gone_at) < 1500) return;                                               /* the mode thread sleeps ~1 s after teardown */
    int m = g_dacx.mode;
    g_dacx.active = 0;
    fprintf(stderr, "usb dac: gadget gone, continuing the switch to mode %d\n", m); fflush(stderr);
    g_dac_exit_ok = 1; int rc = ui_set_source_mode(m); g_dac_exit_ok = 0;
    modes_dac_exit_done(rc == 0);
}
int ui_dac_exit_pending(void){ return g_dacx.active; }   /* the ordered exit is waiting for the gadget (part of modes_source_switch_busy) */
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
int ui_set_source_mode(int mode){
    if(modes_output_busy()){ E->toast("Switching output - try again"); return -1; }
    if(mode < 0 || mode > 3) return -1;
    /* Leaving USB DAC while a computer is attached can wedge mq_player V2.57 for good: close_pcm_capture_thread
     * (0x4614bc) sets the capture state to 2 and pthread_joins, but the capture thread is blocked reading from an
     * idle host; an unplug then fires reopen_capture_dev (0x461644), which overwrites 2 with 3, the reopen at rate 0
     * fails into state 5 and the join never returns - on the player's only command thread. Unplugged first, the
     * thread already sits in state 5 polling every 100 ms, so the close completes. */
    if(session_source_mode() == 1 && mode != 1 && !g_dac_exit_ok && E->host_attached()){
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
     * switch first; modes.c re-enters here with the same mode once that landed (0 = the leave started). USB DAC ->
     * Storage likewise passes through Local so 0642 0 closes the UAC gadget before 0642 1 opens storage. */
    if(mode != 0 && (modes_output_route() == OUT_USB || (session_source_mode() == 1 && mode == 3))) return modes_via_local_then(mode);
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
    if(was_playing && ipc_send_cmd("0201000C0000") < 0){ E->toast("Player is busy - try again"); return -1; }
    if(session_source_mode() == 0 && mode != 0){ track_state_t vs; ipc_get_state(&vs); if(vs.volume > 0) g_dac_carry_vol = vs.volume; }   /* the Local volume, before the player swaps volumes */
    if(source_send(mode) < 0){ E->toast("Couldn't switch mode"); return -1; }
    session_set_source_mode(mode);
    if(mode == 1){ g_dac_enter_t = now_ms(); g_dac_vol_due = g_dac_enter_t + 4500; if(!g_dac_vol_due) g_dac_vol_due = 1; }   /* after the gadget and the player's own per-mode volume have settled */
    if(mode == 0 && was_playing) ipc_send_cmd("0201000C0000");
    /* Remember the picked Working Mode for the next boot (modes_output_boot_restore). Only the picker calls this,
     * so it records user choices; picking any source mode also ends a remembered USB Audio output. USB Storage
     * (mode 3) returns above and is never remembered. */
    session_save_source(mode);
    return 0;
}
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
            if(E->playing() && ipc_send_cmd("0201000C0000") < 0){ storage_unknown("pause request failed"); return; }
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
/* One 100 ms tick for what used to be three LVGL timers (storage_tick, the ordered DAC exit's poll, the one-shot volume carry). */
void srcswitch_tick(void){
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
