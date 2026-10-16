/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef QUICKSETTINGS_H
#define QUICKSETTINGS_H
#define QS_MAX_ITEMS 8
typedef struct { const char *code, *name, *icon; } qs_item_t;
const char *quicksettings_item_glyph(int id);
int quicksettings_item_count(void);
const qs_item_t *quicksettings_item(int id);
int quicksettings_items_load(int *items);
int quicksettings_items_save(const int *items, int count);
void disco_qs_menu_open(void);
#endif
