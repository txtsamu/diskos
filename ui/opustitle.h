/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef OPUSTITLE_H
#define OPUSTITLE_H

typedef enum {
    OPUS_TITLE_FOUND, OPUS_TITLE_ABSENT, OPUS_TITLE_NOT_APPLICABLE, OPUS_TITLE_ERROR
} opus_title_outcome;
typedef enum {
    OPUS_TITLE_NONE, OPUS_TITLE_IO, OPUS_TITLE_MALFORMED, OPUS_TITLE_LIMIT,
    OPUS_TITLE_VALUE, OPUS_TITLE_CHANGED, OPUS_TITLE_CANCELLED
} opus_title_reason;
typedef struct {
    opus_title_outcome outcome;
    opus_title_reason reason;
    unsigned retryable;
    char title[256];
} opus_title_result;
opus_title_result opus_title_read(const char *path, int (*cancel)(void *), void *ctx);
#endif
