/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dacinfo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define EV(...) ((const char[]){ __VA_ARGS__ })

static int feed(dac_stream_t *s, const char *raw, size_t len){ return dacinfo_parse_uevent(raw, len, s); }

static void test_pcm_stream(void){
    static const char m[] = "change@/devices/virtual/usb_gadget\0ACTION=change\0SUBSYSTEM=usb_gadget\0UAC_SRATE=96000\0UAC_FORMAT=PCM\0SEQNUM=812";
    dac_stream_t s = {0, DAC_FMT_UNKNOWN};
    assert(feed(&s, m, sizeof m) == 1);
    assert(s.rate_hz == 96000 && s.fmt == DAC_FMT_PCM);
}

static void test_dsd_stream(void){
    static const char m[] = "change@/x\0UAC_SRATE=2822400\0UAC_FORMAT=DSD\0";
    dac_stream_t s = {0, DAC_FMT_UNKNOWN};
    assert(feed(&s, m, sizeof m) == 1);
    assert(s.rate_hz == 2822400 && s.fmt == DAC_FMT_DSD);
}

static void test_stop_clears_format(void){
    static const char run[]  = "change@/x\0UAC_SRATE=48000\0UAC_FORMAT=PCM\0";
    static const char stop[] = "change@/x\0UAC_SRATE=0\0UAC_FORMAT=UNKNOW\0";
    dac_stream_t s = {0, DAC_FMT_UNKNOWN};
    feed(&s, run, sizeof run);
    assert(s.rate_hz == 48000 && s.fmt == DAC_FMT_PCM);
    assert(feed(&s, stop, sizeof stop) == 1);
    assert(s.rate_hz == 0 && s.fmt == DAC_FMT_UNKNOWN);
}

static void test_rate_alone_keeps_format(void){
    static const char run[]  = "change@/x\0UAC_SRATE=44100\0UAC_FORMAT=PCM\0";
    static const char rate[] = "change@/x\0UAC_SRATE=192000\0";
    dac_stream_t s = {0, DAC_FMT_UNKNOWN};
    feed(&s, run, sizeof run);
    assert(feed(&s, rate, sizeof rate) == 1);
    assert(s.rate_hz == 192000 && s.fmt == DAC_FMT_PCM);
}

static void test_unrelated_uevents_are_ignored(void){
    static const char m[] = "add@/devices/platform/usb\0ACTION=add\0SUBSYSTEM=usb\0DEVTYPE=usb_device\0SEQNUM=1";
    dac_stream_t s = {48000, DAC_FMT_PCM};
    assert(feed(&s, m, sizeof m) == 0);
    assert(s.rate_hz == 48000 && s.fmt == DAC_FMT_PCM);
}

static void test_garbage(void){
    dac_stream_t s = {0, DAC_FMT_UNKNOWN};
    assert(feed(&s, "", 0) == 0);
    static const char junk[] = "UAC_SRATE=abc\0UAC_FORMAT=\0";
    assert(feed(&s, junk, sizeof junk) == 1);
    assert(s.rate_hz == 0 && s.fmt == DAC_FMT_UNKNOWN);
    static const char big[] = "UAC_SRATE=99999999999999\0";
    feed(&s, big, sizeof big);
    assert(s.rate_hz == 0);
    char tail[] = {'U','A','C','_','S','R','A','T','E','=','4','8','0','0','0'};
    s.rate_hz = 0; feed(&s, tail, sizeof tail);
    assert(s.rate_hz == 48000);
}

static void test_hwparams(void){
    const char run[] = "access: RW_INTERLEAVED\nformat: S32_LE\nsubformat: STD\nchannels: 2\nrate: 48000 (48000/1)\nperiod_size: 960\nbuffer_size: 3840\n";
    assert(dacinfo_parse_hwparams(run) == 48000);
    assert(dacinfo_parse_hwparams("closed\n") == 0);
    assert(dacinfo_parse_hwparams("") == 0);
    assert(dacinfo_parse_hwparams("rate: 176400 (176400/1)\n") == 176400);
}

static void test_wording(void){
    char b[32];
    dacinfo_format_rate(b, sizeof b, 48000);    assert(!strcmp(b, "48 kHz"));
    dacinfo_format_rate(b, sizeof b, 44100);    assert(!strcmp(b, "44.1 kHz"));
    dacinfo_format_rate(b, sizeof b, 88200);    assert(!strcmp(b, "88.2 kHz"));
    dacinfo_format_rate(b, sizeof b, 176400);   assert(!strcmp(b, "176.4 kHz"));
    dacinfo_format_rate(b, sizeof b, 384000);   assert(!strcmp(b, "384 kHz"));
    dacinfo_format_rate(b, sizeof b, 2822400);  assert(!strcmp(b, "2.82 MHz"));
    dacinfo_format_rate(b, sizeof b, 0);        assert(!strcmp(b, "-"));
    dacinfo_format_rate(b, 4, 48000);           assert(strlen(b) == 3);
    assert(!strcmp(dacinfo_fmt_name(DAC_FMT_PCM), "PCM"));
    assert(!strcmp(dacinfo_fmt_name(DAC_FMT_DSD), "DSD"));
    assert(!strcmp(dacinfo_fmt_name(DAC_FMT_UNKNOWN), "-"));
}

int main(void){
    test_pcm_stream(); test_dsd_stream(); test_stop_clears_format(); test_rate_alone_keeps_format();
    test_unrelated_uevents_are_ignored(); test_garbage(); test_hwparams(); test_wording();
    puts("dacinfo_test: ok");
    return 0;
}
