/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_DACVOL_SYS_H
#define DISKOS_DACVOL_SYS_H

#include "dacvol.h"

int  dacvol_sys_mixer_probe(int card);
int  dacvol_sys_mixer_probe_poll(dacvol_ctl_t *out);
int  dacvol_sys_mixer_set(int raw);
int  dacvol_sys_mixer_state(void);
void dacvol_sys_key_active(int on);
int  dacvol_sys_key_poll(int *code, int *value);

#endif
