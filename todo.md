# Disco usability TODO

Updated: 2026-10-12. Companion: `handoff.md`.

This is a proposed next batch, recorded at the owner's request. Hints have not
been implemented; this request authorizes documentation only.

## Next: first-use hints

Goal: help new users discover Disco without interrupting playback or adding
expensive effects. Use contextual hints, retired through successful use, with
a startup limit as a fallback.

- [x] Restore current GitHub main (1.3.1, 03d7374) as the development baseline.
- [x] Restore the owner-supplied 1.3.2-polish1 patch against 1.3.1.
- [ ] Recover or rebuild its compiled binary; obtain device feedback.
- [ ] Restore the complete MIPS toolchain and publish a verified durable backup.
- [ ] Inspect existing hint code first (especially `disco_nav_hint`) and extend
      it rather than creating overlapping hints.
- [ ] Closed shard: **Tap for menu**, with a small pointer. Retire after two
      successful openings.
- [ ] Open nav menu: **Arrows switch sections · Tap icon to open**. Retire after
      two successful section-navigation interactions. Check wording against the
      actual behaviour; don't count simply opening the menu as navigation.
- [ ] Library: **Hold a song or album for actions**. Retire after opening an
      appropriate action menu.
- [ ] Music: **Tap the title for more**. Retire after opening the title overlay.
- [ ] Show one hint at a time, for roughly four seconds. Dismiss immediately on
      interaction; record discovery only when the described action succeeds.
- [ ] Use a readable pill with a plain background. Keep controls accessible;
      place correctly in both Left and Right layouts, dark and light variants.
      Avoid continuous animation, blur, pulsing, and idle polling.
- [ ] Stop unsolicited hints after approximately five successful UI starts.
      Define a successful start after initialization, so failures don't consume
      the allowance. Guard against watchdog restart loops. This is a UI-start
      count, not a literal hardware-reboot count.
- [ ] Persist counters once per successful startup or discovery, batching when
      practical. Reuse atomic configuration writes and handle save failures.
- [ ] Add **Settings > System > Help > Show hints again** to reset only the hint
      state. Don't reset theme, shortcuts, or other preferences.
- [ ] Explain configurable Hold/Double tap in their settings screens, rather
      than showing hints that assume a fixed action.
- [ ] Verify discovery, timeout, cancellation, reset, startup cutoff, missing
      config defaults and save failures. Use real LVGL interactions; obtain
      device feedback before calling the experience finished.

Timing, wording and thresholds above are suggested defaults, not device-tested
requirements. Ask for implementation approval before beginning this batch.

## Later: usability consistency pass

- [ ] Audit Back, close, Apply and immediate settings for predictable behaviour.
- [ ] Check readability with bright, dark, busy and missing artwork; long and
      multilingual titles; empty queues, unavailable actions and loading states.
- [ ] Standardize touch targets, pressed feedback, spacing and visual hierarchy.
      Avoid enlarging everything independently and crowding the round screen.
- [ ] Preserve list position when returning from a detail screen.
- [ ] Test the 260 ms centre-icon tap delay from the gesture candidate on device.
      Check that it still feels snappy.
