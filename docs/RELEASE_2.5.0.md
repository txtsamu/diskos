# diskOS Disco! 2.5.0 release handoff

[User-facing release notes](releases/v2.5.0.md) · [Squash](themes/squash.md) · [Null](NULL_THEME.md) · [How-to](HOWTO.md)

This release includes all cumulative work since v2.3.0, with Squash as the website hero. The customizer and dedicated Null section remain; older feature chapters are tighter and existing links are retained. The repository payload and build manifest match the release UI, including the updated on-device How to and volume hint.

## Release contents

- Big fixed-size fonts and touch playback, rising launcher and eight editable icon QS slots.
- Volume B: clean bottom arc, D! dismissal, no plus/minus buttons, no full-screen dimming, 1.8-second idle timeout paused while dragging.
- Separate Music status indicators and toggles, fixed time shadows, lower/larger logo and 20% Blur wash.
- How to and first-use hint guidance for Squash, Disco and Null; Help sits below Battery.
- Original theme IDs and existing settings retained. Squash remains preset 12 with compatibility `simple_*` keys; its Track Font is independently stored as `simple_track_font`.
- The GitHub release workflow refreshes notes as well as assets when updating an existing release.

## Provenance and checks

`payload/mq_ui.build.json` records the UI source commit, source digest, compiler, ABI and exact binary checksums. Release packaging verifies version, hash and size before producing downloads. Native checks cover the touched interactions; the current UI has not been flash-tested on a physical Disc in this environment.

The Git bundle carries the complete release branch and annotated v2.5.0 tag, based on v2.3.0. Merge with `--ff-only`, then push main and the tag. GitHub Actions rebuilds the Windows package and publishes the release assets from the updated payload; Pages publishes the updated docs site according to the repository’s configured Pages source. No remote push occurs while preparing this handoff.
