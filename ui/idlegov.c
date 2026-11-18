/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "idlegov.h"

static const int TMAP[IDLEGOV_TMAP_N] = {0,30,60,120,300};

int idlegov_saver_secs(int idx){ return (idx >= 0 && idx < IDLEGOV_TMAP_N) ? TMAP[idx] : 60; }
int idlegov_screenoff_secs(int idx){ return (idx >= 0 && idx < IDLEGOV_TMAP_N) ? TMAP[idx] : 120; }

void idlegov_boot(idlegov_t *g, int boot_br, int startup_deferred, int written, uint32_t now){
    g->bl_state = startup_deferred ? 2 : 0;
    g->pk.seen = boot_br; g->pk.player_off = (boot_br == 0); g->pk.off_seq = 0;
    if(boot_br == 0 && !startup_deferred){ g->pk.seen = written; g->pk.player_off = 0; }
    g->scr_seq_seen = g->aa03_seq_seen = g->gen_seen = g->inject_for = g->inject_gen = 0;
    g->poweroff_inhibit = g->inject_pending = 0;
    g->inject_due = 0;
    g->last_blpoll = 0;
    g->last_activity = now;
    g->manual_sleep = 0;
    g->manual_sleep_at = 0;
}

void idlegov_request_sleep(idlegov_t *g, uint32_t now){
    g->manual_sleep = 1;
    g->manual_sleep_at = now;
}

int idlegov_screen_frame_step(int on, int bl_state, int *player_off, int poweroff_inhibit){
    if(on){ *player_off = 0; return IDLEGOV_SF_WAKE; }
    int own_off = bl_state == 2 && !*player_off;
    *player_off = 1;
    if(own_off) return poweroff_inhibit ? IDLEGOV_SF_NONE : IDLEGOV_SF_INJECT;
    return IDLEGOV_SF_OFF;
}

void idlegov_screen(idlegov_t *g, int on, unsigned sq, unsigned pq, unsigned gen, uint32_t now, idlegov_screen_out_t *out){
    out->act = IDLEGOV_SF_NONE;
    out->note_input = 0;
    if(gen != g->gen_seen){
        g->gen_seen = gen; g->scr_seq_seen = g->aa03_seq_seen = 0;
        g->poweroff_inhibit = g->inject_pending = 0; g->pk.player_off = 0;
    }
    if(pq != g->aa03_seq_seen){ g->aa03_seq_seen = pq; g->poweroff_inhibit = 1; }
    if(on < 0 || sq == g->scr_seq_seen) return;
    g->scr_seq_seen = sq;
    g->inject_pending = 0;
    out->note_input = 1;
    switch(idlegov_screen_frame_step(on, g->bl_state, &g->pk.player_off, g->poweroff_inhibit)){
    case IDLEGOV_SF_INJECT:
        g->inject_pending = 1; g->inject_for = sq; g->inject_gen = gen;
        g->inject_due = now + IDLEGOV_INJECT_DELAY_MS;
        break;
    case IDLEGOV_SF_OFF:
        g->bl_state = 2;
        out->act = IDLEGOV_SF_OFF;
        break;
    case IDLEGOV_SF_WAKE:
        g->bl_state = 0;
        out->act = IDLEGOV_SF_WAKE;
        break;
    default: break;
    }
}

int idlegov_inject_pending(const idlegov_t *g, uint32_t now){
    return g->inject_pending && (int32_t)(now - g->inject_due) >= 0;
}

int idlegov_inject_take(idlegov_t *g, int on, unsigned sq, unsigned pq, unsigned gen){
    g->inject_pending = 0;
    return gen == g->inject_gen && sq == g->inject_for && on == 0 && pq == g->aa03_seq_seen && !g->poweroff_inhibit;
}

int idlegov_blpoll_due(idlegov_t *g, uint32_t now){
    if((uint32_t)(now - g->last_blpoll) < IDLEGOV_BLPOLL_MS) return 0;
    g->last_blpoll = now;
    return 1;
}

int idlegov_foreign_relevel(const idlegov_t *g, int cbr, int written){
    if(!(cbr > 0 && cbr != written && !g->pk.player_off)) return IDLEGOV_RL_NONE;
    if(g->bl_state == 0) return IDLEGOV_RL_FULL;
    if(g->bl_state == 1) return IDLEGOV_RL_DIM;
    return IDLEGOV_RL_NONE;
}

int idlegov_powerkey_step(idlegov_keys_t *k, int cbr, int ours, unsigned ours_seq, int dark){
    if(cbr < 0 || cbr == k->seen) return IDLEGOV_PK_NONE;
    k->seen = cbr;
    if(cbr == 0){ k->player_off = 1; k->off_seq = ours_seq; return dark ? IDLEGOV_PK_NONE : IDLEGOV_PK_OFF; }
    if(k->player_off){
        if(cbr == ours && ours_seq != k->off_seq) return IDLEGOV_PK_NONE;
        k->player_off = 0; return IDLEGOV_PK_WAKE;
    }
    if(cbr == ours) return IDLEGOV_PK_NONE;
    return IDLEGOV_PK_RELEVEL;
}

int idlegov_powerkey_pass(idlegov_t *g, int cbr, int ours, unsigned ours_seq, int dark, unsigned write_seq){
    if(g->pk.seen < 0){
        g->pk.seen = cbr; g->pk.player_off = (cbr == 0); g->pk.off_seq = write_seq;
        return IDLEGOV_PK_NONE;
    }
    int act = idlegov_powerkey_step(&g->pk, cbr, ours, ours_seq, dark);
    if(act == IDLEGOV_PK_OFF)  g->bl_state = 2;
    if(act == IDLEGOV_PK_WAKE) g->bl_state = 0;
    return act;
}

int idlegov_touch_wake(idlegov_t *g, int any, uint32_t now){
    if(!any || !g->bl_state || g->pk.player_off) return 0;
    g->last_activity = now;
    g->bl_state = 0;
    return 1;
}

void idlegov_saver(idlegov_t *g, uint32_t now, int in_saver, int saver_ok, int saver_secs, int off_secs, idlegov_saver_out_t *out){
    out->show_saver = out->dim = out->off = 0;
    if(g->manual_sleep && !in_saver) g->manual_sleep = 0;
    int manual = g->manual_sleep;
    if(saver_secs > 0 || manual){
        uint32_t idle = manual ? now - g->manual_sleep_at : now - g->last_activity;
        int past_dim = (saver_secs > 0 && idle > (uint32_t)saver_secs*1000);
        if(past_dim && !in_saver && g->bl_state==0 && (saver_ok || manual)){
            out->show_saver = 1; in_saver = 1;
        }
        if((in_saver || past_dim || manual) && g->bl_state==0){
            out->dim = 1; g->bl_state = 1;
        }
        uint32_t off_at = manual ? (uint32_t)(off_secs > 0 ? off_secs : 10)*1000
                                 : (uint32_t)(saver_secs+off_secs)*1000;
        if(g->bl_state==1 && (manual || off_secs > 0) && idle > off_at){
            out->off = 1; g->bl_state = 2;
        }
    }
}
