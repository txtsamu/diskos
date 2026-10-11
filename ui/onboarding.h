/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ONBOARDING_H
#define ONBOARDING_H
enum { HINT_MENU, HINT_NAV, HINT_LIBRARY, HINT_MUSIC, HINT_COUNT };
void onboarding_start(void);
void onboarding_screen(int screen, int nav_open);
void onboarding_library(int actionable);
void onboarding_discovered(int hint);
void onboarding_dismiss(void);
void onboarding_mode_changed(void);
int onboarding_reset(void);
#endif
