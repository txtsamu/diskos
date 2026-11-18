/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_CARD_H
#define DISKOS_CARD_H

struct card_env {
    int  (*umount)(const char *target);
    int  (*umount_lazy)(const char *target, int flags);
    int  (*mount)(const char *src, const char *tgt, const char *fstype, unsigned long flags, const void *data);
    void (*unmount_wait)(void);
    int  (*send_source)(int mode);
    int  (*launch_status)(void);
    int  (*fw_direct_mount)(void);
    int  (*source_mode)(void);
    int  (*writable)(void);
    int  (*coldplug_begin)(void);
    void (*coldplug_end)(void);
};

void card_set_env(const struct card_env *env);

int  card_mounted(void);
int  card_exported_to_host(void);
int  card_host_confirmed(void);
int  card_player_guarded(void);
int  card_unmount(void);
int  card_coldplug_step(int i, int direct, int *nudged);
void card_coldplug_start(void);

#endif
