# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
-include config.mk
APP   := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
NUI   ?= $(APP)
LVGL  ?= $(APP)/lvgl
CROSS ?= mipsel-linux-musl-
CC    := $(CROSS)gcc

REPRO_FLAGS := -ffile-prefix-map=$(APP)/= -ffile-prefix-map=$(LVGL)/=lvgl/
FP_FLAGS := -ffp-contract=off
CFLAGS := -Os -pipe -std=gnu11 -static -pthread -D_GNU_SOURCE -DLV_CONF_INCLUDE_SIMPLE \
  -ffunction-sections -fdata-sections \
  -I$(APP) -I$(NUI) -Wall -MMD -MP $(REPRO_FLAGS) $(FP_FLAGS)
CFLAGS += $(EXTRA_CFLAGS)
CFLAGS += -I$(NUI)/third_party/freetype-2.13.3/include
LDFLAGS := -static -pthread -Wl,--gc-sections
LDLIBS  := $(NUI)/third_party/out/mipsel/libfreetype.a -lm -lrt

LVGL_SRCS := $(sort $(shell find $(LVGL)/src -name "*.c"))
APP_SRCS  := $(APP)/phonehome.c $(APP)/main.c $(APP)/sdio.c $(APP)/fontft.c $(APP)/font_cjk_extra_16.c $(APP)/playstate.c $(APP)/fb_pan.c $(APP)/ipc.c $(APP)/ui.c $(APP)/art.c $(APP)/screenmgr.c $(APP)/home.c $(APP)/library.c $(APP)/musicdb.c $(APP)/settings.c $(APP)/power.c $(APP)/controls.c $(APP)/config.c $(APP)/anim.c $(APP)/search.c $(APP)/saver.c $(APP)/quicksettings.c $(APP)/songinfo.c $(APP)/npmenus.c $(APP)/eqcustom.c $(APP)/apps.c $(APP)/kbinput.c $(APP)/playlistview.c $(APP)/folderbrowser.c $(APP)/books.c $(APP)/upnext.c $(APP)/datetime.c $(APP)/i18n.c $(APP)/i18n_tables.c $(APP)/theme.c $(APP)/theme_kit.c $(APP)/theme_defs.c $(APP)/theme_kits.c $(APP)/theme_validate.c $(APP)/theme_store.c $(APP)/artcache.c $(APP)/colorpick.c $(APP)/albumwall.c $(APP)/debug_ui.c $(APP)/chargemode.c $(APP)/usbhid.c $(APP)/clicks.c $(APP)/procnet.c $(APP)/fontchain.c $(APP)/command_spawn.c $(APP)/modes.c $(APP)/outseq.c $(APP)/session.c $(APP)/srcswitch.c $(APP)/admit.c $(APP)/timekeep.c $(APP)/handoff.c $(APP)/bootsel.c $(APP)/rmguard.c $(APP)/launcher.c $(APP)/transport.c $(APP)/booksess.c $(APP)/tama.c $(APP)/tama_ui.c $(APP)/dacinfo.c $(APP)/dac_ui.c $(APP)/wifi.c $(APP)/bt.c $(APP)/radio.c $(APP)/radio_lv.c $(APP)/weather.c $(APP)/lyrics.c $(APP)/netart.c $(APP)/toast.c $(APP)/splash.c $(APP)/md5.c $(APP)/scanner.c $(APP)/opustitle.c $(APP)/opustitle_worker.c $(APP)/system.c $(APP)/ota.c $(APP)/fwcaps.c $(APP)/font_icons_28.c $(APP)/font_icons_20.c $(APP)/font_weather16.c $(APP)/font_intl_14.c $(APP)/font_intl_16.c $(APP)/font_intl_18.c $(APP)/font_intl_20.c $(APP)/font_mono_14.c $(APP)/font_mono_16.c $(APP)/font_mono_18.c $(APP)/font_mono_20.c $(APP)/font_mono_24.c $(APP)/font_mono_bold_12.c $(APP)/font_dot_18.c $(APP)/font_dot_28.c $(APP)/font_bauh_bold_22.c $(APP)/font_bauh_bold_32.c $(APP)/font_bauh_clk_140.c $(APP)/font_bauh_clk_96.c $(APP)/font_bauh_ui_12.c $(APP)/font_bauh_ui_14.c $(APP)/font_bauh_ui_16.c $(APP)/font_bauh_ui_18.c $(APP)/font_bauh_ui_20.c $(APP)/font_bauh_ui_24.c $(APP)/font_blue_clk_56.c $(APP)/font_blue_sans_14.c $(APP)/font_blue_sans_16.c $(APP)/font_blue_sans_18.c $(APP)/font_blue_sans_20.c $(APP)/font_hifi_seg_20.c $(APP)/font_hifi_seg_64.c $(APP)/font_hifi_ui_12.c $(APP)/font_hifi_ui_14.c $(APP)/font_hifi_ui_16.c $(APP)/font_hifi_ui_18.c $(APP)/font_hifi_ui_20.c $(APP)/font_hifi_ui_24.c $(APP)/font_stone_clk_64.c $(APP)/font_stone_ui_12.c $(APP)/font_stone_ui_14.c $(APP)/font_stone_ui_16.c $(APP)/font_stone_ui_18.c $(APP)/font_stone_ui_20.c $(APP)/font_stone_ui_24.c $(APP)/font_term_clk_72.c $(APP)/font_term_ui_12.c $(APP)/font_term_ui_14.c $(APP)/font_term_ui_16.c $(APP)/font_term_ui_18.c $(APP)/font_term_ui_20.c $(APP)/font_term_ui_24.c $(APP)/font_zine_bold_16.c $(APP)/font_zine_bold_20.c $(APP)/font_zine_clk_50.c $(APP)/font_zine_glitch_26.c $(APP)/font_zine_ui_12.c $(APP)/font_zine_ui_14.c $(APP)/font_zine_ui_16.c $(APP)/font_zine_ui_18.c $(APP)/font_zine_ui_20.c $(APP)/font_zine_ui_24.c $(APP)/font_brand_disk_32.c $(APP)/font_brand_os_32.c $(APP)/sysconfig.c $(APP)/playreq.c $(APP)/outroute.c
APP_SRCS += $(APP)/coverart.c $(APP)/coverart_accent.c $(APP)/coverart_prod.c
SQLITE_FLAGS := -Os -pipe -static -ffunction-sections -fdata-sections -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
  -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DQS=0 -DSQLITE_OMIT_DEPRECATED $(REPRO_FLAGS) $(FP_FLAGS)
OBJS := $(LVGL_SRCS:.c=.o) $(APP_SRCS:.c=.o) $(APP)/sqlite3.o

.DEFAULT_GOAL := mq_ui
THEME_SRCS := $(filter-out $(APP)/theme.c $(APP)/font_%,$(APP_SRCS))
THEME_HDRS  = $(filter-out $(APP)/theme.h $(APP)/sqlite3%,$(shell find $(APP) -name '*.h' -not -path '$(APP)/lvgl/*' -not -name 'stb_image.h'))
THEME_COLOUR_RE := lv_color(32|16)?_(hex|hex3|make|white|black|mix|mix32|16_16_mix|lighten|darken|hsv_to_rgb|from_int|to_hsv|premultiply)[[:space:]]*\(|lv_color(32|16)_t[[:space:]]+[A-Za-z_0-9]+[[:space:]]*=[[:space:]]*\{|\(lv_color(32|16)_t\)[[:space:]]*\{|lv_palette_[a-z]+[[:space:]]*\(|LV_COLOR_MAKE[[:space:]]*\(|theme_(color|accent)_from_(rgb|hsv)[[:space:]]*\([[:space:]]*(0x|[0-9])|\(lv_color_t\)[[:space:]]*\{|lv_color_t[[:space:]]+[A-Za-z_0-9]+[[:space:]]*=[[:space:]]*\{|(\.|->)(red|green|blue)[[:space:]]*=[^=]|\[[0-2]\][[:space:]]*=[[:space:]]*0x[0-9A-Fa-f]{2}
check-theme:
	@if grep -n -E '$(THEME_COLOUR_RE)' $(THEME_SRCS) $(THEME_HDRS); then \
	  echo "check-theme: FAIL - use a theme role (TC(...)) or theme_color_from_rgb() on a runtime value instead"; exit 1; fi
	@if grep -n -E '\b(lv_font_montserrat_[0-9]+|font_icons_[0-9]+)\b' $(THEME_SRCS) $(filter-out $(APP)/lv_conf.h,$(THEME_HDRS)) | grep -v 'LV_FONT_DECLARE'; then \
	  echo "check-theme: FAIL - use a font role (TF(...)) instead"; exit 1; fi

FT_INPUTS := $(shell find $(NUI)/third_party/freetype-2.13.3/include $(NUI)/third_party/freetype-2.13.3/src $(NUI)/third_party/ftconfig -type f) $(NUI)/third_party/build-freetype.sh
$(NUI)/third_party/out/mipsel/libfreetype.a: $(FT_INPUTS)
	CROSS="$(CROSS)" sh $(NUI)/third_party/build-freetype.sh mipsel

mq_ui: $(OBJS) $(NUI)/third_party/out/mipsel/libfreetype.a
	@$(MAKE) --no-print-directory check-theme
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
	cp -f $@ $@.debug
	$(CROSS)strip $@ || true
	@ls -l $@
	@{ echo "artifact: $(notdir $@)"; \
	   echo "md5: $$(md5sum $@ | cut -d' ' -f1)"; \
	   echo "size: $$(stat -c%s $@)"; \
	   echo "built: $$(date -u +%Y-%m-%dT%H:%M:%SZ)"; \
	   echo "cc: $$($(CC) --version | head -1)"; \
	   echo "src_md5: $$(cat $(APP_SRCS) $(APP)/*.h 2>/dev/null | md5sum | cut -d' ' -f1)"; \
	 } > $@.manifest
	@cat $@.manifest

diskos-bootprobe: $(APP)/tools/diskos_bootprobe.c
	$(CC) -Os -static -Wall -Wextra -Werror $(FP_FLAGS) -o $@ $<
	$(CROSS)strip $@ || true
	@md5sum $@

diskos-artdec: $(APP)/tools/diskos_artdec.c $(APP)/tools/stb_image.h
	$(CC) -O2 -static -Wall -Wextra -Werror $(REPRO_FLAGS) $(FP_FLAGS) -o $@ $< -lm
	$(CROSS)strip $@ || true
	@md5sum $@

FT_DIR := $(NUI)/third_party
FT_OUT := $(FT_DIR)/out/mipsel
FT_INC := -I$(FT_DIR)/freetype-2.13.3/include
.PHONY: ft_cross_build ttc_probe ttc_probe_size
ft_cross_build:
	CROSS="$(CROSS)" sh $(FT_DIR)/build-freetype.sh mipsel
ttc_probe: ft_cross_build
	$(CC) -Os -pipe -std=gnu11 -static -Wall -Wextra -Werror $(FP_FLAGS) \
	  $(REPRO_FLAGS) $(FT_INC) -o $(FT_OUT)/ttc_probe \
	  $(APP)/tools/ttc_probe.c $(FT_OUT)/libfreetype.a -lm
	$(CROSS)strip $(FT_OUT)/ttc_probe

ttc_probe_size: ft_cross_build
	@for object in $(OBJS); do test -f "$$object" || { echo "missing object: $$object" >&2; exit 1; }; done
	$(CC) -Os -pipe -std=gnu11 -ffunction-sections -fdata-sections $(FP_FLAGS) \
	  $(REPRO_FLAGS) $(FT_INC) -c $(FT_DIR)/ftconfig/size_anchor.c -o $(FT_OUT)/size_anchor.o
	$(CC) $(LDFLAGS) -o $(FT_OUT)/mq_ui.base $(OBJS) $(LDLIBS)
	$(CC) $(LDFLAGS) -Wl,-u,diskos_ft_size_anchor -o $(FT_OUT)/mq_ui.freetype \
	  $(OBJS) $(FT_OUT)/size_anchor.o $(FT_OUT)/libfreetype.a $(LDLIBS)
	$(CROSS)strip $(FT_OUT)/mq_ui.base $(FT_OUT)/mq_ui.freetype
	@base=$$(stat -c%s $(FT_OUT)/mq_ui.base); with=$$(stat -c%s $(FT_OUT)/mq_ui.freetype); \
	  echo "mq_ui stripped baseline: $$base bytes"; \
	  echo "mq_ui stripped with FreeType: $$with bytes"; \
	  echo "FreeType delta: $$((with - base)) bytes"; \
	  stat -c 'libfreetype.a: %s bytes' $(FT_OUT)/libfreetype.a

.PHONY: checkpunct
checkpunct:
	@if grep -rnP '[\x{2013}\x{2014}\x{2018}\x{2019}\x{201C}\x{201D}\x{2026}]' $(APP_SRCS) $(APP)/*.h; then \
	  echo "checkpunct: FAIL (unsupported punctuation above)"; exit 1; else echo "checkpunct: clean"; fi

$(APP)/sqlite3.o: $(APP)/sqlite3.c
	$(CC) $(SQLITE_FLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) mq_ui mq_ui.debug mq_ui.manifest ipodos-ui diskos-bootprobe diskos-artdec
-include $(OBJS:.o=.d)
