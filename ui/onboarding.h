/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ONBOARDING_H
#define ONBOARDING_H
enum { HINT_MENU, HINT_NAV, HINT_LIBRARY, HINT_MUSIC, HINT_SHORTCUTS, HINT_LOCK, HINT_BACK, HINT_HOME, HINT_QUICK, HINT_RADIO_HOLD, HINT_SEEK, HINT_PRECISION, HINT_MUSIC_GESTURES, HINT_CLOCK, HINT_VOLUME, HINT_AZ, HINT_FLOW, HINT_IMMERSIVE, HINT_POPUP, HINT_HANDLE, HINT_EQ, HINT_RIM, HINT_BRIGHTNESS, HINT_PICK, HINT_COUNT };
const char *onboarding_gesture_text(int hint);
void onboarding_context(int hint);
void onboarding_start(void);
void onboarding_screen(int screen, int nav_open);
void onboarding_library(int actionable);
void onboarding_discovered(int hint);
void onboarding_dismiss(void);
void onboarding_mode_changed(void);
int onboarding_reset(void);
void onboarding_woke_by_touch(void);   /* a timed-out screen was woken by a touch: once, the power-button tip */
#endif
