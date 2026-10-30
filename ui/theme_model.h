/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef THEME_MODEL_H
#define THEME_MODEL_H
#include <stdint.h>
#include "theme_roles.h"

#ifndef THEME_CUSTOM_PATH
#define THEME_CUSTOM_PATH     "/usr/data/diskos-custom-theme.json"
#endif
#define THEME_CUSTOM_MAX      8192
#define THEME_NAME_MAX        24

typedef struct {
    int      revision;
    char     name[THEME_NAME_MAX + 1];
    int      accent_fixed;
    uint32_t pal[2][THEME_CLR_COUNT];
} theme_model_t;

enum { THEME_OK = 0, THEME_E_INVALID = -1, THEME_E_CONFLICT = -2, THEME_E_IO = -3,
       THEME_E_NOT_DURABLE = -4,
       THEME_E_ABSENT = -5 };
#define THEME_REV_REPLACE_DAMAGED (-2)
#define THEME_REV_MAX 1000000000

const char *theme_role_key(int role);
int  theme_model_validate(const theme_model_t *m, char *err, int errcap);
int  theme_model_parse(const char *json, int len, theme_model_t *m, char *err, int errcap);
int  theme_model_serialize(const theme_model_t *m, char *out, int cap);
int  theme_validate_and_save(const char *json, int len, int expect_revision, const char *path, char *err, int errcap);
int  theme_model_load(const char *path, theme_model_t *m, char *err, int errcap);

#endif
