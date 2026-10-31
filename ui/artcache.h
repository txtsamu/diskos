/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef ARTCACHE_H
#define ARTCACHE_H

int  artcache_get(const char *track, const char *cover_out, const char *thumb_out, const char *bg_out);

void artcache_put(const char *track, const char *cover, const char *thumb, const char *bg);
int  artcache_key(const char *track, char *out, int cap);
void artcache_put_if(const char *track, const char *fp_before, const char *cover, const char *thumb, const char *bg);

int  artcache_has(const char *track);
int  artcache_lookup(const char *track, char *key_out, int cap);

int  artcache_get_thumb(const char *track, const char *thumb_out);
int  artcache_get_cover(const char *track, const char *cover_out);
int  artcache_cover_path(const char *track, char *out, int cap);

unsigned artcache_gen(void);

#endif
