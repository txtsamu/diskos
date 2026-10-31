/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "playreq.h"
#include "playstate.h"
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef PLAY_PCM_GLOB
#define PLAY_PCM_GLOB "/proc/asound/card*/pcm*p/sub*/status"
#endif

#define NO_GEN        0xFFFFFFFFu
#define RECENT_MS     10000
#define PLAYING_GRACE 1600
#define NAME_MAX_LEN  380

static const struct playreq_env *E;
void playreq_set_env(const struct playreq_env *env){ E = env; }

static uint32_t g_pending;
static int      g_playing;
static playstate_t g_ps = { -1, 0 };

static unsigned g_sent_gen = NO_GEN;
static uint32_t g_sent_at;
static unsigned g_transport_gen = NO_GEN;
static uint32_t g_transport_at;
static unsigned g_track_seen_gen = NO_GEN;

int  playreq_playing(void){ return g_playing; }
int  playreq_pending(void){ return g_pending != 0; }
void playreq_clear(void){ g_pending = 0; }

int playreq_recent(void){
    return g_pending != 0 || (g_sent_gen == E->generation() && (uint32_t)(E->now() - g_sent_at) < RECENT_MS);
}

void playreq_note_transport(unsigned gen, uint32_t now_ms){ g_transport_gen = gen; g_transport_at = now_ms; }
int  playreq_transport_in(unsigned gen){ return g_transport_gen == gen; }

int playreq_startup_busy(unsigned gen, uint32_t now_ms, int playing, int starting, int have_track){
    if(playing) g_track_seen_gen = gen;
    if(starting || playing) return 1;
    if(g_transport_gen != gen) return 0;
    if(have_track) g_track_seen_gen = gen;
    return have_track || g_track_seen_gen != gen || now_ms - g_transport_at < RECENT_MS;
}

static int pcm_running(void){
    glob_t paths;
    if(glob(PLAY_PCM_GLOB, 0, NULL, &paths) != 0){
        globfree(&paths);
        return 0;
    }
    int running = 0;
    for(size_t i = 0; i < paths.gl_pathc && i < 16 && !running; i++){
        char buf[512];
        int fd = open(paths.gl_pathv[i], O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if(fd < 0) continue;
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if(n > 0){
            buf[n] = 0;
            running = strstr(buf, "state: RUNNING") != NULL;
        }
    }
    globfree(&paths);
    return running;
}

static int pcm_seen(uint32_t now, int fresh){
    static uint32_t at;
    static int valid, running;
    if(!valid || now - at >= 500 || (fresh && at != now)){
        running = pcm_running();
        at = now;
        valid = 1;
    }
    return running;
}

static struct {
    uint32_t request, at;
    unsigned serial, pos_seq, generation, transport;
    uint64_t token;
    long position, duration;
    int have_track, playing, provisional;
} g_pre;

static void arm(const track_state_t *cur, int was_playing){
    g_pending = E->now();
    if(!g_pending) g_pending = 1;

    g_pre.serial++;
    g_pre.request = g_pre.at = g_pending;
    g_pre.pos_seq = cur->pos_seq;
    g_pre.position = cur->position_ms;
    g_pre.generation = cur->player_generation;
    g_pre.transport = E->transport_seq();
    g_pre.token = cur->playback_token;
    g_pre.duration = cur->duration_ms;
    g_pre.have_track = cur->have_track;
    g_pre.playing = was_playing;
    g_pre.provisional = cur->provisional;
}

static int start_failed(const track_state_t *st, uint32_t now){
    static uint32_t request, progress, pos_at, adv_at;
    static unsigned serial, pos_seq, generation, transport;
    static uint64_t token;
    static long position, duration;
    static int active, had_track, seed, advances;
    static int attrib, started, possible, may_start, seen;

    if(!g_pending){
        active = 0;
        return 0;
    }
    if(!active || request != g_pending ||
       serial != g_pre.serial){
        active = 1;
        request = g_pending;
        serial = g_pre.serial;
        progress = request;
        advances = attrib = started = possible = seen = 0;
        may_start = seed = 0;

        if(g_pre.request == request){
            pos_at = g_pre.at;
            pos_seq = g_pre.pos_seq;
            position = g_pre.position;
            generation = g_pre.generation;
            transport = g_pre.transport;
            token = g_pre.token;
            duration = g_pre.duration;
            had_track = g_pre.have_track;
            seed = g_pre.provisional;
            may_start = !g_pre.playing &&
                        (!had_track || position <= 1500);
        } else {
            pos_at = now;
            pos_seq = st->pos_seq;
            position = st->position_ms;
            generation = st->player_generation;
            transport = E->transport_seq();
            token = st->playback_token;
            duration = st->duration_ms;
            had_track = st->have_track;
        }
    }

    if(generation != st->player_generation ||
       transport != E->transport_seq()){
        g_pending = 0;
        active = 0;
        return 0;
    }

    if(seed && st->player_confirmed){
        seed = 0;
        attrib = 0;
        advances = 0;
        progress = now;
        token = st->playback_token;
        duration = st->duration_ms;
        had_track = st->have_track;
        pos_seq = st->pos_seq;
        position = st->position_ms;
        pos_at = now;
    } else if(token != st->playback_token ||
              had_track != st->have_track){
        progress = now;
        advances = 0;
        attrib = !seed && st->have_track &&
                 st->player_confirmed && !st->provisional;
        token = st->playback_token;
        duration = st->duration_ms;
        had_track = st->have_track;
        pos_seq = st->pos_seq;
        position = st->position_ms;
        pos_at = now;
    } else if(duration != st->duration_ms){
        progress = now;
        advances = 0;
        duration = st->duration_ms;
        pos_seq = st->pos_seq;
        position = st->position_ms;
        pos_at = now;
    }

    if(st->pos_seq != pos_seq){
        long long delta = (long long)st->position_ms - position;
        uint32_t gap = now - pos_at;
        long long slack = (long long)gap + 1500;
        int fresh = st->have_track && st->player_confirmed &&
                    !st->provisional &&
                    (int32_t)(st->pos_seq - st->path_seq) > 0;
        int wrap = delta < 0 && duration > 0 &&
                   position >= 0 && position <= duration &&
                   st->position_ms >= 0 &&
                   st->position_ms <= duration &&
                   (long long)duration - position +
                       st->position_ms <= slack;

        if(fresh && delta != 0) progress = now;
        if(fresh && ((delta > 0 && delta <= slack) || wrap)){
            advances++;
            seen = 1;
            adv_at = now;
            if(attrib) started = 1;
            else if(may_start) possible = 1;
        } else {
            advances = 0;
            if(fresh && !wrap && delta < -1500)
                attrib = 1;
        }

        pos_seq = st->pos_seq;
        position = st->position_ms;
        pos_at = now;
        if(advances >= 2 && (attrib || possible)){
            g_pending = 0;
            if(attrib) E->note_playback();
            active = 0;
            return 0;
        }
    }

    uint32_t age = now - request;
    if(age < 45000 && (age < 15000 || now - progress < 6000))
        return 0;

    if(started || possible){
        g_pending = 0;
        active = 0;
        return 0;
    }

    int final = age >= 45000;
    int out = seen && now - adv_at < 6000;
    if(!out) out = pcm_seen(now, final);
    if(!out && !final) out = pcm_seen(now, 1);
    if(out){
        if(final){
            g_pending = 0;
            active = 0;
        }
        return 0;
    }
    return 1;
}

int playreq_tick(const track_state_t *st){
    uint32_t now = E->now();
    g_playing = playstate_playing(&g_ps, st->have_track, st->position_ms, now, PLAYING_GRACE, 0);
    if(g_playing) playreq_note_transport(E->generation(), now);
    if(g_pending && E->tx_waiting()){
        g_pending = now ? now : 1;
        g_pre.request = g_pre.at = g_pending;
    }
    if(!start_failed(st, now)) return 0;
    g_pending = 0;
    return 1;
}

static int send_play(const char *frame, int track){
    playreq_note_transport(E->generation(), E->now());
    (void)E->admit_start(ADMIT_PLAY_LIST);
    track_state_t cur;
    if(track) E->get_state(&cur);
    int rc = E->send(frame);
    if(rc != 0) return rc;
    g_sent_gen = E->generation();
    g_sent_at = E->now();
    if(track) arm(&cur, g_playing);
    return 0;
}

int playreq_play_list(int list_type, const char *name, int pos1, int build_idx){
    if(pos1 < 1) pos1 = 1;
    if(!name) name = "";
    if(strlen(name) >= NAME_MAX_LEN) return -1;
    char f[420];
    int datalen = 8 + (int)strlen(name);
    snprintf(f, sizeof f, "0100%04X%04X%04X%s",
             8 + datalen, (build_idx >= 0 ? build_idx : pos1-1) & 0xFFFF, list_type & 0xFFFF, name);
    int sent = send_play(f, 1) == 0;
    if(sent) fprintf(stderr,"play(build) type=%d pos=%d name='%s' -> %s\n", list_type, pos1, name, f);
    fflush(stderr);
    return sent;
}

int playreq_queue_jump(int ord1){
    char f[24]; snprintf(f, sizeof f, "0100%04X%04X0000", 16, (ord1 - 1) & 0xFFFF);
    int rc = send_play(f, 0);
    fprintf(stderr, "queue jump pos=%d -> %s (rc=%d)\n", ord1, f, rc); fflush(stderr);
    return rc;
}

void playreq_reserve_slot(void){ (void)E->admit_start(ADMIT_PLAY_RESERVED_SLOT); }
