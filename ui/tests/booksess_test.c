/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "booksess.h"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)

#define B "/mnt/sd/books/a.m4b"
#define OTHER "/mnt/sd/music/x.flac"

static uint32_t now;
static track_state_t st;
static int playing;
static const char *admit_why;
static int seeks, seek_rc, seek_last = -1;
static int playpauses, playpause_rc;
static int is_book = 1;
static int progress_rc;
static long progress_pos;
static int saves, save_rc;
static long save_last = -1;
static char save_key[256];
static int adopted_n;
static int sleep_action;
static int ended_n, shutdown_n;

static uint32_t f_now(void){ return now; }
static const char *f_admit(void){ return admit_why; }
static void f_get_state(track_state_t *out){ *out = st; }
static int f_playing(void){ return playing; }
static int f_seek_to(long ms){ seeks++; seek_last = (int)ms; return seek_rc; }
static int f_playpause(void){ playpauses++; return playpause_rc; }
static int f_is_book_path(const char *p){ (void)p; return is_book; }
static int f_progress(const char *key, long *pos){ (void)key; *pos = progress_pos; return progress_rc; }
static int f_save(const char *key, long pos){ saves++; save_last = pos; snprintf(save_key, sizeof save_key, "%s", key); return save_rc; }
static void f_adopted(void){ adopted_n++; }
static int f_sleep_action(void){ return sleep_action; }
static void f_ended(void){ ended_n++; }
static void f_shutdown(void){ shutdown_n++; }

static const struct booksess_env env = {
    .now = f_now, .admit = f_admit, .get_state = f_get_state, .playing = f_playing, .seek_to = f_seek_to,
    .playpause = f_playpause, .is_book_path = f_is_book_path, .book_progress = f_progress, .book_save = f_save,
    .adopted = f_adopted, .sleep_action = f_sleep_action, .sleep_ended = f_ended, .shutdown = f_shutdown,
};

static void track(const char *path, long pos, long dur){
    memset(&st, 0, sizeof st);
    st.have_track = 1;
    snprintf(st.path, sizeof st.path, "%s", path);
    st.position_ms = pos;
    st.duration_ms = dur;
    st.path_seq = 10;
    st.pos_seq = 11;
}

static void clear_fakes(void){
    seeks = 0; seek_rc = 0; seek_last = -1;
    playpauses = 0; playpause_rc = 0;
    is_book = 1; progress_rc = 0; progress_pos = 0;
    saves = 0; save_rc = 1; save_last = -1; save_key[0] = 0;
    adopted_n = 0; ended_n = 0; shutdown_n = 0; sleep_action = 0;
    admit_why = NULL; playing = 1;
}

static void reset(void){
    clear_fakes();
    ui_cancel_book_resume();
    ui_disarm_book_eoc();
    ui_set_sleep_timer(0);
    now += 20000;
    clear_fakes();
    track(OTHER, 0, 0);
}

static void test_smart_rewind(void){
    CHECK(ui_smart_rewind_ms(0) == 0, "rewind: 0 s -> 0");
    CHECK(ui_smart_rewind_ms(29) == 0, "rewind: 29 s -> 0");
    CHECK(ui_smart_rewind_ms(30) == 8000, "rewind: 30 s -> 8000");
    CHECK(ui_smart_rewind_ms(1799) == 8000, "rewind: 1799 s -> 8000");
    CHECK(ui_smart_rewind_ms(1800) == 18000, "rewind: 1800 s -> 18000");
    CHECK(ui_smart_rewind_ms(28799) == 18000, "rewind: 28799 s -> 18000");
    CHECK(ui_smart_rewind_ms(28800) == 30000, "rewind: 28800 s -> 30000");
}

static void test_confirm_and_checkpoint(void){
    reset();
    booksess_begin(B, 0);
    CHECK(ui_book_active(), "begin: session active");
    track(OTHER, 0, 0);
    now += 14999; booksess_tick();
    CHECK(ui_book_active(), "waiting: still active 1 ms before the 15 s deadline");
    now += 1; booksess_tick();
    CHECK(!ui_book_active(), "waiting: unloaded book gives up at the 15 s deadline");
    CHECK(saves == 0, "waiting: nothing saved for a book that never loaded");

    reset();
    booksess_begin(B, 0);
    track(B, 5000, 100000);
    booksess_tick();
    CHECK(ui_book_active() && saves == 0 && seeks == 0, "confirm: first matching tick only confirms");
    now += 10000 - 1; booksess_tick();
    CHECK(saves == 0, "checkpoint: no save 1 ms short of 10 s");
    now += 1; booksess_tick();
    CHECK(saves == 1 && save_last == 5000, "checkpoint: save at 10 s");
    CHECK(strcmp(save_key, B) == 0, "checkpoint: saved under the session book");
    track(B, 9000, 100000);
    now += 5000; booksess_tick();
    CHECK(saves == 1, "checkpoint: throttled inside the 10 s window");
    now += 5000; booksess_tick();
    CHECK(saves == 2 && save_last == 9000, "checkpoint: second save 10 s later");
    playing = 0;
    now += 20000; booksess_tick();
    CHECK(saves == 2, "checkpoint: no save while paused");
    playing = 1;
    track(B, 0, 100000);
    now += 20000; booksess_tick();
    CHECK(saves == 2, "checkpoint: zero position is never saved");
    st.pos_seq = st.path_seq;
    st.position_ms = 70000;
    now += 20000; booksess_tick();
    CHECK(saves == 2, "checkpoint: stale position (pos_seq not after path_seq) is never saved");
    track(B, 70000, 100000);
    save_rc = 0;
    now += 20000; booksess_tick();
    CHECK(saves == 3, "checkpoint: failed save attempted");
    now += 400; booksess_tick();
    CHECK(saves == 4, "checkpoint: failed save retried on the next tick, throttle not advanced");
    save_rc = 1;
    now += 400; booksess_tick();
    CHECK(saves == 5, "checkpoint: success after retry");
    now += 400; booksess_tick();
    CHECK(saves == 5, "checkpoint: success advances the throttle");
}

static void test_track_change_ends_session(void){
    reset();
    booksess_begin(B, 0);
    track(B, 5000, 100000);
    booksess_tick();
    track(OTHER, 1000, 200000);
    booksess_tick();
    CHECK(!ui_book_active(), "track change: confirmed session ends when another track plays");
    CHECK(saves == 0, "track change: no flush for the other track");
}

static void test_resume_seek(void){
    reset();
    booksess_begin(B, 600000);
    track(B, 0, 0);
    booksess_tick();
    booksess_tick();
    CHECK(seeks == 0, "resume: no seek before duration is known");
    track(B, 0, 1000000);
    seek_rc = -1;
    booksess_tick();
    CHECK(seeks == 1 && seek_last == 600000, "resume: seeks to the bookmark once duration is known");
    booksess_tick();
    CHECK(seeks == 2, "resume: failed seek retried on the next tick");
    seek_rc = 0;
    booksess_tick();
    CHECK(seeks == 3 && seek_last == 600000, "resume: seek retried until it succeeds");
    booksess_tick();
    CHECK(seeks == 3, "resume: seek applied once");
    now += 10000;
    booksess_tick();
    CHECK(saves == 0, "resume: zero position not saved");
    track(B, 4000, 1000000);
    now += 10000; booksess_tick();
    CHECK(saves == 0, "resume: stale pre-seek position cannot overwrite the bookmark");
    track(B, 596999, 1000000);
    now += 10000; booksess_tick();
    CHECK(saves == 0, "resume: 3001 ms from the target does not confirm");
    track(B, 597000, 1000000);
    now += 10000; booksess_tick();
    CHECK(saves == 1 && save_last == 597000, "resume: position within 3 s of the target confirms and checkpoints");
    track(B, 603000, 1000000);
    now += 10000; booksess_tick();
    CHECK(saves == 2, "resume: confirmation latches");

    reset();
    booksess_begin(B, 600000);
    track(B, 0, 601000);
    booksess_tick(); booksess_tick();
    CHECK(seeks == 1 && seek_last == 599000, "resume: target clamped to duration - 2000");

    reset();
    booksess_begin(B, 600000);
    track(B, 0, 1500);
    booksess_tick(); booksess_tick();
    CHECK(seeks == 1 && seek_last == 0, "resume: clamp never goes negative");

    reset();
    booksess_begin(B, 600000);
    track(B, 0, 0);
    booksess_tick();
    now += 15000;
    booksess_tick();
    CHECK(seeks == 1 && seek_last == 600000, "resume: past the deadline seeks without waiting for metadata");

    reset();
    booksess_begin(B, 1000);
    track(B, 4000, 100000);
    booksess_tick(); booksess_tick();
    CHECK(seeks == 0, "resume: a bookmark of 1000 ms or less owes no seek");
    now += 10000; booksess_tick();
    CHECK(saves == 1 && save_last == 4000, "resume: no resume owed checkpoints immediately");

    reset();
    booksess_begin(B, 1001);
    track(B, 0, 100000);
    booksess_tick(); booksess_tick();
    CHECK(seeks == 1 && seek_last == 1001, "resume: a bookmark above 1000 ms is honoured");
}

static void test_smart_rewind_on_resume(void){
    reset();
    booksess_begin(B, 0);
    track(B, 50000, 100000);
    booksess_tick(); booksess_tick(); booksess_tick();
    playing = 0;
    booksess_tick();
    now += 29000;
    playing = 1;
    booksess_tick();
    CHECK(seeks == 0, "smart rewind: under 30 s idle does not rewind");

    playing = 0;
    now += 400; booksess_tick();
    now += 30000;
    playing = 1;
    booksess_tick();
    CHECK(seeks == 1 && seek_last == 42000, "smart rewind: 30 s idle backs up 8 s");

    playing = 0;
    now += 400; booksess_tick();
    now += 1800000;
    playing = 1;
    booksess_tick();
    CHECK(seeks == 2 && seek_last == 32000, "smart rewind: 30 min idle backs up 18 s");

    playing = 0;
    now += 400; booksess_tick();
    now += 28800000;
    playing = 1;
    booksess_tick();
    CHECK(seeks == 3 && seek_last == 20000, "smart rewind: 8 h idle backs up 30 s");

    now += 400; booksess_tick();
    CHECK(seeks == 3, "smart rewind: only once per pause");

    reset();
    booksess_begin(B, 0);
    track(B, 3000, 100000);
    booksess_tick(); booksess_tick(); booksess_tick();
    playing = 0;
    booksess_tick();
    now += 60000;
    playing = 1;
    booksess_tick();
    CHECK(seeks == 1 && seek_last == 0, "smart rewind: clamps at the start of the book");
}

static void test_backward_jump(void){
    reset();
    booksess_begin(B, 0);
    track(B, 200000, 1000000);
    booksess_tick();
    now += 10000; booksess_tick();
    CHECK(saves == 1 && save_last == 200000, "jump: baseline checkpoint");
    track(B, 165000, 1000000);
    now += 400; booksess_tick();
    CHECK(ui_book_active(), "jump: exactly 35 s back is not a restart");
    track(B, 130001, 1000000);
    now += 400; booksess_tick();
    CHECK(ui_book_active(), "jump: baseline follows the position so a slow drift never trips it");
    track(B, 94999, 1000000);
    now += 400; booksess_tick();
    CHECK(!ui_book_active(), "jump: more than 35 s back ends the session");
    CHECK(saves == 1, "jump: cancel on a backward jump does not flush the bad position");

    reset();
    booksess_begin(B, 0);
    track(B, 200000, 1000000);
    booksess_tick();
    now += 10000; booksess_tick();
    track(B, 164999, 1000000);
    now += 400; booksess_tick();
    CHECK(!ui_book_active(), "jump: 35001 ms back ends the session");

    reset();
    booksess_begin(B, 0);
    track(B, 200000, 1000000);
    booksess_tick();
    now += 10000; booksess_tick();
    ui_defer_sleep();
    track(B, 10000, 1000000);
    now += 400; booksess_tick();
    CHECK(ui_book_active(), "jump: a backward jump inside the 2.5 s seek guard is a user seek");
    now += 20000;
    track(B, 150000, 1000000);
    booksess_tick();
    track(B, 10000, 1000000);
    now += 400; booksess_tick();
    CHECK(!ui_book_active(), "jump: guard expires after 2.5 s");
}

static void test_cancel_flush(void){
    reset();
    booksess_begin(B, 0);
    track(B, 7000, 100000);
    booksess_tick();
    ui_cancel_book_resume();
    CHECK(!ui_book_active() && saves == 1 && save_last == 7000, "cancel: confirmed position flushed");

    reset();
    booksess_begin(B, 0);
    track(OTHER, 7000, 100000);
    ui_cancel_book_resume();
    CHECK(saves == 0, "cancel: another track's position is never saved under the book");

    reset();
    booksess_begin(B, 0);
    track(B, 7000, 100000);
    st.pos_seq = st.path_seq;
    ui_cancel_book_resume();
    CHECK(saves == 0, "cancel: stale position not flushed");

    reset();
    booksess_begin(B, 0);
    track(B, 0, 100000);
    ui_cancel_book_resume();
    CHECK(saves == 0, "cancel: zero position not flushed");

    reset();
    booksess_begin(B, 600000);
    track(B, 4000, 1000000);
    booksess_tick(); booksess_tick();
    ui_cancel_book_resume();
    CHECK(saves == 0, "cancel: unconfirmed resume seek never flushes over the bookmark");

    reset();
    ui_cancel_book_resume();
    CHECK(saves == 0 && !ui_book_active(), "cancel: no session is a no-op");
}

static void test_overflow(void){
    reset();
    booksess_begin(B, 0);
    track(B, 102000, 100000);
    booksess_tick();
    now += 10000; booksess_tick();
    CHECK(ui_book_active() && saves == 1, "overflow: duration + 2000 is still in range");
    track(B, 102001, 100000);
    now += 10000; booksess_tick();
    CHECK(!ui_book_active() && saves == 1, "overflow: beyond duration + 2000 ends the session without flushing");
}

static void test_user_seek(void){
    reset();
    booksess_begin(B, 0);
    track(B, 5000, 100000);
    ui_book_user_seeked(30000);
    CHECK(saves == 0, "seek: not attributed to an unconfirmed session");
    booksess_tick();
    ui_book_user_seeked(30000);
    CHECK(saves == 1 && save_last == 30000, "seek: confirmed session persists the target at once");
    track(B, 5000, 100000);
    now += 10000; booksess_tick();
    CHECK(saves == 1, "seek: stale pre-seek position cannot overwrite it");
    track(B, 30500, 100000);
    now += 10000; booksess_tick();
    CHECK(saves == 2 && save_last == 30500, "seek: checkpointing resumes once the position reaches the target");

    ui_book_user_seeked(-1);
    track(B, 80000, 100000);
    now += 10000; booksess_tick();
    CHECK(saves == 3, "seek: negative target allows checkpointing without a save");

    reset();
    booksess_begin(B, 600000);
    track(B, 0, 1000000);
    booksess_tick();
    ui_book_user_seeked(1000);
    booksess_tick();
    CHECK(seeks == 0, "seek: a user seek cancels the pending resume seek");

    reset();
    booksess_begin(B, 0);
    track(B, 90000, 1000000);
    booksess_tick();
    now += 10000; booksess_tick();
    ui_book_user_seeked(5000);
    track(B, 5100, 1000000);
    now += 400; booksess_tick();
    CHECK(ui_book_active(), "seek: baseline moves to the seek target so the jump is not a restart");
}

static void test_adopt(void){
    reset();
    track(B, 5000, 100000);
    booksess_hold_off_adopt();
    now += 3999; booksess_tick();
    CHECK(!ui_book_active(), "adopt: held off for 4 s after an explicit play");
    now += 1; booksess_tick();
    CHECK(ui_book_active() && adopted_n == 1, "adopt: adopts a playing book once the hold-off ends");
    CHECK(seeks == 0, "adopt: no seek without a deeper bookmark");
    now += 10000; booksess_tick();
    CHECK(saves == 1 && save_last == 5000, "adopt: checkpoints from the current position");

    reset();
    track(B, 5000, 100000);
    admit_why = "busy";
    booksess_tick();
    CHECK(!ui_book_active(), "adopt: not while local playback is not admitted");
    admit_why = NULL;
    playing = 0;
    booksess_tick();
    CHECK(!ui_book_active(), "adopt: not a paused book");
    playing = 1;
    is_book = 0;
    booksess_tick();
    CHECK(!ui_book_active(), "adopt: not a non-book track");
    is_book = 1;
    st.have_track = 0;
    booksess_tick();
    CHECK(!ui_book_active(), "adopt: not without a track");
    st.have_track = 1;
    progress_rc = -1;
    booksess_tick();
    CHECK(!ui_book_active() && adopted_n == 0, "adopt: bookmark read error retries instead of adopting");
    progress_rc = 0;
    booksess_tick();
    CHECK(ui_book_active(), "adopt: adopts after the read recovers");

    reset();
    track(B, 5000, 100000);
    progress_rc = 1; progress_pos = 40000;
    booksess_tick();
    ui_cancel_book_resume();
    reset();
    track(B, 5000, 100000);
    progress_rc = 1; progress_pos = 40000;
    booksess_tick();
    CHECK(ui_book_active() && seeks == 0, "adopt: bookmark exactly 35 s deeper is adopted normally");

    reset();
    track(B, 5000, 100000);
    progress_rc = 1; progress_pos = 40001;
    booksess_tick();
    CHECK(ui_book_active() && adopted_n == 1, "adopt: deeper bookmark still adopts");
    booksess_tick();
    CHECK(seeks == 1 && seek_last == 40001, "adopt: bookmark more than 35 s deeper is resumed to");
    track(B, 5200, 100000);
    now += 10000; booksess_tick();
    CHECK(saves == 0, "adopt: shallow position not saved over the deeper bookmark");

    reset();
    track(B, -5, 100000);
    progress_rc = 1; progress_pos = 35000;
    booksess_tick();
    booksess_tick();
    CHECK(ui_book_active() && seeks == 0, "adopt: negative position counts as 0 for the 35 s margin");
}

static void test_player_restarted(void){
    reset();
    booksess_begin(B, 0);
    track(B, 5000, 100000);
    booksess_tick();
    booksess_player_restarted();
    CHECK(!ui_book_active() && saves == 0, "restart: session dropped without flushing a position the new player may not share");
    booksess_tick();
    CHECK(!ui_book_active(), "restart: adoption held off after a Player restart");
    now += 4000; booksess_tick();
    CHECK(ui_book_active(), "restart: adopts again after 4 s");

    reset();
    track(B, 5000, 100000);
    booksess_player_restarted();
    booksess_tick();
    CHECK(ui_book_active(), "restart: without a session there is no hold-off");
}

static void test_sleep_timer(void){
    reset();
    ui_set_sleep_timer(1);
    int left = -1;
    CHECK(ui_sleep_state(&left) == 1 && left == 60, "timer: armed with 60 s left");
    now += 30000;
    CHECK(ui_sleep_state(&left) == 1 && left == 30, "timer: 30 s left after 30 s");
    booksess_sleep_tick();
    CHECK(playpauses == 0, "timer: waits until the interval elapses");
    now += 29999; booksess_sleep_tick();
    CHECK(playpauses == 0, "timer: not 1 ms early");
    now += 1; booksess_sleep_tick();
    CHECK(playpauses == 1 && ended_n == 1 && ui_sleep_state(NULL) == 0, "timer: pauses and clears when elapsed");
    booksess_sleep_tick();
    CHECK(playpauses == 1, "timer: fires once");

    reset();
    ui_set_sleep_timer(1);
    now += 90000;
    CHECK(ui_sleep_state(&left) == 1 && left == 0, "timer: remaining time never negative");
    ui_set_sleep_timer(0);
    CHECK(ui_sleep_state(NULL) == 0, "timer: zero minutes disarms");

    reset();
    ui_set_sleep_timer(1);
    now += 60000;
    playpause_rc = -1;
    booksess_sleep_tick();
    CHECK(playpauses == 1 && ended_n == 0 && ui_sleep_state(NULL) == 1, "timer: failed pause stays armed");
    playpause_rc = 0;
    now += 5; booksess_sleep_tick();
    CHECK(playpauses == 2 && ended_n == 1 && ui_sleep_state(NULL) == 0, "timer: failed pause retried");

    reset();
    ui_set_sleep_timer(1);
    now += 60000;
    playing = 0;
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ended_n == 1 && ui_sleep_state(NULL) == 0, "timer: already paused just disarms, never toggles to play");

    reset();
    ui_set_sleep_timer(1);
    now += 60000;
    ui_defer_sleep();
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ui_sleep_state(NULL) == 1, "timer: held off inside the seek guard");
    now += 2499; booksess_sleep_tick();
    CHECK(playpauses == 0, "timer: guard lasts 2.5 s");
    now += 1; booksess_sleep_tick();
    CHECK(playpauses == 1 && ui_sleep_state(NULL) == 0, "timer: fires when the guard ends");

    reset();
    ui_set_sleep_timer(1);
    now += 60000;
    sleep_action = 1;
    ui_defer_sleep();
    booksess_sleep_tick();
    CHECK(shutdown_n == 1 && ended_n == 1 && playpauses == 0 && ui_sleep_state(NULL) == 0, "timer: shutdown action ignores the guard and the play state");
}

static void test_eoc(void){
    reset();
    ui_set_sleep_timer(1);
    ui_set_sleep_eoc(500000, B, 0);
    CHECK(ui_sleep_state(NULL) == 2, "eoc: armed");
    now += 70000;
    track(B, 100000, 900000);
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ui_sleep_state(NULL) == 2, "eoc: arming cancels the duration timer");
    track(B, 499999, 900000);
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ui_sleep_state(NULL) == 2, "eoc: not before the target");
    track(B, 500000, 900000);
    booksess_sleep_tick();
    CHECK(playpauses == 1 && ui_sleep_state(NULL) == 0, "eoc: pauses at the target and disarms");

    reset();
    ui_set_sleep_eoc(500000, B, 0);
    track(B, 500500, 900000);
    ui_defer_sleep();
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ui_sleep_state(NULL) == 2, "eoc: held off inside the seek guard");
    now += 2500; booksess_sleep_tick();
    CHECK(playpauses == 1 && ui_sleep_state(NULL) == 0, "eoc: fires when the guard ends");

    reset();
    ui_set_sleep_eoc(500000, B, 0);
    track(B, 500000, 900000);
    playing = 0;
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ui_sleep_state(NULL) == 0, "eoc: paused at the target just disarms");

    reset();
    ui_set_sleep_eoc(500000, B, 0);
    track(B, 500000, 900000);
    playpause_rc = -1;
    booksess_sleep_tick();
    CHECK(playpauses == 1 && ui_sleep_state(NULL) == 2, "eoc: failed pause stays armed");
    playpause_rc = 0;
    booksess_sleep_tick();
    CHECK(playpauses == 2 && ui_sleep_state(NULL) == 0, "eoc: failed pause retried");

    reset();
    ui_set_sleep_eoc(500000, B, 0);
    track(B, 400000, 900000);
    booksess_sleep_tick();
    track(OTHER, 1000, 200000);
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ui_sleep_state(NULL) == 0, "eoc: mid-book target disarms when the track changes");

    reset();
    ui_set_sleep_eoc(900000, B, 1);
    track(B, 892000, 900000);
    booksess_sleep_tick();
    track(OTHER, 0, 200000);
    booksess_sleep_tick();
    CHECK(playpauses == 1 && ui_sleep_state(NULL) == 0, "eoc: book-end target fulfilled by a rollover within 8 s of the target");

    reset();
    ui_set_sleep_eoc(900000, B, 1);
    track(B, 891999, 900000);
    booksess_sleep_tick();
    track(OTHER, 0, 200000);
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ui_sleep_state(NULL) == 0, "eoc: rollover from more than 8 s before the target only disarms");

    reset();
    ui_set_sleep_eoc(900000, B, 1);
    track(OTHER, 0, 200000);
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ui_sleep_state(NULL) == 0, "eoc: never seen on the book only disarms");

    reset();
    ui_set_sleep_eoc(900000, B, 1);
    track(B, 895000, 900000);
    booksess_sleep_tick();
    track(OTHER, 0, 200000);
    playpause_rc = -1;
    booksess_sleep_tick();
    CHECK(playpauses == 1 && ui_sleep_state(NULL) == 2, "eoc: failed rollover pause stays armed");
    playpause_rc = 0;
    booksess_sleep_tick();
    CHECK(playpauses == 2 && ui_sleep_state(NULL) == 0, "eoc: failed rollover pause retried");

    reset();
    ui_set_sleep_eoc(900000, B, 1);
    track(B, 895000, 900000);
    booksess_sleep_tick();
    track(OTHER, 0, 200000);
    playing = 0;
    booksess_sleep_tick();
    CHECK(playpauses == 0 && ui_sleep_state(NULL) == 0, "eoc: rollover while paused only disarms");

    reset();
    ui_set_sleep_eoc(0, B, 0);
    CHECK(ui_sleep_state(NULL) == 0, "eoc: zero target disarms");
    ui_set_sleep_eoc(1000, "", 0);
    CHECK(ui_sleep_state(NULL) == 0, "eoc: empty path disarms");
    ui_set_sleep_eoc(1000, B, 0);
    ui_disarm_book_eoc();
    CHECK(ui_sleep_state(NULL) == 0, "eoc: explicit disarm");
    ui_set_sleep_eoc(1000, B, 0);
    ui_set_sleep_timer(5);
    CHECK(ui_sleep_state(NULL) == 1, "eoc: arming the duration timer replaces it");
}

int main(void){
    booksess_set_env(&env);
    now = 100000;
    test_smart_rewind();
    test_confirm_and_checkpoint();
    test_track_change_ends_session();
    test_resume_seek();
    test_smart_rewind_on_resume();
    test_backward_jump();
    test_cancel_flush();
    test_overflow();
    test_user_seek();
    test_adopt();
    test_player_restarted();
    test_sleep_timer();
    test_eoc();
    if(fails){ printf("booksess_test: %d failures\n", fails); return 1; }
    printf("booksess_test: smart rewind, confirm, checkpoint, resume, backward jump, cancel, user seek, adopt, restart, sleep timer, eoc ok\n");
    return 0;
}
