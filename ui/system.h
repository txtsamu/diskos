/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_SYSTEM_H
#define DISKOS_SYSTEM_H
#include <stddef.h>
#include <sys/types.h>

#define SYS_UPDATE_PRODUCT   "SNOWSKY_DISC"
#ifndef DISKOS_OTA_REPO
#define DISKOS_OTA_REPO      "b0hemia/diskos"
#endif
#ifndef SYS_GITHUB_LATEST
#define SYS_GITHUB_LATEST    "https://api.github.com/repos/" DISKOS_OTA_REPO "/releases/latest"
#endif
#ifndef SYS_GITHUB_DOWNLOAD
#define SYS_GITHUB_DOWNLOAD  "https://github.com/" DISKOS_OTA_REPO "/releases/download"
#endif

typedef struct {
    const char *version_in;
    const char *wlan_mac;
    const char *bt_mac;
    const char *batt_dir;
    const char *sd_root;
    const char *data_root;
    const char *mounts;
} sys_paths_t;
extern const sys_paths_t SYS_DEV_PATHS;

typedef struct { char label[20]; char value[64]; } sys_about_row_t;
int  sys_about_collect(const sys_paths_t *p, const char *diskos_id, sys_about_row_t *rows, int max);
int  sys_is_mounted(const char *mounts, const char *dir);
int  sys_kv(const char *text, const char *key, char *out, size_t n);
void sys_fw_label(int ver, char *buf, size_t n);
void sys_fmt_bytes(unsigned long long b, char *buf, size_t n);

typedef struct { char path[320]; char name[128]; int version; long long size; } sys_update_t;
int  sys_update_name_ok(const char *name);
int  sys_update_version(const char *name);
int  sys_find_update(const char *dir, sys_update_t *out);
int  sys_read_battery(const sys_paths_t *p);
unsigned long long sys_sd_free(const sys_paths_t *p);

int  sys_parse_tag(const char *json, char *out, size_t n);
int  sys_ver_cmp(const char *a, const char *b);
int  sys_fetch_latest(char *tag, size_t n);

int  sys_reset_match(const char *key);
int  sys_reset_settings(void);

#ifndef SYSTEM_CORE_ONLY
void system_about_open(int v);
void system_update_sd_open(int v);
void system_check_update_open(int v);
void system_ota_allow_changed(int v);
void settings_row_values_refresh(void);
void system_reset_open(int v);
void system_ota_note_playback(void);
void system_ota_health_init(void);
#endif
#endif
