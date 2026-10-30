/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef FWCAPS_H
#define FWCAPS_H

int fw_os_ver(void);

int fw_needs_localplayer_init(void);
int fw_needs_direct_sd_mount(void);

const char *fw_gain_tag(void);

int fw_custom_peq_writable(void);

int fw_has_radio_boot_arbitration(void);

int fw_fav_play_by_love_id(void);
int fw_power_key_driveable(void);
int fw_artist_class_settable(void);
int fw_folder_jump_settable(void);
int fw_track_display_settable(void);

#endif
