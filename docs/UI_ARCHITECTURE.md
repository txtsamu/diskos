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

Tests: `ui/tests/srcswitch_test.c`, including the transaction, the mismatch and the overlay-row cases.

## Restart

`restart` owns the ordered reboot: quiesce SD access, stop readers, save the clock, wait for the card to drain, run
`sync`, optionally arm flash mode, then reboot. The settings screen only shows the confirmation and the row that
starts it.

- `restart_begin(RESTART_UI | RESTART_FLASH)` returns 0 when the transaction starts.
- `restart_tick()` advances it; the UI drives it from a 100 ms timer while `restart_busy()`.
- Every system action comes through `struct restart_env`: drain, quiesce, readers, clock, spawn, wait, kill, flash
  arm, reboot and toast. `main.c` supplies the device adapters; the test supplies fakes.

Flash arming (`/dev/mem`, `0x10000000 + 0xCC`) lives in `restart.c`, not in a UI file. The settings table keeps the
"Restart to Flash Mode" row and its note.

Tests: `ui/tests/restart_test.c` (drain timeout, sync failure, sync timeout, flash arm failure and success).

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

## Verification

Host tests:

```sh
make -C ui/tests run
make -C ui/tests run-py
```

The UI itself builds with the mipsel-musl toolchain (`make -C ui mq_ui`). Without that cross toolchain, every UI
source is checked with the host compiler in syntax-only mode against the vendored LVGL and FreeType headers, with
`-Werror=implicit-function-declaration`.
