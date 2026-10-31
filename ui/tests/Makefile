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

outroute_test: ../outroute.c ../outseq.c outroute_test.c ../outroute.h ../outseq.h
	$(CC) $(CFLAGS) -D_GNU_SOURCE -DOUT_USB_CARD='"$(TDIR)/outroute/card1"' -DOUT_USB_ANY_CARD='"$(TDIR)/outroute/card*/usbid"' \
	  -DROUTE_TEST_ROOT='"$(TDIR)/outroute"' -DROUTE_STATE_DIR='"$(TDIR)/outroute/state"' -DROUTE_STATE_PATH='"$(TDIR)/outroute/state/route"' \
	  -DROUTE_STATE_UID=$(shell id -u) -DROUTE_PROC_ROOT='"$(TDIR)/outroute/proc"' -I.. ../outroute.c ../outseq.c outroute_test.c -o $@

session_test: ../session.c ../outroute.c ../outseq.c session_test.c ../session.h ../outroute.h ../outseq.h
	$(CC) $(CFLAGS) -I.. ../session.c ../outroute.c ../outseq.c session_test.c -o $@

srcswitch_test: ../srcswitch.c ../session.c ../outroute.c ../outseq.c ../sdio.c srcswitch_test.c ../srcswitch.h ../outroute.h ../session.h ../outseq.h
	$(CC) $(CFLAGS) -pthread -D_GNU_SOURCE -DOUT_USB_CARD='"$(TDIR)/card1"' -DOUT_USB_ANY_CARD='"$(TDIR)/card*/usbid"' \
	  -DSD_EXPORT_MARKER='"$(TDIR)/handoff"' -DSD_IO_FAULT_MARKER='"$(TDIR)/fault"' -I.. \
	  ../srcswitch.c ../session.c ../outroute.c ../outseq.c ../sdio.c srcswitch_test.c -o $@

admit_test: ../admit.c ../outroute.c ../outseq.c ../session.c admit_test.c ../admit.h ../outroute.h ../outseq.h ../session.h
	$(CC) $(CFLAGS) -I.. ../admit.c ../outroute.c ../outseq.c ../session.c admit_test.c -o $@

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

keyssh_test: ../procnet.c keyssh_test.c ../procnet.h
	$(CC) $(CFLAGS) -I.. ../procnet.c keyssh_test.c -o $@

radio_test: ../radio.c radio_test.c ../radio.h
	$(CC) $(CFLAGS) -I.. ../radio.c radio_test.c -o $@

ipc_guard_test: ../ipc.c ipc_guard_test.c ../ipc.h
	$(CC) $(CFLAGS) -pthread -I.. ../ipc.c ipc_guard_test.c -o $@

ipc_tx_test: ../ipc.c ipc_tx_test.c ../ipc.h
	$(CC) $(CFLAGS) -pthread -I.. ipc_tx_test.c -o $@

.PHONY: run run-py check clean
run: transport_test config_test playstate_test ipc_guard_test ipc_tx_test outseq_test outroute_test session_test srcswitch_test admit_test dacinfo_test timekeep_test tama_test keyssh_test radio_test clicks_test
	./config_test
	@echo "--- playstate ---"
	./playstate_test
	./ipc_guard_test
	./ipc_tx_test
	@echo "--- outseq ---"
	./outseq_test
	python3 -I outseq_guard_test.py
	@echo "--- outroute ---"
	@mkdir -p $(TDIR)
	./outroute_test
	@echo "--- session ---"
	./session_test
	@echo "--- srcswitch ---"
	@mkdir -p $(TDIR) && rm -f $(TDIR)/fault
	./srcswitch_test
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

clean:
	rm -f transport_test config_test playstate_test ipc_guard_test ipc_tx_test outseq_test outroute_test session_test srcswitch_test admit_test dacinfo_test timekeep_test tama_test keyssh_test radio_test clicks_test

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
