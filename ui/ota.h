/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_OTA_H
#define DISKOS_OTA_H
#include <stddef.h>
#include <stdatomic.h>

typedef struct {
    const char *updates;
    const char *rootpub;
    const char *epochs;
    const char *verifier;
    const char *latest_url;
    const char *dl_base;
    const char *data_root;
    const char *operstate;
    const char *run_file;
    const char *launched_file;
    const char *pcm_glob;
    const char *self_exe;
} ota_paths_t;
extern const ota_paths_t OTA_DEV;

enum { OTA_OK = 0, OTA_E_CANCEL, OTA_E_NET, OTA_E_SPACE, OTA_E_UNSUPPORTED, OTA_E_SIG, OTA_E_OLDER, OTA_E_BAD, OTA_E_IO, OTA_E_OFF, OTA_E_REJECTED };

#define OTA_NFILES 6
#define OTA_MAX_UI (32LL * 1024 * 1024)
#ifndef OTA_STALL_S
#define OTA_STALL_S 40
#endif

typedef struct {
    _Atomic int cancel;
    _Atomic int phase;
    _Atomic long done, total;
    long long need_free;
    char version[40];
} ota_prog_t;

int  ota_asset_name(int i, const char **asset, const char **local);
int  ota_ver_parse(const char *s, int allow_v, int v[3]);
int  ota_ver_cmp(const char *a, int a_v, const char *b, int b_v);
int  ota_assets_ok(const char *json);
int  ota_fetch_release(const ota_paths_t *p, char *tag, size_t n, int *has_bundle);
int  ota_wifi_down(const ota_paths_t *p);
int  ota_supported(const ota_paths_t *p);
int  ota_stage(const ota_paths_t *p, const char *running, const char *tag, ota_prog_t *pg);
int  ota_allowed(void);
int  ota_discard_pending(const ota_paths_t *p);
int  ota_pending(const ota_paths_t *p, char *ver, size_t n);
int  ota_trial_version(const ota_paths_t *p, char *ver, size_t n);
int  ota_trial_decided(const ota_paths_t *p);
int  ota_pcm_running(const ota_paths_t *p);
int  ota_health_write(const ota_paths_t *p);
int  ota_revert_write(const ota_paths_t *p);
int  ota_rollback_take(const ota_paths_t *p);
const char *ota_err_text(int code);
#endif
