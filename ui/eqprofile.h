/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_EQPROFILE_H
#define DISKOS_EQPROFILE_H
typedef struct {
    int preset, read, graphic, gains[10], hz[10];
    double master, q[10];
} eq_profile_t;
/* One sleeping worker, latest request wins. No SQLite or LVGL on callers. */
unsigned eq_profile_request(int preset);
int eq_profile_take(unsigned ticket, eq_profile_t *out);
#endif
