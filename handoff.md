# Disco development handoff

Updated: 2026-10-12. Read `agents/AGENTS.md`, `docs/DEVELOPMENT.md` and `todo.md`.

## Verified starting point

`dev` starts from GitHub `main` release 1.3.1:
`03d737445818ea7b439b427822126d432678b022`.
The owner supplied `1.3.2-polish1-vs-1.3.1.patch`; it applies cleanly to this
release and is restored in dev. That restored checkpoint displays version 1.3.2-polish1; the new hint candidate below is 1.3.2-polish2.
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

The complete compiler archive was recovered and verified: SHA-256
`2ba4be0cfbef2b28314b27a0a6f17b68da368fe496599d0e7541daaab6475b37`.
GCC 11.2.0 now works. Host build recovery required removing empty generated
objects; host configuration also needed a temp-path buffer sized from CFG_PATH
rather than a fixed 64 bytes. Preserve/check toolchain release assets as described
in docs/DEVELOPMENT.md; remote upload remains blocked by GitHub connection access.

## Current candidate: 1.3.2-polish2

Adds four first-use hints and System > Help > First-use hints (Automatic/On/Off),
plus Show hints again. Auto retires learned tips (menu/navigation twice,
Library/Music once) and stops after five healthy UI starts. Each start counts
only after 30 seconds of event-loop uptime. Tips appear after 800 ms, disappear
on interaction or after four seconds, and show at most once per context/session.
On ignores retirement; Off hides tips. Reset affects hint state only.

## Next

The owner authorized the hint implementation before uploading dev. It is now
implemented; obtain device feedback next. Further usability work remains in todo.md.

Main holds approved releases; dev holds work/checkpoints. This branch setup is
authorized. Future releases still require owner approval. Keep private signing
keys and FiiO firmware out of GitHub. Use signed SD updates for test UI installs;
accept the trial before restarting. Leave stable payload/version tags untouched
until the candidate is device-approved.

## Current validation

The patch applies to 1.3.1. Theme/punctuation/source whitespace checks pass,
excluding byte-preserved historical recovery patch context. Real LVGL hint tests
pass on Right/dark and Left/light, covering lifecycle, discovery, persisted state,
save failure, Auto/On/Off and Help controls. Open-only gesture regression tests
also pass on both sides. A static MIPS device binary was built and its ABI checked;
final binary checksum is recorded alongside the deliverable. Device testing is
pending. Main, stable payload, website release version and tags remain unchanged.
