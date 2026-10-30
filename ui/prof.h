/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_PROF_H
#define DISKOS_PROF_H
#ifdef DISKOS_PROFILE
void prof_flush(int px);
void prof_loop(int busy, int bl_state, int anims, unsigned long long handler_ns, unsigned long long body_ns);
#endif
#endif
