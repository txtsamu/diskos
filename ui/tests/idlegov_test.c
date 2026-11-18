/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "idlegov.h"
#include <assert.h>
#include <stdio.h>

static void tmap_values(void){
    assert(idlegov_saver_secs(0) == 0);
    assert(idlegov_saver_secs(1) == 30);
    assert(idlegov_saver_secs(2) == 60);
    assert(idlegov_saver_secs(3) == 120);
    assert(idlegov_saver_secs(4) == 300);
    assert(idlegov_saver_secs(-3) == 60);
    assert(idlegov_saver_secs(5) == 60);
    assert(idlegov_screenoff_secs(0) == 0);
    assert(idlegov_screenoff_secs(1) == 30);
    assert(idlegov_screenoff_secs(2) == 60);
    assert(idlegov_screenoff_secs(3) == 120);
    assert(idlegov_screenoff_secs(4) == 300);
    assert(idlegov_screenoff_secs(-1) == 120);
    assert(idlegov_screenoff_secs(7) == 120);
}

static void boot_state(void){
    idlegov_t g;
    idlegov_boot(&g, 24, 0, 24, 100);
    assert(g.bl_state == 0 && g.pk.seen == 24 && !g.pk.player_off && g.pk.off_seq == 0);
    assert(g.last_activity == 100 && !g.manual_sleep && !g.inject_pending && !g.poweroff_inhibit);
    assert(g.scr_seq_seen == 0 && g.aa03_seq_seen == 0 && g.gen_seen == 0);
    idlegov_boot(&g, 0, 0, 31, 200);
    assert(g.pk.seen == 31 && !g.pk.player_off);
    idlegov_boot(&g, 0, 1, 31, 300);
    assert(g.bl_state == 2 && g.pk.seen == 0 && g.pk.player_off);
    idlegov_boot(&g, -1, 0, 31, 400);
    assert(g.pk.seen == -1 && !g.pk.player_off);
}

static void powerkey_step_transitions(void){
    idlegov_keys_t k = { 20, 0, 5 };
    assert(idlegov_powerkey_step(&k, -1, 24, 7, 0) == IDLEGOV_PK_NONE);
    assert(idlegov_powerkey_step(&k, 20, 24, 7, 0) == IDLEGOV_PK_NONE);
    assert(idlegov_powerkey_step(&k, 24, 24, 7, 0) == IDLEGOV_PK_NONE);
    assert(idlegov_powerkey_step(&k, 30, 24, 7, 0) == IDLEGOV_PK_RELEVEL);
    assert(idlegov_powerkey_step(&k, 0, 24, 7, 0) == IDLEGOV_PK_OFF);
    assert(k.player_off == 1 && k.off_seq == 7 && k.seen == 0);
    assert(idlegov_powerkey_step(&k, 0, 24, 9, 1) == IDLEGOV_PK_NONE);
    assert(idlegov_powerkey_step(&k, 24, 24, 9, 0) == IDLEGOV_PK_NONE);
    assert(k.player_off == 1);
    assert(idlegov_powerkey_step(&k, 25, 26, 9, 0) == IDLEGOV_PK_WAKE);
    assert(k.player_off == 0);
    assert(idlegov_powerkey_step(&k, 26, 26, 9, 0) == IDLEGOV_PK_NONE);
    assert(idlegov_powerkey_step(&k, 27, 26, 9, 0) == IDLEGOV_PK_RELEVEL);

    idlegov_keys_t k2 = { 12, 0, 3 };
    assert(idlegov_powerkey_step(&k2, 0, 12, 3, 0) == IDLEGOV_PK_OFF);
    assert(idlegov_powerkey_step(&k2, 12, 12, 3, 0) == IDLEGOV_PK_WAKE);

    idlegov_keys_t k3 = { 12, 0, 0 };
    assert(idlegov_powerkey_step(&k3, 0, 12, 4, 1) == IDLEGOV_PK_NONE);
    assert(k3.player_off == 1 && k3.off_seq == 4);
}

static void powerkey_pass_adopts_and_moves(void){
    idlegov_t g;
    idlegov_boot(&g, 24, 0, 24, 0);
    g.pk.seen = -1;
    assert(idlegov_powerkey_pass(&g, 0, -1, 99, 0, 7) == IDLEGOV_PK_NONE);
    assert(g.pk.seen == 0 && g.pk.player_off == 1 && g.pk.off_seq == 7);
    assert(idlegov_powerkey_pass(&g, 24, 40, 7, 0, 8) == IDLEGOV_PK_WAKE);
    assert(g.pk.player_off == 0 && g.bl_state == 0);
    assert(idlegov_powerkey_pass(&g, 0, 40, 8, 0, 9) == IDLEGOV_PK_OFF);
    assert(g.bl_state == 2);
    assert(idlegov_powerkey_pass(&g, 0, 40, 8, 1, 9) == IDLEGOV_PK_NONE);
    assert(g.bl_state == 2);
    assert(idlegov_powerkey_pass(&g, 30, 40, 9, 0, 10) == IDLEGOV_PK_WAKE);
    assert(idlegov_powerkey_pass(&g, 31, 40, 10, 0, 11) == IDLEGOV_PK_RELEVEL);
    assert(g.bl_state == 0);
}

static void screen_frame_step_pure(void){
    int po = 5;
    assert(idlegov_screen_frame_step(1, 0, &po, 0) == IDLEGOV_SF_WAKE && po == 0);
    assert(idlegov_screen_frame_step(0, 0, &po, 0) == IDLEGOV_SF_OFF && po == 1);
    po = 0;
    assert(idlegov_screen_frame_step(0, 2, &po, 0) == IDLEGOV_SF_INJECT && po == 1);
    po = 1;
    assert(idlegov_screen_frame_step(0, 2, &po, 0) == IDLEGOV_SF_OFF && po == 1);
    po = 0;
    assert(idlegov_screen_frame_step(0, 2, &po, 1) == IDLEGOV_SF_NONE && po == 1);
    assert(idlegov_screen_frame_step(0, 1, &po, 0) == IDLEGOV_SF_OFF);
}

static void screen_poll_frames(void){
    idlegov_t g; idlegov_screen_out_t o;
    idlegov_boot(&g, 24, 0, 24, 100);

    idlegov_screen(&g, 1, 10, 0, 5, 200, &o);
    assert(o.note_input == 1 && o.act == IDLEGOV_SF_WAKE && g.bl_state == 0);
    assert(!g.poweroff_inhibit && g.scr_seq_seen == 10 && g.pk.player_off == 0);

    idlegov_screen(&g, 1, 10, 0, 5, 210, &o);
    assert(o.note_input == 0 && o.act == IDLEGOV_SF_NONE);

    idlegov_screen(&g, 0, 11, 0, 5, 220, &o);
    assert(o.note_input == 1 && o.act == IDLEGOV_SF_OFF && g.bl_state == 2 && g.pk.player_off == 1);

    g.pk.player_off = 0;
    idlegov_screen(&g, 0, 12, 0, 5, 230, &o);
    assert(o.note_input == 1 && o.act == IDLEGOV_SF_NONE && g.inject_pending == 1);
    assert(g.inject_for == 12 && g.inject_gen == 5 && g.inject_due == 230 + IDLEGOV_INJECT_DELAY_MS);
    assert(!idlegov_inject_pending(&g, 229 + IDLEGOV_INJECT_DELAY_MS));
    assert(idlegov_inject_pending(&g, 230 + IDLEGOV_INJECT_DELAY_MS));
    assert(idlegov_inject_take(&g, 0, 12, 0, 5) == 1);
    assert(g.inject_pending == 0);

    g.pk.player_off = 0;
    idlegov_screen(&g, 0, 13, 0, 5, 240, &o);
    assert(g.inject_pending == 1 && g.inject_for == 13);
    idlegov_screen(&g, 1, 14, 0, 5, 250, &o);
    assert(g.inject_pending == 0 && g.scr_seq_seen == 14 && g.bl_state == 0);
}

static void inject_gates(void){
    idlegov_t g; idlegov_screen_out_t o;
    idlegov_boot(&g, 24, 0, 24, 100);
    idlegov_screen(&g, 1, 10, 0, 5, 200, &o);
    idlegov_screen(&g, 0, 11, 0, 5, 210, &o);
    g.pk.player_off = 0;
    idlegov_screen(&g, 0, 12, 0, 5, 220, &o);
    assert(g.inject_pending == 1);

    assert(!idlegov_inject_pending(&g, 519));
    assert(idlegov_inject_pending(&g, 520) == 1);
    assert(idlegov_inject_take(&g, 1, 12, 0, 5) == 0);
    assert(g.inject_pending == 0);

    g.pk.player_off = 0;
    idlegov_screen(&g, 0, 13, 0, 5, 600, &o);
    assert(g.inject_pending == 1 && g.inject_for == 13);
    idlegov_screen(&g, 0, 13, 9, 5, 610, &o);
    assert(g.poweroff_inhibit == 1 && g.inject_pending == 1 && g.scr_seq_seen == 13);
    assert(idlegov_inject_pending(&g, 1200) == 1);
    assert(idlegov_inject_take(&g, 0, 13, 9, 5) == 0);
    assert(g.inject_pending == 0);

    g.pk.player_off = 0;
    idlegov_screen(&g, 0, 14, 9, 5, 1000, &o);
    assert(g.inject_pending == 0 && g.scr_seq_seen == 14);
    idlegov_screen(&g, 0, 20, 0, 6, 1010, &o);
    assert(g.gen_seen == 6 && !g.poweroff_inhibit && g.pk.player_off == 1);
    assert(g.inject_pending == 1 && g.inject_for == 20 && g.inject_gen == 6);
    assert(idlegov_inject_pending(&g, 1010 + IDLEGOV_INJECT_DELAY_MS) == 1);
    assert(idlegov_inject_take(&g, 0, 20, 0, 6) == 1);
}

static void blpoll_cadence(void){
    idlegov_t g;
    idlegov_boot(&g, 24, 0, 24, 0);
    assert(idlegov_blpoll_due(&g, 249) == 0);
    assert(idlegov_blpoll_due(&g, 250) == 1);
    assert(g.last_blpoll == 250);
    assert(idlegov_blpoll_due(&g, 499) == 0);
    assert(idlegov_blpoll_due(&g, 500) == 1);
    assert(g.last_blpoll == 500);
    assert(idlegov_blpoll_due(&g, 100000) == 1);
}

static void foreign_relevel_gates(void){
    idlegov_t g;
    idlegov_boot(&g, 24, 0, 24, 0);
    g.bl_state = 0;
    assert(idlegov_foreign_relevel(&g, 0, 24) == IDLEGOV_RL_NONE);
    assert(idlegov_foreign_relevel(&g, -1, 24) == IDLEGOV_RL_NONE);
    assert(idlegov_foreign_relevel(&g, 24, 24) == IDLEGOV_RL_NONE);
    assert(idlegov_foreign_relevel(&g, 30, 24) == IDLEGOV_RL_FULL);
    g.bl_state = 1;
    assert(idlegov_foreign_relevel(&g, 30, 24) == IDLEGOV_RL_DIM);
    g.bl_state = 2;
    assert(idlegov_foreign_relevel(&g, 30, 24) == IDLEGOV_RL_NONE);
    g.bl_state = 0; g.pk.player_off = 1;
    assert(idlegov_foreign_relevel(&g, 30, 24) == IDLEGOV_RL_NONE);
}

static void touch_wake_gates(void){
    idlegov_t g;
    idlegov_boot(&g, 24, 0, 24, 0);
    assert(idlegov_touch_wake(&g, 0, 500) == 0);
    g.bl_state = 0;
    assert(idlegov_touch_wake(&g, 1, 500) == 0);
    g.bl_state = 1;
    assert(idlegov_touch_wake(&g, 1, 500) == 1);
    assert(g.bl_state == 0 && g.last_activity == 500);
    g.bl_state = 1; g.pk.player_off = 1;
    assert(idlegov_touch_wake(&g, 1, 600) == 0);
    assert(g.bl_state == 1);
}

static void saver_ladder_transitions(void){
    idlegov_t g; idlegov_saver_out_t o;
    idlegov_boot(&g, 24, 0, 24, 0);
    int saver = idlegov_saver_secs(2);
    int off = idlegov_screenoff_secs(3);

    idlegov_saver(&g, 59999, 0, 1, saver, off, &o);
    assert(!o.show_saver && !o.dim && !o.off);
    idlegov_saver(&g, 60000, 0, 1, saver, off, &o);
    assert(!o.show_saver && !o.dim && !o.off);
    idlegov_saver(&g, 60001, 0, 1, saver, off, &o);
    assert(o.show_saver && o.dim && !o.off && g.bl_state == 1);
    idlegov_saver(&g, 180000, 1, 1, saver, off, &o);
    assert(!o.dim && !o.off && g.bl_state == 1);
    idlegov_saver(&g, 180001, 1, 1, saver, off, &o);
    assert(!o.dim && o.off && g.bl_state == 2);

    idlegov_t g2; idlegov_boot(&g2, 24, 0, 24, 0);
    idlegov_saver(&g2, 61000, 0, 0, saver, off, &o);
    assert(!o.show_saver && o.dim && g2.bl_state == 1);
    idlegov_saver(&g2, 181000, 1, 0, saver, off, &o);
    assert(o.off);

    idlegov_t g3; idlegov_boot(&g3, 24, 0, 24, 0);
    idlegov_saver(&g3, 100, 1, 1, saver, off, &o);
    assert(!o.show_saver && o.dim && !o.off);

    idlegov_t g4; idlegov_boot(&g4, 24, 0, 24, 0);
    int off0 = idlegov_screenoff_secs(0);
    idlegov_saver(&g4, 60001, 0, 1, saver, off0, &o);
    assert(o.show_saver && o.dim);
    idlegov_saver(&g4, 1000000, 1, 1, saver, off0, &o);
    assert(!o.off && g4.bl_state == 1);
}

static void manual_sleep_ladder(void){
    idlegov_t g; idlegov_saver_out_t o;
    idlegov_boot(&g, 24, 0, 24, 0);
    idlegov_request_sleep(&g, 1000);
    int saver0 = idlegov_saver_secs(0);
    int off0 = idlegov_screenoff_secs(0);
    idlegov_saver(&g, 1000, 1, 0, saver0, off0, &o);
    assert(!o.show_saver && o.dim && !o.off && g.bl_state == 1);
    assert(g.manual_sleep == 1);
    idlegov_saver(&g, 10999, 1, 0, saver0, off0, &o);
    assert(!o.off);
    idlegov_saver(&g, 11001, 1, 0, saver0, off0, &o);
    assert(o.off && g.bl_state == 2);

    idlegov_t g2; idlegov_boot(&g2, 24, 0, 24, 0);
    idlegov_request_sleep(&g2, 2000);
    idlegov_saver(&g2, 2000, 0, 1, 0, 0, &o);
    assert(g2.manual_sleep == 0 && !o.dim && !o.off);

    idlegov_t g3; idlegov_boot(&g3, 24, 0, 24, 0);
    idlegov_request_sleep(&g3, 3000);
    idlegov_saver(&g3, 3000, 1, 1, 0, 25, &o);
    assert(o.dim && !o.off);
    idlegov_saver(&g3, 28000, 1, 1, 0, 25, &o);
    assert(!o.off);
    idlegov_saver(&g3, 28001, 1, 1, 0, 25, &o);
    assert(o.off && g3.bl_state == 2);
}

int main(void){
    tmap_values();
    boot_state();
    powerkey_step_transitions();
    powerkey_pass_adopts_and_moves();
    screen_frame_step_pure();
    screen_poll_frames();
    inject_gates();
    blpoll_cadence();
    foreign_relevel_gates();
    touch_wake_gates();
    saver_ladder_transitions();
    manual_sleep_ladder();
    puts("idlegov_test: ok");
    return 0;
}
