# diskOS Disco handoff - 2.2.1 (2026-10-14)

## 2.2.1 specifics (fixes on 2.2.0)

- main.c source_send: USB DAC sends work mode 1 (0657000C0001) instead of 8; device-tested by the owner.
- stats.c stats_tick: any jump back to < 5 s (by > 3 s) starts a new attempt; rec_finish alone decides if it counts.
- sphere_ui.c db_migrate: sqlite3_backup into songsphere.db.tmp, PRAGMA quick_check, rename; any failure removes the
  tmp and uses /usr/data/apps/songsphere/sphere.db for that run (retried next start).
- songsphere/ssdb.c ssdb_pick: cnt_get / cnt_add, only taken songs counted, probe bounded to HN.
- roulette_spin.c: loader_main sets loader_done; left() starts reap_cb (500 ms) that joins a finished loader.
- appshelf.c app_icon_load: 32-bit compression field, BI_RGB only; height bounded before negation.
- Built with the GCC 11.2 MIPS toolchain (musl-cross-make 227df8b; sources from archive.ubuntu.com as below).


## 2.2.0 specifics

- SongSphere built in: sphere_ui.c (SCR_SPHERE; pages land / list / tag / play) sharing ui/songsphere/sphere.c + ssdb.c
  with the add-on program. DB /usr/data/songsphere.db (migrated from /usr/data/apps/songsphere/sphere.db). cfg
  songsphere (Settings > Playback). Mood: songmenu A_MOOD (album/artist), Music track popup mood pill (ov_mood_pill).
  ui_app_play_ids (main.c) shared by the app request hook and SongSphere. The add-on folder is skipped by All apps.
  sphere_rgb(): content colours made by the host program (mq_ui: theme_color_from_rgb; add-on: lv_color_hex).
- Album Roulette built in: roulette_spin.c (SCR_ROULETTE_SPIN: the 1.3.1 app's spin, timings and gestures) +
  roulette_backend.c (its sampler / cover cache / diskos-artdec decoding, verbatim; play via mdb_album_plan +
  ui_play_plan in track order) + roulette_rim.h (the wheel picture). roulette.c's I feel lucky! -> roulette_spin_open().
  APP_BUILTIN_ROULETTE; ids md5("album-roulette") kept; the old folder is skipped; screen_back skips a finished spin.
  Host: roulette-land, roulette-<ms>.
- Apps (appshelf.c, SCR_APPSHELF, Disco Menu code A): five pinned apps (cfg apps_p1..5 = app ids), picker, icons from
  app.conf icon= / icon.bmp (menu_app_t.icon).
- Listening Stats: TOPN 15, main list top 5 + sub-views (stats_test_sub) and Wrapped (stats_test_wrap); swipe strips.
- Host: ssphere-0/1/3, ssphere-tag (SS_KEY), appshelf, appshelf-pick, appshelf-land (APPS_PINS); STATS_SUB, STATS_WRAP.

## 2.1.0 specifics

- Listening Stats (stats.c): recorder stats_tick (main loop) / stats_flush (power off) -> mdb_log_play -> PLAY_LOG
  (AT, MS, PATH, TITLE, ARTIST, ALBUM, AARTIST; history worker). Counts at 30 s or 90 % of a short song; time = position
  progress while playing, capped by real time. Screen SCR_STATS (curvelist), landing via builtin:stats; Disco Menu code S.
  Host: statsapp / statsapp-land, STATS_SCROLL/PERIOD/BACK/EMPTY/ART_DIR. Recorder test: compile stats.c with
  -DSTATS_RECORDER_ONLY (see the 2.1.0 review). Year view at 55k plays ~0.2 s on a PC: watch it on the device.
- App API (main.c): app_env_write -> /tmp/diskos-app-env before app_run; app_request_take reads
  /tmp/diskos-app-request after (action=play|save|playsave, shuffle, name, id=); IDs filtered/deduped (mdb_keep_song_ids).
- SongSphere (ui/songsphere, make songsphere): own program, LVGL + SQLite + fb_pan + Inter; sphere.db in its folder;
  SS_SHOT / SS_BENCH / SS_TEST on a PC. Installed to /usr/data/apps/songsphere (app + app.conf).
- Immersive: cfg imm_style (0 Vinyl, 1 CD, 2 None = still) migrated from disco_imm; imm_lyrics (fade 0 = no shade).
- Dark-screen touch: LVGL touch off while bl_state 2 (or 1 off the saver); the waking touch held until lift (3 s cap).
- mdb_artist_plan in both groupings (By Artist: artist_credited view vs ARTIST=key queue, else type-5 exact queue).
- Hints: pills size to text; HINT_LOCK (hint_lock) after a touch wake, not limited by hint_starts.
- Sign script: keys/last-epoch, default label custom-<epoch>.

## 2.0.0 specifics

- diskOS Disco! for Windows (windows/): C# .NET Framework 4 WinForms, built on Linux with mono (`sh windows/build.sh`).
  Pages: Disclaimer, Check, Prepare (own WSL distro "diskOS-Disco" from diskos-wsl.tar.gz, `sudo sh windows/wsl/build-env.sh`),
  Install (usbipd bind + attach --wsl --auto-attach, `diskos install --variant public --yes`), Update (latest release +
  SHA256SUMS, diskos-sign.sh, SD card), About. Owner flashed a Disc with it successfully (2026-10-13).
- Environment: minimal Ubuntu 24.04 (debootstrap), root user, no systemd/udev; kmod is required (usbipd loads vhci_hcd
  through the distro). Bump Env.Generation and build-env's marker together when its contents change.
- Release workflow builds the Windows zip (diskOS-Disco-for-Windows.zip). Stock backup copied to %LOCALAPPDATA%\diskOS-Disco\backup.
- mq_ui: playback-start timeout 12 s, silent while music plays.

## 1.5.0 (previous)

Baseline: 1.4.1 (tag v1.4.1). 1.5.0 = Boomshine built in + installed apps in the Disco Menu (owner's patch,
reviewed) + app landing pages. SD update only (S97 unchanged). Release notes: docs/releases/v1.5.0.md.

## 1.5.0 specifics

- Boomshine 1.2.0 source in ui/boomshine (MIT; DejaVu glyphs in fonts.h). Compiled into mq_ui with
  -DBOOMSHINE_BUILTIN -Dmain=boomshine_main and its own flags (kept out of the theme gates). main() dispatches
  `--boomshine` (log /tmp/boomshine.log). app_run spawns /proc/self/exe --boomshine for exec "builtin:boomshine".
  Scores: /usr/data/boomshine/scores.conf, copied once from /usr/data/apps/boomshine. Adds about 300 KB.
- menu_apps.c (owner's patch): disco_mnu digits 0-9 -> cfg dm_app0..9 = md5(app dir name). Skips dirs without an
  executable app, and album-roulette/boomshine (built-in entries). SEC[] gained "boomshine" (code B).
- Landing page SCR_APPLAND (roulette.c: applanding_show / boomshine_landing). Menu entries for Boomshine and apps open
  it (arrows too); only its button launches. app.conf: tagline=, about= (\n), button=. All apps/Shortcuts list
  Boomshine first (id md5("boomshine")); app_launch routes it to the landing page like Album Roulette.
- Disc Shelf app.conf gained landing text; install guide now always copies app.conf.
- Not device-tested yet: Boomshine inside mq_ui (fb0/touch handover as for external apps), the 1.2.0 game itself.

## 1.4.1 (previous)

Baseline: 1.4.0 + docs cleanup 2997e14. 1.4.1 = fixes for GitHub issues #1-#8 (reporter ingvarr777) + UI polish,
built with the GCC 11.2 MIPS toolchain. Release notes: docs/releases/v1.4.1.md.

## Working agreement

Owner tests on Snowsky Disc V2.57 (360x360, X2000 MIPS32r2 FP64 NaN2008). Native LVGL previews before layout
approval, then a compiled mq_ui and a Git bundle. Owner pushes main/dev and tags. SSH root@192.168.178.63, no adb/scp.
Install UI by signed SD update; keep fail-closed launch verification and rollback.

### 1.4.1 specifics

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
