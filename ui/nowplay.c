/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "nowplay.h"
#include <stdio.h>
#include <string.h>

static unsigned g_seq;
static int      g_playing;
static int      g_primed;
static char     g_path[256], g_title[IPC_TITLE_CAPACITY], g_artist[160];

void nowplay_invalidate(void){ g_primed = 0; }

void nowplay_publish(const struct nowplay_env *env, const player_view_t *view,
                     const player_evidence_t *ev, int playing, int visible, int force){
    if(!visible) return;
    if(g_primed && !force && view->seq == g_seq && playing == g_playing) return;
    g_seq = view->seq; g_playing = playing; g_primed = 1;

    env->view(view, ev, playing);
    env->home(view, playing);
    env->progress(view, playing);
    env->saver_state(playing);

    const char *path = view->have_track ? view->path : "";
    const char *title = view->have_track ? ipc_display_title(view) : "";
    const char *artist = view->have_track ? view->artist : "";
    int art_changed = strcmp(path, g_path) != 0;
    int song_changed = strcmp(title, g_title) != 0 || strcmp(artist, g_artist) != 0;

    if(art_changed){
        snprintf(g_path, sizeof g_path, "%s", path);
        env->art(view);
    }
    if(art_changed || song_changed){
        snprintf(g_title, sizeof g_title, "%s", title);
        snprintf(g_artist, sizeof g_artist, "%s", artist);
        env->track(view, playing);
    }
    env->quick(playing);
    env->info(view);
    env->menu(view, playing);
}
