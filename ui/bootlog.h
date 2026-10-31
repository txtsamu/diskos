/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_BOOTLOG_H
#define DISKOS_BOOTLOG_H

#ifndef BOOTLOG_MAIN_PATH
#define BOOTLOG_MAIN_PATH "/usr/data/diskos_boot.log"
#endif
#ifndef BOOTLOG_JOURNAL_PATH
#define BOOTLOG_JOURNAL_PATH "/usr/data/diskos_journal.log"
#endif
#ifndef BOOTLOG_DMESG_PATH
#define BOOTLOG_DMESG_PATH "/usr/data/diskos_dmesg.log"
#endif
#ifndef BOOTLOG_DMESG_CMD
#define BOOTLOG_DMESG_CMD "dmesg 2>/dev/null"
#endif

#ifndef BOOTLOG_MAIN_MAX
#define BOOTLOG_MAIN_MAX (192*1024)
#endif
#ifndef BOOTLOG_JOURNAL_MAX
#define BOOTLOG_JOURNAL_MAX (64*1024)
#endif
#ifndef BOOTLOG_DMESG_MAX
#define BOOTLOG_DMESG_MAX (96*1024)
#endif
#ifndef BOOTLOG_KEEP
#define BOOTLOG_KEEP 3
#endif

void bootlog_rotate(const char *path, long max, int keep);
void bootlog_open_stderr(void);
void bootlog_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void bootlog_event_file(const char *label, const char *path);
void bootlog_dmesg(const char *why, int lines);
void bootlog_signal_note(const char *msg, int len);

#endif
