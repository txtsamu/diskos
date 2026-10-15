/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISCO_MENU_APPS_H
#define DISCO_MENU_APPS_H
/* Stable identity is the app's installation directory, matching Shortcuts. */
/* app.conf may also set tagline=, about= and button= for its landing page. */
typedef struct {char id[33],name[64],exec[256],tagline[64],about[160],button[32],icon[256];} menu_app_t;   /* icon: a BMP (app.conf icon=, else icon.bmp), "" = none */
void menu_apps_scan(void);
int menu_apps_count(void);
const menu_app_t *menu_apps_get(int index);
int menu_apps_decode(char code,const char *builtins);
int menu_apps_save(const int *items,int count,const char *builtins);
int menu_apps_decode_named(char code,const char *builtins,const char *prefix);
int menu_apps_save_named(const int *items,int count,const char *builtins,const char *config_key,const char *prefix);
#endif
