# diskOS Disco handoff - replacement 1.4.0 (2026-10-12)

Source baseline: origin/dev c7545b2 (polish8), plus recovered polish9, restored Battery layout, standalone Disc Shelf 1.0.4, approved list changes and the three stability fixes. The original unpublished 1.4.0 bundle is superseded; do not merge or tag it. The release tag v1.4.0 points to the replacement bundle's final payload commit. No remote push performed.

## Working agreement

Owner tests on Snowsky Disc V2.57, 360x360, X2000 MIPS32r2 FP64 NaN2008. Provide native LVGL previews before layout approval, then a complete compiled mq_ui and Git bundle. Owner publishes dev/main/tags. Update todo.md. Device SSH root@192.168.178.63; no adb or scp. Install UI only by signed SD update; keep the fail-closed launch verification and rollback.

## Changes

Read docs/releases/v1.4.0.md for the exact release scope and validation. EQ profile reads are asynchronous, isolated and bounded; both the profile and editor use ui/eqprofile.c. Worker artwork results carry RGB, so ui.c no longer reads SQLite for album colours. Under low RAM, pixel preparation falls back to proven static-buffer loading. Stale/no-track results are released early. QS and mode controls use ui_on's swipe guard.

Approved layouts: curved free-edge QS List/Modes, consistent 22px text, 4px gutter beyond Back's 64px strip; mirror for Left. Disco first in picker; Progress Shape under Layout. Battery open gauge mirrors the nav side, centred heading, larger estimate, pill backgrounds below gauge. QS Arc and fixed radios retained.

Optional apps are in apps/: Disc Shelf 1.0.4 and Album Roulette 1.3.1, with source archives under corresponding-source. Disc Shelf is owner device-tested; it is not embedded in mq_ui. Actual shelf.conf and private keys must never enter Git or packages.

## Build and diagnostics

Use ui/tools/build_device.py with the complete GCC11.2 toolchain, a separate cache, --jobs 1 or 2, --approved and a new output directory. New source files must be git-visible. Preserve mq_ui.debug for address symbolication. Metadata payload/mq_ui.build.json binds binary identity to source. Host: make -C ui CROSS= host-render. Gates: check-theme and checkpunct. Focused tests: eq_profile_async_test.py, art_responsiveness_test.py, music_responsiveness_ui_test.py, stability_test.py, art_transition_test.py; host REVIEW_QS / REVIEW_MODES / REVIEW_DISCO_SETTINGS.

The owner reported freezes and automatic UI restarts into stock in the first 1.4.0 candidate. Four-second SQLite waits were reproduced, but fatal crash/OOM cause remains unconfirmed. Get persistent /usr/data/diskos_boot.log and /usr/data/diskos_install.log after recovery. Do not claim the crash is solved or publish a device-tested status until the owner tests this replacement.

## Next

Owner signs replacement with fresh epoch, tests on Disc, then pushes dev. After acceptance publish main and v1.4.0; the tag workflow packages payload and optional apps, and main updates the website. Windows installer stays shelved in todo-ideas.md.
