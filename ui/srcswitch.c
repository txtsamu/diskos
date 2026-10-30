/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
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

typedef struct { int on; uint32_t period, last; } tm_t;
static void tm_start(tm_t *t, uint32_t period){ t->on = 1; t->period = period; t->last = now_ms(); }
static void tm_stop(tm_t *t){ t->on = 0; }
static int  tm_due(tm_t *t){
    if(!t->on || elapsed(t->last) < t->period) return 0;
    t->last = now_ms(); return 1;
}

#ifndef OUT_USB_CARD
#define OUT_USB_CARD "/proc/asound/card1"
#endif
#ifndef OUT_USB_ANY_CARD
#define OUT_USB_ANY_CARD "/proc/asound/card*/usbid"
#endif
#define OUT_QUIET_MS 500
#define OUT_WAIT_MS  3000
#define OUT_USB_WAIT_MS   20000
#define OUT_USB_SETTLE_MS 1000
_Static_assert((int)OUTSEQ_INTERNAL == OUT_INTERNAL && (int)OUTSEQ_SPDIF == OUT_SPDIF && (int)OUTSEQ_USB == OUT_USB, "outseq route numbers differ from modes.h");
static struct { int target, was_playing, full; unsigned gen; char path[256]; long pos; uint32_t t0, quiet0; tm_t tm; } g_osw;
static int g_rec = 0; static unsigned g_rec_gen = 0; static tm_t g_rec_tm;
static struct { tm_t tm; uint32_t t0, seen; } g_usbw;
static tm_t g_usbgone; static int g_usbgone_miss, g_usbgone_tries;
static tm_t g_bootusb;
static struct { tm_t tm; int mode; uint32_t t0; } g_leave;

static int source_switch(int mode, int exit_done);
static int out_switch(int target, int via_mode);

int modes_output_route(void){ return session_route(); }
void modes_output_reset(void){ session_set_route(OUT_INTERNAL); }
int modes_output_busy(void){ return g_osw.tm.on || g_rec || g_usbw.tm.on; }
void modes_output_adopt(void){
    notify(SRCSWITCH_EV_CHANGED);
    if(session_route() == OUT_USB){ g_usbgone_miss = 0; g_usbgone_tries = 0; if(!g_usbgone.on) tm_start(&g_usbgone, 1000); }
}
static int out_usb_card_ok(void){
    char p[96]; snprintf(p, sizeof p, "%s/usbid", OUT_USB_CARD);
    return access(p, F_OK) == 0;
}
static int out_usb_any_card(void){
    glob_t g; int n = 0;
    if(glob(OUT_USB_ANY_CARD, 0, NULL, &g) == 0){ n = (int)g.gl_pathc; globfree(&g); }
    return n > 0;
}
static void out_rec_clear(void){ tm_stop(&g_rec_tm); g_rec = 0; }
static void out_recover_tick(void){
    if(ipc_generation() != g_rec_gen){ out_rec_clear(); session_set_route(OUT_INTERNAL); notify(SRCSWITCH_EV_CHANGED); return; }
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
static void out_resume(int moved){
    if(!g_osw.was_playing) return;
    track_state_t st; ipc_get_state(&st);
    if(moved && (ipc_generation() != g_osw.gen || !st.have_track || strcmp(st.path, g_osw.path) != 0)){ E->toast("Output switched - press play"); return; }
    if(moved && g_osw.pos > 0 && E->seek_to(g_osw.pos) < 0){ E->toast("Couldn't resume - press play"); return; }
    if(ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0) E->toast("Couldn't resume - press play");
}
#define USBGONE_TRIES 3
static void usbgone_tick(void){
    if(modes_output_route() != OUT_USB){ tm_stop(&g_usbgone); return; }
    if(modes_output_busy()) return;
    if(out_usb_card_ok()){ g_usbgone_miss = 0; g_usbgone_tries = 0; return; }
    if(++g_usbgone_miss < 2) return;
    g_usbgone_miss = 0;
    if(g_usbgone_tries >= USBGONE_TRIES){
        tm_stop(&g_usbgone);
        E->toast("Couldn't leave the USB DAC - pick the internal DAC in Settings");
        return;
    }
    fprintf(stderr, "usb audio: DAC gone - back to the internal DAC (paused)\n"); fflush(stderr);
    if(out_switch(OUT_INTERNAL, 1) == 0){
        g_osw.was_playing = 0;
        if(!g_usbgone_tries++) E->toast("USB DAC disconnected - internal DAC");
    }
}
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
    tm_stop(&g_usbw.tm);
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
    if(rc == OUTSEQ_OK && target == OUT_USB){ session_set_route(target); usbw_start(); notify(SRCSWITCH_EV_CHANGED); return 0; }
    if(rc == OUTSEQ_OK){ session_set_route(target); out_resume(1); session_save_usb_audio(0); notify(SRCSWITCH_EV_CHANGED); return 0; }
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
    if(elapsed(g_osw.t0) >= OUT_WAIT_MS){ osw_stop("Player is busy - try again", 0); return; }
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
    g_osw.full = via_mode || target == OUT_USB || modes_output_route() == OUT_USB;
    g_osw.gen = ipc_generation(); g_osw.pos = st.have_track ? st.position_ms : 0;
    snprintf(g_osw.path, sizeof g_osw.path, "%s", st.have_track ? st.path : "");
    if(was_playing && ipc_send_cmd(PLAYER_CMD_PLAYPAUSE) < 0){ session_set_route(modes_output_route()); E->toast("Player is busy - try again"); return -1; }
    g_osw.t0 = now_ms();
    tm_start(&g_osw.tm, 100);
    return 0;
}
int modes_output_switch(int target){ return out_switch(target, 0); }
int modes_output_mode_switch(int target){ return out_switch(target, 1); }

static _Atomic int g_sd_writable = 1;
static _Atomic int g_sd_writers = 0;
static _Atomic int g_sd_hold = 0;
static _Atomic int g_launch_verdict = -1;
static pthread_mutex_t g_sd_mode_mu = PTHREAD_MUTEX_INITIALIZER;
#ifndef SD_EXPORT_MARKER
#define SD_EXPORT_MARKER "/tmp/.diskos_sd_export_intent"
#endif
typedef enum { SD_LOCAL, SD_DRAIN, SD_WAIT_HOST, SD_HOST, SD_WAIT_LOCAL, SD_UNKNOWN } sd_phase_t;
static sd_phase_t g_sd_phase = SD_LOCAL;
static uint32_t g_sd_deadline;
static int g_sd_return_mode;
static int g_sd_reissue;
static int storage_media_local(void){
    if(atomic_load(&g_launch_verdict) != 1) return 0;
    return !atomic_load(&g_sd_hold) && !E->exported_to_host() && E->media_mounted();
}
int ui_local_playback_allowed(void){
    return atomic_load(&g_launch_verdict) == 1 && g_sd_phase == SD_LOCAL && !g_sd_hold && sd_io_healthy();
}

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
    if(rc != OUTSEQ_REFUSED) modes_output_reset();
    return rc < 0 ? -1 : 0;
}
static void storage_unknown(const char *why){
    sd_io_hold(); atomic_store(&g_sd_writable, 0); g_sd_phase = SD_UNKNOWN;
    fprintf(stderr, "storage: %s; SD access remains held\n", why); fflush(stderr);
    E->toast("Storage state uncertain - reboot device");
}

static struct { int active; int mode; uint32_t t0, gone_at; } g_dacx;
static void leave_end(const char *why){
    tm_stop(&g_leave.tm);
    if(why) E->toast(why);
    notify(SRCSWITCH_EV_ABANDONED);
}
static void dac_exit_tick(void){
    if(!g_dacx.active) return;
    if(elapsed(g_dacx.t0) > 30000){
        g_dacx.active = 0;
        fprintf(stderr, "usb dac: exit timed out waiting for the gadget to go away\n"); fflush(stderr);
        E->toast("Couldn't leave USB DAC - unplug the cable first");
        leave_end(NULL);
        return;
    }
    if(E->dac_gadget_present()){ g_dacx.gone_at = 0; return; }
    if(!g_dacx.gone_at){ g_dacx.gone_at = now_ms() ? now_ms() : 1; return; }
    if(elapsed(g_dacx.gone_at) < 1500) return;
    int m = g_dacx.mode;
    g_dacx.active = 0;
    fprintf(stderr, "usb dac: gadget gone, continuing the switch to mode %d\n", m); fflush(stderr);
    if(source_switch(m, 1) == 0) notify(SRCSWITCH_EV_SETTLE_RESTART);
    else leave_end(NULL);
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
static uint32_t g_dac_enter_t;
static uint32_t g_dac_vol_due;
static void dac_volume_cb(void){
    if(session_source_mode() != 1 || cfg_get_int("dac_match_volume", 1) == 0) return;
    int want = cfg_get_int("dac_volume", 0); if(want <= 0) want = g_dac_carry_vol;
    int mv = cfg_get_int("max_vol", -1);
    if(want <= 0) return;
    if(mv >= 10 && want > mv) want = mv;
    fprintf(stderr, "usb dac: volume -> %d (%s)\n", want, cfg_get_int("dac_volume", 0) > 0 ? "your last USB DAC level" : "carried over from Local"); fflush(stderr);
    E->set_volume(want);
}

const char *srcswitch_switch_msg(int mode){
    static const char *const MSG[4] = {
        "Switching to local playback", "Switching to USB DAC",
        "Switching to Bluetooth receiving", "Switching to USB storage",
    };
    return mode >= 0 && mode < 4 ? MSG[mode] : "";
}
static void leave_tick(void){
    if(elapsed(g_leave.t0) >= 10000){ leave_end("Couldn't return to Local - mode not changed"); return; }
    if(modes_output_busy() || elapsed(g_leave.t0) < 3000) return;
    if(modes_output_route() != OUT_INTERNAL || session_source_mode() != 0){ leave_end("Still not on Local - mode not changed"); return; }
    int m = g_leave.mode;
    tm_stop(&g_leave.tm);
    session_save_usb_audio(0);
    notify(SRCSWITCH_EV_SETTLE_RESTART);
    if(source_switch(m, 0) != 0){ leave_end(NULL); return; }
    E->toast(srcswitch_switch_msg(m));
}
static int via_local(int mode, int exit_done){
    if(g_leave.tm.on){ E->toast("Switching..."); return -1; }
    const char *msg = "Leaving USB DAC...";
    if(modes_output_route() == OUT_USB){
        if(out_switch(OUT_INTERNAL, 1) != 0) return -1;
        g_osw.was_playing = 0;
        msg = "Leaving USB Audio...";
    } else if(source_switch(0, exit_done) != 0) return -1;
    g_leave.mode = mode; g_leave.t0 = now_ms();
    tm_start(&g_leave.tm, 100);
    E->toast(msg);
    return 0;
}
int srcswitch_via_local_pending(void){ return g_leave.tm.on; }
int modes_source_switch_busy(void){
    return g_osw.tm.on || g_leave.tm.on || g_dacx.active || ui_source_switch_pending();
}
static int source_switch(int mode, int exit_done){
    { const char *why = admit_check(ADMIT_OUTPUT_IDLE); if(why){ E->toast(why); return -1; } }
    if(mode < 0 || mode > 3) return -1;
    if(session_source_mode() == 1 && mode != 1 && !exit_done && E->host_attached()){
        if(cfg_get_int("dac_safe_exit", 1) == 0){ E->toast("Unplug the USB cable first"); return -1; }
        return dac_exit_begin(mode);
    }
    if(ui_source_switch_pending()){ E->toast("Storage is switching"); return -1; }
    if(g_sd_phase == SD_UNKNOWN || g_sd_hold || !sd_io_healthy()){ E->toast("SD access is held this boot"); return -1; }
    if(atomic_load(&g_launch_verdict) != 1){ E->toast("SD access is not ready yet"); return -1; }
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
    if(session_source_mode() == 0 && mode != 0){ track_state_t vs; ipc_get_state(&vs); if(vs.volume > 0) g_dac_carry_vol = vs.volume; }
    if(source_send(mode) < 0){ E->toast("Couldn't switch mode"); return -1; }
    session_set_source_mode(mode);
    if(mode == 1){ g_dac_enter_t = now_ms(); g_dac_vol_due = g_dac_enter_t + 4500; if(!g_dac_vol_due) g_dac_vol_due = 1; }
    if(mode == 0 && was_playing) ipc_send_cmd(PLAYER_CMD_PLAYPAUSE);
    session_save_source(mode);
    return 0;
}
int ui_set_source_mode(int mode){ return source_switch(mode, 0); }

static void storage_resume_local(void){
    if(unlink(SD_EXPORT_MARKER) != 0 && errno != ENOENT){ storage_unknown("cannot clear handoff state"); return; }
    if(!sd_io_resume()){ storage_unknown("local mount changed during return"); return; }
    atomic_store(&g_sd_writable, 1); g_sd_phase = SD_LOCAL; session_set_source_mode(g_sd_return_mode);
    session_save_source(g_sd_return_mode);
    g_sd_reissue = 0;
    E->card_returned();
    fprintf(stderr, "storage: local mount confirmed; SD readers and artwork cache resumed\n"); fflush(stderr);
    E->toast("Restarting to reload the card...");
    E->restart();
}
static void storage_step(void){
    if(g_sd_hold || atomic_load(&g_sd_quiesce)) return;
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
            storage_unknown("SD readers did not finish");
        }
    } else if(g_sd_phase == SD_WAIT_HOST){
        if(E->host_confirmed() && !E->media_mounted()){
            if(!storage_mark('H', 3)){ storage_unknown("cannot record completed export"); return; }
            g_sd_phase = SD_HOST;
            g_sd_reissue = 0;
            fprintf(stderr, "storage: export confirmed with no local mount\n"); fflush(stderr);
        } else if((int32_t)(now_ms() - g_sd_deadline) >= 0) storage_unknown("export not confirmed");
        else if(g_sd_reissue && atomic_load(&g_launch_verdict) == 1 && source_send(3) == 0) g_sd_reissue = 0;
    } else if(g_sd_phase == SD_WAIT_LOCAL){
        if(storage_media_local()){ storage_resume_local(); return; }
        if((int32_t)(now_ms() - g_sd_deadline) >= 0) storage_unknown("return mount not confirmed");
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
            if(unlink(SD_EXPORT_MARKER) == 0){ g_sd_phase = SD_LOCAL; atomic_store(&g_sd_writable, 1); }
            else g_sd_phase = SD_UNKNOWN;
        } else g_sd_phase = SD_UNKNOWN;
    } else if(errno != ENOENT || E->exported_to_host()){
        g_sd_phase = SD_UNKNOWN; atomic_store(&g_sd_writable, 0);
    }
    if(g_sd_hold){ g_sd_phase = SD_UNKNOWN; atomic_store(&g_sd_writable, 0); }
    if(g_sd_phase == SD_LOCAL && !g_sd_hold) sd_io_resume();
}

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
int srcswitch_dac_level_settled(void){ return session_source_mode() == 1 && elapsed(g_dac_enter_t) > 8000; }

void srcswitch_set_launch_verdict(int v){ atomic_store(&g_launch_verdict, v); }
void srcswitch_hold(void){ g_sd_hold = 1; }
int  srcswitch_held(void){ return g_sd_hold; }
void srcswitch_revoke(void){
    atomic_store(&g_launch_verdict, 0);
    g_sd_hold = 1;
    atomic_store(&g_sd_writable, 0);
    sd_io_hold();
    g_sd_phase = SD_UNKNOWN;
}
void srcswitch_restart_deadline(void){ if(ui_source_switch_pending()) g_sd_deadline = now_ms() + 20000; }
int  srcswitch_sd_writable(void){ return atomic_load(&g_sd_writable); }
pthread_mutex_t *srcswitch_mode_mutex(void){ return &g_sd_mode_mu; }
