# Settings page tree

[Back to the docs index](README.md)

The Settings screen is a two-level tree: five top groups, and under Display and System a second level of short group pages.
The tree is the `TABLE[]` in `ui/settings.c`. A row whose type is `ST_PAGE` opens the group page named in its `to_page`
column, which is shown by the `SCR_SETSUB` screen; every other row is a setting or an action. Config keys, deep links
(`settings_open_key`) and the Quick Settings tiles are unchanged; only the page a row sits on moved.

No page has more than 9 rows (a page with the optional test-output or flash-mode rows included). A page that was a long
scroll before now fits in two screens at most.

## Tree

| Top group | Page | Rows |
|---|---|---|
| Playback (7) | Playback | Play Mode, Equalizer, Custom EQ, DSD Output, Resume Playback, Artists, Play Through Folders |
| Audio (8, 9 with SPDIF) | Audio | Working Mode, Gain, Max Volume, Balance, ReplayGain, Gapless, DAC Filter, DRE, SPDIF Output (test-output builds only) |
| Display (7) | Display | Brightness, Theme, Appearance, Font Size, Screen & Saver >, Player & Lists >, Home & Swipes > |
| | Screen & Saver (5) | Outdoor Mode, Screensaver, Saver Style, Screen Off, Screen Rotation |
| | Player & Lists (5) | Now Playing, Accent Colour, Album View, Track Numbers, Album Art Cache |
| | Home & Swipes (4) | Quick Settings, Weather on Home, Animations, Back-swipe |
| Network (5) | Network | Wi-Fi, Bluetooth, Bluetooth Codec, Online Album Art, Online Lyrics |
| System (7) | System | Time & Language >, Power & Sleep >, Volume Keys >, Library >, Updates >, About >, Debug Mode |
| | Time & Language (5) | Language, 24-Hour Time, Automatic Time, Time Zone, Set Date & Time |
| | Power & Sleep (7, 8 with flash mode) | Sleep Timer, When Sleep Ends, Idle Power-off, Charging Limit, Default UI, Restart, Power off, Restart to Flash Mode (flash-mode builds only) |
| | Volume Keys (3) | Vol Keys: Press, Vol Keys: Double Press, Vol Keys: Long Press |
| | Library (3) | Rescan Library, Import Playlists, Old tags |
| | Updates (4) | Update diskOS, Allow diskOS Updates, Update from SD Card, Block FiiO update checks |
| | About (4) | About, Device Info, Temperature, Reset diskOS Settings |

Debug Mode stays a direct row of System because it opens the Debug screen (`SCR_DEBUG`: SSH and password, Persistent debug,
Key SSH), a fixed panel, not a list. The Quick Settings Debug tile still opens that screen directly.

## Rows per page before and after

| Page | Before | After |
|---|---|---|
| Playback | 7 | 7 |
| Audio | 8 (9 with SPDIF) | 8 (9 with SPDIF) |
| Display | 21 | 7 |
| Network | 3 | 5 |
| System | 26 (27 with flash mode) | 7 |
| Display sub pages | none | 5, 5, 4 |
| System sub pages | none | 5, 7 (8), 3, 3, 4, 4 |

## What moved

| Setting | Was on | Now on |
|---|---|---|
| Outdoor Mode, Screensaver, Saver Style, Screen Off, Screen Rotation | Display | Display > Screen & Saver |
| Now Playing, Accent Colour, Album View, Track Numbers, Album Art Cache | Display | Display > Player & Lists |
| Quick Settings, Weather on Home, Animations, Back-swipe | Display | Display > Home & Swipes |
| 24-Hour Time | Display | System > Time & Language |
| Online Album Art, Online Lyrics | Display | Network |
| Language, Automatic Time, Time Zone, Set Date & Time | System | System > Time & Language |
| Sleep Timer, When Sleep Ends, Idle Power-off, Charging Limit, Default UI, Restart, Power off, Restart to Flash Mode | System | System > Power & Sleep |
| Vol Keys rows | System | System > Volume Keys |
| Rescan Library, Import Playlists, Old tags | System | System > Library |
| Update diskOS, Allow diskOS Updates, Update from SD Card, Block FiiO update checks | System | System > Updates |
| About, Device Info, Temperature, Reset diskOS Settings | System | System > About |

## Navigation

A group row pushes `SCR_SETSUB` on the same screen stack as every other page, so the back button and the edge back-swipe
pop one level. `g_active_sub` names the open group page and `g_scroll_page` / `g_scroll_y` remember the scroll offset of
the Settings page and the group page, so coming back from a detail or a group page returns to the row that was tapped
instead of the top. Opening a group page always starts it at the top. Deep links open the setting detail directly and
back returns to where the link was tapped (Quick Settings, for the tiles).

## Test

`ui/tests/settings_tree_test.py` (part of `make run-py`) reads `TABLE[]` and `ui/tests/settings_baseline.tsv`, the list of
the 67 settings that existed before the regrouping with their type, config key and build flag. It fails if any of them is
missing, duplicated, retyped or re-keyed, if a page is empty, orphaned, opened from two rows or deeper than two levels, if
a page has more than 9 rows, if a `settings_open_key` call in any source file names a key that no setting has, or if a
page name has no translation row.
