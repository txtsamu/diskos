/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_SDIO_H
#define DISKOS_SDIO_H

void sd_io_init(int (*media_local)(void));
int sd_io_begin(void);
void sd_io_end(void);
void sd_io_hold(void);
unsigned sd_io_active(void);
int sd_io_resume(void);
int sd_io_allowed(void);
void sd_io_fault(void);
int sd_io_healthy(void);

#endif
