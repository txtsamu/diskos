# diskOS Disco 2.3.9 — device-test candidate

Based on v2.3.0 (`005440795943c2af8865cc8e4b6a0f40e50febf9`).
Includes the complete Null theme implementation plus the refinements recorded
in NULL_THEME.md, with all existing Disco features retained.

## Port completion

- Null Music weather glance and clock tap to Weather. The glance fades out
  while its space is used by the open navigation drawer.
- Null title hold: Immersive, Favourite/unfavourite, or Nothing.
- Optional tag track-number prefix on Null Music.
- Empty Music title becomes Go to Library and opens Library on tap.
- Null first-use hints, independent learning counters and reset.
- Configurable double-tap and hold actions on the centre of Null's menu
  handle. Text dragging and holding either end for continuous scroll remain.
- Stats uses Disco/default fonts with global size and language fallbacks,
  and the selected Null colours.

## Shared fix

Disco and Null's track action popup (including the mood pill) now dismisses
with the configured edge back swipe, mirrored in left-handed mode. The popup
consumes the gesture and the underlying Music screen remains. Back navigation
also closes the popup before touching screen history.

## Validation

Focused native pointer checks cover both themes and handedness, popup swipe
and Back dismissal, Null's idle Library link, menu handle double-tap and hold,
and Small/Large default font scaling. Static MIPS ABI checks are performed
by the device build script. Physical Disc validation is pending.

The accompanying binary-inclusive patch applies to a clean v2.3.0 checkout:

```sh
git apply --check diskos-disco-2.3.9-vs-2.3.0.patch
git apply diskos-disco-2.3.9-vs-2.3.0.patch
```

The source archive is a full Git source snapshot (including the matching
payload and font sources/licences), without Git history or build caches.

## Revised 2.3.9 — Null layout fixes

- Null Appearance is fixed Dark, with disabled chooser arrows and an explanation.
- QS clock raised; the lower quick-action block moves above artwork/transport.
  Eight settings targets are now 104 x 50 pixels with adjacent, non-overlapping rows.
- Music clock lowered clear of status icons; degree symbols removed from Music weather.
- Typed Search keeps visible on-colour ink on Null's transparent search button.
- Null screensaver follows Music's black, cover or 20x20 pixel background,
  with an additional 10% black overlay.
- Null EQ minus/plus buttons move outward and upward to clear the central value.

This revision keeps version 2.3.9 and supersedes the previous 2.3.9 test binary.
