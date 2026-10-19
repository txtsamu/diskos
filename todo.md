# diskOS Disco! - to do

Live list. Baseline: 2.5.4 (main 881ca5a, tag v2.5.4). Older notes: `todo-history.md`.
How we work: owner suggests -> Claude offers fixes -> host renders for big visual changes -> owner's go ->
batch -> one test build -> 2-3 days on the Disc -> one release, one bundle, owner pushes + tags.
List words: "add to the list" pins an item; "major" / "minor" sorts it; "priority" puts it first.
Links always point to releases/latest. No upstream diskOS version shown to users.

## Priority
(none)

## Major
(none)

## Minor
- Cover Flow shows no covers on nearly full SD cards: the cover cache stops below 1 GB free. Lower it to about 200 MB.
  When done, update the README "Known limits" line (it says 1 GB).
- Battery: hold the "Since full charge" line to restart the totals by hand.
- Screenshots: refresh any in the docs/site that predate 2.5 (host renders, round format). Needs a look per image.
- Windows app downloads the latest mq_ui instead of the bundled one (low priority; Windows is unsupported).

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
