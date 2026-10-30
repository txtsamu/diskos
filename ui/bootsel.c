/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "bootsel.h"
#include "handoff.h"
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* S96diskos_select writes exactly "diskos\n" or "stock\n". Returns 1 only for a regular, non-symlink file
 * holding exactly those 7 bytes "diskos\n" - the same rule as payload/diskos-selected, the shell reader
 * used by S97 and the boot hook, so the three consumers cannot disagree about one record. (The shell side
 * needs the exact-size rule because BusyBox ash `read` drops NUL bytes.) Missing, unreadable, FIFO/device,
 * wrong size, NUL-containing or any other content is 0 = stock. Non-blocking open so a FIFO planted at the
 * path cannot stall us. */
int boot_select_is_diskos(const char *path){
    char rec[8] = {0};
    ssize_t n = -1;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if(fd < 0) return 0;
    struct stat st;
    if(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 7)
        n = read(fd, rec, sizeof rec);
    close(fd);
    return n == 7 && memcmp(rec, "diskos\n", 7) == 0;
}

/* Enforce the record. Anything but a positive diskOS record hands this process to the stock UI, and that
 * is TERMINAL: if the stock exec fails we exit rather than run diskOS against the boot's decision.
 * fiio_init's watchdog then sees no mq_ui and respawns the stock UI by its bare name. */
void boot_select_enforce(const char *path){
    if(boot_select_is_diskos(path)) return;
    handoff_signal_reset();                  /* exec KEEPS a pending timer AND a blocked mask -
                                              * neither may cross into the stock UI */
    handoff_exec("/usr/bin/mq_ui", "mq_ui"); /* never returns, never falls through into diskOS */
}
