/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_HANDOFF_H
#define DISKOS_HANDOFF_H

/* Handing this process to another program: the signal state that must be normalised first, and the exec itself. Used by
 * the boot-select enforcement (bootsel.c), the player launcher (launcher.c) and the UI re-exec in main.c. No LVGL. */

/* Normalise signal state before ANY exec. Three separate problems, all of which outlive the exec:
 *  - a pending alarm is INHERITED, so our watchdog could fire inside the program we hand off to;
 *  - alarm(0) does not discard a signal that has ALREADY been generated, so we set SIG_IGN and unblock,
 *    which discards it, before restoring the default;
 *  - SIG_IGN itself is inherited across exec, so the disposition must end at SIG_DFL, and the blocked
 *    mask must be cleared (a handler runs with its own signal blocked, so exec'ing from inside one
 *    would otherwise hand the stock binary a blocked SIGALRM).
 * Every call here is async-signal-safe, so this is valid from a handler as well as the normal path. */
void handoff_signal_reset(void);

/* exec path with argv = { argv0, NULL }; if the exec fails, _exit(127) so the firmware watchdog respawns stock. Never
 * returns. Async-signal-safe. The exec itself goes through handoff_execv, which a host test points at a fake (the seam). */
extern int (*handoff_execv)(const char *path, char *const argv[]);
void handoff_exec(const char *path, const char *argv0) __attribute__((noreturn));

#endif
