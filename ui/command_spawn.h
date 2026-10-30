/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef COMMAND_SPAWN_H
#define COMMAND_SPAWN_H
#include <sys/types.h>
int command_spawn(pid_t *pid, char *const argv[]);
int command_spawn_io(pid_t *pid, char *const argv[], int out_fd, int pass_fd);
int command_wait(char *const argv[], int timeout_ms);
#endif
