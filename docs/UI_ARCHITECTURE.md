# UI architecture: one owner per concept

The player UI keeps its policy out of the LVGL screens. This note records the modules that own the decisions a screen
must not re-derive, the interfaces they expose, and the host tests that hold them. It follows the domain terms in
`GLOSSARY.md` and the switch decisions in `docs/adr/`.

## Working Mode switch

`srcswitch` owns one switch from the moment a Working Mode is picked to the moment the Disc is on the new mode. The
interface is a request, a status and one event:

- `ui_set_source_mode(mode)` starts a switch.
- `srcswitch_status()` returns the phase (`LEAVING`, `WAITING`, `ENTERING`, `SETTLING`, `IDLE`), the from and to
  modes, the mode it landed on, and the failure reason.
- The listener receives `SRCSWITCH_EV_CHANGED`, `SRCSWITCH_EV_SETTLE_RESTART` and `SRCSWITCH_EV_ABANDONED`.

The picker in `modes.c` renders the status and no longer guesses the outcome with a settle timer. After the last
deferred step and a 3.2 s settle, `srcswitch` checks the landed mode through `detected_mode` (the sysfs check in
`main.c`) and reports `Mode didn't switch` once. The via Local policy and the ordered USB DAC exit are unchanged; see
ADR-0001.

The Local sequence itself is `0666 6` (internal analog), `0642 0` (no gadget) and `0657 8` (Local Player work
mode), so returning to Local Playback selects the Disc's own CS43131 DAC. The Player re-initialises that DAC on the
way back, which resets Gain, DRE and the DAC Filter. Two seconds after any source sequence lands, `srcswitch` calls
`reapply_audio`; the adapter in `main.c` re-sends only the values diskOS has actually stored, so an unset key leaves
the Player's own value alone instead of forcing Low.

Tests: `ui/tests/srcswitch_test.c`, including the transaction, the mismatch and the overlay-row cases.

## Card

`card` owns the physical microSD facts and their mutations. `sdio` keeps the admission decision; everything else asks
`card` instead of re-deriving the state.

- Facts: `card_mounted()`, `card_exported_to_host()`, `card_host_confirmed()` and `card_player_guarded()`. The export
  check treats an unreadable LUN path as exported, so a card whose state cannot be confirmed never looks safe.
- `card_unmount()` is the **Release ladder**: a plain `umount`; then the Player's USB Storage release with 40 retries at
  100 ms; then a lazy detach; then the block-device `fsync`. Every outcome, including a failed unmount, is logged and
  the shutdown continues.
- `card_coldplug_start()` and `card_coldplug_step()` own the boot-time **Cold mount** and its export rollback.
- Every system action arrives through `struct card_env`; `main.c` supplies the adapters and `card_test` supplies fakes.

Tests: `ui/tests/card_test.c`.

## Card close

`cardclose` is the transaction power off and Restart share before either cuts power: wait for SD access to drain (10 s),
run the sync child (15 s), then unmount through the **Release ladder**. Each ending adds its own last step:

- Power off quiesces first, then runs the shared close, then sends the MCU power frame and keeps the protected recovery
  path. `power.c` keeps its phase names (`PW_DRAIN`, `PW_SYNC`) but the steps come from `cardclose_tick()`.
- Restart quiesces first, then runs the shared close, optionally arms flash mode, then reboots.

`cardclose_begin()` takes a `struct cardclose_ops` of drain, sync, kill, unmount and clock callbacks; `cardclose_tick()`
advances one phase per call; the UI drives it from its 100 ms timer.

Tests: `ui/tests/cardclose_test.c`.

## Restart

`restart` owns the ordered reboot: quiesce SD access, stop readers, save the clock, run the shared **Card close**
(drain, sync, unmount), optionally arm flash mode, then reboot. The settings screen only shows the confirmation and the
row that starts it.

- `restart_begin(RESTART_UI | RESTART_FLASH)` returns 0 when the transaction starts.
- `restart_tick()` advances it; the UI drives it from a 100 ms timer while `restart_busy()`.
- Every system action comes through `struct restart_env`: drain, quiesce, readers, clock, unmount, spawn, wait, kill,
  flash arm, reboot and toast. `main.c` supplies the device adapters; the test supplies fakes.

Flash arming (`/dev/mem`, `0x10000000 + 0xCC`) lives in `restart.c`, not in a UI file. The settings table keeps the
"Restart to Flash Mode" row and its note.

Tests: `ui/tests/restart_test.c` (drain timeout, sync failure, sync timeout, flash arm failure and success).

## Player settings

`playerset` owns the fourteen Player setting senders and the frames they build: equalizer preset, DRE, gain, DAC
filter, replay gain, gapless, artist class, folder jump, track display, memory, max volume, balance, play mode and
favorite. The volume frame and its three senders also live here, so `outroute` calls `playerset_frame_volume()` instead
of rebuilding the frame.

- The senders are the `ui_set_*` entry points the screens already call; the frame builders take the value guards.
- `ui_reapply_audio()` is the order used after a route switch, with three retries 1500 ms apart.
- `struct playerset_env` carries the configuration, DAC-volume and quiet-window hooks; `main.c` supplies them.

Tests: `ui/tests/playerset_test.c`, plus the existing `outroute_test`, `dacvol_test` and `srcswitch_test`.

## Idle governor

`idlegov` owns the power-key, backlight and screen-saver decisions. The caller owns the state struct (`idlegov_t`) and
applies the decisions; the module reads facts and returns actions.

- `idlegov_powerkey_step()` and `idlegov_powerkey_pass()` handle a press, a wake and a foreign brightness change.
- `idlegov_screen_frame_step()` decides whether to send the Player's screen frame, including the inject deferral.
- `idlegov_blpoll_due()` sets the 250 ms backlight poll; `idlegov_saver()` walks the saver ladder and the screen-off
  limit; `idlegov_foreign_relevel()` decides when another owner's brightness has to be re-levelled.
- `IDLEGOV_TMAP_N` keeps the time-map size used by the saver.

Tests: `ui/tests/idlegov_test.c`.

## Transport commands

`transport.h` names the transport actions instead of spelling the frames at every call site:

- `ui_transport_command(UI_TRANSPORT_PLAYPAUSE | NEXT | PREV)` sends the one frame for that action.
- `ui_transport_click_command(clicks)` maps one, two and three clicks to play/pause, next and previous.
- The transport hex now lives only in `ipc.h` and `transport.c`. `ui_seek_to` deliberately stayed in `admit.c`: moving it
  would break `admit_test`'s link closure.

Tests: `ui/tests/transport_test.c`.

## Player view

`ipc` owns the Player link and the decoded state. The public state is a compact view; the evidence fields stay inside
the parser.

- `player_view_t` carries what the UI renders: title, artist, album, path, position, duration, play state, volume,
  ids, flags and change counters.
- `player_evidence_t` carries what only the parser and the play request need: raw ids and their validity, the
  playback token, the Player generation, the path and position sequence numbers, `player_confirmed` and
  `provisional`.
- `ipc_get_view(view)` and `ipc_get_now(view, evidence)` copy under the IPC mutex. `ipc_title_event` carries both.

Consumers that only draw a track use the view. `playreq`, `booksess`, the opus-title worker and the art identity use
the evidence where they need to judge freshness. Four exports with no callers were removed: `ipc_player_mode`,
`ipc_seed_state`, `ipc_send_cmd_unsafe` and `ipc_send_quiet`.

Tests: the existing `ipc_guard_test`, `ipc_tx_test`, `playreq_test`, `booksess_test`, `transport_test`,
`outroute_test`, `dacvol_test` and `srcswitch_test`, all rebuilt against the split types.

## Screens

`screenmgr` owns the lifecycle of every screen through one table in `screenmgr.c`:

```c
screen_def_t { create, refresh, visible, figure }
```

`screens_init` creates the roots and calls each `create`; `transition` calls the target's `refresh` and the
`visible` hooks (Tama, DAC). Adding a screen is one row plus its create function.

The interface is split:

- `screens.h` keeps the screen enum, the manager, and the helpers every module really uses (toast, header, clock,
  accents, backlight, playback plumbing): 94 declarations.
- `screens_ui.h` keeps the screen-owned create, refresh, open and setter declarations: 163 declarations. Nineteen
  modules include it; twenty modules that only use the shared interface do not.

## Now Playing fan-out

`nowplay` owns the change rules for the current-track view and pushes typed updates to the screens:

- `nowplay_publish(env, view, evidence, playing, visible, force)` compares the track sequence, the playing state, the
  path, the title and the artist, and calls only the sinks that need the change.
- `nowplay_invalidate()` forces the next publish (boot state change, returning from an app).
- The sinks are registered in `main.c`: now playing, home, progress, saver state, art, track, quick settings, song
  info and the Now Playing menu.

The main loop no longer keeps `last`, `last_playing`, `np_last_path`, `np_last_title` or `np_last_artist`, and it no
longer decides which screen to touch.

Tests: `ui/tests/nowplay_test.c` (first publish, unchanged view, sequence change, path change, title change,
playing change, invisible deferral, forced republish).

## Play request

The position-advance heuristic moved from `playstate.c` into `playreq.c`. `playstate.c`, `playstate.h` and
`playstate_test.c` are gone; the rule now lives with the only caller, next to the start-failure analysis that reads
the same samples. `playreq_test.c` covers the behavior.

## Build and test harness

`ui/tests/Makefile` declares each host test once: `TESTS += name`, then `name_SRCS`, optional `name_DEPS`, `name_DEFS`,
`name_OBJS`, `name_PRE` and `name_POST`. One `$(eval $(call TEST_RULE,...))` expands that into an explicit build rule;
the `run` prerequisite list, `run` itself and `clean` all derive from `TESTS`. Adding a test is one block, not four
hand-edited lists.

The rule must stay an `eval` template. A static pattern rule such as `$(TESTS): %: $($*_SRCS)` parses with an empty
`$*`, so it silently produces empty prerequisites and binaries never rebuild on source changes.

Temp roots are per test under `ui/tests/.tmp/<test>/`, so two checkouts can run the suite at the same time. Two
exceptions are forced by production code: `card_test` needs an absolute `PATH` entry, and `coverart_pipeline_test`
must stage on `/tmp` because `art.c` renames across devices, so its root is keyed per checkout by a hash.

`ui/Makefile` builds the device source list as a sorted wildcard minus an explicit nine-file exclusion set (seven
standalone host tools with their own `main()`, the profiling shim `prof.c`, and `sqlite3.c`). The source set is
unchanged; the sorted link order changes the binary bytes with no behavior change.

## Verification

Host tests:

```sh
make -C ui/tests run
make -C ui/tests check
```

`run` builds and runs the C tests; `run-py` runs the Python guards; `check` is both. The UI itself builds with the
mipsel-musl toolchain (`make -C ui mq_ui`). Without that cross toolchain, every UI source is checked with the host
compiler in syntax-only mode against the vendored LVGL and FreeType headers, with
`-Werror=implicit-function-declaration`.
