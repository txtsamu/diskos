/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_DACINFO_H
#define DISKOS_DACINFO_H
#include <stddef.h>

typedef enum { DAC_FMT_UNKNOWN = 0, DAC_FMT_PCM = 1, DAC_FMT_DSD = 2 } dac_fmt_t;

typedef struct {
    int       rate_hz;
    dac_fmt_t fmt;
} dac_stream_t;

int dacinfo_parse_uevent(const char *msg, size_t len, dac_stream_t *s);

int dacinfo_parse_hwparams(const char *text);

void dacinfo_format_rate(char *out, size_t n, int hz);

const char *dacinfo_fmt_name(dac_fmt_t f);

void dacinfo_init(void);

dac_stream_t dacinfo_get(void);

#endif
