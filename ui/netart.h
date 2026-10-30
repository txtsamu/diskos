/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef NETART_H
#define NETART_H
#ifndef NETART_DIR
#define NETART_DIR "/tmp/sdcard/.diskos/netart"
#endif

int  netart_have(const char *artist, const char *album, const char *title, char *out, int cap);
void netart_request(const char *track, const char *artist, const char *album, const char *title);
void netart_cancel(void);
int  netart_touch(const char *path);
void netart_reject(const char *path);
int  netart_take_ready(char *track, int cap);
const char *netart_country(int language);
#endif
