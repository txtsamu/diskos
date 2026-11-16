/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "nowplay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int c_view, c_home, c_progress, c_state, c_art, c_track, c_quick, c_info, c_menu;

static void f_view(const player_view_t *v, const player_evidence_t *e, int playing){ (void)v; (void)e; (void)playing; c_view++; }
static void f_home(const player_view_t *v, int playing){ (void)v; (void)playing; c_home++; }
static void f_progress(const player_view_t *v, int playing){ (void)v; (void)playing; c_progress++; }
static void f_state(int playing){ (void)playing; c_state++; }
static void f_art(const player_view_t *v){ (void)v; c_art++; }
static void f_track(const player_view_t *v, int playing){ (void)v; (void)playing; c_track++; }
static void f_quick(int playing){ (void)playing; c_quick++; }
static void f_info(const player_view_t *v){ (void)v; c_info++; }
static void f_menu(const player_view_t *v, int playing){ (void)v; (void)playing; c_menu++; }

static const struct nowplay_env env = {
    .view = f_view, .home = f_home, .progress = f_progress, .saver_state = f_state,
    .art = f_art, .track = f_track, .quick = f_quick, .info = f_info, .menu = f_menu,
};

static player_view_t v;
static player_evidence_t e;

static void zero(void){ c_view = c_home = c_progress = c_state = c_art = c_track = c_quick = c_info = c_menu = 0; }
static void track(const char *path, const char *title, const char *artist){
    memset(&v, 0, sizeof v); memset(&e, 0, sizeof e);
    v.have_track = 1; v.seq = 1;
    snprintf(v.path, sizeof v.path, "%s", path);
    snprintf(v.title, sizeof v.title, "%s", title);
    snprintf(v.artist, sizeof v.artist, "%s", artist);
}
static void prime(void){
    nowplay_invalidate();
    nowplay_publish(&env, &v, &e, 1, 1, 1);
    zero();
}
static void publish(int playing){
    nowplay_publish(&env, &v, &e, playing, 1, 0);
}
static void expect(int view, int home, int progress, int state, int art, int trk, int quick, int info, int menu){
    if(c_view != view || c_home != home || c_progress != progress || c_state != state ||
       c_art != art || c_track != trk || c_quick != quick || c_info != info || c_menu != menu){
        fprintf(stderr, "counts %d %d %d %d %d %d %d %d %d\n",
                c_view, c_home, c_progress, c_state, c_art, c_track, c_quick, c_info, c_menu);
        assert(0);
    }
}

static void first_publish_calls_every_sink(void){
    track("/a.flac", "A", "X"); nowplay_invalidate(); zero();
    publish(1);
    expect(1,1,1,1,1,1,1,1,1);
}
static void same_view_is_ignored(void){
    track("/a.flac", "A", "X"); prime();
    publish(1);
    expect(0,0,0,0,0,0,0,0,0);
}
static void sequence_change_updates_without_touching_art(void){
    track("/a.flac", "A", "X"); prime();
    v.seq++;
    publish(1);
    expect(1,1,1,1,0,0,1,1,1);
}
static void path_change_updates_art_and_track(void){
    track("/a.flac", "A", "X"); prime();
    v.seq++; snprintf(v.path, sizeof v.path, "/b.flac");
    publish(1);
    expect(1,1,1,1,1,1,1,1,1);
}
static void title_change_updates_track_only(void){
    track("/a.flac", "A", "X"); prime();
    v.seq++; snprintf(v.title, sizeof v.title, "B");
    publish(1);
    expect(1,1,1,1,0,1,1,1,1);
}
static void playing_change_updates_without_art(void){
    track("/a.flac", "A", "X"); prime();
    publish(0);
    expect(1,1,1,1,0,0,1,1,1);
}
static void invisible_defers_the_update(void){
    track("/a.flac", "A", "X"); prime();
    v.seq++;
    nowplay_publish(&env, &v, &e, 1, 0, 0);
    expect(0,0,0,0,0,0,0,0,0);
    publish(1);
    expect(1,1,1,1,0,0,1,1,1);
}
static void invalidate_forces_the_next_publish(void){
    track("/a.flac", "A", "X"); prime();
    nowplay_invalidate();
    publish(1);
    expect(1,1,1,1,0,0,1,1,1);
}
static void force_republishes_the_same_view(void){
    track("/a.flac", "A", "X"); prime();
    nowplay_publish(&env, &v, &e, 1, 1, 1);
    expect(1,1,1,1,0,0,1,1,1);
}

int main(void){
    first_publish_calls_every_sink();
    same_view_is_ignored();
    sequence_change_updates_without_touching_art();
    path_change_updates_art_and_track();
    title_change_updates_track_only();
    playing_change_updates_without_art();
    invisible_defers_the_update();
    invalidate_forces_the_next_publish();
    force_republishes_the_same_view();
    puts("nowplay_test: ok");
    return 0;
}
