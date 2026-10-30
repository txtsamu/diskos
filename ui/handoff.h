/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_HANDOFF_H
#define DISKOS_HANDOFF_H

void handoff_signal_reset(void);

extern int (*handoff_execv)(const char *path, char *const argv[]);
void handoff_exec(const char *path, const char *argv0) __attribute__((noreturn));

#endif
