# diskOS Disco! - to do

Live list. Baseline: 2.5.6 (tag v2.5.6). Older notes: `todo-history.md`.
How we work: owner suggests -> Claude offers fixes -> host renders for big visual changes -> owner's go ->
batch -> one test build -> 2-3 days on the Disc -> one release, one bundle, owner pushes + tags.
List words: "add to the list" pins an item; "major" / "minor" sorts it; "priority" puts it first.
Links always point to releases/latest. No upstream diskOS version shown to users.

## Priority
(none)

## Major
(none)

## Waiting on users
- 2.5.6 Bluetooth: AAC/LDAC never tested on a Disc; issue #13 reporter to confirm the fix.

## Major
(none)

## Major
- Dyslexic-friendly font (owner, 2026-10-19): OpenDyslexic 3 (SIL OFL, opendyslexic.org) as a switch under
  Display > Layout & extras > Dyslexic-friendly (On/Off; restarts the interface like a theme change). Overrides all
  text fonts in every theme at every size. Keep as they are: icons, clocks and big numbers (incl. Null's pixel clock
  and Braun's dial), and non-Latin scripts (Thai, CJK, Korean... still from their own fonts). Route B: LVGL's
  built-in runtime TTF renderer (LV_USE_TINY_TTF, currently 0) with one font file (~0.1-0.15 MB) instead of ~2 MB
  of pre-made sizes. Needs a render pass of all themes (wider/taller font overflows clocks-adjacent text, pills,
  buttons, QS, Squash/Null) and a real Disc test (CPU on first draw); not to be released untested.

## Minor
- From the 2.5.6 code review (not done yet): queue lookups wait up to 1.5 s on the UI thread under database contention
  (move longer waits off the UI thread); Bluetooth reconnect (3 x 8 s) can overlap the delayed scan and has no
  cancellation when Bluetooth is turned off or another device is chosen; bulk Favourites runs in one go on the UI
  thread (batch in a transaction / in the background for large selections).
- Screenshots: refresh any in the docs/site that predate 2.5 (host renders, round format).
- Windows app downloads the latest mq_ui instead of the bundled one (low priority; Windows is unsupported).
- Later idea: Korean/CJK in a second, larger size for Music titles (about +0.4 MB per script).

## Done, not yet released (docs only, no rebuild needed)
- Windows warning on the website download card, README and WINDOWS.md.
- Website links now point to releases/latest (were fixed to v2.5.4).
- README: Known limits, contributor note ("disco" commits; no history rewrite), build/screenshot shortcuts,
  Help/hints location, Display settings line.
- How-to: every menu path corrected to the 2.5.4 Display menu (checked against rendered menus), a "Display menu at
  a glance" table, Immersive circular control and Cover style, hold-to-scrub, QS EQ tap/hold, letter wheel.
- UI choices: Disco, Squash and Null sections, the shared Display menu, upstream themes removed in 2.5.1.
- Docs index, Squash and Null guide intros, docs/themes/null.md pointer, tour (accent path, Help row),
  Disc Shelf, Privacy and Development paths.
- CHANGELOG: 2.4.0 entry added, 2.5.1 no longer "Unreleased", dates on 2.5.x.
- handoff.md rewritten for 2.5.4 (old notes kept below as history). Old todo kept as todo-history.md.

## Waiting on the owner's Disc
- 2.5.4's newest bits need everyday use: Immersive circular seek/volume, cover rotation, sliding lyrics,
  Squash weather, Squash/Null battery layout.
