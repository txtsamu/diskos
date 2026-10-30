/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef I18N_H
#define I18N_H

typedef enum { LANG_ZH, LANG_TW, LANG_EN, LANG_JA, LANG_KO, LANG_ES, LANG_PT, LANG_IT, LANG_DE, LANG_FR, LANG_RU,
               LANG_COUNT } i18n_lang_t;

typedef struct { const char *en; const char *text; } i18n_entry_t;
typedef struct { const i18n_entry_t *e; int n; } i18n_table_t;

extern const char *const i18n_lang_names[LANG_COUNT];
extern const i18n_table_t i18n_tables[LANG_COUNT];

int         i18n_lang(void);
const char *i18n_lookup(int lang, const char *en);
const char *tr(const char *en);
const char *tr_sym(const char *sym, const char *en);
int         i18n_needs_fallback_font(void);

#endif
