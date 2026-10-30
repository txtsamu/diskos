/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef CONFIG_H
#define CONFIG_H

void        cfg_load(void);
int         cfg_get_int(const char *key, int def);
int         cfg_set_int(const char *key, int v);
int         cfg_set_int_deferred(const char *key, int v);
int         cfg_flush(void);
void        cfg_begin(void);
int         cfg_commit(void);
const char *cfg_get_str(const char *key, const char *def);
int         cfg_set_str(const char *key, const char *v);
int         cfg_set_str_deferred(const char *key, const char *v);
int         cfg_take_save_error(void);
int         cfg_remove_if(int (*match)(const char *key));

#endif
