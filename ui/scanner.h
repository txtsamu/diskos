/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef SCANNER_H
#define SCANNER_H
enum { OLD_TAGS_AUTO, OLD_TAGS_WESTERN, OLD_TAGS_CYRILLIC };
void scanner_set_old_tags(int mode);
void scanner_abort(void);
int  scanner_cancel(void);
enum { SCAN_OUT_NONE, SCAN_OUT_COMMITTED, SCAN_OUT_CANCELLED, SCAN_OUT_FAILED, SCAN_OUT_NO_SD };
int  scanner_outcome(void);
int  scanner_start(void);
int  scanner_active(void);
void scanner_progress(int *done, int *total);
int  scanner_take_finished(void);
int  scanner_take_committed(void);
int  scanner_no_sd(void);
int  scanner_skipped(void);
int  scanner_unsupported(void);

#define CHAP_TITLE 128
typedef struct { long start_ms; char title[CHAP_TITLE]; } chapter_t;
int  scan_read_chapters(const char *path, chapter_t *out, int max);
int  scan_cue_files(const char *cue_path, char (*out)[1024], int max, int (*seen)(const char *));
int  scan_read_narrator(const char *path, char *out, int cap);
#endif
