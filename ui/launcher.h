/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_LAUNCHER_H
#define DISKOS_LAUNCHER_H

/* The stock Player launcher. The Player's executable name is a symlink to this binary: started under that name, diskOS
 * verifies the SD delete guard and local ownership of the card, publishes the verdict for the UI, and ALWAYS execs the
 * stock player (owner rule: diskOS may switch off its own features, never the user's player). The UI reads the verdict
 * back with launch_status_get(). No LVGL. The things only main.c can do come in through this table, set once. */
struct launcher_env {
    int  (*sd_exported_to_host)(void);                       /* a computer may own the card (fail-closed) */
    int  (*run_bounded)(char *const argv[], int timeout_ms); /* an external command as a killable child; 0 = it succeeded */
    int  (*rmguard_ensure)(void);                            /* the delete guard is in place and first in PATH (rmguard.h) */
    void (*kill_bounded)(void);                              /* kill any run_bounded child; async-signal-safe */
};
void launcher_set_env(const struct launcher_env *env);

/* Invalidate the old verdict, arm the whole-path deadline, check guard and local ownership (one retry), publish the
 * verdict and note any failure in the boot log. Always returns; the caller then calls exec_stock_player(). */
void player_launch_prepare(void);

/* The ONE place that hands off to the stock player: kills any preparation child, normalises signal state, execs
 * /usr/bin/mq_player. Never returns (_exit(127) if the exec fails). */
void exec_stock_player(void) __attribute__((noreturn));

/* 1 = THIS player's launch was confirmed; 0 = reported unprotected/unconfirmed; -1 = not published, malformed, or published
 * by a player that is no longer the one running. */
int launch_status_get(void);

/* Local ownership of the card confirmed (not exported to a host; WORK_MODE reset and read back)? Separate from the guard. */
int player_local_mode_confirmed(void);

/* A fresh first run: no player config database AND confirmed no card (ENOENT, not just any stat failure). */
int player_config_absent_without_card(void);

#endif
