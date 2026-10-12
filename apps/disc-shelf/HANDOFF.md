# Disc Shelf handoff

Standalone app, not integrated into mq_ui. Baseline upload was Disc Shelf 1.0.3.
Current candidate: 1.0.4; owner device acceptance pending.

Approved design: faded oversized CD tower on the right of the landing, cropped
by the round display; Disc Shelf / Navidrome browser; Browse collection and Exit.
Exit only on landing. Theme/dark-light settings read from diskos.conf at launch.
Main browsing flow is Artists -> artist albums -> album tracks. Single-line
names, separate multi-select circles, green local presence outline, CD curvature,
right-side Library A-Z picker and a large Download footer. No edge back gesture.

Entire artist metadata fetched at startup; albums/tracks on demand (owner chose
Artists first). All letter/# matches and artist albums belong to one continuous
list. Engine caches complete responses in SQLite and publishes total_count plus
bounded 32-row viewport windows. main.c retains a stable widget pool and full
scroll extent; command 3 is a background window fetch. Request generations and
result_generation exclude stale responses. Defer window delivery during touches
and the A-Z modal. Clear row pointers before deleting a view; curve callbacks
must not run during rendering/deletion. Back stores request.scroll_y.

Do not change the existing original-file downloader/duplicate/atomic-publish
contract casually. Exit/Cancel stops children and keeps completed files. Green
album outline means local presence, not a complete album. No automatic rescan.
Keep existing shelf.conf on installation. Config may contain private credentials;
never put it in a source or install archive. Package app.conf keeps the existing
/usr/data/apps/disc-shelf/app launcher path.

Build: explicit musl cross prefix, Makefile separate build/host and build/mips.
Tests: sh tests/run_tests.sh, native selftest/continuous in both variants. Test
large collections and stale windows as well as transfer/cancel/error paths.
Device ABI is static MIPS32r2/FP64/NaN2008. Always record checksums/build provenance.
