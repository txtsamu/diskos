# Disco development handoff

Updated: 2026-10-12. Read `agents/AGENTS.md`, `docs/DEVELOPMENT.md` and `todo.md`.

## Verified starting point

`dev` starts from GitHub `main` release 1.3.1:
`03d737445818ea7b439b427822126d432678b022`.
The owner supplied `1.3.2-polish1-vs-1.3.1.patch`; it applies cleanly to this
release and is restored in dev. Source now displays version 1.3.2-polish1.
Main, shipping payload and release assets are unchanged.

The project is a battery-powered Snowsky Disc UI: round 360x360 LVGL, Ingenic
X2000 MIPS32r2, FP64, NaN2008, device firmware V2.57. Prioritize readable controls,
predictable navigation, both Left/Right Disco layouts and inexpensive rendering.
The owner does not code; provide clear explanations, frequent progress updates,
real LVGL previews where requested, and a compiled `mq_ui` for test builds.

## Restored candidate: 1.3.2-polish1

The uploaded patch restores the gesture handler, Controls selectors, docs,
version and real LVGL pointer regression tests. Its original compiled binary
is still missing, and the device test status is unknown. Historical binary SHA-256:
`ac84d102a7f0b3b203f0492c4cb4962c2f21a445fbb7084664d67b1f3a9f19af`.
This hash is a historical reference, not a newly verified binary or proof of
device acceptance. Compile a new binary only after recovering the complete toolchain.

Its intended contract: System > Controls > Double tap and Hold, arrow selectors
and Apply, Back discards pending selection. Options: Music screen, Search, Queue,
Up Next, Shortcuts, Quick Settings, Nothing. Defaults: Search/Music respectively.
Gestures work ONLY while Disco navigation is OPEN, across all sections and both
sides. Closed shard remains tap-to-open. Arrows/swipes remain immediate; centre
taps wait 260 ms unless Double tap is Nothing. Hold consumes release. Closing,
navigation, or leaving the picker cancels the action, including returning inside.
Source regression tests are present in `ui/tests/host_render.c` (`REVIEW_NAV`).
Check the recorded validation below for results from this recovery session.

## Recovery material

`docs/recovery/1.3.0-polished2.patch` preserves older local edits against
`60255a5f7db37f93d011205bdf2271b413803def`. They overlap improvements already in
1.3.1. Do not apply the patch to dev blindly or call it the missing gesture build.

No complete working MIPS toolchain is available in the checked environment.
The remaining small compiler archive was truncated. Recover or rebuild a complete
toolchain and publish a verified archive as a toolchain release asset, with legal
source/license information and SHA-256. Do not publish the broken archive.

## Next

The owner wants usability for new users, beginning with contextual hints. The
plan is in `todo.md`; documenting it does not authorize implementing it. Ask for
the next batch's go-ahead. Inspect existing `disco_nav_hint` before adding hints.

Main holds approved releases; dev holds work/checkpoints. This branch setup is
authorized. Future releases still require owner approval. Keep private signing
keys and FiiO firmware out of GitHub. Use signed SD updates for test UI installs;
accept the trial before restarting. Leave stable payload/version tags untouched
until the candidate is device-approved.

## Validation during recovery

- Owner's patch applies cleanly to release 1.3.1.
- `check-theme`, `checkpunct`, and `git diff --check` pass.
- A fresh host-render build was attempted; linking failed with unresolved UI
  symbols, including `screen_current` and `home_set_art_src`. Repair/diagnose
  this before claiming the restored interaction tests pass in this environment.
- No new MIPS binary or device test was performed.
