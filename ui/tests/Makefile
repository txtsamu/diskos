# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
CC ?= cc
CFLAGS := -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
TESTS_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
TMP := .tmp
TMPABS := $(TESTS_DIR)/.tmp
CPT_TMP := /tmp/diskos-cpt-$(shell printf %s '$(TESTS_DIR)' | md5sum | cut -c1-8)

TESTS += admit_test
admit_test_SRCS := ../admit.c ../bootstate.c ../outroute.c ../outseq.c ../playreq.c ../playerset.c ../session.c admit_test.c
admit_test_DEPS := ../admit.h ../outroute.h ../outseq.h ../session.h

TESTS += booksess_test
booksess_test_SRCS := ../booksess.c booksess_test.c
booksess_test_DEPS := ../booksess.h ../ipc.h

TESTS += bootlog_test
bootlog_test_SRCS := ../bootlog.c bootlog_test.c
bootlog_test_DEPS := ../bootlog.h
bootlog_test_DEFS := -D_GNU_SOURCE -DBOOTLOG_TEST_DIR='"$(TMP)/bootlog_test/bootlog"' \
  -DBOOTLOG_MAIN_PATH='"$(TMP)/bootlog_test/bootlog/main.log"' -DBOOTLOG_JOURNAL_PATH='"$(TMP)/bootlog_test/bootlog/journal.log"' \
  -DBOOTLOG_DMESG_PATH='"$(TMP)/bootlog_test/bootlog/dmesg.log"' \
  -DBOOTLOG_DMESG_CMD='"printf \"line1\\nline2\\nline3\\nline4\\nline5\\n\""' \
  -DBOOTLOG_MAIN_MAX=4096 -DBOOTLOG_JOURNAL_MAX=8192 -DBOOTLOG_DMESG_MAX=65536
bootlog_test_PRE := mkdir -p $(TMP)/bootlog_test/bootlog

TESTS += bootstate_test
bootstate_test_SRCS := ../bootstate.c bootstate_test.c
bootstate_test_DEPS := ../bootstate.h ../ipc.h

TESTS += card_test
card_test_SRCS := ../card.c ../sdfs.c ../rmguard.c ../bootlog.c card_test.c
card_test_DEPS := ../card.h ../sdfs.h ../rmguard.h ../bootlog.h
card_test_DEFS := -D_GNU_SOURCE -DCARD_TEST_ROOT='"$(TMPABS)/card_test/card"' -DCARD_PROC_ROOT='"$(TMPABS)/card_test/card/proc"' \
  -DCARD_MNT='"$(TMPABS)/card_test/card/mnt"' -DCARD_DEV='"$(TMPABS)/card_test/card/dev"' -DCARD_DEV_WHOLE='"$(TMPABS)/card_test/card/disk"' \
  -DCARD_UEVENT='"$(TMPABS)/card_test/card/uevent"' -DCARD_GADGET_DIR='"$(TMPABS)/card_test/card/gadget"' \
  -DCARD_LOG_PATH='"$(TMPABS)/card_test/card/coldplug.log"' -DBOOTLOG_MAIN_PATH='"$(TMPABS)/card_test/card/boot.log"' \
  -DBOOTLOG_JOURNAL_PATH='"$(TMPABS)/card_test/card/journal.log"' -DBOOTLOG_DMESG_PATH='"$(TMPABS)/card_test/card/dmesg.log"'
card_test_FLAGS := -pthread

TESTS += cardclose_test
cardclose_test_SRCS := ../cardclose.c cardclose_test.c
cardclose_test_DEPS := ../cardclose.h

TESTS += clicks_test
clicks_test_SRCS := ../clicks.c clicks_test.c
clicks_test_DEPS := ../clicks.h ../transport.h

TESTS += config_test
config_test_SRCS := ../config.c config_test.c
config_test_DEPS := ../config.h
config_test_DEFS := -DTDIR='"$(TMP)/config_test"' -DCFG_PATH='"$(TMP)/config_test/diskos.conf"' -DCFG_DIR='"$(TMP)/config_test"' \
  -DLEGACY_CONF='"$(TMP)/config_test/nolegacy1"' -DLEGACY_SWIPE='"$(TMP)/config_test/nolegacy2"'

TESTS += coverart_pipeline_test
coverart_pipeline_test_SRCS := ../coverart.c ../coverart_accent.c ../art.c ../artcache.c coverart_pipeline_test.c
coverart_pipeline_test_DEPS := coverart_synth.h ../coverart.h $(TMP)/artdec_fake
coverart_pipeline_test_DEFS := -D_GNU_SOURCE -DCA_TMP='"$(CPT_TMP)"' \
  -DCOVERART_SHARP_OUT='"$(CPT_TMP)/vinyl-%u.bgra"' \
  -DCOVERART_PW_COVER='"$(CPT_TMP)/pw_cover.bmp"' \
  -DCOVERART_PW_THUMB='"$(CPT_TMP)/pw_thumb.bmp"' \
  -DCOVERART_PW_BACKDROP='"$(CPT_TMP)/pw_backdrop.bmp"' -DARTDEC_PATH='"$(TMP)/artdec_fake"' \
  -DCACHE_ROOT='"$(CPT_TMP)/sd/.diskos/artcache"' \
  -DARTCACHE_BASE_DIR='"$(CPT_TMP)/sd/.diskos"' -DARTCACHE_FREE_PATH='"$(CPT_TMP)/sd"' \
  -DMIN_FREE_BYTES=4096ULL
coverart_pipeline_test_FLAGS := -ffp-contract=off -Wno-unknown-pragmas -pthread
coverart_pipeline_test_LIBS := -lm
coverart_pipeline_test_PRE := rm -rf $(CPT_TMP) && mkdir -p $(CPT_TMP)/sd/.diskos $(CPT_TMP)/music

TESTS += coverart_test
coverart_test_SRCS := ../coverart.c ../coverart_accent.c coverart_test.c
coverart_test_DEPS := coverart_synth.h ../coverart.h
coverart_test_DEFS := -D_GNU_SOURCE -DCA_TMP='"$(TMP)/coverart_test"' \
  -DCOVERART_SHARP_OUT='"$(TMP)/coverart_test/vinyl-%u.bgra"' \
  -DCOVERART_PW_COVER='"$(TMP)/coverart_test/pw_cover.bmp"' \
  -DCOVERART_PW_THUMB='"$(TMP)/coverart_test/pw_thumb.bmp"' \
  -DCOVERART_PW_BACKDROP='"$(TMP)/coverart_test/pw_backdrop.bmp"'
coverart_test_FLAGS := -ffp-contract=off -Wno-unknown-pragmas -pthread
coverart_test_LIBS := -lm

TESTS += dacinfo_test
dacinfo_test_SRCS := ../dacinfo.c dacinfo_test.c
dacinfo_test_DEPS := ../dacinfo.h

TESTS += dacvol_sys_test
dacvol_sys_test_SRCS := ../dacvol_sys.c ../dacvol.c ../command_spawn.c dacvol_sys_test.c
dacvol_sys_test_DEPS := ../dacvol_sys.h ../dacvol.h
dacvol_sys_test_DEFS := -D_GNU_SOURCE -DDACVOL_TEST_DIR='"$(TMP)/dacvol_sys_test/dacvol"' \
  -DDACVOL_AMIXER='"$(TMP)/dacvol_sys_test/dacvol/amixer"' \
  -DDACVOL_KEY_SYSFS='"$(TMP)/dacvol_sys_test/dacvol/sys/event%d/name"' \
  -DDACVOL_KEY_DEV='"$(TMP)/dacvol_sys_test/dacvol/dev/event%d"'
dacvol_sys_test_FLAGS := -pthread
dacvol_sys_test_LIBS := -lm

TESTS += dacvol_test
dacvol_test_SRCS := ../dacvol.c dacvol_test.c
dacvol_test_DEPS := ../dacvol.h ../ipc.h
dacvol_test_LIBS := -lm

TESTS += idlegov_test
idlegov_test_SRCS := ../idlegov.c idlegov_test.c
idlegov_test_DEPS := ../idlegov.h

TESTS += ipc_guard_test
ipc_guard_test_SRCS := ../ipc.c ipc_guard_test.c
ipc_guard_test_DEPS := ../ipc.h
ipc_guard_test_FLAGS := -pthread

TESTS += ipc_tx_test
ipc_tx_test_SRCS := ipc_tx_test.c
ipc_tx_test_DEPS := ../ipc.c ../ipc.h
ipc_tx_test_FLAGS := -pthread

TESTS += keyssh_test
keyssh_test_SRCS := ../procnet.c keyssh_test.c
keyssh_test_DEPS := ../procnet.h

TESTS += nowplay_test
nowplay_test_SRCS := ../nowplay.c nowplay_test.c
nowplay_test_DEPS := ../nowplay.h ../ipc.h

TESTS += outroute_test
outroute_test_SRCS := ../outroute.c ../outseq.c ../playerset.c outroute_test.c
outroute_test_DEPS := ../outroute.h ../outseq.h ../playerset.h
outroute_test_DEFS := -D_GNU_SOURCE -DOUT_USB_CARD='"$(TMP)/outroute_test/outroute/card1"' \
  -DOUT_USB_ANY_CARD='"$(TMP)/outroute_test/outroute/card*/usbid"' \
  -DROUTE_TEST_ROOT='"$(TMP)/outroute_test/outroute"' -DROUTE_STATE_DIR='"$(TMP)/outroute_test/outroute/state"' \
  -DROUTE_STATE_PATH='"$(TMP)/outroute_test/outroute/state/route"' \
  -DROUTE_STATE_UID=$(shell id -u) -DROUTE_PROC_ROOT='"$(TMP)/outroute_test/outroute/proc"'

TESTS += outseq_test
outseq_test_SRCS := ../outseq.c outseq_test.c
outseq_test_DEPS := ../outseq.h
outseq_test_POST := python3 -I outseq_guard_test.py

TESTS += playerset_test
playerset_test_SRCS := ../playerset.c playerset_test.c
playerset_test_DEPS := ../playerset.h ../ipc.h

TESTS += playreq_test
playreq_test_SRCS := ../playreq.c playreq_test.c
playreq_test_DEPS := ../playreq.h
playreq_test_DEFS := -D_GNU_SOURCE -DPCM_DIR='"$(TMP)/playreq_test/pcm"' -DPLAY_PCM_GLOB='"$(TMP)/playreq_test/pcm/*/status"'
playreq_test_PRE := mkdir -p $(TMP)/playreq_test/pcm

TESTS += power_test
power_test_SRCS := ../power.c ../cardclose.c power_test.c
power_test_DEPS := ../power.h ../cardclose.h
power_test_DEFS := -DPOWER_CORE_ONLY

TESTS += radio_test
radio_test_SRCS := ../radio.c radio_test.c
radio_test_DEPS := ../radio.h

TESTS += restart_test
restart_test_SRCS := ../restart.c ../cardclose.c restart_test.c
restart_test_DEPS := ../restart.h ../cardclose.h

TESTS += sdfs_test
sdfs_test_SRCS := ../sdfs.c sdfs_test.c
sdfs_test_DEPS := ../sdfs.h

TESTS += sdio_test
sdio_test_SRCS := ../sdio.c ../bootstate.c ../session.c ../outroute.c ../outseq.c ../playreq.c ../playerset.c sdio_test.c
sdio_test_DEPS := ../sdio.h ../session.h ../outseq.h
sdio_test_DEFS := -D_GNU_SOURCE -DSD_EXPORT_MARKER='"$(TMP)/sdio_test/sdio_handoff"' -DSD_IO_FAULT_MARKER='"$(TMP)/sdio_test/sdio_fault"'
sdio_test_FLAGS := -pthread

TESTS += session_test
session_test_SRCS := ../session.c ../outroute.c ../outseq.c ../playreq.c ../playerset.c session_test.c
session_test_DEPS := ../session.h ../outroute.h ../outseq.h ../playreq.h

TESTS += shuffle_test
shuffle_test_SRCS := shuffle_test.c
shuffle_test_DEPS := ../shuffle.h

TESTS += srcswitch_test
srcswitch_test_SRCS := ../srcswitch.c ../usbdbg.c ../bootstate.c ../session.c ../outroute.c ../outseq.c ../playreq.c ../sdio.c \
  ../playerset.c srcswitch_test.c
srcswitch_test_DEPS := ../srcswitch.h ../usbdbg.h ../outroute.h ../session.h ../outseq.h
srcswitch_test_DEFS := -D_GNU_SOURCE -DOUT_USB_CARD='"$(TMP)/srcswitch_test/card1"' \
  -DOUT_USB_ANY_CARD='"$(TMP)/srcswitch_test/card*/usbid"' \
  -DSD_EXPORT_MARKER='"$(TMP)/srcswitch_test/handoff"' -DSD_IO_FAULT_MARKER='"$(TMP)/srcswitch_test/fault"'
srcswitch_test_FLAGS := -pthread

TESTS += sysconfig_test
sysconfig_test_SRCS := ../sysconfig.c sysconfig_test.c
sysconfig_test_DEPS := ../sysconfig.h
sysconfig_test_OBJS := sqlite3_host.o
sysconfig_test_LIBS := -lm

TESTS += tama_test
tama_test_SRCS := ../tama.c tama_test.c
tama_test_DEPS := ../tama.h

TESTS += timekeep_test
timekeep_test_SRCS := ../timekeep.c timekeep_test.c
timekeep_test_DEPS := ../timekeep.h

TESTS += transport_test
transport_test_SRCS := ../transport.c ../bootstate.c transport_test.c
transport_test_DEPS := ../transport.h ../ipc.h ../admit.h

TESTS += uiread_test
uiread_test_SRCS := uiread_test.c
uiread_test_DEPS := ../musicdb.c ../musicdb.h ../sysconfig.h ../sdio.h
uiread_test_OBJS := sqlite3_host.o
uiread_test_DEFS := -DUIREAD_DB='"$(TMP)/uiread_test/uiread.db"'
uiread_test_FLAGS := -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
uiread_test_LIBS := -lm -pthread

TESTS += vbus_test
vbus_test_SRCS := ../vbus.c vbus_test.c
vbus_test_DEPS := ../vbus.h
vbus_test_DEFS := -D_GNU_SOURCE -DVBUS_TEST_DIR='"$(TMP)/vbus_test/vbus"' -DVBUS_UDC_STATE_GLOB='"$(TMP)/vbus_test/vbus/*_state"'
vbus_test_PRE := mkdir -p $(TMP)/vbus_test/vbus

TESTS := $(sort $(TESTS))

sqlite3_host.o: ../sqlite3.c ../sqlite3.h
	$(CC) -O1 -w -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -c ../sqlite3.c -o $@

$(TMP)/artdec_fake: artdec_fake.c coverart_synth.h
	@mkdir -p $(TMP)
	$(CC) -g -O1 -Wall -Wextra -o $@ artdec_fake.c

define TEST_RULE
$(1): $(sort $($(1)_SRCS)) $(sort $($(1)_DEPS)) $($(1)_OBJS)
	$$(CC) $$(CFLAGS) $($(1)_FLAGS) $($(1)_DEFS) -I.. $(sort $($(1)_SRCS)) $($(1)_OBJS) $($(1)_LIBS) -o $$@
endef
$(foreach t,$(TESTS),$(eval $(call TEST_RULE,$(t))))

.PHONY: run run-py check clean
.DEFAULT_GOAL := run
run: $(TESTS)
	@set -e; $(foreach t,$(TESTS),echo "--- $(t:%_test=%) ---";rm -rf $(TMP)/$(t);mkdir -p $(TMP)/$(t);$(if $($(t)_PRE),$($(t)_PRE);)./$(t);$(if $($(t)_POST),$($(t)_POST);))

clean:
	rm -f $(TESTS) sqlite3_host.o
	rm -rf $(TMP) $(CPT_TMP)

PY_COMPLETE := boot_probe boot_select font_coverage gadget_list launcher settings_tree storage
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
