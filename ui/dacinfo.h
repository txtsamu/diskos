/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_DACINFO_H
#define DISKOS_DACINFO_H
#include <stddef.h>

/* USB DAC stream info (GLOSSARY.md: Source mode "USB DAC", the Disc as a sound card for a computer or phone).
 * Two sources, no Player command involved:
 *   - the kernel's UAC gadget uevents (NETLINK_KOBJECT_UEVENT group 1), the same ones the Player's own listener logs as
 *     "UAC_SRATE=48000" / "UAC_FORMAT: PCM". They carry the PCM / DSD verdict; nothing in sysfs does.
 *   - ALSA's capture side of the gadget card (/proc/asound/card<N>/pcm0c/sub0/hw_params, card id "FIIOUAC2Gadget"), read
 *     when no uevent was seen yet (the UI started after the host began streaming), for the rate only.
 * No LVGL. The parsers are pure (host tested in tests/dacinfo_test.c); only dacinfo_init()/dacinfo_get() touch the system. */

typedef enum { DAC_FMT_UNKNOWN = 0, DAC_FMT_PCM = 1, DAC_FMT_DSD = 2 } dac_fmt_t;

typedef struct {
    int       rate_hz;   /* 0 = no stream (the host has not started, or stopped) */
    dac_fmt_t fmt;       /* DAC_FMT_UNKNOWN until a uevent said PCM or DSD (or after the host stopped) */
} dac_stream_t;

/* Fold one raw uevent message (NUL-separated "KEY=value" strings, as the kernel sends it) into *s.
 * UAC_SRATE=<hz> sets the rate (0 = stopped, which also clears the format); UAC_FORMAT=PCM|DSD sets the format
 * (anything else, e.g. the firmware's "UNKNOW", clears it). Returns 1 when the message carried either key, else 0. */
int dacinfo_parse_uevent(const char *msg, size_t len, dac_stream_t *s);

/* The rate in Hz from the text of an ALSA hw_params file ("rate: 48000 (48000/1)"); 0 when it says "closed" or has no rate. */
int dacinfo_parse_hwparams(const char *text);

/* "48 kHz", "44.1 kHz", "176.4 kHz", "2.82 MHz" (>= 1 MHz: native DSD rates); "-" for 0. */
void dacinfo_format_rate(char *out, size_t n, int hz);

/* "PCM", "DSD" or "-" (not known yet). */
const char *dacinfo_fmt_name(dac_fmt_t f);

/* Open the uevent socket (idempotent, non-blocking, failure is silent: the screen then falls back to ALSA). Call once at
 * startup so a stream that begins before the app is opened is not missed. */
void dacinfo_init(void);

/* Drain pending uevents and return the current stream. */
dac_stream_t dacinfo_get(void);

#endif
