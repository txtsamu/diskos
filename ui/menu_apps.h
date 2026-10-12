/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISCO_MENU_APPS_H
#define DISCO_MENU_APPS_H
/* Stable identity is the app's installation directory, matching Shortcuts. */
typedef struct {char id[33],name[64],exec[256];} menu_app_t;
void menu_apps_scan(void);
int menu_apps_count(void);
const menu_app_t *menu_apps_get(int index);
int menu_apps_decode(char code,const char *builtins);
int menu_apps_save(const int *items,int count,const char *builtins);
#endif
