/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "playreq.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PCM_DIR
#define PCM_DIR "/tmp/diskos_cfgtest/pcm"
#endif

#define MAXF 16
static char g_log[MAXF][48];
static int g_n, k_send_rc, k_tx_waiting, g_note_playback, g_transport_at_admit;
static uint32_t k_now;
static unsigned k_gen = 1, k_tseq = 1;
static track_state_t S;

static uint32_t env_now(void){ return k_now; }
static unsigned env_gen(void){ return k_gen; }
static unsigned env_tseq(void){ return k_tseq; }
static void env_state(track_state_t *out){ *out = S; }
static int env_tx_waiting(void){ return k_tx_waiting; }
static int env_send(const char *frame){
    assert(g_n < MAXF);
    snprintf(g_log[g_n++], sizeof g_log[0], "%s", frame);
    if(k_send_rc == 0) k_tseq++;
    return k_send_rc;
}
static admit_sent_t env_admit(admit_play_t kind){
    assert(g_n < MAXF);
    g_transport_at_admit = playreq_transport_in(k_gen);
    snprintf(g_log[g_n++], sizeof g_log[0], "%s", kind == ADMIT_PLAY_LIST ? "A:LIST" : "A:RES");
    return ADMIT_SENT_NONE;
}
static void env_note_playback(void){ g_note_playback++; }
static const struct playreq_env k_env = {
    .now = env_now, .generation = env_gen, .transport_seq = env_tseq, .get_state = env_state,
    .tx_waiting = env_tx_waiting, .send = env_send, .admit_start = env_admit, .note_playback = env_note_playback,
};

static void setup(void){
    playreq_set_env(&k_env);
    k_now = 1000;
    S = (track_state_t){ 0 };
}
static void expect_log(const char *what, const char *const *want, int n){
    int ok = g_n == n;
    for(int i = 0; ok && i < n; i++) ok = !strcmp(g_log[i], want[i]);
    if(!ok){
        fprintf(stderr, "%s: %d entries (want %d):", what, g_n, n);
        for(int i = 0; i < g_n; i++) fprintf(stderr, " %s", g_log[i]);
        fprintf(stderr, "\n");
        assert(0);
    }
}
static int tick_at(uint32_t t){ k_now = t; return playreq_tick(&S); }
static void shows_track(uint64_t token, unsigned pos_seq, long pos){
    S.have_track = 1; S.player_confirmed = 1; S.provisional = 0;
    S.playback_token = token; S.path_seq = 1; S.pos_seq = pos_seq; S.position_ms = pos; S.duration_ms = 200000;
}
static void arm_idle(void){
    setup();
    assert(playreq_play_list(1, "", 1, -1) == 1);
    assert(playreq_pending());
}
static void set_pcm(const char *state){
    char cmd[160];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/0", PCM_DIR, PCM_DIR);
    assert(system(cmd) == 0);
    if(!state) return;
    snprintf(cmd, sizeof cmd, "%s/0/status", PCM_DIR);
    FILE *f = fopen(cmd, "w"); assert(f);
    fprintf(f, "state: %s\n", state); fclose(f);
}

static void starts_list_sequence(void){
    setup();
    assert(playreq_play_list(1, "", 3, -1) == 1);
    expect_log("list", (const char *const[]){ "A:LIST", "0100001000020001" }, 2);
    assert(g_transport_at_admit == 1);
    assert(playreq_pending() && playreq_recent());

    setup(); g_n = 0;
    assert(playreq_play_list(3, "Jazz", 2, -1) == 1);
    expect_log("named list", (const char *const[]){ "A:LIST", "0100001400010003Jazz" }, 2);

    g_n = 0;
    assert(playreq_play_list(6, "", 1, 42) == 1);
    expect_log("favourite build index", (const char *const[]){ "A:LIST", "01000010002A0006" }, 2);

    g_n = 0;
    assert(playreq_play_list(7, NULL, 0, -1) == 1);
    expect_log("null name, pos clamped", (const char *const[]){ "A:LIST", "0100001000000007" }, 2);
}
static void rejects_a_long_name_without_side_effects(void){
    setup();
    char name[400]; memset(name, 'a', sizeof name - 1); name[sizeof name - 1] = 0;
    assert(playreq_play_list(1, name, 1, -1) == -1);
    assert(g_n == 0 && !playreq_pending() && !playreq_recent() && !playreq_transport_in(k_gen));
    name[379] = 0;
    assert(playreq_play_list(1, name, 1, -1) == 1 && g_n == 2);
}
static void failed_send_notes_transport_but_arms_nothing(void){
    setup(); k_send_rc = -1;
    assert(playreq_play_list(1, "", 1, -1) == 0);
    expect_log("refused send", (const char *const[]){ "A:LIST", "0100001000000001" }, 2);
    assert(!playreq_pending() && !playreq_recent() && playreq_transport_in(k_gen));
    g_n = 0;
    assert(playreq_queue_jump(2) == -1);
    assert(!playreq_recent());
}
static void queue_jump_sequence(void){
    setup();
    assert(playreq_queue_jump(3) == 0);
    expect_log("jump", (const char *const[]){ "A:LIST", "0100001000020000" }, 2);
    assert(g_transport_at_admit == 1);
    assert(!playreq_pending() && playreq_recent());
}
static void reserved_slot_then_list_is_two_starts(void){
    setup();
    playreq_reserve_slot();
    assert(playreq_play_list(5, "", 2, -1) == 1);
    expect_log("reserved slot list", (const char *const[]){ "A:RES", "A:LIST", "0100001000010005" }, 3);
    g_n = 0;
    playreq_reserve_slot();
    assert(playreq_play_list(5, "", 1, -1) == 1);
    expect_log("book", (const char *const[]){ "A:RES", "A:LIST", "0100001000000005" }, 3);
    playreq_clear();
    assert(!playreq_pending());
}

static void recent_window(void){
    setup();
    assert(!playreq_recent());
    k_now = 5000; assert(playreq_queue_jump(1) == 0);
    assert(playreq_recent());
    k_now = 5000 + 9999; assert(playreq_recent());
    k_now = 5000 + 10000; assert(!playreq_recent());
    k_now = 5000; assert(playreq_queue_jump(1) == 0);
    k_gen++;
    assert(!playreq_recent());
    assert(playreq_play_list(1, "", 1, -1) == 1);
    assert(playreq_recent());
    k_now = 5000 + 20000; assert(playreq_recent());
    playreq_clear();
    assert(!playreq_recent());
}
static void recent_window_survives_tick_wrap(void){
    setup();
    k_now = UINT32_MAX - 100; assert(playreq_queue_jump(1) == 0);
    k_now = 50; assert(playreq_recent());
    k_now = 9898; assert(playreq_recent());
    k_now = 9899; assert(!playreq_recent());
}

static void startup_busy_window(void){
    setup();
    assert(!playreq_startup_busy(1, 1000, 0, 0, 0));
    assert(playreq_startup_busy(1, 1000, 1, 0, 0));
    assert(playreq_startup_busy(1, 1000, 0, 1, 0));
    assert(!playreq_transport_in(1));
    playreq_note_transport(1, 3000);
    assert(playreq_transport_in(1) && !playreq_transport_in(2));
    assert(playreq_startup_busy(1, 10000, 0, 0, 0));
    assert(playreq_startup_busy(1, 3000 + 9999, 0, 0, 0));
    assert(playreq_startup_busy(1, 3000 + 10000, 0, 0, 1));
}
static void startup_busy_after_playback_was_seen(void){
    setup();
    playreq_note_transport(1, 3000);
    assert(playreq_startup_busy(1, 3000, 1, 0, 0));
    assert(playreq_startup_busy(1, 3000 + 9999, 0, 0, 0));
    assert(!playreq_startup_busy(1, 3000 + 10000, 0, 0, 0));
}
static void startup_busy_ignores_older_generations(void){
    setup();
    playreq_note_transport(1, 0);
    assert(!playreq_startup_busy(2, 100, 0, 0, 1));
    assert(!playreq_startup_busy(2, 100, 0, 0, 0));
}

static void playing_follows_the_position(void){
    setup();
    shows_track(1, 2, 60000);
    assert(tick_at(100000) == 0 && playreq_playing());
    assert(playreq_transport_in(k_gen));
    S.position_ms = 61000; assert(tick_at(101000) == 0 && playreq_playing());
    assert(tick_at(101000 + 1599) == 0 && playreq_playing());
    assert(tick_at(101000 + 1600) == 0 && !playreq_playing());
    S.position_ms = 62000; tick_at(103000); assert(playreq_playing());
    S.have_track = 0; tick_at(104000); assert(!playreq_playing());
}
static void playing_note_is_per_generation(void){
    setup();
    shows_track(1, 2, 60000);
    tick_at(2000);
    assert(playreq_transport_in(1) && !playreq_transport_in(2));
    k_gen = 2; S.position_ms = 61000; tick_at(3000);
    assert(playreq_transport_in(2));
}

static void a_start_with_metadata_progress_retires_the_request(void){
    arm_idle();
    assert(tick_at(1500) == 0 && playreq_pending());
    shows_track(1, 2, 0);
    assert(tick_at(2000) == 0 && playreq_pending());
    shows_track(1, 3, 1000);
    assert(tick_at(3000) == 0 && playreq_pending());
    shows_track(1, 4, 2000);
    assert(tick_at(4000) == 0 && !playreq_pending());
    assert(g_note_playback == 1);
}
static void a_resume_of_the_current_song_retires_the_request_without_a_note(void){
    setup();
    shows_track(7, 5, 0);
    assert(playreq_play_list(1, "", 1, -1) == 1);
    shows_track(7, 6, 1000);
    assert(tick_at(2000) == 0 && playreq_pending());
    shows_track(7, 7, 2000);
    assert(tick_at(3000) == 0 && !playreq_pending());
    assert(g_note_playback == 0);
}
static void a_play_over_a_playing_song_needs_a_new_track(void){
    setup();
    shows_track(7, 5, 60000);
    tick_at(1000);
    assert(playreq_playing());
    assert(playreq_play_list(1, "", 1, -1) == 1);
    shows_track(7, 6, 61000);
    assert(tick_at(2000) == 0);
    shows_track(7, 7, 62000);
    assert(tick_at(3000) == 0 && playreq_pending());
    shows_track(7, 8, 63000);
    assert(tick_at(4000) == 0 && playreq_pending());
}

static void silence_fails_the_request_at_fifteen_seconds(void){
    arm_idle(); set_pcm(NULL);
    assert(tick_at(1000 + 14999) == 0 && playreq_pending());
    assert(tick_at(1000 + 15000) == 1 && !playreq_pending());
    assert(tick_at(1000 + 15500) == 0);
}
static void metadata_progress_buys_a_six_second_window(void){
    arm_idle(); set_pcm(NULL);
    shows_track(1, 2, 0);
    assert(tick_at(1000 + 14000) == 0);
    assert(tick_at(1000 + 14000 + 5999) == 0 && playreq_pending());
    assert(tick_at(1000 + 14000 + 6000) == 1 && !playreq_pending());
}
static void a_running_pcm_stream_holds_the_request_until_the_hard_limit(void){
    arm_idle(); set_pcm("RUNNING");
    assert(tick_at(1000 + 15000) == 0 && playreq_pending());
    assert(tick_at(1000 + 44999) == 0 && playreq_pending());
    assert(tick_at(1000 + 45000) == 0 && !playreq_pending());
}
static void a_stopped_pcm_stream_does_not_hold_the_request(void){
    arm_idle(); set_pcm("SETUP");
    assert(tick_at(1000 + 15000) == 1);
}
static void pcm_observations_are_cached_for_half_a_second(void){
    arm_idle(); set_pcm("RUNNING");
    assert(tick_at(1000 + 15000) == 0 && playreq_pending());
    set_pcm("SETUP");
    assert(tick_at(1000 + 15000 + 100) == 0 && playreq_pending());
    assert(tick_at(1000 + 15000 + 600) == 1 && !playreq_pending());
}
static void a_new_player_or_a_new_transport_attempt_supersedes_the_request(void){
    arm_idle();
    S.player_generation = 3;
    assert(tick_at(2000) == 0 && !playreq_pending());
    arm_idle();
    k_tseq++;
    assert(tick_at(2000) == 0 && !playreq_pending());
}
static void a_queued_send_restarts_the_clock(void){
    arm_idle(); set_pcm(NULL);
    k_tx_waiting = 1; assert(tick_at(5000) == 0);
    k_tx_waiting = 0;
    assert(tick_at(5000 + 14999) == 0 && playreq_pending());
    assert(tick_at(5000 + 15000) == 1);
}
static void a_cleared_request_stays_clear(void){
    arm_idle();
    playreq_clear();
    assert(tick_at(1000 + 60000) == 0 && !playreq_pending());
    assert(playreq_play_list(1, "", 1, -1) == 1);
    assert(playreq_pending());
}
static void a_second_request_is_monitored_separately(void){
    arm_idle(); set_pcm(NULL);
    assert(tick_at(1000 + 14000) == 0);
    assert(playreq_play_list(1, "", 2, -1) == 1);
    assert(tick_at(1000 + 14000) == 0);
    assert(tick_at(1000 + 14000 + 14999) == 0 && playreq_pending());
    assert(tick_at(1000 + 14000 + 15000) == 1);
}

static void run(const char *name, void (*fn)(void)){
    fflush(stdout);
    pid_t p = fork();
    assert(p >= 0);
    if(p == 0){ fn(); fflush(stdout); _exit(0); }
    int st = 0; waitpid(p, &st, 0);
    if(!WIFEXITED(st) || WEXITSTATUS(st) != 0){ fprintf(stderr, "playreq_test FAILED: %s\n", name); exit(1); }
    printf("  ok %s\n", name);
}
#define RUN(f) run(#f, f)

int main(void){
    RUN(starts_list_sequence);
    RUN(rejects_a_long_name_without_side_effects);
    RUN(failed_send_notes_transport_but_arms_nothing);
    RUN(queue_jump_sequence);
    RUN(reserved_slot_then_list_is_two_starts);
    RUN(recent_window);
    RUN(recent_window_survives_tick_wrap);
    RUN(startup_busy_window);
    RUN(startup_busy_after_playback_was_seen);
    RUN(startup_busy_ignores_older_generations);
    RUN(playing_follows_the_position);
    RUN(playing_note_is_per_generation);
    RUN(a_start_with_metadata_progress_retires_the_request);
    RUN(a_resume_of_the_current_song_retires_the_request_without_a_note);
    RUN(a_play_over_a_playing_song_needs_a_new_track);
    RUN(silence_fails_the_request_at_fifteen_seconds);
    RUN(metadata_progress_buys_a_six_second_window);
    RUN(a_running_pcm_stream_holds_the_request_until_the_hard_limit);
    RUN(a_stopped_pcm_stream_does_not_hold_the_request);
    RUN(pcm_observations_are_cached_for_half_a_second);
    RUN(a_new_player_or_a_new_transport_attempt_supersedes_the_request);
    RUN(a_queued_send_restarts_the_clock);
    RUN(a_cleared_request_stays_clear);
    RUN(a_second_request_is_monitored_separately);
    puts("playreq_test: ok");
    return 0;
}
