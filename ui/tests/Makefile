# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
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

session_test: ../session.c ../outseq.c ../playreq.c ../playstate.c session_test.c ../session.h ../outseq.h ../playreq.h
	$(CC) $(CFLAGS) -I.. ../session.c ../outseq.c ../playreq.c ../playstate.c session_test.c -o $@

srcswitch_test: ../srcswitch.c ../session.c ../outseq.c ../playreq.c ../playstate.c ../sdio.c srcswitch_test.c ../srcswitch.h ../modes.h ../session.h ../outseq.h
	$(CC) $(CFLAGS) -pthread -D_GNU_SOURCE -DOUT_USB_CARD='"$(TDIR)/card1"' -DOUT_USB_ANY_CARD='"$(TDIR)/card*/usbid"' \
	  -DSD_EXPORT_MARKER='"$(TDIR)/handoff"' -DSD_IO_FAULT_MARKER='"$(TDIR)/fault"' -I.. \
	  ../srcswitch.c ../session.c ../outseq.c ../playreq.c ../playstate.c ../sdio.c srcswitch_test.c -o $@

admit_test: ../admit.c ../outseq.c ../session.c ../playreq.c ../playstate.c admit_test.c ../admit.h ../outseq.h ../session.h
	$(CC) $(CFLAGS) -I.. ../admit.c ../outseq.c ../session.c ../playreq.c ../playstate.c admit_test.c -o $@

playreq_test: ../playreq.c ../playstate.c playreq_test.c ../playreq.h ../playstate.h
	$(CC) $(CFLAGS) -D_GNU_SOURCE -DPCM_DIR='"$(TDIR)/pcm"' -DPLAY_PCM_GLOB='"$(TDIR)/pcm/*/status"' -I.. ../playreq.c ../playstate.c playreq_test.c -o $@

dacinfo_test: ../dacinfo.c dacinfo_test.c ../dacinfo.h
	$(CC) $(CFLAGS) -I.. ../dacinfo.c dacinfo_test.c -o $@

timekeep_test: ../timekeep.c timekeep_test.c ../timekeep.h
	$(CC) $(CFLAGS) -I.. ../timekeep.c timekeep_test.c -o $@

tama_test: ../tama.c tama_test.c ../tama.h
	$(CC) $(CFLAGS) -I.. ../tama.c tama_test.c -o $@

clicks_test: ../clicks.c clicks_test.c ../clicks.h
	$(CC) $(CFLAGS) -I.. ../clicks.c clicks_test.c -o $@

transport_test: ../transport.c transport_test.c ../transport.h ../ipc.h ../admit.h
	$(CC) $(CFLAGS) -I.. ../transport.c transport_test.c -o $@

booksess_test: ../booksess.c booksess_test.c ../booksess.h ../ipc.h
	$(CC) $(CFLAGS) -I.. ../booksess.c booksess_test.c -o $@

keyssh_test: ../procnet.c keyssh_test.c ../procnet.h
	$(CC) $(CFLAGS) -I.. ../procnet.c keyssh_test.c -o $@

radio_test: ../radio.c radio_test.c ../radio.h
	$(CC) $(CFLAGS) -I.. ../radio.c radio_test.c -o $@

ipc_guard_test: ../ipc.c ipc_guard_test.c ../ipc.h
	$(CC) $(CFLAGS) -pthread -I.. ../ipc.c ipc_guard_test.c -o $@

ipc_tx_test: ../ipc.c ipc_tx_test.c ../ipc.h
	$(CC) $(CFLAGS) -pthread -I.. ipc_tx_test.c -o $@

sqlite3_host.o: ../sqlite3.c ../sqlite3.h
	$(CC) -O1 -w -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -c ../sqlite3.c -o $@

sysconfig_test: ../sysconfig.c sysconfig_test.c ../sysconfig.h sqlite3_host.o
	$(CC) $(CFLAGS) -I.. ../sysconfig.c sysconfig_test.c sqlite3_host.o -lm -o $@

.PHONY: run run-py check clean
run: sysconfig_test transport_test config_test playstate_test playreq_test ipc_guard_test ipc_tx_test outseq_test session_test srcswitch_test admit_test dacinfo_test timekeep_test tama_test keyssh_test radio_test clicks_test booksess_test
	./config_test
	@echo "--- playstate ---"
	./playstate_test
	./ipc_guard_test
	./ipc_tx_test
	@echo "--- outseq ---"
	./outseq_test
	python3 -I outseq_guard_test.py
	@echo "--- session ---"
	./session_test
	@echo "--- srcswitch ---"
	@mkdir -p $(TDIR) && rm -f $(TDIR)/fault
	./srcswitch_test
	@echo "--- playreq ---"
	@mkdir -p $(TDIR)/pcm && rm -rf $(TDIR)/pcm/*
	./playreq_test
	@echo "--- admit ---"
	./admit_test
	@echo "--- dacinfo ---"
	./dacinfo_test
	@echo "--- timekeep ---"
	./timekeep_test
	@echo "--- tama ---"
	./tama_test
	@echo "--- keyssh ---"
	./keyssh_test
	@echo "--- radio ---"
	./radio_test
	@echo "--- transport ---"
	./transport_test
	@echo "--- clicks ---"
	./clicks_test
	@echo "--- booksess ---"
	./booksess_test
	@echo "--- sysconfig ---"
	./sysconfig_test

clean:
	rm -f sysconfig_test sqlite3_host.o playreq_test transport_test config_test playstate_test ipc_guard_test ipc_tx_test outseq_test session_test srcswitch_test admit_test dacinfo_test timekeep_test tama_test keyssh_test radio_test clicks_test booksess_test

PY_COMPLETE := boot_probe boot_select font_coverage launcher storage
PY_UNITTEST := outseq_guard rmguard
run-py:
	@set -e; for t in $(PY_COMPLETE); do \
	  echo "--- $$t (python) ---"; \
	  out=$$(CC="$(CC)" python3 -I $${t}_test.py) || { echo "$$out"; exit 1; }; \
	  echo "$$out"; \
	  echo "$$out" | tail -n 1 | grep -qx "COMPLETE $${t}_test.py" || { echo "FAIL $$t: did not reach its COMPLETE line"; exit 1; }; \
	done; \
	for t in $(PY_UNITTEST); do echo "--- $$t (python) ---"; CC="$(CC)" python3 -I $${t}_test.py; done

check: run run-py
