# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# Off-device host tests for diskOS pure-logic modules (arch#5 harness).
# Native x86-64 build with sanitizers + CFG_* paths pointed at a temp dir. Does NOT touch the
# device build (separate dir, separate Makefile). Run: make -C tests run
CC ?= cc
CFLAGS := -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
TDIR := /tmp/diskos_cfgtest
DEFS := -DCFG_PATH='"$(TDIR)/diskos.conf"' -DCFG_DIR='"$(TDIR)"' \
        -DLEGACY_CONF='"$(TDIR)/nolegacy1"' -DLEGACY_SWIPE='"$(TDIR)/nolegacy2"'

config_test: ../config.c config_test.c ../config.h
	$(CC) $(CFLAGS) $(DEFS) -I.. ../config.c config_test.c -o $@

playstate_test: ../playstate.c playstate_test.c ../playstate.h
	$(CC) $(CFLAGS) -I.. ../playstate.c playstate_test.c -o $@

outseq_test: ../outseq.c outseq_test.c ../outseq.h
	$(CC) $(CFLAGS) -I.. ../outseq.c outseq_test.c -o $@

timekeep_test: ../timekeep.c timekeep_test.c ../timekeep.h
	$(CC) $(CFLAGS) -I.. ../timekeep.c timekeep_test.c -o $@

ipc_guard_test: ipc_guard_test.c
	sed -n '/^int ipc_frame_is_dangerous/,/^}/p' ../ipc.c > ipc_guard_fn.c.tmp
	(echo '#include <string.h>'; echo '#include <stdlib.h>'; cat ipc_guard_fn.c.tmp) > ipc_guard_fn.c
	$(CC) $(CFLAGS) ipc_guard_test.c ipc_guard_fn.c -o $@
	rm -f ipc_guard_fn.c.tmp ipc_guard_fn.c

.PHONY: run clean
run: config_test playstate_test ipc_guard_test outseq_test timekeep_test
	./config_test
	@echo "--- playstate ---"
	./playstate_test
	./ipc_guard_test
	@echo "--- outseq ---"
	./outseq_test
	python3 -I outseq_guard_test.py
	@echo "--- timekeep ---"
	./timekeep_test

clean:
	rm -f config_test playstate_test ipc_guard_test outseq_test timekeep_test
