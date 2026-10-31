/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "booksess.h"
#include "musicdb.h"
#include "power.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct booksess_env *E;

static char     g_book_sess[BOOKSESS_PATH_MAX];
static long     g_book_resume_ms = -1;
static int      g_book_resume_done;
static int      g_book_confirmed;
static uint32_t g_book_deadline;
static uint32_t g_book_last_save;
static int      g_book_was_playing;
static uint32_t g_book_pause_tick;
static long     g_book_last_pos = -1;
static long     g_book_resume_target = 0;
static int      g_book_ckpt_ok = 0;
static uint32_t g_book_noadopt_until;
static uint32_t g_sleep_seek_guard;

static uint32_t g_sleep_start = 0, g_sleep_ms = 0;
static long     g_sleep_eoc = -1;
static char     g_sleep_eoc_path[256];
static long     g_sleep_eoc_lastpos = -1;
static int      g_sleep_eoc_atend = 0;

void booksess_set_env(const struct booksess_env *env){ E = env; }

int ui_book_active(void){ return g_book_sess[0] != 0; }

void ui_cancel_book_resume(void){
    if(g_book_sess[0] && g_book_ckpt_ok){
        track_state_t s; E->get_state(&s);
        if(s.have_track && strcmp(s.path, g_book_sess) == 0 && s.position_ms > 0
           && (int32_t)(s.pos_seq - s.path_seq) > 0)
            E->book_save(g_book_sess, s.position_ms);
    }
    g_book_sess[0] = 0;
    g_book_resume_ms = -1;
    g_book_resume_done = 0;
    g_book_confirmed = 0;
    g_book_was_playing = 0;
    g_book_pause_tick = 0;
    g_book_last_pos = -1;
    g_book_resume_target = 0;
    g_book_ckpt_ok = 0;
}

void ui_defer_sleep(void){ g_sleep_seek_guard = E->now() + 2500; }

void ui_book_user_seeked(long target_ms){
    g_sleep_seek_guard = E->now() + 2500;
    if(g_book_sess[0] && g_book_confirmed){
        g_book_resume_ms = -1; g_book_resume_done = 1; g_book_pause_tick = 0;
        if(target_ms >= 0){
            g_book_last_pos = target_ms;
            g_book_resume_target = target_ms;
            g_book_ckpt_ok = 0;
            E->book_save(g_book_sess, target_ms);
        } else {
            g_book_ckpt_ok = 1;
        }
    }
}

void booksess_hold_off_adopt(void){ g_book_noadopt_until = E->now() + 4000; }

void booksess_player_restarted(void){
    if(g_book_sess[0]){
        g_book_ckpt_ok = 0;
        ui_cancel_book_resume();
        booksess_hold_off_adopt();
    }
}

void booksess_begin(const char *path, long resume_ms){
    snprintf(g_book_sess, sizeof g_book_sess, "%s", path);
    g_book_resume_ms   = (resume_ms > 1000) ? resume_ms : 0;
    g_book_resume_done = (g_book_resume_ms > 0) ? 0 : 1;
    g_book_resume_target = g_book_resume_ms;
    g_book_ckpt_ok     = (g_book_resume_ms > 0) ? 0 : 1;
    g_book_confirmed   = 0;
    g_book_deadline    = E->now() + 15000;
    g_book_last_save   = E->now();
}

long ui_smart_rewind_ms(long idle_seconds){
    if(idle_seconds < 30)    return 0;
    if(idle_seconds < 1800)  return 8000;
    if(idle_seconds < 28800) return 18000;
    return 30000;
}

void booksess_tick(void){
    if(E->admit()) return;
    if(!g_book_sess[0]){
        if(DISKOS_AUDIOBOOKS && E->now() >= g_book_noadopt_until){
            track_state_t s; E->get_state(&s);
            if(s.have_track && E->is_book_path(s.path) && E->playing()){
                long cur = (s.position_ms < 0) ? 0 : s.position_ms;
                long saved = 0;
                int have_bm = E->book_progress(s.path, &saved);
                if(have_bm < 0) return;
                snprintf(g_book_sess, sizeof g_book_sess, "%s", s.path);
                g_book_confirmed = 1;
                g_book_was_playing = 1; g_book_pause_tick = 0;
                g_book_last_save = E->now();
                if(have_bm == 1 && saved > cur + 35000){
                    g_book_resume_ms = saved; g_book_resume_done = 0; g_book_resume_target = saved;
                    g_book_ckpt_ok = 0; g_book_last_pos = -1;
                } else {
                    g_book_resume_ms = 0; g_book_resume_done = 1; g_book_resume_target = 0;
                    g_book_ckpt_ok = 1; g_book_last_pos = cur;
                }
                E->adopted();
            }
        }
        return;
    }
    track_state_t st; E->get_state(&st);
    uint32_t now = E->now();
    int past_deadline = (int32_t)(now - g_book_deadline) >= 0;
    int is_ours = (st.path[0] && strcmp(st.path, g_book_sess) == 0);
    if(!is_ours){
        if(g_book_confirmed){ ui_cancel_book_resume(); return; }
        if(past_deadline) ui_cancel_book_resume();
        return;
    }
    if(!g_book_confirmed){ g_book_confirmed = 1; return; }
    if(!g_book_resume_done){
        if(g_book_resume_ms > 0){
            if(st.duration_ms <= 0 && !past_deadline) return;
            long rt = g_book_resume_ms;
            if(st.duration_ms > 0 && rt > st.duration_ms - 2000){ rt = st.duration_ms - 2000; if(rt < 0) rt = 0; }
            g_book_resume_target = rt;
            if(E->seek_to(rt) != 0) return;
        }
        g_book_resume_done = 1;
        g_book_last_save = now;
        g_book_was_playing = E->playing();
        return;
    }
    {
        int np = E->playing();
        if(g_book_was_playing && !np){ g_book_pause_tick = now; }
        else if(!g_book_was_playing && np && g_book_pause_tick){
            long rw = ui_smart_rewind_ms((long)((E->now() - g_book_pause_tick) / 1000u));
            if(rw > 0){ long t = st.position_ms - rw; if(t < 0) t = 0; E->seek_to(t); }
            g_book_pause_tick = 0;
        }
        g_book_was_playing = np;
    }
    if((int32_t)(st.pos_seq - st.path_seq) <= 0) return;
    int seek_guarded = E->now() < g_sleep_seek_guard;
    if(!seek_guarded && g_book_last_pos >= 0 && st.position_ms < g_book_last_pos - 35000){
        g_book_ckpt_ok = 0;
        ui_cancel_book_resume();
        return;
    }
    g_book_last_pos = st.position_ms;
    if(st.position_ms <= 0) return;
    if(st.duration_ms > 0 && st.position_ms > st.duration_ms + 2000){ g_book_ckpt_ok = 0; ui_cancel_book_resume(); return; }
    if(!g_book_ckpt_ok){
        if(labs(st.position_ms - g_book_resume_target) <= 3000) g_book_ckpt_ok = 1;
        else return;
    }
    if(!E->playing()) return;
    if((int32_t)(now - g_book_last_save) < 10000) return;
    if(E->book_save(g_book_sess, st.position_ms) == 0) return;
    g_book_last_save = now;
}

void ui_disarm_book_eoc(void){ g_sleep_eoc = -1; g_sleep_eoc_lastpos = -1; g_sleep_eoc_atend = 0; }

void ui_set_sleep_timer(int minutes){
    ui_disarm_book_eoc();
    if(minutes>0){ g_sleep_start=E->now(); g_sleep_ms=(uint32_t)minutes*60000u; }
    else g_sleep_ms=0;
}

void ui_set_sleep_eoc(long target_ms, const char *path, int at_book_end){
    g_sleep_ms = 0;
    g_sleep_eoc_lastpos = -1;
    if(target_ms > 0 && path && path[0]){ g_sleep_eoc = target_ms; g_sleep_eoc_atend = at_book_end ? 1 : 0; snprintf(g_sleep_eoc_path, sizeof g_sleep_eoc_path, "%s", path); }
    else { g_sleep_eoc = -1; g_sleep_eoc_atend = 0; }
}

int ui_sleep_state(int *secs_left){
    if(g_sleep_ms){
        if(secs_left){ long r = (long)g_sleep_ms - (long)(E->now() - g_sleep_start); *secs_left = (int)((r>0?r:0)/1000); }
        return 1;
    }
    return g_sleep_eoc >= 0 ? 2 : 0;
}

typedef enum { PW_SLEEP_WAIT, PW_SLEEP_PAUSE, PW_SLEEP_DISARM, PW_SLEEP_SHUTDOWN } pw_sleep_act_t;
static pw_sleep_act_t sleep_decide(uint32_t armed_ms, uint32_t elapsed_ms, int guarded, int playing, int action){
    if(!armed_ms || elapsed_ms < armed_ms) return PW_SLEEP_WAIT;
    if(action == POWER_SLEEP_SHUTDOWN) return PW_SLEEP_SHUTDOWN;
    if(guarded) return PW_SLEEP_WAIT;
    return playing ? PW_SLEEP_PAUSE : PW_SLEEP_DISARM;
}

void booksess_sleep_tick(void){
    int playing = E->playing();
    int sleep_guarded = E->now() < g_sleep_seek_guard;
    switch(sleep_decide(g_sleep_ms, g_sleep_ms ? E->now() - g_sleep_start : 0, sleep_guarded, playing, E->sleep_action())){
    case PW_SLEEP_SHUTDOWN:
        g_sleep_ms = 0; E->sleep_ended();
        E->shutdown();
        break;
    case PW_SLEEP_DISARM: g_sleep_ms = 0; E->sleep_ended(); break;
    case PW_SLEEP_PAUSE:
        if(E->playpause() == 0){ g_sleep_ms = 0; E->sleep_ended(); }
        break;
    case PW_SLEEP_WAIT: break;
    }
    if(g_sleep_eoc >= 0){
        track_state_t sst; E->get_state(&sst);
        if(strcmp(sst.path, g_sleep_eoc_path) != 0){
            int completed = (g_sleep_eoc_atend && g_sleep_eoc_lastpos >= 0 && g_sleep_eoc_lastpos >= g_sleep_eoc - 8000);
            if(!(completed && playing && E->playpause() != 0)) ui_disarm_book_eoc();
        } else {
            g_sleep_eoc_lastpos = sst.position_ms;
            if(sst.position_ms >= g_sleep_eoc && !sleep_guarded){
                if(!playing){ ui_disarm_book_eoc(); }
                else if(E->playpause() == 0){ ui_disarm_book_eoc(); }
            }
        }
    }
}
