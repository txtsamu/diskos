/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "dacinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>

/* The value of "KEY=" in the entry kv[0..l) (not NUL-terminated at the end of a message), copied bounded and terminated. */
static int entry_value(const char *kv, size_t l, const char *key, char *out, size_t outn){
    size_t kl = strlen(key);
    if(l < kl || memcmp(kv, key, kl)) return 0;
    size_t vl = l - kl;
    if(vl >= outn) vl = outn - 1;
    memcpy(out, kv + kl, vl); out[vl] = 0;
    return 1;
}

int dacinfo_parse_uevent(const char *msg, size_t len, dac_stream_t *s){
    int hit = 0;
    for(size_t i = 0; i < len; ){
        const char *kv = msg + i;
        size_t l = strnlen(kv, len - i);
        char v[24];
        if(entry_value(kv, l, "UAC_SRATE=", v, sizeof v)){
            long hz = strtol(v, NULL, 10);
            s->rate_hz = hz > 0 && hz < 100000000 ? (int)hz : 0;
            if(!s->rate_hz) s->fmt = DAC_FMT_UNKNOWN;   /* the host stopped: the old verdict no longer applies */
            hit = 1;
        } else if(entry_value(kv, l, "UAC_FORMAT=", v, sizeof v)){
            /* the Player's log shows "PCM" and "UNKNOW"; the DSD spelling is not captured yet, so match its prefix */
            s->fmt = !strncmp(v, "PCM", 3) ? DAC_FMT_PCM : !strncmp(v, "DSD", 3) ? DAC_FMT_DSD : DAC_FMT_UNKNOWN;
            hit = 1;
        }
        i += l + 1;
    }
    return hit;
}

int dacinfo_parse_hwparams(const char *text){
    const char *p = strstr(text, "rate:");
    if(!p) return 0;
    long hz = strtol(p + 5, NULL, 10);
    return hz > 0 && hz < 100000000 ? (int)hz : 0;
}

void dacinfo_format_rate(char *out, size_t n, int hz){
    if(hz <= 0) snprintf(out, n, "-");
    else if(hz >= 1000000) snprintf(out, n, "%.2f MHz", hz / 1000000.0);
    else if(hz % 1000 == 0) snprintf(out, n, "%d kHz", hz / 1000);
    else snprintf(out, n, "%.1f kHz", hz / 1000.0);
}

const char *dacinfo_fmt_name(dac_fmt_t f){ return f == DAC_FMT_PCM ? "PCM" : f == DAC_FMT_DSD ? "DSD" : "-"; }

/* ---- system side ------------------------------------------------------------------------------------------------ */
static int g_sock = -1;
static dac_stream_t g_uev;   /* what the uevents said */
static int g_uev_seen;       /* at least one UAC uevent arrived since the UI started */

void dacinfo_init(void){
    if(g_sock >= 0) return;
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if(fd < 0) return;
    struct sockaddr_nl sa; memset(&sa, 0, sizeof sa);
    sa.nl_family = AF_NETLINK; sa.nl_groups = 1;   /* group 1 = the kernel's own uevents */
    int rcv = 128 * 1024; setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
    if(bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0){ close(fd); return; }
    g_sock = fd;
}

static void drain(void){
    if(g_sock < 0) return;
    char buf[2048];
    for(;;){
        struct sockaddr_nl from; socklen_t fl = sizeof from;
        ssize_t n = recvfrom(g_sock, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if(n < 0){
            if(errno == ENOBUFS){ g_uev_seen = 0; g_uev.fmt = DAC_FMT_UNKNOWN; continue; }   /* overflowed: the verdict may be stale */
            break;   /* EAGAIN: empty */
        }
        if(from.nl_pid != 0) continue;   /* only the kernel */
        if(dacinfo_parse_uevent(buf, (size_t)n, &g_uev)) g_uev_seen = 1;
    }
}

/* The gadget's sound card is "FIIOUAC2Gadget" (card1 on the device); look it up rather than assume the number. */
static int alsa_rate(void){
    for(int c = 0; c < 8; c++){
        char path[96], id[48] = {0};
        snprintf(path, sizeof path, "/proc/asound/card%d/id", c);
        FILE *f = fopen(path, "r");
        if(!f) continue;
        if(!fgets(id, sizeof id, f)) id[0] = 0;
        fclose(f);
        if(strncmp(id, "FIIOUAC2Gadget", 14)) continue;
        snprintf(path, sizeof path, "/proc/asound/card%d/pcm0c/sub0/hw_params", c);
        f = fopen(path, "r");
        if(!f) return 0;
        char txt[256]; size_t n = fread(txt, 1, sizeof txt - 1, f); fclose(f);
        txt[n] = 0;
        return dacinfo_parse_hwparams(txt);
    }
    return 0;
}

dac_stream_t dacinfo_get(void){
    drain();
    dac_stream_t s = g_uev;
    if(!g_uev_seen) s.rate_hz = alsa_rate();   /* no uevent since the UI started: ALSA still knows the rate */
    return s;
}
