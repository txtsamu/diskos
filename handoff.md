# diskOS Disco handoff - 1.4.1 (2026-10-12)

Baseline: 1.4.0 (tag v1.4.0, owner device-tested) + docs cleanup 2997e14. 1.4.1 = fixes for GitHub issues #1-#8
(reporter ingvarr777) + UI polish, built with the GCC 11.2 MIPS toolchain. Release notes: docs/releases/v1.4.1.md.

## Working agreement

Owner tests on Snowsky Disc V2.57 (360x360, X2000 MIPS32r2 FP64 NaN2008). Native LVGL previews before layout
approval, then a compiled mq_ui and a Git bundle. Owner pushes main/dev and tags. SSH root@192.168.178.63, no adb/scp.
Install UI by signed SD update; keep fail-closed launch verification and rollback.

## 1.4.1 specifics

- **Needs a reflash**: payload/S97diskos_install changed (#7: first update after a fresh flash, boot-count write
  failure rolls back, transient verify failures keep builds (ota_bundle_ok returns 2), log cap, stale mq_ui.rejected
  removed before install). S97 is only installed by flashing.
- mq_ui: run record read as 1024 bytes (#8), rejected-build refusal at staging (OTA_E_REJECTED), queue_tick cached
  ID on a 50 ms read-only connection (#1), LVGL assert -> abort, memory-aware decode budget (#2), popups closed on
  screen change (#6), rename/move carry MY_LOVE/PLAY_STATS/BOOK_PROGRESS/SONG paths (#5), per-album cover reuse and
  embedded synced lyrics (#4), streamed Ogg/FLAC comments (#3).
- UI: QS "List" (was Current) with Library curve; wider Music title/progress (UL 34..310); Library ROW_H 52 and fonts
  +2 px per Font Size (s_font22 added to the user-font chain); nav Double tap/Hold under Disco Layout with Volume,
  Screen off, Play/Pause, Next (cfg disco_nav_v=2 migrates old "Nothing" 6 -> 10); restart notice before re-exec;
  Shortcuts swipe hint (hint_shortcuts); "Number Before Title".
- payload/diskos-artdec and diskos-bootprobe unchanged from 1.4.0 (the standalone decoder's behaviour is the same).
- Not done on purpose: separate DB connections for worker threads (#1 item 3), 07:00/20:00 appearance re-exec
  (#2 item 3), queueing songs with paths over 255 bytes (#5).

## Build and checks

ui/tools/build_device.py --toolchain /opt/mipsel-n2008-musl-cross --cache <dir> --out <new dir> --approved.
Toolchain: musl-cross-make 227df8b + ui/toolchain/mcm-config.mak; sources from archive.ubuntu.com (gcc-11 orig
contains gcc-11.2.0.tar.xz; binutils/gmp repacked, gmp with a stub doc/Makefile.in), Linux headers cloned from
sabotage-linux/kernel-headers v4.19.88-2, config.sub from /usr/share/misc; rehash changed tarballs in hashes/.
Host: make -C ui CROSS= host-render; REVIEW_QS / REVIEW_NAV / REVIEW_HINTS / REVIEW_DISCO_SETTINGS (both sides);
gates check-theme, checkpunct. Known: render_test immersive (find_sharp_image) fails on the host, as before.
