/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_VBUS_H
#define DISKOS_VBUS_H

#define VBUS_STAT_NONE 0
#define VBUS_STAT_OTG  7

int vbus_stat(void);
int vbus_stat_external(int stat);
int vbus_present(void);
int vbus_external_input(void);
int vbus_plain_charger(void);

#endif
