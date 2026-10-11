# Disco usability TODO

Updated: 2026-10-12. Companion: `handoff.md`.

The owner authorized this batch before uploading dev. First-use hints are
implemented in candidate 1.3.2-polish2; device acceptance remains pending.

## First-use hints: implemented in 1.3.2-polish2

- [x] Restore 1.3.1 baseline and owner-supplied 1.3.2-polish1 gesture patch.
- [x] Restore complete MIPS toolchain; verify archive SHA-256 and compile candidate.
- [x] Replace the old shard hint with four contextual, theme-aware tips.
- [x] Retire tips through discovery; stop Automatic after five healthy starts.
- [x] Provide Help > First-use hints (Automatic/On/Off) and Show hints again.
- [x] Verify both side layouts, four-second expiry, interaction dismissal,
      discovery, persistence, save failure and Help controls with real LVGL.
- [x] Keep rendering inexpensive and stable payload/release tags unchanged.
- [ ] Owner device test and acceptance of 1.3.2-polish2.
- [ ] Upload the updated dev bundle using owner's GitHub credentials.
- [ ] Publish toolchain and candidate binary/source/checksum backups as durable
      development/toolchain release assets. These are not uploaded yet.

## Later: usability consistency pass

- [ ] Audit Back, close, Apply and immediate settings for predictable behaviour.
- [ ] Check readability with bright, dark, busy and missing artwork; long and
      multilingual titles; empty queues, unavailable actions and loading states.
- [ ] Standardize touch targets, pressed feedback, spacing and visual hierarchy.
      Avoid enlarging everything independently and crowding the round screen.
- [ ] Preserve list position when returning from a detail screen.
- [ ] Test the 260 ms centre-icon tap delay from the gesture candidate on device.
      Check that it still feels snappy.
