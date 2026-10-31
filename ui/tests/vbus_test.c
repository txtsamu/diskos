/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "vbus.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)

static void udc(const char *name, const char *state){
    char p[300];
    snprintf(p, sizeof p, "%s/%s", VBUS_TEST_DIR, name);
    FILE *f = fopen(p, "w");
    assert(f);
    if(state) fputs(state, f);
    fclose(f);
}
static void wipe(void){
    char c[400];
    snprintf(c, sizeof c, "rm -rf %s && mkdir -p %s", VBUS_TEST_DIR, VBUS_TEST_DIR);
    assert(system(c) == 0);
}

static void test_stat_external(void){
    CHECK(!vbus_stat_external(-1), "an unreadable status is not an external input");
    CHECK(!vbus_stat_external(VBUS_STAT_NONE), "0 is no input");
    for(int s = 1; s <= 6; s++) CHECK(vbus_stat_external(s), "1..6 is an external source feeding the Disc");
    CHECK(!vbus_stat_external(VBUS_STAT_OTG), "7 is the Disc sourcing power itself (USB Audio), not a charger");
}

static void test_plain_charger(void){
    wipe();
    CHECK(vbus_plain_charger() == 1, "no UDC at all counts as a plain charger");
    udc("a_state", "not attached\n");
    CHECK(vbus_plain_charger() == 1, "not attached is a plain charger");
    udc("b_state", "configured\n");
    CHECK(vbus_plain_charger() == 0, "a configured host is not a plain charger");
    udc("b_state", "powered\n");
    CHECK(vbus_plain_charger() == 0, "any state other than not attached is not plain");
    udc("b_state", "not attached\n");
    CHECK(vbus_plain_charger() == 1, "back to not attached");
}

int main(void){
    test_stat_external();
    test_plain_charger();
    if(fails){ printf("FAIL (%d failures)\n", fails); return 1; }
    printf("PASS (0 failures)\n");
    return 0;
}
